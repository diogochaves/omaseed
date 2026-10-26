#pragma once

// MoveJob — a folder's move, followed for as long as it takes (board 42 §4–§6, DECISIONS C15
// M6, M10–M16, M18, #96).
//
// It starts from a MovePlan and sends its torrents in waves: the smallest alone first (its
// answer is the server's word on "can you write there"), then the rest smallest first, keeping
// three with the server (one moving, two behind it: qBittorrent moves one at a time, so this
// costs no speed). Before each send it reads the room again and pauses itself rather than
// leave the drive under the low-disk threshold.
//
// What it hears is engine-neutral: the torrents as each poll leaves them (setTorrents(),
// onSynced()), a failure with its reason (onFailed()), the answer to a send (onSendResult()),
// whether the server can be reached (setConnected()). What it asks for is a signal,
// sendRequested(hashes, location). QbtMoveFeed ties it to qBittorrent; another engine would
// feed it the same way.
//
// A torrent has landed when a poll shows its save path at the target and it isn't moving:
// that reads the same in any language. The one "moving now" is the first one sent that hasn't
// landed (qBittorrent works through its queue in the order sent).
//
// Stuck (M14): nothing landed for 4 × the current torrent's expected time, and at least 10 min
// (a file growing at the destination counts as progress, through growthProbe). It stops
// sending until told to keep waiting, or until the server comes back after a restart, when
// what hadn't landed is sent again.
//
// The job is saved to its file as it goes (at most once a second) and taken up again by
// restore() at the next start (M13); it is kept after the end until the summary is closed
// (closeSummary()), and for 24 h after it, errors and missing files among its torrents are
// tied to its record (M15).
//
// Every move is a job (DECISIONS C16 L1, #103): a folder's, a selection's, a move back's (L11)
// or a retry's. Its `spec` (kind, title, the sheet's inputs) travels with it; a job queued
// behind another is planned again from it when its turn comes (L12), through replan. A job
// that isn't a folder's is quiet (L6): it shows only once it has run for the show delay (3 s)
// or paused itself; one that lands sooner is told in a line (L7). While a torrent moves, its
// bytes at the target are read every 2 s through the growth probe (L10): progress, speed and
// time left for one big torrent, and the progress that tells "stuck" from "slow" (M14).

#include "core/ApiError.hpp"
#include "core/MovePlan.hpp"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <functional>

