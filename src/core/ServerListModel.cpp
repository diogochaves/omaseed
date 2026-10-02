#include "core/ServerListModel.hpp"

#include "core/CertificatePin.hpp"
#include "core/SecretStore.hpp"
#include "core/ServerStore.hpp"

#include <QCoreApplication>
#include <QUrl>

namespace omaseed::core {

namespace {

QString stringField(const QVariantMap &fields, const char *key)
{
    return fields.value(QString::fromLatin1(key)).toString().trimmed();
}

} // namespace

ServerListModel::ServerListModel(ServerStore *store, SecretStore *secrets, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_secrets(secrets)
{
    if (m_store != nullptr) {
        connect(m_store, &ServerStore::changed, this, &ServerListModel::reload);
        reload();
    }
}

int ServerListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ServerListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Server &server = m_rows.at(index.row());
    const Probe probe = m_probes.value(server.id);

    switch (role) {
    case IdRole:
        return server.id;
    case NameRole:
        return server.name.isEmpty() ? server.url.host() : server.name;
    case UrlRole:
        return server.url.toString(QUrl::RemoveUserInfo);
    case UrlTextRole:
        return urlText(server.url);
    case AuthModeRole:
        return authModeToString(server.authMode);
    case AuthModeTextRole:
        return authModeText(server.authMode);
    case UsernameRole:
        return server.username;
    case IsDefaultRole:
        return m_store != nullptr && !server.id.isEmpty() && m_store->defaultId() == server.id;
    case PathMapTextRole:
        return pathMapText(server.pathMap);
    case ReachableRole:
        return probe.reachable;
    case ReachableTextRole:
        if (!probe.probed)
            return QString();
        if (probe.reachable) {
            return probe.latencyMs >= 0
                       ? QCoreApplication::translate("ServerListModel", "yes · %1 ms")
                             .arg(probe.latencyMs)
                       : QCoreApplication::translate("ServerListModel", "yes");
        }
        return probe.error.isEmpty()
                   ? QCoreApplication::translate("ServerListModel", "no")
                   : QCoreApplication::translate("ServerListModel", "no · %1").arg(probe.error);
    case TorrentCountRole:
        return probe.torrentCount;
    case LastErrorRole:
        return probe.error;
    case VersionRole:
        return probe.version;
    case ApiVersionRole:
        return probe.apiVersion;
    case VersionTextRole:
        return probe.version.isEmpty() ? QString() : QStringLiteral("v%1").arg(probe.version);
    case CertificateRole:
        return server.certificateSha256;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ServerListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {UrlRole, "url"},
        {UrlTextRole, "urlText"},
        {AuthModeRole, "authMode"},
        {AuthModeTextRole, "authModeText"},
        {UsernameRole, "username"},
        {IsDefaultRole, "isDefault"},
        {PathMapTextRole, "pathMapText"},
        {ReachableRole, "reachable"},
        {ReachableTextRole, "reachableText"},
        {TorrentCountRole, "torrentCount"},
        {LastErrorRole, "lastError"},
        {VersionRole, "version"},
        {ApiVersionRole, "apiVersion"},
        {VersionTextRole, "versionText"},
        {CertificateRole, "certificateSha256"},
    };
}

QString ServerListModel::defaultId() const
{
    return m_store != nullptr ? m_store->defaultId() : QString();
}

int ServerListModel::indexOfId(const QString &id) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return int(i);
    }
    return -1;
}

QString ServerListModel::idAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : QString();
}

QVariantMap ServerListModel::serverMap(const QString &id) const
{
    const int row = indexOfId(id);
    if (row < 0)
        return {};

    QVariantMap map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row, 0), it.key()));

    const Server &server = m_rows.at(row);
    map.insert(QStringLiteral("pathMapRemote"),
               server.pathMap.isEmpty() ? QString() : server.pathMap.constFirst().remote);
    map.insert(QStringLiteral("pathMapLocal"),
               server.pathMap.isEmpty() ? QString() : server.pathMap.constFirst().local);
    return map;
}

QString ServerListModel::add(const QVariantMap &fields, const QString &secretIn)
{
    if (m_store == nullptr)
        return {};

    QString urlPassword;
    Server server = serverFromMap(fields, &urlPassword);
    if (!server.url.isValid() || server.url.host().isEmpty())
        return {};
    const QString secret = secretIn.isEmpty() ? urlPassword : secretIn;
    if (server.id.isEmpty())
        server.id = Server::newId();
    if (server.name.isEmpty())
        server.name = server.url.host();
    server.addedAt = QDateTime::currentDateTimeUtc();

    if (!m_store->add(server))
        return {};
    writeSecret(server, secret);
    if (fields.value(QStringLiteral("isDefault")).toBool() || m_store->count() == 1)
        m_store->setDefault(server.id);
    return server.id;
}

