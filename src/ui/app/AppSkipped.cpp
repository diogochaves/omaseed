// App's #118 block (board 44 §2, DECISIONS C17 D1–D4): what the details torrent's skipped
// files hold on this machine's disk, "Free the space…", and adding a hybrid again. Kept out of
// App.cpp so it reads as one piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/PieceStates.hpp"
#include "core/ProblemLog.hpp"
#include "core/TorrentFilesModel.hpp"

#include <QJsonArray>
#include <QSet>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

/// The most rows the sheet lists; the rest are "… N more".
constexpr int kPlanRows = 200;

bool isRunning(TorrentState state)
{
    switch (state) {
    case TorrentState::StoppedDownload:
    case TorrentState::StoppedUpload:
    case TorrentState::PausedDownload:
    case TorrentState::PausedUpload:
    case TorrentState::Error:
    case TorrentState::MissingFiles:
        return false;
    default:
        return true;
    }
}

QString verdictName(FreeItem::Verdict verdict)
{
    switch (verdict) {
    case FreeItem::Verdict::Delete:
        return QStringLiteral("delete");
    case FreeItem::Verdict::Open:
        return QStringLiteral("open");
    case FreeItem::Verdict::Pieces:
        return QStringLiteral("pieces");
    case FreeItem::Verdict::Shared:
        return QStringLiteral("shared");
    case FreeItem::Verdict::Outside:
        return QStringLiteral("outside");
    }
    return QString();
}

} // namespace

void App::setUpSkipped()
{
    m_skipped = new SkippedOnDisk(SkippedOnDisk::readAllocated, this);
    connect(m_skipped, &SkippedOnDisk::readingChanged, this, [this]() {
        const SkippedReading &reading = m_skipped->reading();
        if (reading.hash == m_details->hash())
            m_details->files()->setOnDisk(reading.allocatedByIndex());
        Q_EMIT skippedOnDiskChanged();
    });
    // Another torrent: its own reading, once its file list is in.
    connect(m_details, &TorrentDetails::hashChanged, this, [this]() {
        m_skipped->clear();
        m_skippedAfterRefresh = m_filesTabActive;
    });
    connect(m_details, &TorrentDetails::refreshed, this, [this]() {
        if (!m_skippedAfterRefresh || !m_filesTabActive)
            return;
        m_skippedAfterRefresh = false;
        refreshSkippedOnDisk();
    });
    // A tick or a skip: the next file list says what the disk holds now (docs/44 §7).
    connect(m_details->files(), &TorrentFilesModel::summaryChanged, this, [this]() {
        m_skippedAfterRefresh = m_filesTabActive;
    });
}

QVariantMap App::skippedOnDiskMap() const
{
    const SkippedReading &reading = m_skipped->reading();
    QVariantMap map{{QStringLiteral("hash"), reading.hash},
                    {QStringLiteral("seen"), reading.seen},
                    {QStringLiteral("held"), reading.held},
                    {QStringLiteral("heldText"), Format::size(reading.held)},
                    {QStringLiteral("holding"), reading.holding},
                    {QStringLiteral("band"), reading.seen && reading.bandShown()},
                    {QStringLiteral("partlyWritten"), reading.partlyWritten},
                    {QStringLiteral("busy"), !m_skippedStep.isEmpty()},
                    {QStringLiteral("step"), m_skippedStep}};
    if (!reading.hash.isEmpty() && m_engine->torrents()->indexOfHash(reading.hash) >= 0) {
        const Torrent &torrent = m_engine->torrents()->torrent(reading.hash);
        const FreeSpaceProbe::Answer answer = m_freeSpace.answer(torrent.savePath);
        map.insert(QStringLiteral("where"), answer.where);
        map.insert(QStringLiteral("fileSystem"), answer.fileSystem);
        map.insert(QStringLiteral("name"), torrent.name);
    }
    return map;
}

void App::setFilesTabActive(bool active)
{
    if (m_filesTabActive == active)
        return;
    m_filesTabActive = active;
    if (!active)
        return;
    // Read now if the list is here; otherwise once it comes in.
    if (m_details->files()->fileCount() > 0)
        refreshSkippedOnDisk();
    else
        m_skippedAfterRefresh = true;
}

