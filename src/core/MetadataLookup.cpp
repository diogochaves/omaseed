#include "core/MetadataLookup.hpp"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace omaseed::core {
namespace {

Q_LOGGING_CATEGORY(lcLookup, "omaseed.lookup")

// docs/29 §3 and D2/D3.
constexpr qint64 kQuietUntil = 500;
constexpr qint64 kSlowFrom = 10000;
constexpr qint64 kNeverFrom = 60000;
constexpr qint64 kSlowPollEvery = 5000;

bool isRunning(MetadataLookup::Phase phase)
{
    using Phase = MetadataLookup::Phase;
    return phase == Phase::Quiet || phase == Phase::Fetching || phase == Phase::Slow ||
           phase == Phase::Never;
}

} // namespace

MetadataLookup::MetadataLookup(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_monotonic.start();
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &MetadataLookup::wake);
}

void MetadataLookup::setFetch(Fetch fetch)
{
    m_fetch = std::move(fetch);
}

void MetadataLookup::setManualClock(Clock clock)
{
    m_manualClock = std::move(clock);
    m_timer->stop();
}

void MetadataLookup::setClockScale(double scale)
{
    m_scale = scale > 0 ? scale : 1.0;
}

qint64 MetadataLookup::now() const
{
    return m_manualClock ? m_manualClock() : m_monotonic.elapsed();
}

qint64 MetadataLookup::elapsed() const
{
    return qint64(double(now() - m_startedAt) * m_scale);
}