bool ServerListModel::update(const QVariantMap &fields, const QString &secretIn)
{
    if (m_store == nullptr)
        return false;

    QString urlPassword;
    Server server = serverFromMap(fields, &urlPassword);
    const std::optional<Server> existing = m_store->byId(server.id);
    if (!existing)
        return false;
    if (!server.url.isValid() || server.url.host().isEmpty())
        return false;
    if (server.name.isEmpty())
        server.name = server.url.host();
    server.addedAt = existing->addedAt;
    server.isDefault = existing->isDefault;
    if (!fields.contains(QStringLiteral("certificateSha256")))
        server.certificateSha256 = existing->certificateSha256;
    const QString secret = secretIn.isEmpty() ? urlPassword : secretIn;

    if (!m_store->update(server))
        return false;
    if (m_secrets != nullptr && existing->authMode != server.authMode) {
        // The kind the old mode used is dead now; for "none" neither kind is.
        if (existing->authMode == AuthMode::Cookie || server.authMode == AuthMode::None)
            m_secrets->removeSecret(server.id, SecretStore::Kind::Password);
        if (existing->authMode != AuthMode::Cookie || server.authMode == AuthMode::None)
            m_secrets->removeSecret(server.id, SecretStore::Kind::ApiKey);
    }
    writeSecret(server, secret);
    return true;
}

bool ServerListModel::remove(const QString &id)
{
    if (m_store == nullptr || !m_store->byId(id))
        return false;
    if (m_secrets != nullptr) {
        m_secrets->removeSecret(id, SecretStore::Kind::ApiKey);
        m_secrets->removeSecret(id, SecretStore::Kind::Password);
    }
    m_probes.remove(id);
    return m_store->remove(id);
}

bool ServerListModel::setDefault(const QString &id)
{
    return m_store != nullptr && m_store->setDefault(id);
}

void ServerListModel::setProbeResult(const QString &id,
                                     bool reachable,
                                     const QString &version,
                                     const QString &apiVersion,
                                     int torrentCount,
                                     int latencyMs,
                                     const QString &error)
{
    if (id.isEmpty())
        return; // a server that is not saved yet has no row to write into
    Probe &probe = m_probes[id];
    probe.probed = true;
    probe.reachable = reachable;
    probe.latencyMs = latencyMs;
    probe.error = error;
    if (torrentCount >= 0)
        probe.torrentCount = torrentCount;
    if (!version.isEmpty()) {
        // qBittorrent answers app/version with "v5.2.3"; the roles print the number and put
        // the "v" back only in `VersionTextRole`.
        probe.version = version.startsWith(QLatin1Char('v')) ? version.mid(1) : version;
    }
    if (!apiVersion.isEmpty())
        probe.apiVersion = apiVersion;

    const int row = indexOfId(id);
    if (row < 0)
        return;
    const QModelIndex changedIndex = index(row, 0);
    Q_EMIT dataChanged(changedIndex,
                       changedIndex,
                       {ReachableRole,
                        ReachableTextRole,
                        TorrentCountRole,
                        LastErrorRole,
                        VersionRole,
                        ApiVersionRole,
                        VersionTextRole});
}

Server ServerListModel::serverFromMap(const QVariantMap &fields, QString *urlPassword)
{
    Server server;
    server.id = stringField(fields, "id");
    server.name = stringField(fields, "name");

    QString urlText = stringField(fields, "url");
    if (!urlText.isEmpty() && !urlText.contains(QStringLiteral("://")))
        urlText.prepend(QStringLiteral("http://"));
    while (urlText.endsWith(QLatin1Char('/')))
        urlText.chop(1);
    server.url = QUrl(urlText);

    bool ok = false;
    const AuthMode mode = authModeFromString(stringField(fields, "authMode"), &ok);
    server.authMode = ok ? mode : AuthMode::None;
    server.username = stringField(fields, "username");

    // `http://user:pass@host` typed into the URL field: credentials never stay in the URL
    // (it is written to servers.json and logged). The user name moves to its field; the
    // password is handed back as the cookie login's secret, or dropped.
    if (!server.url.userInfo().isEmpty()) {
        if (server.username.isEmpty())
            server.username = server.url.userName();
        if (urlPassword != nullptr && server.authMode == AuthMode::Cookie)
            *urlPassword = server.url.password();
        server.url.setUserInfo(QString());
    }

    const QString remote = stringField(fields, "pathMapRemote");
    const QString local = stringField(fields, "pathMapLocal");
    if (!remote.isEmpty() && !local.isEmpty())
        server.pathMap.append(PathMapping{remote, local});

    server.certificateSha256 = CertificatePin::normalized(stringField(fields, "certificateSha256"));
    return server;
}

QString ServerListModel::authModeText(AuthMode mode)
{
    switch (mode) {
    case AuthMode::ApiKey:
        return QCoreApplication::translate("ServerListModel", "API key");
    case AuthMode::Cookie:
        return QCoreApplication::translate("ServerListModel", "cookie");
    case AuthMode::None:
        break;
    }
    return QCoreApplication::translate("ServerListModel", "none");
}

QString ServerListModel::urlText(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return {};
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
}

QString ServerListModel::pathMapText(const QList<PathMapping> &mappings)
{
    if (mappings.isEmpty())
        return {};
    const PathMapping &mapping = mappings.constFirst();
    if (mapping.remote.isEmpty() || mapping.local.isEmpty())
        return {};
    return QStringLiteral("%1 → %2").arg(mapping.remote, mapping.local);
}

void ServerListModel::reload()
{
    beginResetModel();
    m_rows = m_store != nullptr ? m_store->servers() : QList<Server>();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT changed();
}

void ServerListModel::writeSecret(const Server &server, const QString &secret)
{
    if (m_secrets == nullptr || secret.isEmpty())
        return;
    const SecretStore::Kind kind = server.authMode == AuthMode::Cookie ? SecretStore::Kind::Password
                                                                       : SecretStore::Kind::ApiKey;
    m_secrets->setSecret(server.id, kind, secret);
}

} // namespace omaseed::core
