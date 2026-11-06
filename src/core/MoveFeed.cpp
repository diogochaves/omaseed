#include "core/MoveFeed.hpp"

#include "core/FolderTree.hpp"
#include "core/FreeSpace.hpp"
#include "core/MoveJob.hpp"
#include "core/ProblemLog.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/EngineFeatures.hpp"

#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace omaseed::core {

namespace {

// C16 L10: the band reads it every 2 s; a reading this fresh is reused.
constexpr qint64 kGrowthEveryMs = 1500;
// M5 on the server: a target's free space is asked again after this long, and trusted for
// this long (the job asks it between two moves, never during one).
constexpr qint64 kRoomAgainMs = 10000;
constexpr qint64 kRoomTrustMs = 5 * 60 * 1000;
// One send in flight on an engine that holds every answer while it moves (K22); qBittorrent's
// three (C15 M12) otherwise.
constexpr int kAheadHolding = 1;
constexpr int kAhead = 3;
// Board 47 §E on Deluge (movesShowAtOnce): a torrent whose send was taken reads Moving on the
// next poll sent after the answer. Still not moving, in its old folder, on two polls after the
// answer and 3 s on, the move failed without a word (assumed: two polls make sure one was
// sent after the answer; 3 s covers a slow one at the window's 1.5 s cadence).
constexpr int kSilentPolls = 2;
constexpr qint64 kSilentMs = 3000;

qint64 sizeOf(const QString &localPath)
{
    const QFileInfo info(localPath);
    if (!info.exists())
        return 0;
    if (!info.isDir())
        return info.size();
    qint64 bytes = 0;
    QDirIterator it(
        localPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

bool withServer(MoveJob::Step step)
{
    return step == MoveJob::Step::Queued || step == MoveJob::Step::Moving;
}

} // namespace

MoveFeed::MoveFeed(SyncEngine *engine, MoveJob *job, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_job(job)
{
    job->setTorrents(engine->torrents());
    connect(job, &MoveJob::sendRequested, this, &MoveFeed::send);
    connect(job, &MoveJob::stateChanged, this, &MoveFeed::watch);
    connect(engine, &SyncEngine::synced, this, &MoveFeed::synced);
    connect(engine, &SyncEngine::serverLost, job, [job]() { job->setConnected(false); });
    connect(engine, &SyncEngine::serverBack, job, [job]() { job->setConnected(true); });
    connect(engine->problems(),
            &ProblemLog::jobMoveFailed,
            this,
            [this](const QString &hash,
                   const QDateTime &at,
                   const QString &reason,
                   const QString &causeKey,
                   const QString &line) {
                if (m_failures == Failures::Log && !m_job.isNull())
                    m_job->onFailed(hash, at, reason, causeKey, line);
            });
    // K22: while the engine holds its answers, its poll's turns still tick M14's clock; the
    // torrent it is moving reads as moving, though nothing about it changes meanwhile.
    connect(engine, &SyncEngine::busyTick, this, [this]() {
        if (!m_job.isNull())
            m_job->onSynced(false);
    });
    job->setMovingHint([this](const QString &hash) {
        const EngineClient *client = m_engine.isNull() ? nullptr : m_engine->client();
        return client != nullptr && client->busyMoving().contains(hash);
    });
    // M14 on an engine that holds its answers: a move that looks stuck no longer counts as
    // busy, so a silence after it is C12's "didn't answer".
    connect(job, &MoveJob::pausedItself, this, [this](MoveJob::PauseReason reason) {
        EngineClient *client = m_engine.isNull() ? nullptr : m_engine->client();
        if (reason == MoveJob::PauseReason::Stuck && client != nullptr)
            client->releaseBusyMoving();
    });
    job->setRoomProbe([this](const QString &path) { return room(path); });
    job->setGrowthProbe(
        [this](const QString &hash, const QString &target) { return bytesAt(hash, target); });
    watch();
}

void MoveFeed::setFreeSpace(const FreeSpaceProbe *probe)
{
    m_probe = probe;
}

void MoveFeed::setFeatures(const EngineFeatures &features)
{
    m_failures = features.moveStopsOnFailure ? Failures::Torrent : Failures::Log;
    m_holdsAnswers = features.movesHoldAnswers;
    m_roomOnServer = features.roomOnServer;
    m_engineProgress = features.moveProgress;
    m_showsAtOnce = features.movesShowAtOnce && m_failures == Failures::Torrent;
    if (!m_showsAtOnce)
        m_unseen.clear();
    if (!m_job.isNull())
        m_job->setAhead(m_holdsAnswers ? kAheadHolding : kAhead);
    watch();
}

QPair<QString, QString> MoveFeed::causeOf(const QString &engineText)
{
    // Read as the error block reads it (ProblemMatch::readEngineText, C25 K24).
    const QString message = ProblemMatch::readEngineText(engineText).message;
    ProblemLine line;
    line.kind = ProblemLine::Kind::MoveFailed;
    line.systemMessage = message;
    return {ProblemMatch::describe(line).causeKey, message};
}

void MoveFeed::send(const QStringList &hashes, const QString &location)
{
    EngineClient *client = m_engine.isNull() ? nullptr : m_engine->client();
    if (client == nullptr) {
        m_job->onSendResult(
            hashes, ApiError(ApiError::Kind::Network, QStringLiteral("no server to send to")));
        return;
    }
    for (const QString &hash : hashes) {
        m_answered.remove(hash);
        m_unseen.remove(hash);
    }
    QPointer<MoveJob> job = m_job;
    QPointer<MoveFeed> self(this);
    client->setLocation(hashes, location)
        ->then(this, [self, job, hashes](const Result<Done> &result) {
            if (!self.isNull() && !result.isError()) {
                for (const QString &hash : hashes) {
                    self->m_answered.insert(hash);
                    if (self->m_showsAtOnce) {
                        QElapsedTimer since;
                        since.start();
                        self->m_unseen.insert(hash, {since, 0});
                    }
                }
            }
            if (!job.isNull())
                job->onSendResult(hashes, result.isError() ? result.error() : ApiError());
        });
}

void MoveFeed::synced(bool fullUpdate)
{
    if (m_job.isNull())
        return;
    // A sync is the server answering. The engine's serverBack says so too, but not after a
    // restart while lost (saving the server's sheet restarts it): the job would wait forever.
    if (!m_job->isConnected())
        m_job->setConnected(true);
    if (m_failures == Failures::Torrent)
        readFailures();
    m_job->onSynced(fullUpdate);
    readRoom();
    watch();
}

void MoveFeed::readFailures()
{
    if (m_answered.isEmpty() || m_engine.isNull())
        return;
    TorrentModel *torrents = m_engine->torrents();
    const QList<MoveJob::Entry> entries = m_job->entries();
    for (const MoveJob::Entry &entry : entries) {
        if (!m_answered.contains(entry.hash))
            continue;
        if (!withServer(entry.step) || torrents->indexOfHash(entry.hash) < 0) {
            m_answered.remove(entry.hash);
            m_unseen.remove(entry.hash);
            continue;
        }
        const Torrent &torrent = torrents->torrent(entry.hash);
        const bool home = SharedData::normalize(torrent.savePath) == entry.from;
        const TorrentPhase phase = torrent.status.phase;
        auto unseen = m_unseen.find(entry.hash);
        if (unseen != m_unseen.end()) {
            if (phase == TorrentPhase::Moving || phase == TorrentPhase::Errored || !home) {
                m_unseen.erase(unseen); // it moves, landed, or failed in words
            } else if (++unseen->second >= kSilentPolls && unseen->first.elapsed() >= kSilentMs) {
                // Board 47 §E: taken, never moving, still at home: Deluge didn't move it.
                m_unseen.erase(unseen);
                m_answered.remove(entry.hash);
                m_job->onNotMoved(entry.hash);
                continue;
            }
        }
        // Board 47 §4: in error after the send, its folder still the old one.
        if (phase != TorrentPhase::Errored || !home)
            continue;
        m_answered.remove(entry.hash);
        const QString text = torrent.errorMessage;
        const auto [causeKey, reason] = causeOf(text);
        m_job->onFailed(entry.hash, QDateTime(), reason.isEmpty() ? text : reason, causeKey, text);
    }
}

void MoveFeed::readRoom()
{
    if (!m_roomOnServer || seesDisks() || m_engine.isNull() || m_job.isNull())
        return;
    EngineClient *client = m_engine->client();
    if (client == nullptr || !client->busyMoving().isEmpty())
        return;
    switch (m_job->state()) {
    case MoveJob::State::Running:
    case MoveJob::State::Pausing:
    case MoveJob::State::Paused:
        break;
    default:
        return;
    }
    // The targets still to send to, the first few in the order they go.
    QStringList targets;
    for (const MoveJob::Entry &entry : m_job->entries()) {
        if (entry.step == MoveJob::Step::Waiting && !targets.contains(entry.to))
            targets << entry.to;
        if (targets.size() >= 3)
            break;
    }
    for (const QString &target : std::as_const(targets)) {
        const auto read = m_room.constFind(target);
        if (m_roomAsked.contains(target) ||
            (read != m_room.cend() && read->second.elapsed() < kRoomAgainMs))
            continue;
        m_roomAsked.insert(target);
        QPointer<MoveFeed> self(this);
        client->freeSpace(target)->then(this, [self, target](const Result<FreeSpaceAt> &result) {
            if (self.isNull())
                return;
            self->m_roomAsked.remove(target);
            if (result.isError() || result.value().bytesFree < 0)
                return;
            QElapsedTimer at;
            at.start();
            self->m_room.insert(target, {result.value().bytesFree, at});
        });
    }
}

qint64 MoveFeed::room(const QString &path) const
{
    if (m_probe != nullptr) {
        const FreeSpaceProbe::Answer answer = m_probe->answer(path);
        if (answer.source != FreeSpaceProbe::Source::Unknown)
            return answer.bytesFree;
    }
    if (!m_roomOnServer)
        return -1;
    const auto read = m_room.constFind(path);
    if (read == m_room.cend() || read->second.elapsed() > kRoomTrustMs)
        return -1;
    return read->first;
}

void MoveFeed::watch()
{
    if (m_job.isNull() || m_engine.isNull())
        return;
    ProblemLog *problems = m_engine->problems();
    if (m_failures != Failures::Log) {
        problems->watchJob({}, {});
        return;
    }
    switch (m_job->state()) {
    case MoveJob::State::Running:
    case MoveJob::State::Pausing:
    case MoveJob::State::Paused:
    case MoveJob::State::Ending:
        problems->watchJob(m_job->hashes(), m_job->startedAt());
        break;
    default:
        problems->watchJob({}, {});
        break;
    }
}

bool MoveFeed::seesDisks() const
{
    return m_probe != nullptr && m_probe->seesServerDisks();
}

qint64 MoveFeed::bytesAt(const QString &hash, const QString &target)
{
    if (m_engine.isNull() || (!seesDisks() && !m_engineProgress))
        return -1;
    TorrentModel *torrents = m_engine->torrents();
    if (torrents->indexOfHash(hash) < 0)
        return -1;
    const Torrent &torrent = torrents->torrent(hash);
    if (m_engineProgress) {
        // K21: Deluge's own % of the move (the bytes of its files at the target over its bytes
        // done), wherever its disks are; nothing until it reads Moving. libtorrent makes each
        // file full length at the target before it copies it, so the % rises file by file and
        // one file reads 100 % from the start: #162's blind rule (MoveJob) still applies.
        const MoveJob::Entry *entry = m_job.isNull() ? nullptr : m_job->entry(hash);
        if (entry == nullptr || torrent.status.phase != TorrentPhase::Moving)
            return -1;
        return qint64(std::clamp(torrent.progress, 0.0, 1.0) * double(entry->bytes));
    }
    const QString save = SharedData::normalize(torrent.savePath);
    const QString content = SharedData::normalize(torrent.contentPath);
    // A torrent added without a subfolder has no folder of its own to measure.
    if (content.isEmpty() || content == save || !SharedData::isUnder(content, save))
        return -1;
    const QString rel = content.mid(save == QLatin1String("/") ? 1 : save.size() + 1);
    const QString local = m_probe->localPathFor(FolderTree::join(target, rel));
    if (local.isEmpty())
        return -1;
    auto cached = m_grown.find(hash);
    if (cached != m_grown.end() && cached->second.isValid() &&
        cached->second.elapsed() < kGrowthEveryMs)
        return cached->first;
    QElapsedTimer clock;
    clock.start();
    const qint64 bytes = sizeOf(local);
    m_grown.insert(hash, {bytes, clock});
    return bytes;
}

} // namespace omaseed::core