QString MetadataLookup::phaseName() const
{
    switch (m_phase) {
    case Phase::Idle:
        return QStringLiteral("idle");
    case Phase::Private:
        return QStringLiteral("private");
    case Phase::Waiting:
        return QStringLiteral("waiting");
    case Phase::Quiet:
        return QStringLiteral("quiet");
    case Phase::Fetching:
        return QStringLiteral("fetching");
    case Phase::Slow:
        return QStringLiteral("slow");
    case Phase::Never:
        return QStringLiteral("never");
    case Phase::Done:
        return QStringLiteral("done");
    case Phase::Unsupported:
        return QStringLiteral("unsupported");
    case Phase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

int MetadataLookup::seconds() const
{
    return m_lastSeconds;
}

bool MetadataLookup::isActive() const
{
    return m_phase == Phase::Waiting || isRunning(m_phase);
}

void MetadataLookup::start(const QString &key, const QString &source, const QString &kind)
{
    if (!key.isEmpty() && key == m_key && m_phase != Phase::Idle && m_phase != Phase::Private)
        return;
    ++m_generation;
    m_timer->stop();
    m_key = key;
    m_source = source;
    m_kind = kind;
    m_privateHost.clear();
    m_metadata = Metadata();
    m_inFlight = false;
    m_lastSeconds = 0;
    m_phase = Phase::Waiting;
    if (m_serverReady) {
        if (QbtClient::supportsFetchMetadata(m_apiVersion)) {
            begin();
            return;
        }
        m_phase = Phase::Unsupported;
    }
    Q_EMIT changed();
}

void MetadataLookup::startPrivate(const QString &key, const QString &source, const QString &host)
{
    if (!key.isEmpty() && key == m_key && m_phase == Phase::Private && host == m_privateHost)
        return;
    ++m_generation;
    m_timer->stop();
    m_key = key;
    m_source = source;
    m_kind = QStringLiteral("magnet");
    m_privateHost = host;
    m_metadata = Metadata();
    m_inFlight = false;
    m_lastSeconds = 0;
    m_phase = Phase::Private;
    Q_EMIT changed();
}

void MetadataLookup::setServer(bool ready, const QString &apiVersion)
{
    m_serverReady = ready;
    m_apiVersion = apiVersion;
    if (m_phase != Phase::Waiting || !ready)
        return;
    if (!QbtClient::supportsFetchMetadata(apiVersion)) {
        setPhase(Phase::Unsupported);
        return;
    }
    begin();
}

void MetadataLookup::stop()
{
    ++m_generation;
    m_timer->stop();
    m_inFlight = false;
    const bool wasIdle = m_phase == Phase::Idle && m_key.isEmpty();
    m_phase = Phase::Idle;
    m_key.clear();
    m_source.clear();
    m_kind.clear();
    m_privateHost.clear();
    m_metadata = Metadata();
    m_lastSeconds = 0;
    if (!wasIdle)
        Q_EMIT changed();
}

void MetadataLookup::begin()
{
    m_startedAt = now();
    m_nextPoll = 0;
    m_lastSeconds = 0;
    m_phase = Phase::Quiet;
    Q_EMIT changed();
    poll();
    arm();
}

void MetadataLookup::wake()
{
    if (!isRunning(m_phase))
        return;
    const qint64 at = elapsed();
    const Phase phase = phaseAt(at);
    const int secs = int(at / 1000);
    const bool moved = phase != m_phase || secs != m_lastSeconds;
    m_phase = phase;
    m_lastSeconds = secs;
    if (moved)
        Q_EMIT changed();
    if (!m_inFlight && at >= m_nextPoll)
        poll();
    arm();
}

void MetadataLookup::poll()
{
    if (!m_fetch || m_inFlight)
        return;
    Reply<QbtClient::FetchedMetadata> *reply = m_fetch(m_source);
    if (reply == nullptr)
        return;
    m_inFlight = true;
    const int generation = m_generation;
    reply->then(this, [this, generation](const Result<QbtClient::FetchedMetadata> &result) {
        onAnswer(generation, result);
    });
}

void MetadataLookup::onAnswer(int generation, const Result<QbtClient::FetchedMetadata> &result)
{
    if (generation != m_generation || !isRunning(m_phase))
        return;
    m_inFlight = false;

    if (result.isError()) {
        const ApiError &error = result.error();
        // D11: no such endpoint here.
        if (error.kind() == ApiError::Kind::NotFound) {
            setPhase(Phase::Unsupported);
            return;
        }
        // qBittorrent refused the source itself ("Unable to parse", 415, …): asking again
        // would get the same answer, so the sheet goes back to today's.
        const int status = error.httpStatus();
        if (error.kind() == ApiError::Kind::Parse ||
            (error.kind() == ApiError::Kind::Unknown && status >= 400 && status < 500)) {
            qCWarning(lcLookup).noquote() << "fetchMetadata refused:" << error.toString();
            setPhase(Phase::Failed);
            return;
        }
        // A network error, a timeout, a 5xx, a session being re-established: try again,
        // as qBittorrent's own Web UI does.
    } else if (result.value().ready) {
        const std::optional<Metadata> metadata = parse(result.value().body);
        if (!metadata) {
            qCWarning(lcLookup) << "fetchMetadata answered 200 without a usable info";
            setPhase(Phase::Failed);
            return;
        }
        m_metadata = *metadata;
        m_timer->stop();
        setPhase(Phase::Done);
        Q_EMIT fetched();
        return;
    }
    m_nextPoll = nextPollAt(elapsed());
    wake();
}

void MetadataLookup::setPhase(Phase phase)
{
    if (!isRunning(phase))
        m_timer->stop();
    if (phase == m_phase)
        return;
    m_phase = phase;
    Q_EMIT changed();
}

void MetadataLookup::arm()
{
    if (m_manualClock || !isRunning(m_phase)) {
        m_timer->stop();
        return;
    }
    const qint64 at = elapsed();
    qint64 target = (at / 1000 + 1) * 1000; // the seconds the sheet prints
    if (!m_inFlight)
        target = std::min(target, m_nextPoll);
    for (const qint64 boundary : {kQuietUntil, kSlowFrom, kNeverFrom}) {
        if (boundary > at)
            target = std::min(target, boundary);
    }
    const double real = double(std::max<qint64>(1, target - at)) / m_scale;
    m_timer->start(int(std::clamp(std::ceil(real), 1.0, double(std::numeric_limits<int>::max()))));
}

qint64 MetadataLookup::nextPollAt(qint64 elapsedMs)
{
    if (elapsedMs < 250)
        return 250;
    if (elapsedMs < 500)
        return 500;
    if (elapsedMs < 1000)
        return 1000;
    if (elapsedMs < kNeverFrom)
        return (elapsedMs / 1000 + 1) * 1000;
    return kNeverFrom + ((elapsedMs - kNeverFrom) / kSlowPollEvery + 1) * kSlowPollEvery;
}

MetadataLookup::Phase MetadataLookup::phaseAt(qint64 elapsedMs)
{
    if (elapsedMs < kQuietUntil)
        return Phase::Quiet;
    if (elapsedMs < kSlowFrom)
        return Phase::Fetching;
    if (elapsedMs < kNeverFrom)
        return Phase::Slow;
    return Phase::Never;
}

std::optional<MetadataLookup::Metadata> MetadataLookup::parse(const QJsonObject &body)
{
    const QJsonObject info = body.value(QStringLiteral("info")).toObject();
    const QString name = info.value(QStringLiteral("name")).toString();
    if (info.isEmpty() || name.isEmpty())
        return std::nullopt;

    Metadata metadata;
    metadata.name = name;
    metadata.infoHashV1 = body.value(QStringLiteral("infohash_v1")).toString().toLower();
    metadata.infoHashV2 = body.value(QStringLiteral("infohash_v2")).toString().toLower();
    metadata.torrentId = body.value(QStringLiteral("hash")).toString().toLower();
    if (metadata.torrentId.isEmpty())
        metadata.torrentId = TorrentFile::torrentIdFor(metadata.infoHashV1, metadata.infoHashV2);
    metadata.pieceLength = info.value(QStringLiteral("piece_length")).toInteger();
    const QJsonValue isPrivate = info.value(QStringLiteral("private"));
    metadata.isPrivate = isPrivate.isBool() ? isPrivate.toBool() : isPrivate.toInt() != 0;

    // A multi-file torrent's paths start with its folder, `<info.name>/`; a single file's path
    // is its name. TorrentFileEntry keeps paths below the folder, as TorrentFile does.
    const QString root = name + QLatin1Char('/');
    const QJsonArray files = info.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        QString path = file.value(QStringLiteral("path")).toString();
        if (path.startsWith(root)) {
            path.remove(0, root.size());
            metadata.multiFile = true;
        }
        const qint64 length = file.value(QStringLiteral("length")).toInteger();
        metadata.files.append({path, length});
        metadata.totalSize += length;
    }
    if (metadata.files.isEmpty()) {
        const qint64 length = info.value(QStringLiteral("length")).toInteger();
        metadata.files.append({name, length});
        metadata.totalSize = length;
    }
    if (metadata.files.size() > 1)
        metadata.multiFile = true;

    for (const QJsonValue &value : body.value(QStringLiteral("trackers")).toArray()) {
        const QString url = value.toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty() && !metadata.trackers.contains(url))
            metadata.trackers.append(url);
    }
    return metadata;
}

} // namespace omaseed::core
