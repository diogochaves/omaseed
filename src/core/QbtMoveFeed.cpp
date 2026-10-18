#include "core/QbtMoveFeed.hpp"

#include "core/FolderTree.hpp"
#include "core/FreeSpace.hpp"
#include "core/MoveJob.hpp"
#include "core/ProblemLog.hpp"
#include "core/QbtClient.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include <QDirIterator>
#include <QFileInfo>

namespace omaseed::core {

namespace {

// C16 L10: the band reads it every 2 s; a reading this fresh is reused.
constexpr qint64 kGrowthEveryMs = 1500;

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

} // namespace

QbtMoveFeed::QbtMoveFeed(SyncEngine *engine, MoveJob *job, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_job(job)
{
    job->setTorrents(engine->torrents());
    connect(job, &MoveJob::sendRequested, this, &QbtMoveFeed::send);
    connect(job, &MoveJob::stateChanged, this, &QbtMoveFeed::watch);
    connect(engine, &SyncEngine::synced, this, &QbtMoveFeed::synced);
    connect(engine, &SyncEngine::serverLost, job, [job]() { job->setConnected(false); });
    connect(engine, &SyncEngine::serverBack, job, [job]() { job->setConnected(true); });
    connect(engine->problems(), &ProblemLog::jobMoveFailed, job, &MoveJob::onFailed);
    job->setRoomProbe([this](const QString &path) -> qint64 {
        if (m_probe == nullptr)
            return -1;
        const FreeSpaceProbe::Answer answer = m_probe->answer(path);
        return answer.source == FreeSpaceProbe::Source::Unknown ? -1 : answer.bytesFree;
    });
    job->setGrowthProbe(
        [this](const QString &hash, const QString &target) { return bytesAt(hash, target); });
    watch();
}

void QbtMoveFeed::setFreeSpace(const FreeSpaceProbe *probe)
{
    m_probe = probe;
}

void QbtMoveFeed::send(const QStringList &hashes, const QString &location)
{
    QbtClient *client = m_engine.isNull() ? nullptr : m_engine->client();
    if (client == nullptr) {
        m_job->onSendResult(
            hashes, ApiError(ApiError::Kind::Network, QStringLiteral("no server to send to")));
        return;
    }
    QPointer<MoveJob> job = m_job;
    client->torrentsSetLocation(hashes, location)
        ->then(this, [job, hashes](const QbtResult<QByteArray> &result) {
            if (!job.isNull())
                job->onSendResult(hashes, result.isError() ? result.error() : ApiError());
        });
}

void QbtMoveFeed::synced(bool fullUpdate)
{
    if (m_job.isNull())
        return;
    m_job->onSynced(fullUpdate);
    watch();
}

void QbtMoveFeed::watch()
{
    if (m_job.isNull() || m_engine.isNull())
        return;
    ProblemLog *problems = m_engine->problems();
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

qint64 QbtMoveFeed::bytesAt(const QString &hash, const QString &target)
{
    if (m_probe == nullptr || !m_probe->seesServerDisks() || m_engine.isNull())
        return -1;
    TorrentModel *torrents = m_engine->torrents();
    if (torrents->indexOfHash(hash) < 0)
        return -1;
    const Torrent &torrent = torrents->torrent(hash);
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
