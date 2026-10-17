#include "core/engines/qbt/QbtSettingsExport.hpp"

#include "core/engines/qbt/QbtClient.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimeZone>
#include <QUrlQuery>

namespace omaseed::core {

namespace {

const QString kPreferences = QStringLiteral("preferences");
const QString kCategories = QStringLiteral("categories");
const QString kTags = QStringLiteral("tags");
const QString kRss = QStringLiteral("rss");
const QString kItems = QStringLiteral("items");
const QString kRules = QStringLiteral("rules");
const QString kServer = QStringLiteral("server");

QJsonArray arrayWithoutSecrets(const QJsonArray &array)
{
    QJsonArray out;
    for (const QJsonValue &value : array) {
        if (value.isObject())
            out.append(QbtSettingsExport::withoutSecrets(value.toObject()));
        else if (value.isArray())
            out.append(arrayWithoutSecrets(value.toArray()));
        else
            out.append(value);
    }
    return out;
}

int countFeeds(const QJsonObject &folder)
{
    int n = 0;
    for (auto it = folder.constBegin(); it != folder.constEnd(); ++it) {
        const QJsonObject item = it.value().toObject();
        if (item.contains(QLatin1String("url")))
            ++n;
        else
            n += countFeeds(item);
    }
    return n;
}

} // namespace

int QbtSettingsSnapshot::feedCount() const
{
    return countFeeds(rssItems);
}

QbtSettingsExport::QbtSettingsExport(QbtEngine *engine, QObject *parent)
    : QObject(parent)
    , m_client(engine != nullptr ? engine->client() : nullptr)
{}

QStringList QbtSettingsExport::knownSecretKeys()
{
    return {QStringLiteral("web_ui_password"),
            QStringLiteral("web_ui_api_key"),
            QStringLiteral("proxy_password"),
            QStringLiteral("mail_notification_password"),
            QStringLiteral("dyndns_password"),
            // qBittorrent 4.1 sent the Web UI's HTTPS key and certificate themselves.
            QStringLiteral("ssl_key"),
            QStringLiteral("ssl_cert"),
            // An RSS rule's torrent parameters (5.x).
            QStringLiteral("ssl_private_key")};
}

bool QbtSettingsExport::isSecretKey(const QString &key)
{
    static const QStringList named = knownSecretKeys();
    if (named.contains(key))
        return true;
    // The forms qBittorrent names its secrets in, so a key a later version adds stays out too.
    // Not a bare "token": `bdecode_token_limit` is a number.
    static const QStringList suffixes = {QStringLiteral("password"),
                                         QStringLiteral("_api_key"),
                                         QStringLiteral("_apikey"),
                                         QStringLiteral("_secret"),
                                         QStringLiteral("_private_key"),
                                         QStringLiteral("_token")};
    const QString lower = key.toLower();
    for (const QString &suffix : suffixes) {
        if (lower.endsWith(suffix))
            return true;
    }
    return false;
}

QJsonObject QbtSettingsExport::withoutSecrets(const QJsonObject &object)
{
    QJsonObject out;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isSecretKey(it.key()))
            continue;
        const QJsonValue value = it.value();
        if (value.isObject())
            out.insert(it.key(), withoutSecrets(value.toObject()));
        else if (value.isArray())
            out.insert(it.key(), arrayWithoutSecrets(value.toArray()));
        else
            out.insert(it.key(), value);
    }
    return out;
}

QByteArray QbtSettingsExport::toJson(const QbtSettingsSnapshot &snapshot)
{
    const QDateTime created =
        snapshot.created.isValid() ? snapshot.created : QDateTime::currentDateTimeUtc();
    QJsonObject root{
        {QStringLiteral("format"), kFormat},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("created"), created.toTimeZone(QTimeZone::UTC).toString(Qt::ISODate)},
        {QStringLiteral("app"), snapshot.appVersion},
        {kServer,
         QJsonObject{{QStringLiteral("name"), snapshot.serverName},
                     {QStringLiteral("url"), snapshot.serverUrl},
                     {QStringLiteral("qbittorrent"), snapshot.version},
                     {QStringLiteral("webApi"), snapshot.apiVersion}}},
        {kPreferences, withoutSecrets(snapshot.preferences)},
        {kCategories, withoutSecrets(snapshot.categories)},
        {kTags, QJsonArray::fromStringList(snapshot.tags)},
        {kRss,
         QJsonObject{{kItems, withoutSecrets(snapshot.rssItems)},
                     {kRules, withoutSecrets(snapshot.rssRules)}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<QbtSettingsSnapshot> QbtSettingsExport::parse(const QByteArray &bytes, QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<QbtSettingsSnapshot> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("This file is not a qBittorrent settings export."));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != kFormat)
        return fail(QStringLiteral("This file is not a qBittorrent settings export."));
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kVersion)
        return fail(
            QStringLiteral("This export was made by a newer omaseed (format %1).").arg(version));
    if (!root.value(kPreferences).isObject())
        return fail(QStringLiteral("The export is damaged: it has no preferences."));

    QbtSettingsSnapshot s;
    s.created =
        QDateTime::fromString(root.value(QStringLiteral("created")).toString(), Qt::ISODate);
    s.appVersion = root.value(QStringLiteral("app")).toString();
    const QJsonObject server = root.value(kServer).toObject();
    s.serverName = server.value(QStringLiteral("name")).toString();
    s.serverUrl = server.value(QStringLiteral("url")).toString();
    s.version = server.value(QStringLiteral("qbittorrent")).toString();
    s.apiVersion = server.value(QStringLiteral("webApi")).toString();
    s.preferences = withoutSecrets(root.value(kPreferences).toObject());
    s.categories = withoutSecrets(root.value(kCategories).toObject());
    for (const QJsonValue &tag : root.value(kTags).toArray()) {
        if (tag.isString() && !tag.toString().isEmpty())
            s.tags.append(tag.toString());
    }
    const QJsonObject rss = root.value(kRss).toObject();
    s.rssItems = withoutSecrets(rss.value(kItems).toObject());
    s.rssRules = withoutSecrets(rss.value(kRules).toObject());
    return s;
}

