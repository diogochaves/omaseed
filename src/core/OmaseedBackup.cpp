#include "core/OmaseedBackup.hpp"

#include "core/Paths.hpp"
#include "core/SeedLedger.hpp"
#include "core/ServerStore.hpp"
#include "core/StateFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUuid>

namespace omaseed::core {

namespace {

const QString kManifest = QStringLiteral("manifest.json");
const QString kState = QStringLiteral("state.json");
const QString kServers = QStringLiteral("servers.json");
const QString kLedgerDir = QStringLiteral("ledger/");
const QByteArray kSqliteMagic("SQLite format 3\0", 16);

QFileDevice::Permissions ownerOnly()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

/// A file's bytes; nothing when it does not exist or cannot be read.
std::optional<QByteArray> readAll(const QString &path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    QFile::setPermissions(path, ownerOnly());
    return true;
}

/// A connection of its own to a SQLite file, closed and removed on the way out.
class Sqlite
{
public:
    explicit Sqlite(const QString &path)
        : m_name(QStringLiteral("omaseed-backup-") + QUuid::createUuid().toString(QUuid::Id128))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        m_ok = db.open();
        if (!m_ok)
            m_error = db.lastError().text();
    }
    ~Sqlite()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    Sqlite(const Sqlite &) = delete;
    Sqlite &operator=(const Sqlite &) = delete;

    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
    bool m_ok = false;
    QString m_error;
};

/// A consistent copy of a ledger file, taken with `VACUUM INTO` (safe while SeedLedger has the
/// file open and writing). Nothing when there is no file or SQLite refuses.
std::optional<QByteArray> snapshotLedger(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path))
        return std::nullopt;
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error)
            *error = QStringLiteral("no temporary folder for the history copy");
        return std::nullopt;
    }
    const QString out = tmp.filePath(QStringLiteral("ledger.sqlite"));
    {
        Sqlite sqlite(path);
        if (!sqlite.ok()) {
            if (error)
                *error = QStringLiteral("cannot read %1: %2").arg(path, sqlite.error());
            return std::nullopt;
        }
        QSqlQuery q(sqlite.db());
        q.prepare(QStringLiteral("VACUUM INTO ?"));
        q.addBindValue(out);
        if (!q.exec()) {
            if (error)
                *error = QStringLiteral("cannot copy %1: %2").arg(path, q.lastError().text());
            return std::nullopt;
        }
    }
    return readAll(out);
}

QJsonObject parseObject(const QByteArray &bytes, bool *ok)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    *ok = parseError.error == QJsonParseError::NoError && doc.isObject();
    return doc.object();
}

} // namespace

int OmaseedBackup::History::days(qint64 now) const
{
    if (!exists())
        return 0;
    return int(std::max<qint64>(1, (now - start + 86399) / 86400));
}

QString OmaseedBackup::defaultBackupsDir()
{
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/omaseed/backups");
}

OmaseedBackup::Sources OmaseedBackup::defaultSources()
{
    Sources s;
    s.statePath = StateFile::path();
    s.serversPath = paths::configDir() + QStringLiteral("/servers.json");
    s.ledgerPath = [](const QString &id) { return SeedLedger::defaultPath(id); };
    s.backupsDir = defaultBackupsDir();
    s.machine = QSysInfo::machineHostName();
    return s;
}

QString OmaseedBackup::datedName(const QString &dir, const QString &prefix, qint64 now)
{
    const QString stamp =
        QDateTime::fromSecsSinceEpoch(now).toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    return dir + QLatin1Char('/') + prefix + QLatin1Char('-') + stamp + kSuffix;
}