namespace omaseed::core {

class TorrentModel;

class MoveJob : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,     ///< no job
        Running,  ///< sending and following
        Pausing,  ///< nothing more is sent; waiting for the ones with the server
        Paused,   ///< see pauseReason()
        Ending,   ///< "end the move": as Pausing, then Ended
        Carrying, ///< the torrents are done; loose files are being carried (M17)
        Done,     ///< every torrent landed, failed or was left out; the summary shows
        Ended,    ///< ended early; the summary shows
    };
    Q_ENUM(State)

    enum class PauseReason {
        None,
        User,        ///< pause
        Quit,        ///< "pause and quit" (M13)
        FirstFailed, ///< the first torrent didn't move (M6, T2)
        Refused,     ///< the server refused a send (409, 403)
        NoSpace,     ///< "No space left on device" in the log (T3)
        NearlyFull,  ///< the next one would leave the drive under the threshold (T3)
        Stuck,       ///< M14, T4
    };
    Q_ENUM(PauseReason)

    enum class Step {
        Waiting, ///< not sent yet
        Queued,  ///< with the server, behind another
        Moving,  ///< with the server, the one moving now
        Moved,
        Failed,
        LeftOut,      ///< M8 / spans: never sent
        AlreadyThere, ///< skipped
        Removed,      ///< removed from the server during the job
        Elsewhere,    ///< moved by someone else (its category, automatic management)
    };
    Q_ENUM(Step)

    struct Entry
    {
        QString hash;
        QString name;
        QString from;
        QString to;
        qint64 bytes = 0;
        Step step = Step::Waiting;
        QString reason;   ///< a failure: libtorrent's text, or the refusal
        QString causeKey; ///< noAccess, diskFull… (TorrentProblem)
        QString line;     ///< the log's line, for details
        QString why;      ///< LeftOut: the path that decided it
        QDateTime sentAt;
        QDateTime movingSince; ///< when it became the one moving now
        QDateTime landedAt;
        int sends = 0;
        int quietPolls = 0;   ///< polls in a row it read neither moving nor landed
        bool unsure = false;  ///< a send that got no answer: the next polls tell
        bool autoTmm = false; ///< it used automatic management before the move (L11)
        /// It was running (not stopped, errored or missing its files) when the job first sent
        /// it: a recheck after the move starts only these (DECISIONS C20, #138). Saved with
        /// the job, so a pause and quit keeps it.
        bool wasRunning = false;
        QString later; ///< after the end: "error" or "missingFiles" within 24 h
    };

    /// What the summary band and the notice say (E1, E2).
    struct Summary
    {
        QString kind;  ///< "folder", "selection", "back", "retry"
        QString title; ///< "films", "6 torrents"
        QString root;
        QString newPath;
        int total = 0; ///< torrents the job meant to move
        int moved = 0;
        int failed = 0;
        int leftOut = 0;
        int alreadyThere = 0;
        int removed = 0;
        int elsewhere = 0;
        int notMoved = 0; ///< still waiting when it ended early
        qint64 bytesMoved = 0;
        QDateTime startedAt;
        QDateTime endedAt;
        bool endedEarly = false;
        QStringList failedHashes; ///< smallest first
    };

    explicit MoveJob(QObject *parent = nullptr);

    // ---- what it needs ---------------------------------------------------------------------

    /// The rows it reads on each poll. Not owned.
    void setTorrents(TorrentModel *torrents);
    /// Tests set the time; the app leaves the system clock.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    /// Free bytes at a server path, -1 when this machine can't tell. Read before every send.
    void setRoomProbe(std::function<qint64(const QString &serverPath)> probe)
    {
        m_roomProbe = std::move(probe);
    }
    /// Bytes of `hash` already at `target`, -1 unknown: a file growing there is progress.
    void setGrowthProbe(std::function<qint64(const QString &hash, const QString &target)> probe)
    {
        m_growthProbe = std::move(probe);
        updateLiveTimer();
    }
    void setLowDiskThreshold(qint64 bytes) { m_lowDiskThreshold = bytes; }
    /// M13: `~/.config/omaseed/moves/<serverId>.json`, where the app keeps a server's job.
    static QString defaultFilePath(const QString &serverId);
    /// Where the job is saved; empty saves nothing. restore() reads it.
    void setFilePath(const QString &path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }
    /// How many it keeps with the server at once (3).
    void setAhead(int ahead) { m_ahead = std::max(1, ahead); }

    // ---- starting, and the controls ---------------------------------------------------------

    /// Starts `plan` (canMove() must hold). `serverId` names the server in the file. With a
    /// job running, the plan waits behind it instead (M18) and this returns false. `spec`
    /// says what it is (`kind`, `title`) and how to plan it again (replan()).
    bool start(const MovePlan &plan,
               const QString &serverId,
               bool carryLoose = false,
               qint64 looseBytes = 0,
               const QJsonObject &spec = {});
    /// The plan as the job keeps it (the file, the queue).
    static QJsonObject
    planJson(const MovePlan &plan, bool carryLoose, qint64 looseBytes, const QJsonObject &spec);
    /// L12: plans a queued job again from its spec when its turn comes: the plan's json
    /// (planJson()), or an empty object when it can't move any more (it is dropped).
    void setReplan(std::function<QJsonObject(const QJsonObject &spec)> replan)
    {
        m_replan = std::move(replan);
    }
    /// Plans waiting behind the running one, oldest first: their roots.
    QStringList queuedRoots() const;
    /// The same, by title ("films", "2 torrents"), and their specs.
    QStringList queuedTitles() const;
    QList<QJsonObject> queuedSpecs() const;
    /// Drops a plan waiting behind the running one.
    void dropQueued(int index);

    void pause(PauseReason reason = PauseReason::User);
    void resume();
    /// End the move (J3): nothing more is sent, nothing moves back.
    void end();
    /// T4's "keep waiting": back to running, the clock for stuck starts again.
    void keepWaiting();
    /// Loose files are carried (#98): the job is Done with these figures.
    void finishCarry(int carried, int stayed);
    /// Closes the summary: the job is forgotten (its file goes), and the next queued one starts.
    void closeSummary();

    /// Takes up the job saved in filePath(). False when there is none (or it can't be read).
    bool restore();
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);
    /// Writes the file now (the app calls it on quit).
    void saveNow();

    // ---- what it hears ---------------------------------------------------------------------

    /// One poll was applied. `fullUpdate`: the server may have restarted (its queue is gone).
    void onSynced(bool fullUpdate = false);
    /// The server says `hash` didn't move, at `at` (its log), for `reason`.
    void onFailed(const QString &hash,
                  const QDateTime &at,
                  const QString &reason,
                  const QString &causeKey,
                  const QString &line);
    /// The answer to a sendRequested().
    void onSendResult(const QStringList &hashes, const ApiError &error);
    /// Whether the server can be reached and is the one open (C12: no requests otherwise).
    void setConnected(bool connected);

    // ---- what it shows ---------------------------------------------------------------------

    State state() const { return m_state; }
    bool isActive() const;
    QJsonObject spec() const { return m_spec; }
    /// "folder", "selection", "back" (L11), "retry" (M16).
    QString kind() const;
    /// "films" for a folder; a selection's from its spec ("6 torrents").
    QString title() const;
    /// L6: a job that isn't a folder's shows only after the show delay, or once it pauses.
    /// C16 L6: a selection's move shows no band at first. One that carries loose files (a move
    /// back's, C32 D19) shows it as a folder's does, so what stayed is named in its summary.
    bool isQuiet() const { return kind() != QLatin1String("folder") && !m_carryLoose; }
    bool wasShown() const { return m_shown; }
    void setShowDelay(std::chrono::milliseconds delay) { m_showTimer.setInterval(delay); }
    /// L10: how often the moving torrent's bytes at its target are read (2 s).
    void setLiveEvery(std::chrono::milliseconds every) { m_liveTimer.setInterval(every); }
    /// Reads the moving torrent's bytes at its target now (the timer's tick).
    void sampleLive();
    /// L10: `hash`'s bytes at its target while it moves, -1 when it isn't moving or this
    /// machine can't tell.
    qint64 liveBytes(const QString &hash) const;
    /// L11: the torrents that moved, by hash: where they were, and where they are now.
    QHash<QString, QString> movedFrom() const;
    QHash<QString, QString> movedTo() const;
    /// L11: those of them that used automatic management before (it stays off).
    QStringList hadAutoTmm() const;
    PauseReason pauseReason() const { return m_pauseReason; }
    QString serverId() const { return m_serverId; }
    QString root() const { return m_root; }
    QString newPath() const { return m_newPath; }
    bool sameDrive() const { return m_sameDrive; } ///< "renaming": no time left
    bool carryLoose() const { return m_carryLoose; }
    qint64 looseBytes() const { return m_looseBytes; }
    bool isConnected() const { return m_connected; }
    const QList<Entry> &entries() const { return m_entries; }
    const Entry *entry(const QString &hash) const;
    /// The hashes the job follows (to send, with the server, done): for ProblemLog::watchJob.
    QStringList hashes() const;

    int total() const; ///< torrents it means to move
    int moved() const { return count(Step::Moved); }
    int failed() const { return count(Step::Failed); }
    int withServer() const { return count(Step::Queued) + count(Step::Moving); }
    int waiting() const { return count(Step::Waiting); }
    int count(Step step) const;
    qint64 bytesTotal() const;
    qint64 bytesMoved() const;
    int percent() const;
    /// Bytes landed per second over the last 10 min of landings; 0 before the first.
    qint64 speed() const;
    /// Seconds left, -1 when it can't tell (no landing yet, or a rename).
    qint64 secondsLeft() const;
    /// The one moving now, or empty.
    QString current() const;
    QDateTime startedAt() const { return m_startedAt; }
    QDateTime lastLandedAt() const { return m_lastLandedAt; }
    /// M14: how long the current one should take at this job's speed (0 unknown).
    std::chrono::seconds expected() const;
    /// J4: landed while omaseed was closed, and for how long it was, after restore().
    int landedWhileAway() const { return m_landedWhileAway; }
    qint64 secondsAway() const { return m_secondsAway; }
    /// T3: what the paused "nearly full" was about.
    QString nearlyFullTorrent() const { return m_nearlyFullHash; }
    qint64 nearlyFullFreeAfter() const { return m_nearlyFullFreeAfter; }
    Summary summary() const;
    /// The torrents saved under the folder now that the job doesn't know and that qBittorrent
    /// added after it started (#164): added during it.
    QStringList addedDuring() const;

