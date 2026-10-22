#pragma once

// SyncEngine — one sync/maindata poll loop for one server, and the models it feeds.
//
// Protocol (WebUI API v2). The first call sends rid=0 and gets `full_update: true` with the
// whole world; every later call sends the rid the server last answered with and gets only
// what moved. Exactly one poll is ever in flight: the next one is scheduled when the
// previous one has been applied, so a slow server slows the loop down instead of queueing up.
//
// Cadence: 1.5 s while the window is visible, 5 s when it is not, 15 s after an error that is
// not a lost connection, and every 10 s with a visible countdown while the server is gone
// (spec §7 Fig. 23). The engine sets the client's maxRetries to 0 while it runs so it owns
// the cadence by itself; the previous value is restored by stop().
//
// Events, for the tray and the notifier (spec §6):
//   torrentCompleted  a known torrent reached progress 1 or got a completion_on in a
//                     *partial* update — never from the full one, which would fire for the
//                     whole library at startup.
//   diskAlmostFull    server_state.free_space_on_disk crossed the threshold downwards.
//   serverLost        the first network or timeout failure, once, not once per poll.
//   serverBack        the first success after that.
//   authRequired      401/403: polling stops until start() is called again.

#include "core/ApiError.hpp"
#include "core/QbtReply.hpp"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <chrono>
#include <optional>

class QTimer;

namespace omaseed::core {

class CategoryModel;
class FilterCounts;
class GlobalState;
class ProblemLog;
class QbtClient;
class ServerLog;
class TagModel;
class TorrentModel;
class TrackerModel;

class SyncEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(qint64 lowDiskThreshold READ lowDiskThreshold WRITE setLowDiskThreshold NOTIFY
                   lowDiskThresholdChanged)

public:
    explicit SyncEngine(QObject *parent = nullptr);
    ~SyncEngine() override;

    /// The client to poll. Not owned; changing it stops the loop and clears the models.
    void setClient(QbtClient *client);
    QbtClient *client() const { return m_client; }

    TorrentModel *torrents() const { return m_torrents; }
    CategoryModel *categories() const { return m_categories; }
    TagModel *tags() const { return m_tags; }
    TrackerModel *trackers() const { return m_trackers; }
    GlobalState *globalState() const { return m_globalState; }
    FilterCounts *counts() const { return m_counts; }
    /// docs/28: why errored torrents are in error, read from the server's log after each poll.
    ProblemLog *problems() const { return m_problemLog; }
    /// docs/32 §6 (#56): the whole log for the log face, read after each poll while it shows.
    ServerLog *serverLog() const { return m_serverLog; }

    bool isRunning() const { return m_running; }
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    /// The last rid the server answered with; 0 means "ask for a full update next".
    int rid() const { return m_rid; }

    qint64 lowDiskThreshold() const { return m_lowDiskThreshold; }
    void setLowDiskThreshold(qint64 bytes);

    /// Applies one sync/maindata-shaped object straight into the models, with no network
    /// and no poll loop behind it: `--demo table` and the tests fill the views with
    /// fictional data through this. Everything a real poll does to the models happens
    /// (firstSyncFinished included, on the first full update); nothing that belongs to the
    /// loop does — the engine stays stopped, no client is needed, and `rid` only moves if
    /// the object carries one.
    void applyDemoData(const QJsonObject &data);

    /// A torrent field the server has been asked to change (a torrent option from the menu or
    /// General, M12). The model shows `value` at once instead of 1.5–5 s later, and no poll
    /// puts the old value back while the request is out: a poll that carries `key` for `hash`
    /// only tells the engine what the server holds. `current` is that value as the model had
    /// it, which is what the server holds until a poll says otherwise. Calls stack: each one is
    /// answered by one settlePendingTorrentValue().
    void setPendingTorrentValue(const QString &hash,
                                const QString &key,
                                const QJsonValue &value,
                                const QJsonValue &current);
    /// The server answered one request for `key` on `hash`. Refused, and with nothing else
    /// asked meanwhile, the model goes back to what the server holds at once; accepted, it
    /// keeps `value` until a poll sent after this answer has been applied, and from then on
    /// maindata is authoritative again — the value the server reported (or, when no poll
    /// carried the field, the one it held before) replaces it. A server that took the request
    /// and ignored it therefore ends up showing its real value.
    void settlePendingTorrentValue(const QString &hash, const QString &key, bool refused);
    /// The value a pending change is heading to, or nothing when `key` is not pending on `hash`.
    std::optional<QJsonValue> pendingTorrentValue(const QString &hash, const QString &key) const;
    /// How many torrent fields are pending; the tests read it.
    int pendingTorrentValueCount() const;

    /// DECISIONS C20 (#142): which errored torrents are stopped, from qBittorrent's `stopped`
    /// filter (`state` says only `error`), into TorrentModel::setErrorStops(). Runs after every
    /// sync, and is called after a stop or start was sent; one small request while some
    /// torrent is in error, none otherwise. A call while a read is out reads again after it.
    void readErrorStops();

