#pragma once

// OmaseedBackup — omaseed's own backup file and its restore (docs/24 §2, §5; #31).
//
// A backup is a plain tar (TarArchive) holding:
//   manifest.json            format, version, when, on which machine, by which omaseed, and
//                            each server's name, address and history span
//   state.json               every setting (StateFile), as it was
//   servers.json             the server list: addresses, names, kinds of login, path maps.
//                            It never holds a secret; those stay in the keyring (SecretStore)
//   ledger/<server id>.sqlite  each server's seed history (SeedLedger), copied with
//                            `VACUUM INTO`, which is safe while the app writes to it
//   anything else            kept as `extras`: #32 adds `qbittorrent/<server id>.json` here
//
// Restore (D4 a, docs/24 §5): the parts you tick. Before anything changes, a backup of what is
// here now is written to `<backups>/before-restore-<date>.omaseed`; if that fails, nothing is
// restored. Servers are matched by address, not by omaseed's internal id, so a backup from
// another machine finds the same server: a match keeps its local entry and takes the
// backup's path maps, the rest are added (and need their login typed once). For history, the
// longer one wins: the backup's replaces this machine's only when it began earlier, and
// what it replaces is in the safety copy. Settings replace state.json whole, but for the
// table layouts (`tables`, keyed by server id): each goes under its server's id here, and a
// server here that the backup brings no layout for keeps its own (#34).
//
// Everything reads and writes the paths in `Sources`, so tests run on temporary directories.
// The caller (App) flushes and closes the open ledger before restore() and reopens it after.

#include "core/Server.hpp"
#include "core/TarArchive.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace omaseed::core {

class ServerStore;

class OmaseedBackup
{
public:
    static constexpr int kVersion = 1;
    static inline const QString kFormat = QStringLiteral("omaseed-backup");
    static inline const QString kSuffix = QStringLiteral(".omaseed");

    /// Where everything is read from and written to.
    struct Sources
    {
        QString statePath;   ///< StateFile::path()
        QString serversPath; ///< servers.json
        /// A server id's ledger file (SeedLedger::defaultPath).
        std::function<QString(const QString &)> ledgerPath;
        QString backupsDir; ///< `<data>/omaseed/backups`: safety and automatic copies
        QString machine;    ///< the host name, for the manifest
        QString appVersion;
        /// More entries for the archive (#32: each server's qBittorrent settings).
        std::function<QList<TarArchive::Entry>()> extras;
    };
    /// The real paths, following XDG_CONFIG_HOME and XDG_DATA_HOME.
    static Sources defaultSources();
    static QString defaultBackupsDir();

    /// One server's history: when its ledger began (unix seconds); 0 when it has none.
    struct History
    {
        qint64 start = 0;
        bool exists() const { return start > 0; }
        /// Whole days from `start` to `now`, at least 1 for any history.
        int days(qint64 now) const;
    };

    struct ServerPart
    {
        Server server; ///< as servers.json had it (no secret)
        History history;
        bool hasLedger = false;
    };

    struct Contents
    {
        QDateTime created;
        QString machine;
        QString appVersion;
        bool hasSettings = false;
        QJsonObject state;
        bool hasServers = false;
        QString defaultId;
        QList<ServerPart> servers;
        QHash<QString, QByteArray> ledgers; ///< backup server id -> SQLite file
        QList<TarArchive::Entry> extras;
    };

    /// The backup archive of everything `sources` points at, made at `now`.
    static QByteArray create(const Sources &sources, qint64 now, QString *error = nullptr);
    /// Writes `create()` to `path` atomically (0600). False with `error` on any failure.
    static bool
    createFile(const Sources &sources, const QString &path, qint64 now, QString *error = nullptr);

    /// A backup's contents, or nothing when `archive` is not an omaseed backup this build
    /// reads, or is damaged. Nothing is written.
    static std::optional<Contents> parse(const QByteArray &archive, QString *error = nullptr);
    static std::optional<Contents> parseFile(const QString &path, QString *error = nullptr);

    /// When a ledger file began; an empty History when it has none or cannot be read.
    static History historyOf(const QString &ledgerFile);
    /// What servers are compared by: scheme, host (lower case), port and path, without a
    /// trailing slash.
    static QString addressKey(const QUrl &url);

    /// How one backup server lines up with this machine.
    struct Match
    {
        ServerPart part;
        QString localId; ///< the matching local server's id; empty when it would be added
        History local;   ///< that server's history here
        /// D4 a: the backup's history would replace this machine's (it began earlier, or
        /// there is none here). False when the backup has none.
        bool backupLonger = false;
    };
    static QList<Match>
    match(const Contents &contents, const QList<Server> &local, const Sources &sources);

    struct Plan
    {
        bool settings = true;
        bool servers = true;
        QStringList history; ///< backup server ids whose history to restore
    };
    struct Outcome
    {
        QString safetyCopy;          ///< where what was here went first
        QStringList addedServers;    ///< ids
        QStringList keptServers;     ///< local ids that matched
        QStringList historyRestored; ///< local ids whose ledger was replaced
        QStringList historyKept;     ///< local ids whose (longer) ledger stayed
    };
    /// Restores `plan`'s parts of `contents` into `sources` and `store`, after the safety
    /// copy. History for a server that neither matches nor is restored is skipped.
    static std::optional<Outcome> restore(const Contents &contents,
                                          const Plan &plan,
                                          ServerStore &store,
                                          const Sources &sources,
                                          qint64 now,
                                          QString *error = nullptr);

    /// `<dir>/<prefix>-yyyy-MM-dd-HHmmss.omaseed` for `now`, local time.
    static QString datedName(const QString &dir, const QString &prefix, qint64 now);
};

} // namespace omaseed::core
