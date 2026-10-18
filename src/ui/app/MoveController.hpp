#pragma once

// MoveController — moving a folder, as the window sees it (board 42, DECISIONS C15, #97).
//
// The core has the parts: FolderTree / FolderModel (the rail's folders), MovePlan (the
// preflight), LooseFiles (the files no torrent owns), MoveJob and QbtMoveFeed (the job). This
// puts them together for the server the window is on and words them for QML:
//
// - `folders`, the rail's group, rebuilt after each poll (a poll that changes nothing changes
//   nothing), with a move's marks on it (F3);
// - plan(), the move sheet's content (S1–S3): every line of the preflight, worded; the file
//   lists it needs (torrents/files) and the walk for loose files (off the main thread) are
//   fetched as it goes, and `planRevision` moves when there is more to read;
// - the job: one MoveJob per server (M18), saved in its file and taken up again at the next
//   start (M13) or when the window comes back to that server; `band` is the band under the
//   hero (J1–J4, T1–T4, E1, L1); `stepFor()` is the Move column's word (J1);
// - the end: one notice (E2) through the hook App gives; the failures' "recheck smallest
//   first, then start the complete ones" (E3) and "retry" (M16).
//
// Loose files are carried by #98: `carryWanted` finishes at once here, with nothing carried.

#include "core/FolderModel.hpp"
#include "core/LooseCarry.hpp"
#include "core/LooseFiles.hpp"
#include "core/MoveJob.hpp"
#include "core/MovePlan.hpp"
#include "core/ServerFolder.hpp"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

namespace omaseed::core {
class FreeSpaceProbe;
class QbtMoveFeed;
class SyncEngine;
} // namespace omaseed::core

namespace omaseed::app {

class MoveController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(omaseed::core::FolderModel *folders READ folders CONSTANT)
    /// A job the band shows: running, paused, or its summary (not Idle).
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    /// A job that is still moving (or would, resumed): quit asks first (M13, J5).
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// "idle", "running", "pausing", "paused", "ending", "carrying", "done", "ended".
    Q_PROPERTY(QString state READ stateName NOTIFY changed)
    Q_PROPERTY(QString root READ root NOTIFY changed)
    Q_PROPERTY(QString newPath READ newPath NOTIFY changed)
    /// The band: kind (accent, yellow, red, green), glyph, headline, line, failedText,
    /// progress (0–1, or -1 for none), actions [{id, text}].
    Q_PROPERTY(QVariantMap band READ band NOTIFY changed)
    /// The job's torrents, in the order they are sent (J1's "show").
    Q_PROPERTY(QStringList jobHashes READ jobHashes NOTIFY changed)
    /// Moves whenever any step or figure changes, for the Move column.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// J6: "Moving games: 52 %, about 50 min left"; "" without a running job.
    Q_PROPERTY(QString trayLine READ trayLine NOTIFY changed)
    /// J5's line: "212 of 387 moved. Quitting pauses it: …".
    Q_PROPERTY(QString quitLine READ quitLine NOTIFY changed)
    /// Moves when plan() has more to say (a file list or the loose files' walk came in).
    Q_PROPERTY(int planRevision READ planRevision NOTIFY planChanged)
    /// A recheck run (E3) is going: how many are left.
    Q_PROPERTY(int rechecksLeft READ rechecksLeft NOTIFY rechecksChanged)

public:
    struct Hooks
    {
        core::SyncEngine *engine = nullptr;
        const core::FreeSpaceProbe *probe = nullptr;
        std::function<qint64()> lowDiskBytes;
        std::function<QString()> serverName;
        /// One desktop notice: summary, body.
        std::function<void(const QString &, const QString &)> notify;
        /// Tests set the time; the app leaves the system clock.
        std::function<QDateTime()> clock;
    };

    explicit MoveController(QObject *parent = nullptr);
    ~MoveController() override;

    void setHooks(Hooks hooks);

    /// The server the window is on now; "" for none (first run, demo). The job of the one it
    /// leaves is saved and let go; that server's file takes it up again on the way back.
    void setServer(const QString &serverId);
    QString serverId() const { return m_serverId; }

    core::FolderModel *folders() const { return m_folders; }
    core::MoveJob *job() const { return m_job.get(); }

    bool shown() const;
    bool active() const;
    QString stateName() const;
    QString root() const;
    QString newPath() const;
    QVariantMap band() const;
    QStringList jobHashes() const;
    int revision() const { return m_revision; }
    QString trayLine() const;
    QString quitLine() const;
    int planRevision() const { return m_planRevision; }
    int rechecksLeft() const { return int(m_rechecks.size()) + (m_recheckHash.isEmpty() ? 0 : 1); }

    // ---- the sheet (S1–S3) ------------------------------------------------------------------

    /// The sheet for moving `root` to `newPath`, with the loose files carried when `carry`.
    /// Keys: title, header, explain, pathRows [{from, to, count}], checks [{kind, glyph, text,
    /// note, hashes}], loose {state, rows, count, bytes}, carryOffered, carryDefault, blocker,
    /// canMove, moveCount, moveHashes, moveText, queuedBehind.
    Q_INVOKABLE QVariantMap plan(const QString &root, const QString &newPath, bool carry);
    /// S2: every file no torrent owns under `root`, [{path, size, what, kind}], from the last
    /// walk; empty until it has run.
    Q_INVOKABLE QVariantList looseRows(const QString &root) const;
    /// S2's "copy the list": the same, as text.
    Q_INVOKABLE QString looseText(const QString &root) const;
    /// The carried job's loose files that stayed, and why (board 42 L2's "show what stayed").
    Q_INVOKABLE QVariantList carryRows() const;
    /// The same, as text for the clipboard.
    Q_INVOKABLE QString carryText() const;
    /// The scope line's words for a picked folder (F1).
    Q_INVOKABLE QString folderLine(const QString &path) const;
    /// The folder a move starts from when nothing is picked: the save path of `hash`.
    Q_INVOKABLE QString folderOf(const QString &hash) const;
    /// Starts the move (or queues it behind the running one, M18). False when the plan
    /// can't move.
    Q_INVOKABLE bool start(const QString &root, const QString &newPath, bool carry);