SkippedOnDisk::LocalPathOf App::localPathOfFiles(const Torrent &torrent) const
{
    // D4: without a view of the server's disks nothing is read, and nothing is claimed.
    if (!m_freeSpace.seesServerDisks())
        return {};
    const FreeSpaceProbe probe = m_freeSpace;
    const QString contentPath = torrent.contentPath;
    const QString savePath = torrent.savePath;
    return [probe, contentPath, savePath](const QString &path) {
        return probe.localPathFor(TorrentFilesModel::serverPathOf(contentPath, savePath, path));
    };
}

void App::refreshSkippedOnDisk()
{
    const QString hash = m_details->hash();
    if (hash.isEmpty() || m_demoMode || !m_hasServer || m_engine->torrents()->indexOfHash(hash) < 0)
        return;
    const Torrent torrent = m_engine->torrents()->torrent(hash);
    const QJsonArray files = m_details->files()->files();
    const SkippedOnDisk::LocalPathOf localPathOf = localPathOfFiles(torrent);
    const qint64 size = torrent.totalSize > 0 ? torrent.totalSize : torrent.size;
    // The pieces only matter to the plan; the column and the band need the disk alone.
    m_skipped->refresh(hash, files, QByteArray(), size, localPathOf);
}

FreeSkippedJob::Input App::freeInputFor(const Torrent &torrent) const
{
    FreeSkippedJob::Input input;
    input.hash = torrent.hash;
    input.name = torrent.name;
    input.running = isRunning(torrent.state);
    input.hybrid = torrent.isHybrid();
    input.torrentSize = torrent.totalSize > 0 ? torrent.totalSize : torrent.size;
    input.localPathOf = localPathOfFiles(torrent);
    input.contentLocal = m_freeSpace.localPathFor(torrent.contentPath);
    // Board 26's guard: what every other torrent of the library uses.
    const TorrentModel *torrents = m_engine->torrents();
    for (int row = 0; row < torrents->count(); ++row) {
        const Torrent &other = torrents->torrentAt(row);
        if (other.hash == torrent.hash)
            continue;
        const QString local = m_freeSpace.localPathFor(other.contentPath);
        if (!local.isEmpty())
            input.sharedRoots.append(local);
    }
    return input;
}

void App::planFreeSkipped(const QString &hash)
{
    if (hash.isEmpty() || m_demoMode || !m_hasServer || m_engine->torrents()->indexOfHash(hash) < 0)
        return;
    const Torrent torrent = m_engine->torrents()->torrent(hash);
    const FreeSkippedJob::Input input = freeInputFor(torrent);
    auto answer = [this, hash](QVariantMap plan) {
        plan.insert(QStringLiteral("hash"), hash);
        Q_EMIT freeSkippedPlanned(plan);
    };
    m_client->torrentsFiles(hash)->then(
        this, [this, input, answer](const QbtResult<QJsonArray> &files) {
            if (files.isError()) {
                answer({{QStringLiteral("error"), actionFailureText(files.error())}});
                return;
            }
            const QJsonArray list = files.value();
            m_client->torrentsPieceStates(input.hash)
                ->then(this, [this, input, list, answer](const QbtResult<QJsonArray> &pieces) {
                    const QByteArray states =
                        pieces.isError() ? QByteArray() : PieceStates::fromJson(pieces.value());
                    const SkippedReading reading = SkippedOnDisk::read(input.hash,
                                                                       list,
                                                                       states,
                                                                       input.torrentSize,
                                                                       input.localPathOf,
                                                                       input.reader);
                    QSet<QString> paths;
                    for (const SkippedFile &file : reading.files) {
                        if (file.holdsSpace())
                            paths.insert(file.localPath);
                    }
                    const FreePlan plan =
                        SkippedOnDisk::plan(reading,
                                            input.hybrid,
                                            input.contentLocal,
                                            input.sharedRoots,
                                            SkippedOnDisk::openFileHolders(paths, input.procRoot));
                    m_lastFreePlan = plan;
                    m_lastFreePlanHash = input.hash;
                    QVariantList items;
                    for (const FreeItem &item : plan.items) {
                        if (items.size() >= kPlanRows)
                            break;
                        items.append(QVariantMap{
                            {QStringLiteral("name"), item.file.path},
                            {QStringLiteral("holdsText"), Format::size(item.file.allocated)},
                            {QStringLiteral("verdict"), verdictName(item.verdict)},
                            {QStringLiteral("openBy"), item.openBy}});
                    }
                    const Torrent &torrent = m_engine->torrents()->torrent(input.hash);
                    answer({{QStringLiteral("name"), input.name},
                            {QStringLiteral("folder"), torrent.contentPath},
                            {QStringLiteral("seen"), reading.seen},
                            {QStringLiteral("hybrid"), input.hybrid},
                            {QStringLiteral("running"), input.running},
                            {QStringLiteral("freed"), plan.freed},
                            {QStringLiteral("freedText"), Format::size(plan.freed)},
                            {QStringLiteral("deletes"), plan.deletes},
                            {QStringLiteral("recheck"), plan.recheck},
                            {QStringLiteral("leftWithPieces"), plan.leftWithPieces},
                            {QStringLiteral("leftText"), Format::size(plan.leftWithPiecesBytes)},
                            {QStringLiteral("kept"), int(plan.items.size()) - plan.deletes},
                            {QStringLiteral("items"), items},
                            {QStringLiteral("more"), int(plan.items.size()) - int(items.size())}});
                });
        });
}

