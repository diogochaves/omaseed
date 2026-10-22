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
// Every move is a job (board 43, DECISIONS C16, #103): a selection starts one too
// (planSelection(), startSelection(); App::setLocation goes through it), and so do move back
// (planBack(), startBack(), L11) and the failures' retry. A job that isn't a folder's shows
// only after 3 s (L6): one that lands sooner is told by note() (L7), and gives a desktop notice
// only when it showed and ran past a minute or the window was hidden at its end (L8). A move
// started while another runs waits behind it and is checked again when its turn comes (L12).
// The sheet's places (L4): recent destinations per server, the library's folders, the
// categories' folders.
//
// A move that lands goes into the server's MoveHistory (board 45 D7–D8, DECISIONS C18, #133):
// move back stays offered after the summary closes, from the torrent's details, the log and
// the palette (lastMoveOf(), moveLines(), newestMove(), planBackOf(), startBackOf()).
//
// The sheet's half lives in MoveControllerSheet.cpp; the words both use in MoveWords.hpp.

#include "core/FolderModel.hpp"
#include "core/LooseCarry.hpp"
#include "core/LooseFiles.hpp"
#include "core/MoveHistory.hpp"
#include "core/MoveJob.hpp"
#include "core/MovePlan.hpp"
#include "core/ServerFolder.hpp"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
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
#include <optional>

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
    /// A job the band shows: running, paused, or its summary (not Idle); a quiet one only
    /// once it has run 3 s (C16 L6).
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    /// A job that is still moving (or would, resumed) and shows: quit asks first (M13, J5).
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// "folder", "selection", "back", "retry"; "" without a job.
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    /// "films", "6 torrents", a torrent's name.
    Q_PROPERTY(QString title READ title NOTIFY changed)
    /// L12: the moves waiting behind this one, by title, oldest first.
    Q_PROPERTY(QStringList queued READ queued NOTIFY changed)
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
    /// Moves when the history of finished moves changes (C18 D7–D8): read it again.
    Q_PROPERTY(int historyRevision READ historyRevision NOTIFY historyChanged)

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
        /// L4: a server's recent destinations, newest first, from state.json and back.
        std::function<QStringList(const QString &serverId)> loadRecents;
        std::function<void(const QString &serverId, const QStringList &paths)> saveRecents;
        /// C18 D8: a server's finished moves, from state.json and back.
        std::function<QJsonArray(const QString &serverId)> loadHistory;
        std::function<void(const QString &serverId, const QJsonArray &moves)> saveHistory;
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
    QString kind() const;
    QString title() const;
    QStringList queued() const;
    QVariantMap band() const;
    QStringList jobHashes() const;
    int revision() const { return m_revision; }
    QString trayLine() const;
    QString quitLine() const;
    int planRevision() const { return m_planRevision; }
    int rechecksLeft() const { return int(m_rechecks.size()) + (m_recheckHash.isEmpty() ? 0 : 1); }
    int historyRevision() const { return m_historyRevision; }
    const core::MoveHistory &history() const { return m_history; }

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

    // ---- a selection (board 43, C16) ----------------------------------------------------------

    /// The sheet for moving `hashes` to `newPath` by `rule` ("oneFolder", the default, or
    /// "keepPlaces", L5). The keys of plan(), with `loose` empty, and: savePaths (how many),
    /// commonRoot, ruleOffered (two save paths or more), rule. Its checks speak only when they
    /// have something to say; when all pass, one green line (L3).
    Q_INVOKABLE QVariantMap planSelection(const QStringList &hashes,
                                          const QString &newPath,
                                          const QString &rule);
    /// Starts it as a job (or queues it, L12). The file lists or the server's listings it
    /// needs are fetched first, and it starts once they are in. False when it can't move: the
    /// reason goes out through note().
    Q_INVOKABLE bool
    startSelection(const QStringList &hashes, const QString &newPath, const QString &rule);
    /// L11: the sheet for moving the last job's torrents back, each to where it was.
    Q_INVOKABLE QVariantMap planBack();
    Q_INVOKABLE bool startBack();

    // ---- after the summary closes (board 45 D7–D8, C18, #133) --------------------------------

    /// The move back of a finished move in the history, `only` one of its torrents when given
    /// (the details' "this one only"). The sheet's words, as planBack()'s; a torrent not where
    /// that move put it stays, named with the later move that took it ("move that one back
    /// first": a check line's `backFirst`) or as moved outside omaseed.
    Q_INVOKABLE QVariantMap planBackOf(const QString &id, const QString &only = QString());
    Q_INVOKABLE bool startBackOf(const QString &id, const QString &only = QString());
    /// The torrent's last move while the history keeps it, for its details: id, title, when,
    /// from, to, others (the other torrents in it), carried ("loose files too" or ""), canBack
    /// (some of the move can go back), canBackThis (this one can); {} for none.
    Q_INVOKABLE QVariantMap lastMoveOf(const QString &hash) const;
    /// The newest move, for the palette's "undo last move": id, title, when, where, count,
    /// canBack; {} for none.
    Q_INVOKABLE QVariantMap newestMove() const;
    /// The history as log lines, oldest first: at (seconds), message, id.
    QVariantList moveLines() const;
    /// L4: places for the field, `typed` narrowing them: [{path, kind (recent, folder,
    /// category), count, name}], recent first.
    Q_INVOKABLE QVariantList places(const QString &typed) const;
    /// L12: drops the `index`th move waiting behind this one.
    Q_INVOKABLE void dropQueued(int index);

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
    /// E3: the rows of "recheck these", smallest first: [{hash, name, bytes, starts, then,
    /// hybrid, why}]; `starts` when it was running when the job sent it (DECISIONS C20, #138).
    /// A hybrid is not rechecked (C21): `hybrid`, its `then` "not rechecked" and `why` the
    /// line that says why.
    Q_INVOKABLE QVariantList recheckRows(const QStringList &hashes) const;
    /// E3: recheck `hashes` one at a time, smallest first; each that comes out complete
    /// starts, if it was running when the job sent it (C20); the others stay stopped. Hybrids
    /// are left out (C21).
    Q_INVOKABLE void recheckSmallestFirst(const QStringList &hashes);
    /// The same on rows recheckRows() gave earlier, as they said: the sheet's, read when it
    /// opened, so a move again started from it since (which replaces the job) changes nothing.
    Q_INVOKABLE void recheckListed(const QVariantList &rows);
    /// M16 "retry": the failed ones again to the place the job meant for them, as a job of
    /// their own (C16 L9).
    Q_INVOKABLE void retryFailed();
    /// C21: one failed torrent (a hybrid the recheck leaves out) again to the job's target
    /// for it, as retry does. False when it can't start (the reason goes out through note()).
    Q_INVOKABLE bool moveAgain(const QString &hash);
    /// C21: one failed torrent back to where it was before the job, as move back does.
    Q_INVOKABLE bool moveFailedBack(const QString &hash);

    /// J5's "pause and quit" (and any quit): saved now, started again at the next launch.
    Q_INVOKABLE void pauseForQuit();
    /// Writes the job's file now (the app's last breath).
    void saveNow();

