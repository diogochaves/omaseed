#include "core/SyncEngine.hpp"

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/TagModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QTimer>

namespace omaseed::core {

namespace {

QLatin1StringView key(const char *name)
{
    return QLatin1StringView(name);
}

} // namespace

SyncEngine::SyncEngine(QObject *parent)
    : QObject(parent)
    , m_torrents(new TorrentModel(this))
    , m_categories(new CategoryModel(this))
    , m_tags(new TagModel(this))
    , m_trackers(new TrackerModel(this))
    , m_globalState(new GlobalState(this))
    , m_counts(new FilterCounts(this))
    , m_pollTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    m_counts->setModel(m_torrents);

    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &SyncEngine::poll);

    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &SyncEngine::tickCountdown);
}

SyncEngine::~SyncEngine()
{
    if (m_client != nullptr && m_savedMaxRetries >= 0)
        m_client->setMaxRetries(m_savedMaxRetries);
}

void SyncEngine::setClient(QbtClient *client)
{
    if (m_client == client)
        return;
    stop();
    m_client = client;
    m_rid = 0;
    m_probed = false;
    m_hadFullUpdate = false;
    m_lowDiskReported = false;
    m_torrents->clear();
    m_categories->clear();
    m_tags->clear();
    m_trackers->clear();
    m_globalState->reset();
}

void SyncEngine::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    Q_EMIT visibleChanged();

    // A window that comes back wants fresh numbers now, not in five seconds.
    if (m_running && !m_lost && !m_pollInFlight) {
        m_pollTimer->stop();
        if (visible)
            poll();
        else
            schedule(normalInterval());
    }
}

void SyncEngine::setLowDiskThreshold(qint64 bytes)
{
    if (m_lowDiskThreshold == bytes)
        return;
    m_lowDiskThreshold = bytes;
    m_lowDiskReported = false;
    Q_EMIT lowDiskThresholdChanged();
}

void SyncEngine::setIntervals(std::chrono::milliseconds visible,
                              std::chrono::milliseconds hidden,
                              std::chrono::milliseconds error,
                              std::chrono::milliseconds retry)
{
    m_visibleInterval = visible;
    m_hiddenInterval = hidden;
    m_errorInterval = error;
    m_retryInterval = retry;
}

std::chrono::milliseconds SyncEngine::normalInterval() const
{
    return m_visible ? m_visibleInterval : m_hiddenInterval;
}

void SyncEngine::start()
{
    if (m_client == nullptr || m_running)
        return;

    m_running = true;
    m_lost = false;
    m_rid = 0;
    m_hadFullUpdate = false;
    m_globalState->setSyncing(true);
    m_globalState->setRetryInSeconds(0);
    Q_EMIT runningChanged();

    // The engine owns the retry cadence; the client must not double it with its own.
    if (m_savedMaxRetries < 0)
        m_savedMaxRetries = m_client->maxRetries();
    m_client->setMaxRetries(0);

    if (!m_probed) {
        m_client->probe()->then(this, [this](const QbtResult<ServerInfo> &result) {
            if (!m_running)
                return;
            if (result.isError()) {
                onPollError(result.error());
                return;
            }
            m_probed = true;
            m_globalState->setServerVersions(result.value().version, result.value().apiVersion);
            poll();
        });
        return;
    }

    poll();
}

void SyncEngine::stop()
{
    m_pollTimer->stop();
    m_countdownTimer->stop();
    m_globalState->setSyncing(false);
    m_globalState->setRetryInSeconds(0);

    if (m_client != nullptr && m_savedMaxRetries >= 0) {
        m_client->setMaxRetries(m_savedMaxRetries);
        m_savedMaxRetries = -1;
    }

    if (!m_running)
        return;
    m_running = false;
    Q_EMIT runningChanged();
}

void SyncEngine::retryNow()
{
    if (!m_running || m_pollInFlight)
        return;
    m_pollTimer->stop();
    m_countdownTimer->stop();
    m_globalState->setRetryInSeconds(0);
    poll();
}

void SyncEngine::schedule(std::chrono::milliseconds delay)
{
    if (!m_running)
        return;
    m_pollTimer->start(delay);
}

void SyncEngine::poll()
{
    if (!m_running || m_client == nullptr || m_pollInFlight)
        return;

    m_pollInFlight = true;
    m_client->syncMainData(m_rid)->then(this, [this](const QbtResult<QJsonObject> &result) {
        m_pollInFlight = false;
        if (!m_running)
            return;
        if (result.isError())
            onPollError(result.error());
        else
            onPollResult(result.value());
    });
}

void SyncEngine::onPollResult(const QJsonObject &data)
{
    const bool wasLost = m_lost;
    m_lost = false;
    m_countdownTimer->stop();
    m_globalState->setRetryInSeconds(0);
    m_globalState->setLastError(ApiError());
    m_globalState->setConnected(true);
    m_globalState->setLastSyncTime(QDateTime::currentDateTime());

    applyData(data);

    if (wasLost)
        Q_EMIT serverBack();

    schedule(normalInterval());
}