std::optional<QbtSettingsSnapshot> QbtSettingsExport::parseFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read %1.").arg(path);
        return std::nullopt;
    }
    return parse(file.readAll(), error);
}

bool QbtSettingsExport::writeFile(const QbtSettingsSnapshot &snapshot,
                                  const QString &path,
                                  QString *error)
{
    const QByteArray bytes = toJson(snapshot);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void QbtSettingsExport::collect(const QString &serverName,
                                const QString &serverUrl,
                                const QString &appVersion)
{
    if (m_busy)
        return;
    if (m_client.isNull()) {
        Q_EMIT failed(QStringLiteral("no server"));
        return;
    }
    m_busy = true;
    m_snapshot = QbtSettingsSnapshot();
    m_snapshot.created = QDateTime::currentDateTimeUtc();
    m_snapshot.serverName = serverName;
    m_snapshot.serverUrl = serverUrl;
    m_snapshot.appVersion = appVersion;
    m_step = 0;
    step();
}

void QbtSettingsExport::fail(const QString &error)
{
    m_busy = false;
    Q_EMIT failed(error);
}

void QbtSettingsExport::step()
{
    if (m_client.isNull()) {
        fail(QStringLiteral("no server"));
        return;
    }
    const auto objectFrom = [](const QByteArray &body, bool *ok) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        *ok = parseError.error == QJsonParseError::NoError && doc.isObject();
        return doc.object();
    };
    switch (m_step++) {
    case 0:
        m_client->appVersion()->then(this, [this](const Result<QString> &r) {
            if (r.isError())
                return fail(r.error().toString());
            m_snapshot.version = r.value();
            step();
        });
        return;
    case 1:
        m_client->webApiVersion()->then(this, [this](const Result<QString> &r) {
            if (r.isError())
                return fail(r.error().toString());
            m_snapshot.apiVersion = r.value();
            step();
        });
        return;
    case 2:
        m_client->appPreferences()->then(this, [this](const Result<QJsonObject> &r) {
            if (r.isError())
                return fail(r.error().toString());
            m_snapshot.preferences = withoutSecrets(r.value());
            step();
        });
        return;
    case 3:
        m_client->torrentsCategories()->then(this, [this](const Result<QJsonObject> &r) {
            if (r.isError())
                return fail(r.error().toString());
            m_snapshot.categories = withoutSecrets(r.value());
            step();
        });
        return;
    case 4:
        m_client->torrentsTags()->then(this, [this](const Result<QJsonArray> &r) {
            if (r.isError())
                return fail(r.error().toString());
            for (const QJsonValue &tag : r.value())
                m_snapshot.tags.append(tag.toString());
            step();
        });
        return;
    case 5: {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("withData"), QStringLiteral("false"));
        m_client->get(QStringLiteral("rss/items"), query)
            ->then(this, [this, objectFrom](const Result<QByteArray> &r) {
                if (r.isError())
                    return fail(r.error().toString());
                bool ok = false;
                m_snapshot.rssItems = withoutSecrets(objectFrom(r.value(), &ok));
                if (!ok)
                    return fail(QStringLiteral("rss/items: not a JSON object"));
                step();
            });
        return;
    }
    case 6:
        m_client->get(QStringLiteral("rss/rules"))
            ->then(this, [this, objectFrom](const Result<QByteArray> &r) {
                if (r.isError())
                    return fail(r.error().toString());
                bool ok = false;
                m_snapshot.rssRules = withoutSecrets(objectFrom(r.value(), &ok));
                if (!ok)
                    return fail(QStringLiteral("rss/rules: not a JSON object"));
                step();
            });
        return;
    default:
        m_busy = false;
        Q_EMIT collected(m_snapshot);
        return;
    }
}

} // namespace omaseed::core
