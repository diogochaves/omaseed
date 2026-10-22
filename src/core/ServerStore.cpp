#include "core/ServerStore.hpp"

#include "core/Paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>

namespace omaseed::core {

namespace {

Q_LOGGING_CATEGORY(lcServers, "omaseed.servers")

/// 2 since #84: the engine of each server (Server::engine). 1 had none: all qBittorrent.
constexpr int kFileVersion = 2;

QFileDevice::Permissions ownerReadWrite()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

QFileDevice::Permissions ownerReadWriteExec()
{
    return ownerReadWrite() | QFileDevice::ExeOwner;
}

} // namespace

ServerStore::ServerStore(QObject *parent)
    : QObject(parent)
{}

QString ServerStore::filePath()
{
    return paths::configDir() + QStringLiteral("/servers.json");
}

int ServerStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).id == id)
            return i;
    }
    return -1;
}

void ServerStore::failLoad(const QString &message)
{
    m_loadError = message;
    m_fileUnreadable = true;
    m_keptPath = filePath();
    qCWarning(lcServers).noquote() << message;
    // Moved aside now rather than at the next save, so the first-run page can say where the
    // user's list went before anything is added (Q9).
    moveUnreadableAside();
    Q_EMIT loadFailed(message);
}

bool ServerStore::moveUnreadableAside() const
{
    if (!m_fileUnreadable)
        return true;
    if (!QFile::exists(filePath())) {
        m_fileUnreadable = false;
        return true;
    }
    const QString stem = filePath() + QStringLiteral(".unreadable-") +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString aside = stem;
    for (int n = 1; QFile::exists(aside); ++n)
        aside = stem + QStringLiteral("-%1").arg(n);
    if (!QFile::rename(filePath(), aside)) {
        qCWarning(lcServers).noquote()
            << QStringLiteral("%1 is unreadable and could not be moved to %2; it stays in place")
                   .arg(filePath(), aside);
        return false;
    }
    qCWarning(lcServers).noquote()
        << QStringLiteral("moved the unreadable %1 to %2").arg(filePath(), aside);
    m_fileUnreadable = false;
    m_keptPath = aside;
    return true;
}

bool ServerStore::load()
{
    QFile file(filePath());
    if (!file.exists())
        return true; // nothing on disk yet; not an error

    if (!file.open(QIODevice::ReadOnly)) {
        failLoad(QStringLiteral("could not open %1: %2").arg(file.fileName(), file.errorString()));
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failLoad(QStringLiteral("%1 is not valid JSON: %2")
                     .arg(file.fileName(), parseError.errorString()));
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(kFileVersion);
    if (version > kFileVersion) {
        failLoad(QStringLiteral("%1 has version %2, this build understands up to %3")
                     .arg(file.fileName())
                     .arg(version)
                     .arg(kFileVersion));
        return false;
    }
    m_loadError.clear();
    m_keptPath.clear();
    m_fileUnreadable = false;

    QList<Server> loaded;
    for (const QJsonValue &value : root.value(QStringLiteral("servers")).toArray()) {
        if (!value.isObject())
            continue;
        bool ok = false;
        Server server = Server::fromJson(value.toObject(), &ok);
        if (ok)
            loaded.append(server);
    }

    m_servers = loaded;
    m_defaultId = root.value(QStringLiteral("defaultId")).toString();

    // The default flag on each server mirrors the store's defaultId; keep them consistent
    // regardless of what an older or hand-edited file happened to record per-server.
    for (Server &server : m_servers)
        server.isDefault = (server.id == m_defaultId);

    Q_EMIT changed();
    return true;
}

bool ServerStore::save() const
{
    if (!m_persistent)
        return true;

    const QString dir = paths::configDir();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, ownerReadWriteExec());

    // The file on disk could not be read (corrupt, or written by a newer build) and load()
    // could not move it aside: it may hold the user's whole server list, so it is never
    // overwritten. One more try; if it still cannot be moved, nothing is written at all.
    if (!moveUnreadableAside()) {
        qCWarning(lcServers).noquote()
            << QStringLiteral("not saving: %1 is unreadable and could not be moved aside")
                   .arg(filePath());
        return false;
    }

    QJsonArray array;
    for (const Server &server : m_servers) {
        Server toWrite = server;
        toWrite.isDefault = (server.id == m_defaultId);
        array.append(toWrite.toJson());
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kFileVersion;
    root[QStringLiteral("servers")] = array;
    root[QStringLiteral("defaultId")] = m_defaultId;

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    QFile::setPermissions(filePath(), ownerReadWrite());
    return true;
}

QList<Server> ServerStore::servers() const
{
    return m_servers;
}

int ServerStore::count() const
{
    return m_servers.size();
}

std::optional<Server> ServerStore::byId(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return std::nullopt;
    Server server = m_servers.at(i);
    server.isDefault = (server.id == m_defaultId);
    return server;
}

std::optional<Server> ServerStore::defaultServer() const
{
    if (m_defaultId.isEmpty())
        return std::nullopt;
    return byId(m_defaultId);
}

bool ServerStore::add(const Server &server)
{
    if (server.id.isEmpty() || indexOf(server.id) >= 0)
        return false;

    m_servers.append(server);
    if (m_defaultId.isEmpty() || m_servers.size() == 1)
        m_defaultId = server.id;

    save();
    Q_EMIT added(server.id);
    Q_EMIT changed();
    return true;
}

bool ServerStore::update(const Server &server)
{
    const int i = indexOf(server.id);
    if (i < 0)
        return false;

    m_servers[i] = server;
    save();
    Q_EMIT updated(server.id);
    Q_EMIT changed();
    return true;
}

bool ServerStore::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;

    m_servers.removeAt(i);
    if (m_defaultId == id) {
        m_defaultId = m_servers.isEmpty() ? QString() : m_servers.first().id;
    }

    save();
    Q_EMIT removed(id);
    Q_EMIT changed();
    return true;
}

bool ServerStore::setDefault(const QString &id)
{
    if (!id.isEmpty() && indexOf(id) < 0)
        return false;

    m_defaultId = id;
    save();
    Q_EMIT changed();
    return true;
}

} // namespace omaseed::core