Q_SIGNALS:
    void changed();
    void planChanged();
    void rechecksChanged();
    /// L7: a quiet move landed ("2 moved to /srv/media/games"), or didn't (`failed`: the
    /// torrents it is about); also a move that couldn't start, and a queued one dropped.
    void note(const QString &text, const QStringList &hashes, bool failed);
    /// DECISIONS C18 D1–D3 (board 45 §2): the same quiet move for the strip, as
    /// core::ActionOutcome's fields by name (`action` "move" or "moveBack", `hashes`,
    /// `failedHashes`, `name`, `failedName`, `target`, `cause`, `details`).
    void outcome(const QVariantMap &outcome);
    void historyChanged();

private:
    void attachJob();
    void detachJob();
    void rebuildFolders();
    void scheduleFolders();
    void updateMarks();
    void onJobChanged();
    void onFinished();
    /// C18 D1: a quiet move's outcome for the strip (see outcome()).
    QVariantMap quietOutcome(const core::MoveJob::Summary &summary,
                             const QString &cause,
                             const QString &details) const;
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
    /// A job's spec (kind, title, hashes, rule, newPath, targets, expectedAt; a folder's root
    /// and carry) as a plan.
    core::MovePlan planFor(const QJsonObject &spec) const;
    /// Fetches what planFor(spec) still needs; true while something is on its way.
    bool fetchFor(const core::MovePlan &plan);
    QVariantMap selectionWords(const core::MovePlan &plan, const QJsonObject &spec);
    QVariantList
    checkLines(const core::MovePlan &plan, const QString &newPath, const QString &name);
    QJsonObject
    selectionSpec(const QStringList &hashes, const QString &newPath, const QString &rule) const;
    QJsonObject backSpec() const;
    /// C18 D7: a history record's move back, `only` one torrent when given.
    QJsonObject backSpecOf(const QString &id, const QString &only) const;
    /// The library's save path by hash, for MoveHistory::check().
    QHash<QString, QString> savePaths() const;
    /// The record of the job that ran last (by its start), "" for none.
    QString recordIdOfJob() const;
    /// The job landed: its record into the history, or a move back out of it.
    void recordFinished();
    void saveHistory();
    bool startSpec(const QJsonObject &spec);
    void tryPending();
    QJsonObject replan(const QJsonObject &spec) const;
    void rememberDestination(const QString &path);
    void closeQuietSummary();
    /// The job's summary goes, and the next queued move starts.
    void forgetSummary();
    /// A shown job's summary is kept while the next move runs under it, unseen (#105).
    void holdSummary();
    bool holding() const;
    /// The job's record (or its held summary's): `hash` was running when the job sent it.
    bool wasRunning(const QString &hash) const;
    QJsonObject retrySpec() const;
    /// C21: retrySpec() for `hash` alone, or (`back`) that torrent to where it was.
    QJsonObject failedSpec(const QString &hash, bool back) const;
    bool noticeWanted() const;
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
    QStringList m_recents;       ///< L4, the server's, newest first
    core::MoveHistory m_history; ///< C18 D7–D8, the server's
    int m_historyRevision = 0;
    QJsonObject m_pending;  ///< a start waiting for file lists or listings
    QDateTime m_restoredAt; ///< J4's line shows for a while after a restore

    // A shown job's summary, as it read at its end, kept while the move after it runs unseen
    // (L6, L12): the band and its actions read it until it is closed or the job under it
    // shows. Not saved: a restart forgets it.
    struct Held
    {
        QVariantMap band;
        QString kind;
        QString title;
        QString state;
        QString root;
        QString newPath;
        QString details;
        QStringList hashes;
        QStringList failed;
        QSet<QString> running; ///< MoveJob::Entry::wasRunning, for the recheck (C20)
        QVariantList carryRows;
        QHash<QString, QVariantMap> steps;
        QJsonObject back;
        QJsonObject retry;
    };
    std::optional<Held> m_held;

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
    QSet<QString> m_recheckStarts; ///< the ones that start once complete (C20)
    QString m_recheckHash;
    bool m_recheckSeenChecking = false;
    int m_recheckPolls = 0;
};

} // namespace omaseed::app
