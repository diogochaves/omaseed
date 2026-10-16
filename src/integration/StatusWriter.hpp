#pragma once

// StatusWriter — the file the bar widget `chaves.omaseed` mirrors (T6.1, #12):
// `$XDG_RUNTIME_DIR/omaseed/status.json`, schema v2 as plugin/STATUS.md spells it.
//
// It reads what the app already knows (SyncEngine's GlobalState, torrents, counts and
// categories, the server's preferences) and the few things only it keeps: the upload of the
// last hour, the upload per day (state.json "status" › "week"), completions and ratio
// milestones (state.json "status" › "recent"), and the session's share. The pure figures are
// core::StatusDigest.
//
// Cadence: a change is written at most once per `minInterval` (1 s); with no change the file is
// rewritten every `heartbeat` (10 s) so the widget can tell a live app from a dead one. Speeds
// are rounded to three significant figures before the change test.
//
// Cost (#36): the parts worked out from every torrent (on offer, keeping alive, active) are
// kept until the torrents change or one of them turns quiet, today's top seeders walk only
// the torrents the seed ledger saw give today, and the ratio milestones follow each poll's
// diff, so an idle poll or an upload sample costs the same at ten torrents as at thousands.
// Each write goes to
// `status.json.tmp` (0600, in a 0700 directory) and is renamed into place, so a reader never
// sees half a file. `removeFile()` on a clean quit.
//
// No secret is ever written: the server is its name and host:port, and an error is a phrase.

#include "core/StatusDigest.hpp"

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>

class QFileSystemWatcher;

namespace omaseed::core {
class AltSpeed;
class SeedLedger;
class ServerPreferences;
class SyncEngine;
} // namespace omaseed::core

namespace omaseed::integration {

class BarMenu;

class StatusWriter : public QObject
{
    Q_OBJECT

public:
    static constexpr int schemaVersion = 2;

    explicit StatusWriter(QObject *parent = nullptr);
    ~StatusWriter() override;

    /// "$XDG_RUNTIME_DIR/omaseed/status.json" (Qt's runtime location, then the temporary
    /// directory, when it is unset).
    static QString defaultPath();
    /// Where the file goes; the default until set. Tests point it into a temporary directory.
    void setPath(const QString &path);
    QString path() const { return m_path; }

    /// Defaults 1000 and 10000 ms; tests shorten them.
    void setIntervals(int minIntervalMs, int heartbeatMs);
    /// The time the figures use ("now", "today"); the system clock until set.
    void setClock(std::function<QDateTime()> clock);

    // --- sources ----------------------------------------------------------------------------
    void setEngine(omaseed::core::SyncEngine *engine);
    void setPreferences(omaseed::core::ServerPreferences *preferences);
    /// docs/31 D12: what `alt` says beyond on and the limits (kind, until, byHand). Without
    /// one the set reads as today's "alt", with no end.
    void setAltSpeed(const omaseed::core::AltSpeed *alt);
    /// The seed ledger "today's top seeders" read their upload from (#47). The caller keeps it
    /// alive and fed for as long as the writer has it; without one the list is empty.
    void setLedger(const omaseed::core::SeedLedger *ledger);
    /// docs/33 §11 (D24, DECISIONS C13): the shared menu, written as `servers` and `menu`
    /// (added within v2). Without one both lists are empty and the widget shows no ⋯.
    void setMenu(const BarMenu *menu);
    /// The server the engine is on. A different `id` starts the session over (share, hour,
    /// milestones) and loads that server's week and recent list. `url` keeps only host:port.
    void setServer(const QString &id, const QString &name, const QUrl &url);
    /// Why the server cannot be reached, as a short phrase; empty while it can.
    void setServerError(const QString &error);
    void setWindowShown(bool shown);
    /// A server path to a local one (the server's path map). Identity until set.
    void setPathMapper(std::function<QString(const QString &)> mapper);
    /// Whether the library already holds a torrent with these info-hashes (`inLibrary`).
    void setLibraryCheck(std::function<bool(const QString &, const QString &)> check);
    /// The folder `downloads` lists today's .torrent files from; watched while it exists.
    void setDownloadsFolder(const QString &folder);

    /// SyncEngine::torrentCompleted, with the server's content path.
    void addCompletion(const QString &hash,
                       const QString &name,
                       qint64 size,
                       const QString &category,
                       const QString &contentPath);