void SyncEngine::onPollError(const ApiError &error)
{
    m_globalState->setLastError(error);

    switch (error.kind()) {
    case ApiError::Kind::Cancelled:
        return; // stop() or cancelAll(): nothing to report, nothing to reschedule
    case ApiError::Kind::Unauthorized:
    case ApiError::Kind::Forbidden:
        m_globalState->setConnected(false);
        m_globalState->setSyncing(false);
        stop();
        Q_EMIT authRequired(error);
        return;
    case ApiError::Kind::Network:
    case ApiError::Kind::Timeout:
        enterLostState(error);
        return;
    default:
        break;
    }

    // Parse, 4xx, 5xx: the server is there but unhappy. Back off, keep what we have.
    m_globalState->setSyncing(false);
    schedule(m_errorInterval);
}

void SyncEngine::enterLostState(const ApiError &error)
{
    m_globalState->setConnected(false);
    m_globalState->setSyncing(false);
    // The next successful poll must rebuild everything: a server that came back may have
    // forgotten our rid entirely.
    m_rid = 0;

    const bool firstLoss = !m_lost;
    m_lost = true;

    m_globalState->setRetryInSeconds(int(m_retryInterval.count() / 1000));
    m_countdownTimer->start();
    schedule(m_retryInterval);

    if (firstLoss)
        Q_EMIT serverLost(error);
}

void SyncEngine::tickCountdown()
{
    const int next = m_globalState->retryInSeconds() - 1;
    m_globalState->setRetryInSeconds(next > 0 ? next : 0);
    if (next <= 0)
        m_countdownTimer->stop();
}

void SyncEngine::applyData(const QJsonObject &data)
{
    const bool fullUpdate = data.value(key("full_update")).toBool(false);
    const QJsonObject torrents = data.value(key("torrents")).toObject();

    // What to announce afterwards, decided against the state before the merge.
    QStringList completed;
    QStringList newTrackerErrors;
    if (!fullUpdate) {
        for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
            const int row = m_torrents->indexOfHash(it.key());
            if (row < 0)
                continue; // a torrent we have never seen: added, not completed
            const Torrent &before = m_torrents->torrentAt(row);
            const QJsonObject diff = it.value().toObject();

            const bool nowComplete = diff.value(key("progress")).toDouble(before.progress) >= 1.0;
            const bool hasCompletionOn = diff.value(key("completion_on")).toInteger(0) > 0;
            if ((nowComplete || hasCompletionOn) && !before.isComplete() &&
                !before.completionOn.isValid())
                completed.append(it.key());

            const auto errorIt = diff.constFind(key("has_tracker_error"));
            if (errorIt != diff.constEnd() && errorIt->toBool(false) && !before.hasTrackerError)
                newTrackerErrors.append(it.key());
        }
    }

    if (fullUpdate) {
        m_torrents->applyFullUpdate(torrents);
        m_categories->applyFullUpdate(data.value(key("categories")).toObject());
        m_tags->applyFullUpdate(data.value(key("tags")).toArray());
        m_trackers->applyFullUpdate(data.value(key("trackers")).toObject());
    } else {
        m_torrents->applyPartial(torrents, data.value(key("torrents_removed")).toArray());
        m_categories->applyPartial(data.value(key("categories")).toObject(),
                                   data.value(key("categories_removed")).toArray());
        m_tags->applyPartial(data.value(key("tags")).toArray(),
                             data.value(key("tags_removed")).toArray());
        m_trackers->applyPartial(data.value(key("trackers")).toObject(),
                                 data.value(key("trackers_removed")).toArray());
    }

    // FilterCounts has already recomputed (TorrentModel::updated fires synchronously), so
    // the trackerless bucket is one lookup away.
    m_trackers->setTrackerlessHashes(m_counts->trackerlessHashes());

    m_globalState->applyServerState(data.value(key("server_state")).toObject());

    const int nextRid = data.value(key("rid")).toInt(m_rid);
    m_rid = nextRid > 0 ? nextRid : m_rid;

    if (fullUpdate) {
        m_globalState->setSyncing(false);
        if (!m_hadFullUpdate) {
            m_hadFullUpdate = true;
            Q_EMIT firstSyncFinished();
        }
    }

    for (const QString &hash : completed) {
        const Torrent &t = m_torrents->torrent(hash);
        Q_EMIT torrentCompleted(hash, t.name, t.size, t.category, t.contentPath);
    }

    if (!newTrackerErrors.isEmpty()) {
        QMap<QString, QStringList> byHost;
        for (const QString &hash : newTrackerErrors) {
            const Torrent &t = m_torrents->torrent(hash);
            QStringList hosts;
            if (!t.tracker.isEmpty()) {
                hosts.append(trackerHost(t.tracker));
            } else {
                // No working tracker left: name every host this torrent announces to.
                for (const QString &host : m_trackers->hosts()) {
                    if (host != trackerlessKey() && m_trackers->hashesFor(host).contains(hash))
                        hosts.append(host);
                }
            }
            if (hosts.isEmpty())
                hosts.append(trackerlessKey());
            for (const QString &host : hosts)
                byHost[host].append(hash);
        }
        for (auto it = byHost.constBegin(); it != byHost.constEnd(); ++it)
            Q_EMIT trackerError(it.key(), it.value());
    }

    const qint64 freeSpace = m_globalState->freeSpaceOnDisk();
    if (freeSpace > 0 && freeSpace < m_lowDiskThreshold) {
        if (!m_lowDiskReported) {
            m_lowDiskReported = true;
            Q_EMIT diskAlmostFull(freeSpace);
        }
    } else {
        m_lowDiskReported = false;
    }

    Q_EMIT synced(fullUpdate);
}

} // namespace omaseed::core
