#include "core/PauseHold.hpp"

#include "core/ApiError.hpp"
#include "core/engines/EngineClient.hpp"

#include <QJsonValue>
#include <QTimer>

#include <algorithm>

namespace omaseed::core {

namespace {

const QString kHashes = QStringLiteral("hashes");
const QString kForced = QStringLiteral("forced");

QSet<QString> setOf(const QJsonValue &value)
{
    QSet<QString> out;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QString hash = item.toString().toLower();
        if (!hash.isEmpty())
            out.insert(hash);
    }
    return out;
}

QStringList sorted(const QSet<QString> &set)
{
    QStringList out(set.cbegin(), set.cend());
    std::sort(out.begin(), out.end());
    return out;
}

/// The server answered and said no: the stop didn't land. A network error, a timeout or a
/// cancel (a server switch drops what was in flight) may have reached it.
bool refused(ApiError::Kind kind)
{
    switch (kind) {
    case ApiError::Kind::Unauthorized:
    case ApiError::Kind::Forbidden:
    case ApiError::Kind::NotFound:
    case ApiError::Kind::Conflict:
    case ApiError::Kind::Server:
        return true;
    default:
        return false;
    }
}

} // namespace

PauseHold::PauseHold(QObject *parent)
    : QObject(parent)
{}

void PauseHold::setClient(EngineClient *client)
{
    m_client = client;
}

void PauseHold::setServer(const QString &id)
{
    if (m_server == id)
        return;
    m_server = id;
    Q_EMIT changed();
}

void PauseHold::setStore(const QJsonObject &store)
{
    m_store = store;
    Q_EMIT changed();
}

int PauseHold::count() const
{
    return int(entry(m_server).hashes.size());
}

QStringList PauseHold::hashes() const
{
    return sorted(entry(m_server).hashes);
}

QStringList PauseHold::forced() const
{
    return sorted(entry(m_server).forced);
}

PauseHold::Entry PauseHold::entry(const QString &server) const
{
    if (server.isEmpty())
        return {};
    const QJsonObject object = m_store.value(server).toObject();
    Entry out;
    out.hashes = setOf(object.value(kHashes));
    out.forced = setOf(object.value(kForced)) & out.hashes;
    return out;
}

void PauseHold::setEntry(const QString &server, const Entry &entry)
{
    if (entry.hashes.isEmpty()) {
        m_store.remove(server);
    } else {
        QJsonObject object;
        object.insert(kHashes, QJsonArray::fromStringList(sorted(entry.hashes)));
        if (!entry.forced.isEmpty())
            object.insert(kForced, QJsonArray::fromStringList(sorted(entry.forced & entry.hashes)));
        m_store.insert(server, object);
    }
    Q_EMIT storeChanged();
    Q_EMIT changed();
}

void PauseHold::finish()
{
    m_busy = false;
    Q_EMIT changed();
}

bool PauseHold::pause()
{
    if (!m_client || m_server.isEmpty() || m_busy)
        return false;
    m_busy = true;
    Q_EMIT changed();
    const QString server = m_server;
    m_client->running()->then(this, [this, server](const Result<QList<RunningTorrent>> &result) {
        if (result.isError()) {
            finish();
            Q_EMIT paused(0, result.error().toString());
            return;
        }
        pauseListed(server, result.value());
    });
    return true;
}

