#include "core/ServerListModel.hpp"

#include "core/CertificatePin.hpp"
#include "core/SecretStore.hpp"
#include "core/ServerStore.hpp"

#include <QCoreApplication>
#include <QLocale>
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
    case IsCurrentRole:
        return !server.id.isEmpty() && server.id == m_currentId;
    case StateRole: {
        if (!server.id.isEmpty() && server.id == m_currentId)
            return m_currentState.isEmpty() ? QStringLiteral("live") : m_currentState;
        const Seen seen = m_seen.value(server.id);
        if (!seen.error.isEmpty())
            return QStringLiteral("failed");
        return seen.at.isValid() ? QStringLiteral("seen") : QStringLiteral("unknown");
    }
    case LastCountRole:
        return m_seen.value(server.id).torrents;
    case LastSeenRole:
        return whenText(m_seen.value(server.id).at, QDateTime::currentDateTime());
    case LastFailedRole:
        return !m_seen.value(server.id).error.isEmpty();
    case LastReasonRole:
        return m_seen.value(server.id).error;
    case LastDetailRole:
        return m_seen.value(server.id).detail;
    case CountTextRole: {
        if (!server.id.isEmpty() && server.id == m_currentId) {
            if (m_currentState == QLatin1String("down"))
                return QCoreApplication::translate("ServerListModel", "down");
            if (m_currentState == QLatin1String("refused") ||
                m_currentState == QLatin1String("certificate"))
                return QStringLiteral("!");
            if (m_currentCount >= 0)
                return QString::number(m_currentCount);
        }
        const Seen seen = m_seen.value(server.id);
        if (!seen.error.isEmpty())
            return QStringLiteral("!");
        return seen.torrents >= 0 ? QString::number(seen.torrents) : QString();
    }
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
        {IsCurrentRole, "isCurrent"},
        {StateRole, "serverState"},
        {LastCountRole, "lastCount"},
        {LastSeenRole, "lastSeen"},
        {LastFailedRole, "lastFailed"},
        {LastReasonRole, "lastReason"},
        {LastDetailRole, "lastDetail"},
        {CountTextRole, "countLabel"},
    };
}

QString ServerListModel::defaultId() const
{
    return m_store != nullptr ? m_store->defaultId() : QString();
}

void ServerListModel::setCurrent(const QString &id, const QString &state, int count)
{
    if (id == m_currentId && state == m_currentState && count == m_currentCount)
        return;
    const QString before = m_currentId;
    m_currentId = id;
    m_currentState = state;
    m_currentCount = count;
    const QList<int> roles{IsCurrentRole, StateRole, CountTextRole};
    if (before != id)
        emitRow(before, roles);
    emitRow(id, roles);
}

void ServerListModel::setLastSeen(const QString &id, int count, const QDateTime &at)
{
    if (id.isEmpty())
        return;
    Seen &seen = m_seen[id];
    seen.torrents = count;
    seen.at = at;
    seen.error.clear();
    seen.detail.clear();
    emitRow(id,
            {StateRole,
             LastCountRole,
             LastSeenRole,
             LastFailedRole,
             LastReasonRole,
             LastDetailRole,
             CountTextRole});
    Q_EMIT lastSeenChanged();
}

void ServerListModel::setLastFailure(const QString &id,
                                     const QString &reason,
                                     const QString &detail,
                                     const QDateTime &at)
{
    if (id.isEmpty())
        return;
    Seen &seen = m_seen[id];
    seen.at = at;
    seen.error =
        reason.isEmpty() ? QCoreApplication::translate("ServerListModel", "error") : reason;
    seen.detail = detail;
    emitRow(
        id,
        {StateRole, LastSeenRole, LastFailedRole, LastReasonRole, LastDetailRole, CountTextRole});
    Q_EMIT lastSeenChanged();
}

QJsonObject ServerListModel::lastSeenJson() const
{
    QJsonObject servers;
    for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it) {
        if (!it.value().at.isValid())
            continue;
        QJsonObject seen;
        seen[QStringLiteral("torrents")] = it.value().torrents;
        seen[QStringLiteral("at")] = it.value().at.toUTC().toString(Qt::ISODate);
        seen[QStringLiteral("error")] = it.value().error;
        seen[QStringLiteral("detail")] = it.value().detail;
        QJsonObject server;
        server[QStringLiteral("lastSeen")] = seen;
        servers[it.key()] = server;
    }
    return servers;
}

void ServerListModel::loadLastSeen(const QJsonObject &servers)
{
    m_seen.clear();
    for (auto it = servers.constBegin(); it != servers.constEnd(); ++it) {
        const QJsonObject seen = it.value().toObject().value(QStringLiteral("lastSeen")).toObject();
        const QDateTime at =
            QDateTime::fromString(seen.value(QStringLiteral("at")).toString(), Qt::ISODate);
        if (!at.isValid())
            continue;
        Seen &entry = m_seen[it.key()];
        entry.torrents = seen.value(QStringLiteral("torrents")).toInt(-1);
        entry.at = at.toLocalTime();
        entry.error = seen.value(QStringLiteral("error")).toString();
        entry.detail = seen.value(QStringLiteral("detail")).toString();
    }
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0, 0),
                           index(int(m_rows.size()) - 1, 0),
                           {StateRole,
                            LastCountRole,
                            LastSeenRole,
                            LastFailedRole,
                            LastReasonRole,
                            LastDetailRole,
                            CountTextRole});
}

QString ServerListModel::busiestOther() const
{
    QString best;
    int most = -1;
    for (const Server &server : m_rows) {
        if (server.id == m_currentId)
            continue;
        const Seen seen = m_seen.value(server.id);
        if (seen.error.isEmpty() && seen.torrents > most) {
            most = seen.torrents;
            best = server.id;
        }
    }
    return best;
}

QString ServerListModel::whenText(const QDateTime &at, const QDateTime &now)
{
    if (!at.isValid())
        return {};
    const QDateTime local = at.toLocalTime();
    const QString time = local.toString(QStringLiteral("HH:mm"));
    const qint64 days = local.date().daysTo(now.toLocalTime().date());
    if (days == 0)
        return QCoreApplication::translate("ServerListModel", "today %1").arg(time);
    if (days == 1)
        return QCoreApplication::translate("ServerListModel", "yesterday %1").arg(time);
    return QStringLiteral("%1 %2").arg(QLocale::c().toString(local.date(), QStringLiteral("d MMM")),
                                       time);
}

void ServerListModel::emitRow(const QString &id, const QList<int> &roles)
{
    const int row = id.isEmpty() ? -1 : indexOfId(id);
    if (row < 0)
        return;
    const QModelIndex at = index(row, 0);
    Q_EMIT dataChanged(at, at, roles);
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
    if (m_seen.remove(id) > 0)
        Q_EMIT lastSeenChanged();
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