    /// Starts sampling and writing; the first write follows at once.
    void start();
    /// Stops the timers, saves the week, and deletes the file (a clean quit). Does nothing
    /// to the file when start() never ran.
    void removeFile();

    /// The document the next write would carry, `t` included.
    QJsonObject snapshot() const;
    int writeCount() const { return m_writeCount; }
    /// How many times the per-torrent parts were worked out from the whole library (tests).
    int torrentPartsBuilds() const { return m_partsBuilds; }

public Q_SLOTS:
    /// Something the file shows may have moved: write it within `minInterval`.
    void markDirty();
    /// Re-reads the category the widget's last add used (state.json "widgetAdd" › "category").
    void reloadAddCategory();

Q_SIGNALS:
    void written();
    /// #24 T7: a torrent's ratio crossed 1, 2 or 5 (the same crossings `recent` keeps).
    void milestone(const QString &hash, const QString &name, int ratio);

private:
    QDateTime now() const;
    void tick();
    /// Writes the document when it changed, or always with `force` (the heartbeat).
    void write(bool force);
    void heartbeat();
    void sampleUpload();
    void onSynced();
    void rescanDownloads();
    void applyDownloads(const QJsonArray &found);
    void loadServerState();
    void saveServerState();
    void pushRecent(const QJsonObject &entry);
    bool writeFile(const QByteArray &bytes);
    QJsonObject content() const;
    QString serverKey() const;

    struct TorrentParts
    {
        qint64 onOffer = 0;
        QJsonObject alive;
        QJsonArray active;
    };
    /// The parts of the document that walk every torrent, for `now`: the kept ones while
    /// they still hold.
    const TorrentParts &torrentParts(const QDateTime &now) const;
    /// Today's top seeders, kept until the torrents, the ledger or the day change.
    const QJsonArray &topSeeders(const QDateTime &now) const;

    QString m_path;
    int m_minInterval = 1000;
    int m_heartbeatInterval = 10000;
    std::function<QDateTime()> m_clock;

    QPointer<omaseed::core::SyncEngine> m_engine;
    QPointer<omaseed::core::ServerPreferences> m_preferences;
    QPointer<const omaseed::core::AltSpeed> m_alt;
    QPointer<const BarMenu> m_menu;
    QString m_serverId;
    QString m_serverName;
    QString m_serverHostPort;
    QString m_serverError;
    bool m_windowShown = false;
    std::function<QString(const QString &)> m_pathMapper;
    std::function<bool(const QString &, const QString &)> m_libraryCheck;

    QString m_downloadsFolder;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_rescanTimer;
    QDate m_scanDate;
    int m_scanGeneration = 0;
    /// `path`, `name`, `size`, `at`, `infoHashV1`, `infoHashV2` of today's files.
    QJsonArray m_downloads;

    omaseed::core::StatusDigest::Counter m_sessionUp;
    omaseed::core::StatusDigest::Counter m_sessionDown;
    omaseed::core::StatusDigest::UploadSampler m_sampler;
    omaseed::core::StatusDigest::WeekLedger m_week;
    omaseed::core::StatusDigest::RatioMilestones m_milestones;
    /// TorrentModel::revision() at the last milestone check; false until a whole-list one.
    quint64 m_milestonesRevision = 0;
    bool m_milestonesFed = false;

    mutable TorrentParts m_parts;
    mutable bool m_partsValid = false;
    mutable const void *m_partsModel = nullptr;
    mutable quint64 m_partsRevision = 0;
    mutable qint64 m_partsAt = 0;      ///< unix seconds they were worked out for
    mutable qint64 m_partsQuietAt = 0; ///< when one of them turns quiet; 0 never
    mutable int m_partsBuilds = 0;
    const omaseed::core::SeedLedger *m_ledger = nullptr;
    mutable QJsonArray m_topSeeders;
    mutable bool m_topValid = false;
    mutable const void *m_topModel = nullptr;
    mutable const void *m_topLedger = nullptr;
    mutable quint64 m_topRevision = 0;
    mutable quint64 m_topLedgerRevision = 0;
    mutable QDate m_topDay;
    QJsonArray m_recent;
    bool m_weekDirty = false;
    QDateTime m_weekSavedAt;
    QString m_addCategory;

    bool m_started = false;
    QTimer m_writeTimer;
    QTimer m_heartbeatTimer;
    QTimer m_sampleTimer;
    QElapsedTimer m_sinceWrite; ///< invalid until the first write
    QJsonObject m_lastContent;
    int m_writeCount = 0;
};

} // namespace omaseed::integration