OmaseedBackup::History OmaseedBackup::historyOf(const QString &ledgerFile)
{
    History history;
    if (!QFileInfo::exists(ledgerFile))
        return history; // never create one by asking
    Sqlite sqlite(ledgerFile);
    if (!sqlite.ok())
        return history;
    QSqlQuery q(sqlite.db());
    if (q.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'start'")) && q.next())
        history.start = q.value(0).toLongLong();
    return history;
}

QString OmaseedBackup::addressKey(const QUrl &url)
{
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const int defaultPort = url.scheme() == QLatin1String("https") ? 443 : 80;
    return url.scheme().toLower() + QStringLiteral("://") + url.host().toLower() +
           QLatin1Char(':') + QString::number(url.port(defaultPort)) + path;
}

QByteArray OmaseedBackup::create(const Sources &sources, qint64 now, QString *error)
{
    QList<TarArchive::Entry> entries;
    QJsonObject manifest{{QStringLiteral("format"), kFormat},
                         {QStringLiteral("version"), kVersion},
                         {QStringLiteral("created"),
                          QDateTime::fromSecsSinceEpoch(now, QTimeZone::UTC).toString(Qt::ISODate)},
                         {QStringLiteral("machine"), sources.machine},
                         {QStringLiteral("app"), sources.appVersion}};

    const std::optional<QByteArray> state = readAll(sources.statePath);
    bool stateOk = false;
    if (state)
        parseObject(*state, &stateOk);
    manifest[QStringLiteral("settings")] = stateOk;
    if (stateOk)
        entries.append({kState, *state, now});

    QJsonArray serverList;
    if (const std::optional<QByteArray> servers = readAll(sources.serversPath)) {
        bool ok = false;
        const QJsonObject root = parseObject(*servers, &ok);
        if (ok) {
            entries.append({kServers, *servers, now});
            for (const QJsonValue &value : root.value(QStringLiteral("servers")).toArray()) {
                bool serverOk = false;
                const Server server = Server::fromJson(value.toObject(), &serverOk);
                if (!serverOk)
                    continue;
                QJsonObject row{{QStringLiteral("id"), server.id},
                                {QStringLiteral("name"), server.name},
                                {QStringLiteral("url"), server.url.toString()}};
                if (sources.ledgerPath) {
                    QString why;
                    const QString path = sources.ledgerPath(server.id);
                    if (const std::optional<QByteArray> ledger = snapshotLedger(path, &why)) {
                        entries.append(
                            {kLedgerDir + server.id + QStringLiteral(".sqlite"), *ledger, now});
                        row[QStringLiteral("ledger")] = true;
                        row[QStringLiteral("historyStart")] = historyOf(path).start;
                    } else if (!why.isEmpty()) {
                        if (error)
                            *error = why;
                        return {};
                    }
                }
                serverList.append(row);
            }
        }
    }
    manifest[QStringLiteral("servers")] = serverList;

    if (sources.extras) {
        for (const TarArchive::Entry &extra : sources.extras()) {
            if (extra.name == kManifest || extra.name == kState || extra.name == kServers ||
                extra.name.startsWith(kLedgerDir))
                continue; // the parts above are omaseed's own
            entries.append(extra);
        }
    }

    entries.prepend({kManifest, QJsonDocument(manifest).toJson(QJsonDocument::Indented), now});
    return TarArchive::write(entries, error);
}

bool OmaseedBackup::createFile(const Sources &sources,
                               const QString &path,
                               qint64 now,
                               QString *error)
{
    const QByteArray archive = create(sources, now, error);
    if (archive.isEmpty())
        return false;
    return writeAtomically(path, archive, error);
}

std::optional<OmaseedBackup::Contents> OmaseedBackup::parse(const QByteArray &archive,
                                                            QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<Contents> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QString why;
    const std::optional<QList<TarArchive::Entry>> entries = TarArchive::read(archive, &why);
    if (!entries)
        return fail(QStringLiteral("This file is not an omaseed backup (%1).").arg(why));

    QHash<QString, QByteArray> byName;
    for (const TarArchive::Entry &entry : *entries)
        byName.insert(entry.name, entry.data);
    if (!byName.contains(kManifest))
        return fail(QStringLiteral("This file is not an omaseed backup: it has no manifest."));
    bool ok = false;
    const QJsonObject manifest = parseObject(byName.value(kManifest), &ok);
    if (!ok || manifest.value(QStringLiteral("format")).toString() != kFormat)
        return fail(QStringLiteral("This file is not an omaseed backup."));
    const int version = manifest.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kVersion)
        return fail(
            QStringLiteral("This backup was made by a newer omaseed (format %1).").arg(version));

    Contents contents;
    contents.created =
        QDateTime::fromString(manifest.value(QStringLiteral("created")).toString(), Qt::ISODate);
    contents.machine = manifest.value(QStringLiteral("machine")).toString();
    contents.appVersion = manifest.value(QStringLiteral("app")).toString();

    if (manifest.value(QStringLiteral("settings")).toBool()) {
        contents.state = parseObject(byName.value(kState), &ok);
        if (!ok)
            return fail(QStringLiteral("The backup is damaged: its settings do not read."));
        contents.hasSettings = true;
    }

    QHash<QString, qint64> starts;
    QSet<QString> withLedger;
    for (const QJsonValue &value : manifest.value(QStringLiteral("servers")).toArray()) {
        const QJsonObject row = value.toObject();
        const QString id = row.value(QStringLiteral("id")).toString();
        starts.insert(id, qint64(row.value(QStringLiteral("historyStart")).toDouble()));
        if (row.value(QStringLiteral("ledger")).toBool())
            withLedger.insert(id);
    }
    if (byName.contains(kServers)) {
        const QJsonObject root = parseObject(byName.value(kServers), &ok);
        if (!ok)
            return fail(QStringLiteral("The backup is damaged: its server list does not read."));
        contents.hasServers = true;
        contents.defaultId = root.value(QStringLiteral("defaultId")).toString();
        for (const QJsonValue &value : root.value(QStringLiteral("servers")).toArray()) {
            bool serverOk = false;
            ServerPart part;
            part.server = Server::fromJson(value.toObject(), &serverOk);
            if (!serverOk)
                continue;
            const QString ledgerName = kLedgerDir + part.server.id + QStringLiteral(".sqlite");
            if (withLedger.contains(part.server.id)) {
                const QByteArray ledger = byName.value(ledgerName);
                if (!ledger.startsWith(kSqliteMagic))
                    return fail(
                        QStringLiteral("The backup is damaged: the history of %1 does not read.")
                            .arg(part.server.name));
                contents.ledgers.insert(part.server.id, ledger);
                part.hasLedger = true;
                part.history.start = starts.value(part.server.id);
            }
            contents.servers.append(part);
        }
    } else if (!withLedger.isEmpty()) {
        return fail(QStringLiteral("The backup is damaged: it has history but no server list."));
    }

    for (const TarArchive::Entry &entry : *entries) {
        if (entry.name == kManifest || entry.name == kState || entry.name == kServers ||
            entry.name.startsWith(kLedgerDir))
            continue;
        contents.extras.append(entry);
    }
    return contents;
}

