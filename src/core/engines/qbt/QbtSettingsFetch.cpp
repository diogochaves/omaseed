#include "core/engines/qbt/QbtSettingsFetch.hpp"

#include "core/ApiError.hpp"
#include "core/Reply.hpp"
#include "core/engines/qbt/QbtClient.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QFileInfo>

namespace omaseed::core {

namespace {

// Where an omaseed backup keeps each server's settings (App::qbtBackupExtras, #32 D3 a).
const QString kQbtEntryPrefix = QStringLiteral("qbittorrent/");

} // namespace

QbtSettingsFetch::QbtSettingsFetch(QObject *parent)
    : QObject(parent)
{}

// An engine still reading is this object's child and goes with it; no signal follows.
QbtSettingsFetch::~QbtSettingsFetch() = default;

void QbtSettingsFetch::fetch(const Server &server, const QString &secret, const QString &appVersion)
{
    if (m_busy)
        return;
    m_busy = true;
    m_mode = server.authMode;

    auto *client = new QbtEngine(this);
    client->setPinnedCertificate(server.certificateSha256);
    client->setBaseUrl(server.url);
    client->setMaxRetries(0);
    client->setTimeout(m_timeout);
    switch (server.authMode) {
    case AuthMode::ApiKey:
        client->setApiKey(secret);
        break;
    case AuthMode::Cookie:
        client->setCredentials(server.username, secret);
        break;
    case AuthMode::None:
    case AuthMode::Password: // Deluge's; a qBittorrent never has it
        break;
    }
    m_client = client;

    const QString name = server.name;
    const QString url = server.url.toString(QUrl::RemoveUserInfo);
    client->probe()->then(
        this, [this, client, name, url, appVersion](const Result<ServerInfo> &result) {
            if (result.isError()) {
                finish(std::nullopt, reasonText(result.error(), m_mode));
                return;
            }
            auto *exporter = new QbtSettingsExport(client, this);
            m_export = exporter;
            connect(exporter,
                    &QbtSettingsExport::collected,
                    this,
                    [this](const QbtSettingsSnapshot &snapshot) { finish(snapshot, QString()); });
            connect(exporter, &QbtSettingsExport::failed, this, [this](const QString &why) {
                finish(std::nullopt, why);
            });
            exporter->collect(name, url, appVersion);
        });
}

void QbtSettingsFetch::finish(const std::optional<QbtSettingsSnapshot> &snapshot,
                              const QString &reason)
{
    if (!m_busy)
        return;
    m_busy = false;
    if (m_export)
        m_export->deleteLater();
    m_export = nullptr;
    QbtEngine *engine = m_client;
    m_client = nullptr;
    if (engine) {
        // A password login leaves a session on the server: end it, then drop the engine. An
        // API key (or no login) has none.
        QbtClient *client = engine->client();
        if (m_mode == AuthMode::Cookie && client->authState() == QbtClient::AuthState::Ok)
            client->post(QStringLiteral("auth/logout"))
                ->then(engine, [engine](const Result<QByteArray> &) { engine->deleteLater(); });
        else
            engine->deleteLater();
    }
    if (snapshot)
        Q_EMIT fetched(*snapshot);
    else
        Q_EMIT failed(reason);
}

QString QbtSettingsFetch::reasonText(const ApiError &error, AuthMode mode)
{
    if (error.kind() == ApiError::Kind::Certificate)
        return QStringLiteral("its certificate is not trusted");
    const QString refused = credentialRefusedText(error, mode);
    if (!refused.isEmpty())
        return refused;
    if (error.kind() == ApiError::Kind::Timeout)
        return QStringLiteral("no answer in time");
    if (error.kind() == ApiError::Kind::Network && !error.message().isEmpty()) {
        // "GET app/version: Connection refused" reads "connection refused".
        QString text = error.message().section(QStringLiteral(": "), -1).trimmed();
        if (text.isEmpty())
            return error.toString();
        text[0] = text.at(0).toLower();
        return text;
    }
    return error.toString();
}

std::optional<QbtSettingsSnapshot>
QbtSettingsFetch::fromBackup(const OmaseedBackup::Contents &contents, const Server &server)
{
    std::optional<QbtSettingsSnapshot> byAddress;
    const QString address = OmaseedBackup::addressKey(server.url);
    for (const TarArchive::Entry &entry : contents.extras) {
        if (!entry.name.startsWith(kQbtEntryPrefix))
            continue;
        std::optional<QbtSettingsSnapshot> s = QbtSettingsExport::parse(entry.data);
        if (!s)
            continue;
        if (QFileInfo(entry.name).completeBaseName() == server.id)
            return s;
        if (!byAddress && OmaseedBackup::addressKey(QUrl(s->serverUrl)) == address)
            byAddress = s;
    }
    return byAddress;
}

std::optional<QbtSettingsSnapshot> QbtSettingsFetch::newestSaved(
    const Server &server, const QHash<QString, QByteArray> &session, const QStringList &copies)
{
    std::optional<QbtSettingsSnapshot> best;
    if (session.contains(server.id))
        best = QbtSettingsExport::parse(session.value(server.id));
    for (const QString &path : copies) {
        const std::optional<OmaseedBackup::Contents> contents = OmaseedBackup::parseFile(path);
        if (!contents)
            continue;
        const std::optional<QbtSettingsSnapshot> saved = fromBackup(*contents, server);
        if (!saved)
            continue;
        // Copies come newest first: the first that has the server is the newest it can give.
        if (!best || (saved->created.isValid() && saved->created > best->created))
            best = saved;
        break;
    }
    return best;
}

} // namespace omaseed::core