void App::freeSkippedSpace(const QString &hash)
{
    if (hash.isEmpty() || demoGuard(QStringLiteral("freeSkippedSpace"), 1) || !m_hasServer ||
        !m_skippedStep.isEmpty() || m_engine->torrents()->indexOfHash(hash) < 0)
        return;
    const Torrent torrent = m_engine->torrents()->torrent(hash);
    auto *job = new FreeSkippedJob(m_client, this);
    connect(job, &FreeSkippedJob::step, this, [this](const QString &what) {
        m_skippedStep = what;
        Q_EMIT skippedOnDiskChanged();
    });
    connect(job,
            &FreeSkippedJob::finished,
            this,
            [this, job, hash](const FreeSkippedJob::Outcome &outcome) {
                job->deleteLater();
                m_skippedStep.clear();
                if (outcome.rechecked)
                    m_engine->problems()->noteRechecked({hash});
                Q_EMIT skippedSpaceFreed(
                    {{QStringLiteral("hash"), hash},
                     {QStringLiteral("ok"), outcome.ok},
                     {QStringLiteral("error"), outcome.error},
                     {QStringLiteral("deleted"), outcome.deleted},
                     {QStringLiteral("freedText"), Format::size(outcome.freed)},
                     {QStringLiteral("kept"), outcome.kept},
                     {QStringLiteral("failed"), int(outcome.failed.size())},
                     {QStringLiteral("rechecked"), outcome.rechecked}});
                Q_EMIT skippedOnDiskChanged();
                m_skippedAfterRefresh = m_filesTabActive;
                m_details->refreshNow();
            });
    m_skippedStep = QStringLiteral("starting");
    Q_EMIT skippedOnDiskChanged();
    job->start(freeInputFor(torrent));
}

void App::addTorrentAgain(const QString &hash, bool freeLeftovers)
{
    if (hash.isEmpty() || demoGuard(QStringLiteral("addTorrentAgain"), 1) || !m_hasServer ||
        !m_skippedStep.isEmpty() || m_engine->torrents()->indexOfHash(hash) < 0)
        return;
    const QString name = m_engine->torrents()->torrent(hash).name;
    AddAgainJob::Input input;
    input.hash = hash;
    if (freeLeftovers && m_lastFreePlanHash == hash)
        input.deletePaths = m_lastFreePlan.deletePaths() + m_lastFreePlan.piecesPaths();
    // What a program opened since the plan stays (C17 D3).
    input.remove = [](const QString &path) {
        if (!SkippedOnDisk::openFileHolders({path}).isEmpty())
            return false;
        return QFile::remove(path);
    };
    auto *job = new AddAgainJob(m_client, this);
    connect(job, &AddAgainJob::step, this, [this](const QString &what) {
        m_skippedStep = what;
        Q_EMIT skippedOnDiskChanged();
    });
    connect(job,
            &AddAgainJob::finished,
            this,
            [this, job, hash, name](const AddAgainJob::Outcome &outcome) {
                job->deleteLater();
                m_skippedStep.clear();
                m_lastFreePlanHash.clear();
                Q_EMIT torrentAddedAgain(
                    {{QStringLiteral("hash"), outcome.hash.isEmpty() ? hash : outcome.hash},
                     {QStringLiteral("ok"), outcome.ok},
                     {QStringLiteral("error"), outcome.error},
                     {QStringLiteral("name"), name},
                     {QStringLiteral("deleted"), outcome.deleted},
                     {QStringLiteral("warnings"), outcome.warnings}});
                Q_EMIT skippedOnDiskChanged();
            });
    m_skippedStep = QStringLiteral("reading");
    Q_EMIT skippedOnDiskChanged();
    job->start(input);
}

} // namespace omaseed::app