    // ---- the job's controls --------------------------------------------------------------

    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void end();
    Q_INVOKABLE void keepWaiting();
    Q_INVOKABLE void closeSummary();
    /// J1: the Move column's word for `hash`: {text, glyph, kind (ok, bad, busy, dim)}, empty
    /// when it isn't in the job.
    Q_INVOKABLE QVariantMap stepFor(const QString &hash) const;
    /// The failed ones, smallest first (E1).
    Q_INVOKABLE QStringList failedHashes() const;
    /// "details": the record as text (old and new path of each, the failures with their log
    /// lines), for the sheet and for c.
    Q_INVOKABLE QString detailsText() const;
    /// E3: the rows of "recheck these": [{hash, name, bytes, then}].
    Q_INVOKABLE QVariantList recheckRows(const QStringList &hashes) const;
    /// E3: recheck `hashes` one at a time, smallest first; each that comes out complete
    /// starts.
    Q_INVOKABLE void recheckSmallestFirst(const QStringList &hashes);
    /// M16 "retry": sends the failed ones again to the place the job meant for them.
    Q_INVOKABLE void retryFailed();

    /// J5's "pause and quit" (and any quit): saved now, started again at the next launch.
    Q_INVOKABLE void pauseForQuit();
    /// Writes the job's file now (the app's last breath).
    void saveNow();

Q_SIGNALS:
    void changed();
    void planChanged();
    void rechecksChanged();

private:
    void attachJob();
    void detachJob();
    void rebuildFolders();
    void scheduleFolders();
    void updateMarks();
    void onJobChanged();
    void onFinished();
    void onPausedItself(core::MoveJob::PauseReason reason);
    void onCarryWanted();
    void onCarryRan(const QString &serverId,
                    const QString &root,
                    const QString &from,
                    const QString &to,
                    const core::LooseCarry::Result &result);
    QString carryFilePath() const;
    bool
    loadCarry(const QString &root, QString *from, QString *to, QList<core::CarryFile> *files) const;
    void stopCarry();
    void onSynced();
    void bump();

    QList<core::Torrent> library() const;
    QString mountRootOf(const QString &serverPath) const;
    QStringList ownedLocalPaths(const QString &root) const;
    void fetchFiles(const QStringList &hashes);
    // C16 L13 (#102): a server whose disks this machine can't see answers for them.
    QStringList ownedServerPaths(const QString &root) const;
    QStringList foldersToList(const QString &root) const;
    core::LooseFiles::OnServer looseOnServer(const QString &root) const;
    void fetchListings(const QStringList &folders);
    void scanLoose(const QString &root);
    core::MovePlan makePlan(const QString &root, const QString &newPath, bool carry) const;
    QDateTime now() const;
    QString serverName() const;
    void recheckNext();

    QPointer<core::FolderModel> m_folders;
    Hooks m_hooks;
    QString m_serverId;
    std::unique_ptr<core::MoveJob> m_job;
    std::unique_ptr<core::QbtMoveFeed> m_feed;
    QTimer m_foldersTimer;
    int m_revision = 0;
    int m_planRevision = 0;
    QDateTime m_restoredAt; ///< J4's line shows for a while after a restore

    // M17: the loose files' carry, run off the main thread after the torrents.
    std::shared_ptr<std::atomic_bool> m_carryStop;
    std::shared_ptr<std::atomic_bool> m_carryRunning;
    bool m_carryPaused = false;
    core::LooseCarry::Progress m_carryProgress;
    QDateTime m_carryStartedAt;
    qint64 m_carryStartBytes = -1;
    core::LooseCarry::Result m_carryResult;
    bool m_carryDone = false;

    // torrents/files, by hash: the names below the save path (for torrents added without a
    // subfolder). Kept for the server it was read from.
    QHash<QString, QStringList> m_files;
    QSet<QString> m_filesPending;

    // app/getDirectoryContent, by folder as the server writes it (C16 L13, #102), for a server
    // whose disks this machine can't see. Kept for the server it was read from; read again in
    // the background once a minute old, the old listing standing until the new one is in.
    QHash<QString, core::ServerFolder> m_serverFolders;
    QHash<QString, QDateTime> m_serverFoldersAt;
    QSet<QString> m_listingsPending;
    bool m_serverLists = true; ///< false once the server said it can't list folders

    struct LooseWalk
    {
        QString root;
        QString local;
        QDateTime at;
        bool running = false;
        core::LooseFiles::Result result;
    };
    QHash<QString, LooseWalk> m_loose; ///< by root
    int m_walkSerial = 0;

    // E3: the recheck run.
    QStringList m_rechecks;
    QString m_recheckHash;
    bool m_recheckSeenChecking = false;
    int m_recheckPolls = 0;
};

} // namespace omaseed::app
