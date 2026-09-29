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
#include <QTimer>

#include <algorithm>
#include <iterator>

namespace omaseed::core {

namespace {

QLatin1StringView key(const char *name)
{
    return QLatin1StringView(name);
}

/// How long a pending torrent value may wait for its answer and a poll that shows it (a lost
/// server answers neither): after that, what the server last said wins.
constexpr std::chrono::seconds kPendingLifetime{30};

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
    m_pending.clear();
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
    // m_hadFullUpdate stays: a restart (retry after a refused login, a window-driven start)
    // keeps the rows the next full update is compared against, so a download that finished
    // meanwhile is still announced. setClient(), the server switch, clears both (REG-5).
    m_globalState->setSyncing(true);
    m_globalState->setRetryInSeconds(0);
    Q_EMIT runningChanged();

    // The engine owns the retry cadence; the client must not double it with its own.
    if (m_savedMaxRetries < 0)
        m_savedMaxRetries = m_client->maxRetries();
    m_client->setMaxRetries(0);

    poll(); // probes first while the server's version is still unknown
}

void SyncEngine::stop()
{
    // Whatever is in flight belongs to the run being stopped (and, on a server switch, to
    // the previous server): drop it, and make sure a late answer can never be applied.
    ++m_generation;
    m_pollInFlight = false;
    if (QbtReplyBase *reply = m_pollReply.data()) {
        m_pollReply.clear();
        reply->abort();
    }

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
    const quint64 generation = m_generation;

    // A probe that failed (server not up yet at start) is retried here, so the version and
    // API version reach GlobalState once the server answers.
    if (!m_probed) {
        m_pollReply =
            m_client->probe()->then(this, [this, generation](const QbtResult<ServerInfo> &result) {
                if (generation != m_generation)
                    return; // stopped or switched meanwhile: not ours to apply
                m_pollReply.clear();
                m_pollInFlight = false;
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

    const quint64 serial = ++m_pollsSent;
    m_pollReply = m_client->syncMainData(m_rid)->then(
        this, [this, generation, serial](const QbtResult<QJsonObject> &result) {
            if (generation != m_generation)
                return; // stopped or switched meanwhile: not ours to apply
            m_pollReply.clear();
            m_pollInFlight = false;
            if (!m_running)
                return;
            if (result.isError()) {
                onPollError(result.error());
            } else {
                m_appliedPoll = serial;
                onPollResult(result.value());
            }
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

void SyncEngine::applyDemoData(const QJsonObject &data)
{
    // Sent and applied at once, as far as pending values are concerned.
    m_appliedPoll = ++m_pollsSent;
    applyData(data);
}

void SyncEngine::applyData(const QJsonObject &data)
{
    const bool fullUpdate = data.value(key("full_update")).toBool(false);
    const QJsonArray removedTorrents = data.value(key("torrents_removed")).toArray();
    const QJsonObject torrents =
        withPendingValues(data.value(key("torrents")).toObject(), removedTorrents, fullUpdate);

    // What to announce afterwards, decided against the state before the merge. Only the
    // first full update is silent (there is no "before" yet); a later one (after a lost
    // server, or a server that forgot our rid) still compares against the rows we hold.
    QStringList completed;
    if (!fullUpdate || m_hadFullUpdate) {
        for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
            const int row = m_torrents->indexOfHash(it.key());
            const QJsonObject diff = it.value().toObject();
            if (row < 0) {
                // A full update lists everything: an unknown row there says nothing about
                // when it finished.
                if (fullUpdate)
                    continue;
                // New since the last poll. Complete with bytes downloaded: it was added and
                // finished between two polls (a small torrent does, in one 1.5 s interval).
                // Complete with nothing downloaded: added over data already on disk, which
                // is not a finished download.
                if (diff.value(key("progress")).toDouble() >= 1.0 &&
                    diff.value(key("downloaded")).toInteger() > 0)
                    completed.append(it.key());
                continue;
            }
            const Torrent &before = m_torrents->torrentAt(row);

            const bool nowComplete = diff.value(key("progress")).toDouble(before.progress) >= 1.0;
            const bool hasCompletionOn = diff.value(key("completion_on")).toInteger(0) > 0;
            if ((nowComplete || hasCompletionOn) && !before.isComplete() &&
                !before.completionOn.isValid())
                completed.append(it.key());
        }
    }

    if (fullUpdate) {
        m_torrents->applyFullUpdate(torrents);
        m_categories->applyFullUpdate(data.value(key("categories")).toObject());
        m_tags->applyFullUpdate(data.value(key("tags")).toArray());
        m_trackers->applyFullUpdate(data.value(key("trackers")).toObject());
    } else {
        m_torrents->applyPartial(torrents, removedTorrents);
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

    settleAnsweredValues();

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

// ---- pending torrent values (M12) ------------------------------------------------------------

void SyncEngine::setPendingTorrentValue(const QString &hash,
                                        const QString &key,
                                        const QJsonValue &value,
                                        const QJsonValue &current)
{
    if (m_torrents->indexOfHash(hash) < 0)
        return;
    auto &fields = m_pending[hash];
    const auto it = fields.find(key);
    PendingValue &pending = it != fields.end() ? *it : fields[key];
    if (it == fields.end())
        pending.serverValue = current; // a stacked request keeps what the server said
    pending.target = value;
    ++pending.unanswered;
    pending.answered = false;
    pending.expiry = QDeadlineTimer(kPendingLifetime);
    patchTorrents(QJsonObject{{hash, QJsonObject{{key, value}}}});
}

void SyncEngine::settlePendingTorrentValue(const QString &hash, const QString &key, bool refused)
{
    const auto fields = m_pending.find(hash);
    if (fields == m_pending.end())
        return;
    const auto it = fields->find(key);
    if (it == fields->end())
        return;
    it->unanswered = std::max(0, it->unanswered - 1);
    if (it->unanswered > 0)
        return;
    if (refused) {
        // Nothing changed on the server: what it holds is what the polls last said.
        const QJsonValue serverValue = it->serverValue;
        fields->erase(it);
        if (fields->isEmpty())
            m_pending.erase(fields);
        patchTorrents(QJsonObject{{hash, QJsonObject{{key, serverValue}}}});
        return;
    }
    it->answered = true;
    it->settleAfterPoll = m_pollsSent;
}

std::optional<QJsonValue> SyncEngine::pendingTorrentValue(const QString &hash,
                                                          const QString &key) const
{
    const auto fields = m_pending.constFind(hash);
    if (fields == m_pending.constEnd())
        return std::nullopt;
    const auto it = fields->constFind(key);
    if (it == fields->constEnd())
        return std::nullopt;
    return it->target;
}

int SyncEngine::pendingTorrentValueCount() const
{
    int count = 0;
    for (const auto &fields : m_pending)
        count += int(fields.size());
    return count;
}

QJsonObject
SyncEngine::withPendingValues(QJsonObject torrents, const QJsonArray &removed, bool full)
{
    if (m_pending.isEmpty())
        return torrents;
    for (const QJsonValue &gone : removed)
        m_pending.remove(gone.toString());
    for (auto fields = m_pending.begin(); fields != m_pending.end();) {
        const auto row = torrents.find(fields.key());
        if (row == torrents.end()) {
            // A full update lists every torrent: one it leaves out is gone.
            fields = full ? m_pending.erase(fields) : std::next(fields);
            continue;
        }
        QJsonObject diff = row->toObject();
        bool changed = false;
        for (auto it = fields->begin(); it != fields->end(); ++it) {
            if (!diff.contains(it.key()))
                continue;
            // What the server holds now; the model keeps showing the change asked for, so a
            // poll the server answered before it took the request cannot flip it back.
            it->serverValue = diff.value(it.key());
            diff.insert(it.key(), it->target);
            changed = true;
        }
        if (changed)
            *row = diff;
        ++fields;
    }
    return torrents;
}

void SyncEngine::settleAnsweredValues()
{
    QJsonObject released;
    for (auto fields = m_pending.begin(); fields != m_pending.end();) {
        QJsonObject values;
        for (auto it = fields->begin(); it != fields->end();) {
            const bool shown = it->answered && m_appliedPoll > it->settleAfterPoll;
            if (shown || it->expiry.hasExpired()) {
                values.insert(it.key(), it->serverValue);
                it = fields->erase(it);
            } else {
                ++it;
            }
        }
        if (!values.isEmpty())
            released.insert(fields.key(), values);
        fields = fields->isEmpty() ? m_pending.erase(fields) : std::next(fields);
    }
    if (!released.isEmpty())
        patchTorrents(released);
}

void SyncEngine::patchTorrents(const QJsonObject &values)
{
    // Only rows the model has: applyPartial() would add an unknown hash as a new torrent.
    QJsonObject known;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (m_torrents->indexOfHash(it.key()) >= 0)
            known.insert(it.key(), it.value());
    }
    if (!known.isEmpty())
        m_torrents->applyPartial(known, {});
}

} // namespace omaseed::core