    /// Shortens every interval; the tests use it to watch the cadence without waiting.
    void setIntervals(std::chrono::milliseconds visible,
                      std::chrono::milliseconds hidden,
                      std::chrono::milliseconds error,
                      std::chrono::milliseconds retry);

public Q_SLOTS:
    /// Probes the server (version, API) and starts polling. Calling it again while running
    /// does nothing; calling it after authRequired() starts a fresh attempt.
    void start();
    /// Stops the loop and leaves the models as they are (the disconnected state shows them).
    void stop();
    /// Skips the rest of the countdown and polls now.
    void retryNow();

Q_SIGNALS:
    void runningChanged();
    void visibleChanged();
    void lowDiskThresholdChanged();

    /// The first full update for this client has been applied (once per setClient(), not
    /// again after a stop() and start()).
    void firstSyncFinished();
    /// Every applied poll, full or partial.
    void synced(bool fullUpdate);

    void torrentCompleted(const QString &hash,
                          const QString &name,
                          qint64 sizeBytes,
                          const QString &category,
                          const QString &contentPath);
    void diskAlmostFull(qint64 freeBytes);
    void serverLost(const omaseed::core::ApiError &error);
    void serverBack();
    void authRequired(const omaseed::core::ApiError &error);

private:
    void schedule(std::chrono::milliseconds delay);
    void poll();
    void onPollResult(const QJsonObject &data);
    void onPollError(const ApiError &error);
    void applyData(const QJsonObject &data);
    void enterLostState(const ApiError &error);
    void tickCountdown();
    /// docs/44 W1: silentSeconds from m_lastReply, and the timer that counts it, on while
    /// the latest failure is a timeout.
    void updateSilence(bool timedOut);
    std::chrono::milliseconds normalInterval() const;
    /// Keeps pending values over what a poll carries, noting what the server reported.
    QJsonObject withPendingValues(QJsonObject torrents, const QJsonArray &removed, bool full);
    /// Hands the fields whose pending time is over back to what the server holds.
    void settleAnsweredValues();
    /// Puts `released` ({hash: {key: value}}) into the rows that still exist.
    void patchTorrents(const QJsonObject &released);

    /// setPendingTorrentValue()'s bookkeeping for one field of one torrent.
    struct PendingValue
    {
        QJsonValue target;           ///< what the model shows meanwhile
        QJsonValue serverValue;      ///< what the server holds, as far as the polls said
        int unanswered = 0;          ///< requests sent and not answered yet
        bool answered = false;       ///< every request answered and accepted
        quint64 settleAfterPoll = 0; ///< answered: settle once a later poll is applied
        QDeadlineTimer expiry;       ///< a server that never answers does not pin it forever
    };
    /// hash → JSON key → the pending change.
    QHash<QString, QHash<QString, PendingValue>> m_pending;
    /// sync/maindata requests sent so far, and the number of the last one applied; a poll
    /// sent after an answer is the first that is sure to show it.
    quint64 m_pollsSent = 0;
    quint64 m_appliedPoll = 0;

    QbtClient *m_client = nullptr;

    TorrentModel *m_torrents = nullptr;
    CategoryModel *m_categories = nullptr;
    TagModel *m_tags = nullptr;
    TrackerModel *m_trackers = nullptr;
    GlobalState *m_globalState = nullptr;
    FilterCounts *m_counts = nullptr;
    ProblemLog *m_problemLog = nullptr;
    ServerLog *m_serverLog = nullptr;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;
    /// docs/44 W1: counts GlobalState::silentSeconds up while the server times out.
    QTimer *m_silenceTimer = nullptr;
    /// Since the server last replied (or the run started, before it ever did).
    QElapsedTimer m_lastReply;

    bool m_running = false;
    bool m_visible = true;
    bool m_pollInFlight = false;
    /// The probe or sync/maindata request in flight, so stop() can abort it.
    QPointer<QbtReplyBase> m_pollReply;
    /// C20 (#142): the `stopped` filter's read in flight, and whether another was asked for
    /// meanwhile.
    QPointer<QbtReplyBase> m_errorStopsReply;
    bool m_errorStopsAgain = false;
    /// Bumped by stop() (and so by setClient()); a reply from an older generation is dropped.
    quint64 m_generation = 0;
    bool m_probed = false;
    bool m_hadFullUpdate = false;
    bool m_lost = false;
    bool m_lowDiskReported = false;
    int m_rid = 0;
    int m_savedMaxRetries = -1;

    qint64 m_lowDiskThreshold = 2LL * 1024 * 1024 * 1024; ///< 2 GiB (spec §6 "disk almost full")

    std::chrono::milliseconds m_visibleInterval{1500};
    std::chrono::milliseconds m_hiddenInterval{5000};
    std::chrono::milliseconds m_errorInterval{15000};
    std::chrono::milliseconds m_retryInterval{10000};
};

} // namespace omaseed::core