void PauseHold::pauseListed(const QString &server, const QList<RunningTorrent> &running)
{
    Entry after = entry(server);
    QSet<QString> stopping;
    QSet<QString> added;
    for (const RunningTorrent &torrent : running) {
        const QString hash = torrent.hash.toLower();
        if (hash.isEmpty())
            continue;
        stopping.insert(hash);
        if (!after.hashes.contains(hash))
            added.insert(hash);
        after.hashes.insert(hash);
        if (torrent.forced)
            after.forced.insert(hash);
        else
            after.forced.remove(hash);
    }
    if (stopping.isEmpty()) {
        finish();
        Q_EMIT paused(0, QString());
        return;
    }

    // Held before the stop is sent, so a crash between the two can't lose them.
    setEntry(server, after);
    const QStringList hashes = sorted(stopping);
    m_client->stop(hashes)->then(
        this, [this, server, added, count = int(hashes.size())](const Result<Done> &result) {
            if (result.isError()) {
                if (refused(result.error().kind())) {
                    Entry back = entry(server);
                    back.hashes -= added;
                    back.forced -= added;
                    setEntry(server, back);
                }
                finish();
                Q_EMIT paused(0, result.error().toString());
                return;
            }
            m_sinceStop.start();
            m_stoppedOn = server;
            finish();
            Q_EMIT paused(count, QString());
        });
}

bool PauseHold::resume()
{
    if (!m_client || m_server.isEmpty() || m_busy || !held())
        return false;
    m_busy = true;
    Q_EMIT changed();
    askStopped(m_server);
    return true;
}

void PauseHold::askStopped(const QString &server)
{
    if (!m_client) {
        resumeFailed(ApiError(ApiError::Kind::Cancelled, QStringLiteral("no server")));
        return;
    }
    // Every stopped torrent: the held ones among them are started.
    m_client->stoppedAmong({})->then(this, [this, server](const Result<QStringList> &result) {
        if (result.isError()) {
            resumeFailed(result.error());
            return;
        }
        resumeListed(server, result.value());
    });
}

void PauseHold::resumeListed(const QString &server, const QStringList &stopped)
{
    const Entry holding = entry(server);
    // A stop of ours still landing (see the header): ask again in a moment rather than skip.
    if (m_stoppedOn == server && m_sinceStop.isValid() && m_sinceStop.elapsed() < m_settleMs) {
        QSet<QString> stoppedSet;
        for (const QString &hash : stopped)
            stoppedSet.insert(hash.toLower());
        if (!(holding.hashes - stoppedSet).isEmpty()) {
            QTimer::singleShot(kSettleStepMs, this, [this, server]() { askStopped(server); });
            return;
        }
    }
    QSet<QString> normal;
    QSet<QString> forced;
    for (const QString &stoppedHash : stopped) {
        const QString hash = stoppedHash.toLower();
        if (!holding.hashes.contains(hash))
            continue; // stopped on purpose, or by someone else: not ours to start
        (holding.forced.contains(hash) ? forced : normal).insert(hash);
    }
    // Held but not stopped now: removed meanwhile, or started by hand.
    const int skipped = int(holding.hashes.size() - normal.size() - forced.size());
    if (normal.isEmpty()) {
        startForced(server, sorted(forced), 0, skipped);
        return;
    }
    const QStringList hashes = sorted(normal);
    m_client->start(hashes)->then(
        this,
        [this, server, forced, skipped, count = int(hashes.size())](const Result<Done> &result) {
            if (result.isError()) {
                resumeFailed(result.error());
                return;
            }
            startForced(server, sorted(forced), count, skipped);
        });
}

void PauseHold::startForced(const QString &server,
                            const QStringList &forced,
                            int started,
                            int skipped)
{
    const auto done = [this, server, started, skipped, count = int(forced.size())]() {
        setEntry(server, Entry());
        finish();
        Q_EMIT resumed(started + count, skipped, QString());
    };
    if (forced.isEmpty()) {
        done();
        return;
    }
    // A plain start would bring a force-started torrent back under the queue; setting force
    // start starts it as it was.
    m_client->setForceStart(forced, true)->then(this, [this, done](const Result<Done> &result) {
        if (result.isError()) {
            resumeFailed(result.error());
            return;
        }
        done();
    });
}

void PauseHold::resumeFailed(const ApiError &error)
{
    finish();
    Q_EMIT resumed(0, 0, error.toString());
}

} // namespace omaseed::core