std::optional<OmaseedBackup::Contents> OmaseedBackup::parseFile(const QString &path, QString *error)
{
    const std::optional<QByteArray> bytes = readAll(path);
    if (!bytes) {
        if (error)
            *error = QStringLiteral("Cannot read %1.").arg(path);
        return std::nullopt;
    }
    return parse(*bytes, error);
}

QList<OmaseedBackup::Match>
OmaseedBackup::match(const Contents &contents, const QList<Server> &local, const Sources &sources)
{
    QList<Match> out;
    for (const ServerPart &part : contents.servers) {
        Match m;
        m.part = part;
        const QString key = addressKey(part.server.url);
        for (const Server &server : local) {
            if (addressKey(server.url) == key) {
                m.localId = server.id;
                break;
            }
        }
        if (!m.localId.isEmpty() && sources.ledgerPath)
            m.local = historyOf(sources.ledgerPath(m.localId));
        m.backupLonger = part.hasLedger && part.history.exists() &&
                         (!m.local.exists() || part.history.start < m.local.start);
        out.append(m);
    }
    return out;
}

std::optional<OmaseedBackup::Outcome> OmaseedBackup::restore(const Contents &contents,
                                                             const Plan &plan,
                                                             ServerStore &store,
                                                             const Sources &sources,
                                                             qint64 now,
                                                             QString *error)
{
    Outcome outcome;
    // What is here goes first, or nothing changes.
    outcome.safetyCopy = datedName(sources.backupsDir, QStringLiteral("before-restore"), now);
    QString why;
    if (!createFile(sources, outcome.safetyCopy, now, &why)) {
        if (error)
            *error = QStringLiteral("Nothing was restored: the copy of what is here failed (%1).")
                         .arg(why);
        return std::nullopt;
    }

    if (plan.settings && contents.hasSettings) {
        const QByteArray bytes = QJsonDocument(contents.state).toJson(QJsonDocument::Indented);
        if (!writeAtomically(sources.statePath, bytes, error))
            return std::nullopt;
    }

    // Servers: by address. The backup's id stays when it is free here.
    const QList<Match> matches = match(contents, store.servers(), sources);
    QHash<QString, QString> localIdOf;
    for (const Match &m : matches) {
        if (!m.localId.isEmpty()) {
            localIdOf.insert(m.part.server.id, m.localId);
            if (plan.servers) {
                if (std::optional<Server> here = store.byId(m.localId)) {
                    here->pathMap = m.part.server.pathMap;
                    store.update(*here);
                }
                outcome.keptServers.append(m.localId);
            }
            continue;
        }
        if (!plan.servers)
            continue;
        Server added = m.part.server;
        if (store.byId(added.id))
            added.id = Server::newId();
        added.isDefault = false;
        if (!store.add(added))
            continue;
        localIdOf.insert(m.part.server.id, added.id);
        outcome.addedServers.append(added.id);
    }
    if (plan.servers && store.defaultId().isEmpty() && localIdOf.contains(contents.defaultId))
        store.setDefault(localIdOf.value(contents.defaultId));

    // History: the longer one wins (D4 a); what it replaces is in the safety copy.
    for (const Match &m : matches) {
        if (!plan.history.contains(m.part.server.id) || !m.part.hasLedger || !sources.ledgerPath)
            continue;
        const QString localId = localIdOf.value(m.part.server.id);
        if (localId.isEmpty())
            continue;
        const QString path = sources.ledgerPath(localId);
        const History here = historyOf(path);
        if (here.exists() && here.start <= m.part.history.start) {
            outcome.historyKept.append(localId);
            continue;
        }
        // A WAL or index left from the old file would be read against the new one.
        QFile::remove(path + QStringLiteral("-wal"));
        QFile::remove(path + QStringLiteral("-shm"));
        if (!writeAtomically(path, contents.ledgers.value(m.part.server.id), error))
            return std::nullopt;
        outcome.historyRestored.append(localId);
    }
    return outcome;
}

} // namespace omaseed::core
