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
//   trackerError      a torrent's has_tracker_error turned true. qBittorrent 5.x reports
//                     that flag per torrent inside maindata, so this costs no extra request;
//                     the host comes from the torrent's current `tracker` URL, or, when it
//                     has none left, from the `trackers` map that names the announce URLs
//                     the torrent belongs to. Grouped by host, one signal per host.
//   diskAlmostFull    server_state.free_space_on_disk crossed the threshold downwards.
//   serverLost        the first network or timeout failure, once, not once per poll.
//   serverBack        the first success after that.
//   authRequired      401/403: polling stops until start() is called again.

#include "core/ApiError.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <chrono>

class QTimer;

namespace omaseed::core {

class CategoryModel;
class FilterCounts;
class GlobalState;
class QbtClient;
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

    /// The first full update has been applied.
    void firstSyncFinished();
    /// Every applied poll, full or partial.
    void synced(bool fullUpdate);

    void torrentCompleted(const QString &hash,
                          const QString &name,
                          qint64 sizeBytes,
                          const QString &category,
                          const QString &contentPath);
    void trackerError(const QString &host, const QStringList &hashes);
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
    std::chrono::milliseconds normalInterval() const;

    QbtClient *m_client = nullptr;

    TorrentModel *m_torrents = nullptr;
    CategoryModel *m_categories = nullptr;
    TagModel *m_tags = nullptr;
    TrackerModel *m_trackers = nullptr;
    GlobalState *m_globalState = nullptr;
    FilterCounts *m_counts = nullptr;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;

    bool m_running = false;
    bool m_visible = true;
    bool m_pollInFlight = false;
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