Q_SIGNALS:
    /// Ask the server to move `hashes` to `location`; answer with onSendResult().
    void sendRequested(const QStringList &hashes, const QString &location);
    /// Any figure changed.
    void changed();
    void stateChanged();
    /// The job paused itself (T2–T4): one notice (E2).
    void pausedItself(omaseed::core::MoveJob::PauseReason reason);
    /// The torrents are done and loose files are to be carried (#98).
    void carryWanted();
    /// L12: a queued job that can't move any more was dropped when its turn came.
    void queuedDropped(const QString &title);
    /// Done or Ended: the summary and the one notice.
    void finished();

private:
    QDateTime now() const;
    Entry *find(const QString &hash);
    /// Whether the library has `hash` running now (Entry::wasRunning).
    bool isRunningNow(const QString &hash) const;
    void setState(State state);
    void pump();
    void settle();
    void checkStuck();
    void landed(Entry &entry);
    void finishTorrents();
    void touch();
    void save();
    void reset();
    void startPlan(const QJsonObject &plan);
    void startNext();
    void updateLiveTimer();

    QPointer<TorrentModel> m_torrents;
    std::function<QDateTime()> m_clock;
    std::function<qint64(const QString &)> m_roomProbe;
    std::function<qint64(const QString &, const QString &)> m_growthProbe;
    std::function<QJsonObject(const QJsonObject &)> m_replan;
    qint64 m_lowDiskThreshold = 2LL * 1024 * 1024 * 1024;
    int m_ahead = 3;
    QString m_filePath;

    State m_state = State::Idle;
    PauseReason m_pauseReason = PauseReason::None;
    QString m_serverId;
    QJsonObject m_spec;
    bool m_shown = false;
    QTimer m_showTimer;
    QTimer m_liveTimer;
    QString m_root;
    QString m_newPath;
    bool m_sameDrive = false;
    bool m_carryLoose = false;
    qint64 m_looseBytes = 0;
    int m_carried = -1;
    int m_stayed = -1;
    QList<Entry> m_entries;
    QList<QJsonObject> m_queued; ///< plans waiting behind this one (M18)
    bool m_connected = true;
    bool m_probeDone = false; ///< the first torrent landed (or the job went past it)
    QDateTime m_startedAt;
    QDateTime m_endedAt;
    QDateTime m_runningSince; ///< for the speed: since the last start or resume
    QDateTime m_lastLandedAt;
    QDateTime m_progressAt; ///< the last sign of progress, for stuck
    // L10: the moving torrent's bytes at its target.
    QString m_liveHash;
    qint64 m_liveBytes = -1;
    qint64 m_liveFirstBytes = -1;
    QDateTime m_liveFirstAt;
    /// #162: its first reading was already its whole size, so its readings mean nothing.
    bool m_liveBlind = false;
    QString m_nearlyFullHash;
    qint64 m_nearlyFullFreeAfter = 0;
    int m_landedWhileAway = 0;
    qint64 m_secondsAway = 0;
    bool m_restored = false; ///< restore() ran and no poll has been applied since
    QDateTime m_savedAt;     ///< from the file, for J4
    QTimer m_saveTimer;
};

} // namespace omaseed::core
