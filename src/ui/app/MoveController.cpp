#include "ui/app/MoveController.hpp"

#include "ui/app/MoveWords.hpp"

#include "core/ErrorWords.hpp"
#include "core/Format.hpp"
#include "core/FreeSpace.hpp"
#include "core/QbtClient.hpp"
#include "core/QbtMoveFeed.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

namespace omaseed::app {

using core::FolderTree;
using core::MoveJob;
using core::MovePlan;
using core::SharedData;
using namespace movewords;

namespace {

/// J4's "omaseed was closed for 9 min" stays this long after the job is taken up again.
constexpr qint64 kRestoredLineSeconds = 600;
/// E3: a torrent that never reads "checking" within these polls is taken as checked.
constexpr int kRecheckQuietPolls = 4;

bool isChecking(core::TorrentState state)
{
    return state == core::TorrentState::CheckingDownload ||
           state == core::TorrentState::CheckingUpload ||
           state == core::TorrentState::CheckingResumeData;
}

QString stateKey(MoveJob::State state)
{
    switch (state) {
    case MoveJob::State::Idle:
        return QStringLiteral("idle");
    case MoveJob::State::Running:
        return QStringLiteral("running");
    case MoveJob::State::Pausing:
        return QStringLiteral("pausing");
    case MoveJob::State::Paused:
        return QStringLiteral("paused");
    case MoveJob::State::Ending:
        return QStringLiteral("ending");
    case MoveJob::State::Carrying:
        return QStringLiteral("carrying");
    case MoveJob::State::Done:
        return QStringLiteral("done");
    case MoveJob::State::Ended:
        return QStringLiteral("ended");
    }
    return QStringLiteral("idle");
}

} // namespace

MoveController::MoveController(QObject *parent)
    : QObject(parent)
    , m_folders(new core::FolderModel(this))
{
    m_foldersTimer.setSingleShot(true);
    m_foldersTimer.setInterval(0);
    connect(&m_foldersTimer, &QTimer::timeout, this, &MoveController::rebuildFolders);
}

MoveController::~MoveController()
{
    stopCarry();
    saveNow();
    m_feed.reset();
}

void MoveController::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
    if (m_hooks.engine == nullptr)
        return;
    connect(m_hooks.engine, &core::SyncEngine::synced, this, &MoveController::onSynced);
    core::TorrentModel *torrents = m_hooks.engine->torrents();
    connect(torrents, &core::TorrentModel::modelReset, this, &MoveController::scheduleFolders);
    connect(torrents, &core::TorrentModel::rowsRemoved, this, &MoveController::scheduleFolders);
    connect(torrents, &core::TorrentModel::rowsInserted, this, &MoveController::scheduleFolders);
}

void MoveController::setServer(const QString &serverId)
{
    if (serverId == m_serverId && (m_job != nullptr || serverId.isEmpty()))
        return;
    detachJob();
    m_serverId = serverId;
    m_files.clear();
    m_filesPending.clear();
    m_serverFolders.clear();
    m_serverFoldersAt.clear();
    m_listingsPending.clear();
    m_serverLists = true;
    m_loose.clear();
    m_rechecks.clear();
    m_recheckHash.clear();
    if (!serverId.isEmpty())
        attachJob();
    scheduleFolders();
    bump();
    Q_EMIT planChanged();
    Q_EMIT rechecksChanged();
}

void MoveController::attachJob()
{
    m_job = std::make_unique<MoveJob>();
    if (m_hooks.clock)
        m_job->setClock(m_hooks.clock);
    if (m_hooks.lowDiskBytes)
        m_job->setLowDiskThreshold(m_hooks.lowDiskBytes());
    m_job->setFilePath(MoveJob::defaultFilePath(m_serverId));
    connect(m_job.get(), &MoveJob::changed, this, &MoveController::onJobChanged);
    connect(m_job.get(), &MoveJob::stateChanged, this, &MoveController::onJobChanged);
    connect(m_job.get(), &MoveJob::finished, this, &MoveController::onFinished);
    connect(m_job.get(), &MoveJob::pausedItself, this, &MoveController::onPausedItself);
    connect(m_job.get(), &MoveJob::carryWanted, this, &MoveController::onCarryWanted);
    if (m_hooks.engine != nullptr) {
        m_feed = std::make_unique<core::QbtMoveFeed>(m_hooks.engine, m_job.get());
        m_feed->setFreeSpace(m_hooks.probe);
    }
    if (m_job->restore()) {
        m_restoredAt = now();
        // The torrents were done and the loose files due when omaseed stopped (#98 carries
        // them; here nothing is).
        if (m_job->state() == MoveJob::State::Carrying)
            QTimer::singleShot(0, this, &MoveController::onCarryWanted);
        QString from;
        QString to;
        QList<core::CarryFile> files;
        if (m_job->state() == MoveJob::State::Done &&
            loadCarry(m_job->root(), &from, &to, &files)) {
            m_carryResult = core::LooseCarry::tally(files);
            m_carryDone = true;
        }
    }
}

void MoveController::detachJob()
{
    if (m_job == nullptr)
        return;
    stopCarry();
    m_job->saveNow();
    m_feed.reset();
    m_job.reset();
    m_restoredAt = QDateTime();
}

void MoveController::bump()
{
    ++m_revision;
    updateMarks();
    Q_EMIT changed();
}

void MoveController::onJobChanged()
{
    bump();
}

void MoveController::onSynced()
{
    scheduleFolders();
    if (!m_recheckHash.isEmpty())
        recheckNext();
}

QDateTime MoveController::now() const
{
    return m_hooks.clock ? m_hooks.clock() : QDateTime::currentDateTime();
}

QString MoveController::serverName() const
{
    return m_hooks.serverName ? m_hooks.serverName() : QString();
}

// ---- the folders (F1–F3) --------------------------------------------------------------------

QList<core::Torrent> MoveController::library() const
{
    QList<core::Torrent> out;
    if (m_hooks.engine == nullptr)
        return out;
    const core::TorrentModel *torrents = m_hooks.engine->torrents();
    const int rows = torrents->rowCount();
    out.reserve(rows);
    for (int row = 0; row < rows; ++row)
        out << torrents->torrentAt(row);
    return out;
}

QString MoveController::mountRootOf(const QString &serverPath) const
{
    // F1: a drive's top is the heading. This machine knows its mount points; the server's path
    // for one is the server path less what the path map put below the mount.
    if (m_hooks.probe == nullptr || !m_hooks.probe->seesServerDisks())
        return {};
    const QString local = SharedData::normalize(m_hooks.probe->localPathFor(serverPath));
    const core::FreeSpaceProbe::Answer answer = m_hooks.probe->answer(serverPath);
    if (local.isEmpty() || answer.source != core::FreeSpaceProbe::Source::Local)
        return {};
    const QString mount = SharedData::normalize(answer.where);
    if (mount.isEmpty() || !SharedData::isUnder(local, mount))
        return {};
    const QString below = mount == QLatin1String("/") ? local.mid(1) : local.mid(mount.size() + 1);
    const QString server = SharedData::normalize(serverPath);
    if (below.isEmpty())
        return server;
    if (!server.endsWith(QLatin1Char('/') + below))
        return {};
    const QString top = server.left(server.size() - below.size() - 1);
    return top.isEmpty() ? QStringLiteral("/") : top;
}

void MoveController::scheduleFolders()
{
    if (!m_foldersTimer.isActive())
        m_foldersTimer.start();
}

void MoveController::rebuildFolders()
{
    QHash<QString, QString> mounts;
    const auto mountRoot = [this, &mounts](const QString &path) {
        const auto cached = mounts.constFind(path);
        if (cached != mounts.cend())
            return *cached;
        const QString root = mountRootOf(path);
        mounts.insert(path, root);
        return root;
    };
    // The rows follow the list on show, a demo's too; only the job needs a server.
    m_folders->setNodes(FolderTree::build(library(), mountRoot));
    updateMarks();
}

void MoveController::updateMarks()
{
    QHash<QString, QString> marks;
    if (m_job != nullptr) {
        switch (m_job->state()) {
        case MoveJob::State::Running:
        case MoveJob::State::Pausing:
        case MoveJob::State::Paused:
        case MoveJob::State::Ending: {
            // F3: the folder being moved shows the job's percentage, the save paths below it
            // the moving glyph.
            const QString root = SharedData::normalize(m_job->root());
            for (const core::FolderNode &node : m_folders->nodes()) {
                if (node.path == root)
                    marks.insert(node.path, QStringLiteral("%1 %").arg(m_job->percent()));
                else if (SharedData::isUnder(node.path, root))
                    marks.insert(node.path, QStringLiteral("↻"));
            }
            break;
        }
        default:
            break;
        }
    }
    m_folders->setMarks(marks);
}

QString MoveController::folderOf(const QString &hash) const
{
    if (m_hooks.engine == nullptr || m_hooks.engine->torrents()->indexOfHash(hash) < 0)
        return {};
    return SharedData::normalize(m_hooks.engine->torrents()->torrent(hash).savePath);
}

QString MoveController::folderLine(const QString &path) const
{
    const QVariantMap figures = m_folders->figures(SharedData::normalize(path));
    if (figures.isEmpty())
        return {};
    QStringList parts;
    parts << figures.value(QStringLiteral("name")).toString()
          << figures.value(QStringLiteral("path")).toString()
          << counted(figures.value(QStringLiteral("count")).toInt(),
                     tr("%1 torrent"),
                     tr("%1 torrents"))
          << tr("%1 on disk")
                 .arg(core::Format::size(figures.value(QStringLiteral("bytes")).toLongLong()));
    const int savePaths = figures.value(QStringLiteral("savePaths")).toInt();
    if (savePaths > 1)
        parts << counted(savePaths, tr("%1 save path"), tr("%1 save paths"));
    const auto walk = m_loose.constFind(figures.value(QStringLiteral("path")).toString());
    if (walk != m_loose.cend() && !walk->running && walk->result.complete && walk->result.loose > 0)
        parts << counted(
            walk->result.loose, tr("%1 file no torrent owns"), tr("%1 files no torrent owns"));
    return parts.join(QStringLiteral(" · "));
}

bool MoveController::start(const QString &rootIn, const QString &newPathIn, bool carry)
{
    if (m_job == nullptr)
        return false;
    const QString root = SharedData::normalize(rootIn);
    const QString newPath = SharedData::normalize(newPathIn.trimmed());
    const MovePlan plan = makePlan(root, newPath, carry);
    if (!plan.canMove() || plan.namesCheck() == MovePlan::NamesCheck::Pending)
        return false;
    // J3's "move the rest…": the summary of the last job gives way to the new one.
    if (m_job->state() == MoveJob::State::Done || m_job->state() == MoveJob::State::Ended)
        m_job->closeSummary();
    qint64 looseBytes = 0;
    if (carry) {
        const auto walk = m_loose.constFind(root);
        if (walk != m_loose.cend())
            looseBytes = walk->result.looseBytes;
    }
    if (m_hooks.lowDiskBytes)
        m_job->setLowDiskThreshold(m_hooks.lowDiskBytes());
    m_restoredAt = QDateTime();
    const bool started = m_job->start(plan, m_serverId, carry, looseBytes);
    if (started) {
        // M9/M17: the dry run shown in the sheet is what gets carried, so it is kept with the
        // job and survives a restart. A job queued behind another walks its folder afresh
        // when its turn comes (onCarryWanted).
        QFile::remove(carryFilePath());
        m_carryResult = {};
        m_carryDone = false;
        m_carryPaused = false;
        const auto walk = m_loose.constFind(root);
        if (carry && walk != m_loose.cend() && !walk->running && walk->result.complete &&
            m_hooks.probe != nullptr) {
            QList<core::CarryFile> files;
            for (const core::LooseFile &file : walk->result.files) {
                core::CarryFile item;
                item.relative = file.relative;
                item.size = file.size;
                item.modified = file.modified;
                item.why = file.why;
                switch (file.verdict) {
                case core::LooseFile::Verdict::Loose:
                    item.why.clear();
                    break;
                case core::LooseFile::Verdict::Recent:
                    item.outcome = core::CarryFile::Outcome::Recent;
                    break;
                case core::LooseFile::Verdict::Unknown:
                    item.outcome = core::CarryFile::Outcome::Unknown;
                    break;
                case core::LooseFile::Verdict::NeverCounts:
                    continue;
                }
                files << item;
            }
            QSaveFile out(carryFilePath());
            QDir().mkpath(QFileInfo(carryFilePath()).path());
            if (out.open(QIODevice::WriteOnly)) {
                out.write(
                    QJsonDocument(
                        QJsonObject{{QStringLiteral("root"), root},
                                    {QStringLiteral("from"), walk->local},
                                    {QStringLiteral("to"), m_hooks.probe->localPathFor(newPath)},
                                    {QStringLiteral("files"), core::LooseCarry::toJson(files)}})
                        .toJson(QJsonDocument::Compact));
                out.commit();
            }
        }
    }
    bump();
    return true;
}

// ---- the job --------------------------------------------------------------------------------

bool MoveController::shown() const
{
    return m_job != nullptr && m_job->state() != MoveJob::State::Idle;
}

bool MoveController::active() const
{
    return m_job != nullptr && m_job->isActive();
}

QString MoveController::stateName() const
{
    return m_job == nullptr ? QStringLiteral("idle") : stateKey(m_job->state());
}

QString MoveController::root() const
{
    return m_job == nullptr ? QString() : m_job->root();
}

QString MoveController::newPath() const
{
    return m_job == nullptr ? QString() : m_job->newPath();
}

QStringList MoveController::jobHashes() const
{
    QStringList out;
    if (m_job == nullptr)
        return out;
    for (const MoveJob::Entry &entry : m_job->entries())
        out << entry.hash;
    return out;
}

void MoveController::pause()
{
    if (m_job == nullptr)
        return;
    if (m_job->state() == MoveJob::State::Carrying) {
        // L1's pause: the file being copied is dropped (its partial copy removed) and the
        // carry waits; resume, or the next start, takes it up again.
        if (m_carryStop != nullptr)
            m_carryStop->store(true);
        return;
    }
    m_job->pause();
}

void MoveController::resume()
{
    if (m_job == nullptr)
        return;
    if (m_job->state() == MoveJob::State::Carrying) {
        if (m_carryPaused)
            onCarryWanted();
        return;
    }
    m_job->resume();
}

void MoveController::end()
{
    if (m_job != nullptr)
        m_job->end();
}

void MoveController::keepWaiting()
{
    if (m_job != nullptr)
        m_job->keepWaiting();
}

void MoveController::closeSummary()
{
    if (m_job != nullptr &&
        (m_job->state() == MoveJob::State::Done || m_job->state() == MoveJob::State::Ended)) {
        QFile::remove(carryFilePath());
        m_carryResult = {};
        m_carryDone = false;
    }
    if (m_job != nullptr)
        m_job->closeSummary();
    bump();
}

void MoveController::pauseForQuit()
{
    if (m_job == nullptr)
        return;
    stopCarry();
    m_job->pause(MoveJob::PauseReason::Quit);
    m_job->saveNow();
}

void MoveController::saveNow()
{
    if (m_job != nullptr)
        m_job->saveNow();
}

QVariantMap MoveController::stepFor(const QString &hash) const
{
    if (m_job == nullptr)
        return {};
    const MoveJob::Entry *entry = m_job->entry(hash);
    if (entry == nullptr)
        return {};
    QString text;
    QString glyph;
    QString kind = QStringLiteral("dim");
    switch (entry->step) {
    case MoveJob::Step::Waiting:
        glyph = QStringLiteral("·");
        text = tr("waiting");
        break;
    case MoveJob::Step::Queued:
        glyph = QStringLiteral("◌");
        text = tr("queued");
        kind = QStringLiteral("busy");
        break;
    case MoveJob::Step::Moving:
        glyph = QStringLiteral("↻");
        text = tr("moving now");
        kind = QStringLiteral("busy");
        break;
    case MoveJob::Step::Moved:
        glyph = QStringLiteral("✓");
        text = tr("moved");
        kind = QStringLiteral("ok");
        break;
    case MoveJob::Step::Failed:
        glyph = QStringLiteral("✕");
        text = tr("failed");
        kind = QStringLiteral("bad");
        break;
    case MoveJob::Step::LeftOut:
        glyph = QStringLiteral("✕");
        text = tr("left out");
        kind = QStringLiteral("bad");
        break;
    case MoveJob::Step::AlreadyThere:
        glyph = QStringLiteral("·");
        text = tr("already there");
        break;
    case MoveJob::Step::Removed:
        glyph = QStringLiteral("·");
        text = tr("removed");
        break;
    case MoveJob::Step::Elsewhere:
        glyph = QStringLiteral("·");
        text = tr("moved elsewhere");
        break;
    }
    return {{QStringLiteral("text"), text},
            {QStringLiteral("glyph"), glyph},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("reason"), entry->reason}};
}

QStringList MoveController::failedHashes() const
{
    if (m_job == nullptr)
        return {};
    QList<const MoveJob::Entry *> failed;
    for (const MoveJob::Entry &entry : m_job->entries()) {
        if (entry.step == MoveJob::Step::Failed)
            failed << &entry;
    }
    std::stable_sort(
        failed.begin(), failed.end(), [](const MoveJob::Entry *a, const MoveJob::Entry *b) {
            return a->bytes < b->bytes;
        });
    QStringList out;
    for (const MoveJob::Entry *entry : failed)
        out << entry->hash;
    return out;
}

QString MoveController::trayLine() const
{
    if (m_job == nullptr)
        return {};
    const QString name = FolderTree::baseName(m_job->root());
    switch (m_job->state()) {
    case MoveJob::State::Running: {
        const QString left = timeLeft(m_job->secondsLeft());
        return left.isEmpty() ? tr("Moving %1: %2 %").arg(name).arg(m_job->percent())
                              : tr("Moving %1: %2 %, %3").arg(name).arg(m_job->percent()).arg(left);
    }
    case MoveJob::State::Pausing:
    case MoveJob::State::Paused:
        return tr("Moving %1: paused at %2 %").arg(name).arg(m_job->percent());
    case MoveJob::State::Ending:
        return tr("Moving %1: ending").arg(name);
    case MoveJob::State::Carrying:
        return tr("Moving %1: carrying the loose files").arg(name);
    default:
        return {};
    }
}

QString MoveController::quitLine() const
{
    if (m_job == nullptr || !m_job->isActive())
        return {};
    const QString moved = tr("%1 of %2 moved.").arg(m_job->moved()).arg(m_job->total());
    const int with = m_job->withServer();
    if (with == 0)
        return moved + QLatin1Char(' ') +
               tr("Quitting pauses it: the rest wait until omaseed runs again.");
    return moved + QLatin1Char(' ') +
           counted(with,
                   tr("Quitting pauses it: the %1 with qBittorrent finishes, and the rest wait "
                      "until omaseed runs again."),
                   tr("Quitting pauses it: the %1 with qBittorrent finish, and the rest wait "
                      "until omaseed runs again."));
}

QVariantMap MoveController::band() const
{
    QVariantMap band;
    if (m_job == nullptr || m_job->state() == MoveJob::State::Idle)
        return band;
    const MoveJob &job = *m_job;
    const QString name = FolderTree::baseName(job.root());
    const QString server = serverName();
    const auto moved = [&job]() { return tr("%1 of %2").arg(job.moved()).arg(job.total()); };
    const auto entryName = [&job](const QString &hash) {
        const MoveJob::Entry *entry = job.entry(hash);
        return entry == nullptr ? QString() : entry->name;
    };
    QVariantList actions;
    QString kind = QStringLiteral("accent");
    QString glyph;
    QString headline;
    QString line;
    QString failedText;
    double progress = -1;
    if (job.failed() > 0)
        failedText = tr("%1 failed").arg(job.failed());

    switch (job.state()) {
    case MoveJob::State::Idle:
        break;
    case MoveJob::State::Running: {
        glyph = QStringLiteral("↻");
        progress = job.percent() / 100.0;
        const bool resumed = m_restoredAt.isValid() &&
                             m_restoredAt.secsTo(now()) < kRestoredLineSeconds &&
                             job.secondsAway() > 0;
        headline = (job.sameDrive() ? tr("Renaming %1 to %2") : tr("Moving %1 to %2"))
                       .arg(name, job.newPath());
        if (resumed)
            headline += tr(", resumed");
        QStringList parts;
        if (!job.isConnected()) {
            parts << tr("waiting for %1").arg(server.isEmpty() ? tr("the server") : server)
                  << moved();
        } else {
            if (resumed)
                parts << tr("omaseed was closed for %1; %2 landed meanwhile")
                             .arg(span(job.secondsAway()))
                             .arg(job.landedWhileAway());
            parts << moved();
            if (!job.sameDrive())
                parts << tr("%1 of %2")
                             .arg(core::Format::size(job.bytesMoved()),
                                  core::Format::size(job.bytesTotal()));
            parts << QStringLiteral("%1 %").arg(job.percent());
            if (!job.sameDrive()) {
                const QString left = timeLeft(job.secondsLeft());
                if (!left.isEmpty())
                    parts << (job.speed() > 0
                                  ? tr("%1 at %2").arg(left, core::Format::speed(job.speed()))
                                  : left);
            }
            const QString current = job.current();
            if (!current.isEmpty()) {
                const MoveJob::Entry *entry = job.entry(current);
                parts << tr("now %1 (%2)").arg(entry->name, core::Format::size(entry->bytes));
            }
        }
        line = parts.join(QStringLiteral(" · "));
        actions << action(QStringLiteral("pause"), tr("pause"))
                << action(QStringLiteral("show"), tr("show"))
                << action(QStringLiteral("details"), tr("details"));
        break;
    }
    case MoveJob::State::Pausing:
        glyph = QStringLiteral("‖");
        headline = tr("Pausing the move of %1").arg(name);
        line = tr("%1 · %2 still with qBittorrent, which finishes them · nothing more is sent")
                   .arg(moved())
                   .arg(job.withServer());
        actions << action(QStringLiteral("resume"), tr("resume"))
                << action(QStringLiteral("end"), tr("end the move"))
                << action(QStringLiteral("details"), tr("details"));
        break;
    case MoveJob::State::Paused: {
        actions << action(QStringLiteral("resume"), tr("resume"));
        switch (job.pauseReason()) {
        case MoveJob::PauseReason::FirstFailed:
        case MoveJob::PauseReason::Refused: {
            // T2: the first torrent didn't move; nothing else was sent.
            kind = QStringLiteral("red");
            glyph = QStringLiteral("✕");
            const QStringList failed = failedHashes();
            const MoveJob::Entry *first = failed.isEmpty() ? nullptr : job.entry(failed.first());
            const QString target = first == nullptr ? job.newPath() : first->to;
            if (first != nullptr && first->causeKey == QLatin1String("cantCreate")) {
                headline = tr("qBittorrent can't create %1").arg(target);
            } else {
                const core::ErrorWords::Text words = core::ErrorWords::forMoveFailure(
                    first == nullptr ? QString() : first->name,
                    target,
                    QString(),
                    first == nullptr ? QStringLiteral("noAccess") : first->causeKey,
                    first == nullptr ? QString() : first->reason);
                headline = words.headline;
            }
            line = job.pauseReason() == MoveJob::PauseReason::FirstFailed
                       ? tr("The first torrent, %1, didn't move: “%2”. Nothing else was sent; "
                            "everything "
                            "is still at %3.")
                             .arg(first == nullptr ? QString() : first->name,
                                  first == nullptr ? QString() : first->reason,
                                  job.root())
                       : tr("%1 didn't move: “%2”. Nothing more is sent.")
                             .arg(first == nullptr ? QString() : first->name,
                                  first == nullptr ? QString() : first->reason);
            failedText.clear(); // the headline says it
            actions.clear();
            actions << action(QStringLiteral("changeFolder"), tr("change the folder…"))
                    << action(QStringLiteral("end"), tr("end the move"));
            break;
        }
        case MoveJob::PauseReason::NoSpace:
        case MoveJob::PauseReason::NearlyFull: {
            // T3: the destination fills up.
            kind = QStringLiteral("yellow");
            glyph = QStringLiteral("!");
            const QString drive = job.newPath();
            const QString hash = job.nearlyFullTorrent();
            const core::ErrorWords::Text words =
                core::ErrorWords::forNearlyFull(entryName(hash), drive, job.nearlyFullFreeAfter());
            // T3 and board 42 §8: a disk qBittorrent found full says so; "nearly full" is
            // omaseed's own reading before it sends (M5).
            headline = job.pauseReason() == MoveJob::PauseReason::NoSpace
                           ? tr("The move of %1 is paused: %2 is full").arg(name, drive)
                           : tr("The move of %1 is paused: %2").arg(name, words.headline);
            line = (job.pauseReason() == MoveJob::PauseReason::NearlyFull
                        ? words.line
                        : tr("qBittorrent reports “No space left on device”.")) +
                   QLatin1Char(' ') +
                   tr("%1 moved, %2 waiting.").arg(job.moved()).arg(job.waiting());
            actions << action(QStringLiteral("end"), tr("end the move"));
            break;
        }
        case MoveJob::PauseReason::Stuck: {
            // T4.
            kind = QStringLiteral("yellow");
            glyph = QStringLiteral("!");
            const QString current = job.current();
            const MoveJob::Entry *entry = current.isEmpty() ? nullptr : job.entry(current);
            const qint64 movingFor = entry == nullptr || !entry->movingSince.isValid()
                                         ? 0
                                         : entry->movingSince.secsTo(now());
            const core::ErrorWords::Text words =
                core::ErrorWords::forStuck(entry == nullptr ? QString() : entry->name,
                                           std::chrono::seconds(movingFor),
                                           job.lastLandedAt(),
                                           job.expected());
            headline = words.headline;
            line = words.line + QLatin1Char(' ') + tr("omaseed has stopped sending.");
            actions.clear();
            actions << action(QStringLiteral("keepWaiting"), tr("keep waiting"))
                    << action(QStringLiteral("end"), tr("end the move"));
            break;
        }
        default:
            glyph = QStringLiteral("‖");
            headline = tr("The move of %1 is paused").arg(name);
            line = tr("%1 · %2 waiting · nothing is sent until you resume")
                       .arg(moved())
                       .arg(job.waiting());
            actions << action(QStringLiteral("end"), tr("end the move"));
            break;
        }
        actions << action(QStringLiteral("details"), tr("details"));
        break;
    }
    case MoveJob::State::Ending:
        glyph = QStringLiteral("‖");
        headline = tr("Ending the move of %1").arg(name);
        line = tr("%1 · %2 still with qBittorrent, which finishes them · nothing more is sent")
                   .arg(moved())
                   .arg(job.withServer());
        actions << action(QStringLiteral("details"), tr("details"));
        break;
    case MoveJob::State::Carrying: {
        // L1: the same band, after the torrents.
        const core::LooseCarry::Progress &carry = m_carryProgress;
        QStringList parts;
        if (carry.total > 0) {
            parts << tr("%1 of %2").arg(carry.done).arg(carry.total);
            if (carry.bytesTotal > 0) {
                parts << tr("%1 of %2")
                             .arg(core::Format::size(carry.bytesDone),
                                  core::Format::size(carry.bytesTotal));
                progress = double(carry.bytesDone) / double(carry.bytesTotal);
            }
        }
        if (m_carryPaused) {
            glyph = QStringLiteral("‖");
            headline = tr("Carrying the loose files to %1: paused").arg(job.newPath());
            parts << tr("nothing is being copied");
            actions << action(QStringLiteral("resume"), tr("resume"));
        } else {
            glyph = QStringLiteral("↻");
            headline = carry.total > 0 ? counted(carry.total,
                                                 tr("Carrying %1 loose file to %2"),
                                                 tr("Carrying %1 loose files to %2"))
                                             .arg(job.newPath())
                                       : tr("Carrying the loose files to %1").arg(job.newPath());
            const qint64 seconds = m_carryStartedAt.isValid() ? m_carryStartedAt.secsTo(now()) : 0;
            const qint64 copied = m_carryStartBytes < 0 ? 0 : carry.bytesDone - m_carryStartBytes;
            if (seconds >= 5 && copied > 0 && carry.bytesTotal > carry.bytesDone) {
                const QString left =
                    timeLeft((carry.bytesTotal - carry.bytesDone) * seconds / copied);
                if (!left.isEmpty())
                    parts << left;
            }
            if (!carry.current.isEmpty())
                parts << (carry.checking ? tr("now %1/%2 (checking the copy)")
                                         : tr("now %1/%2 (copying, then checking)"))
                             .arg(name, carry.current);
            if (parts.isEmpty())
                parts << tr("%1 · then the files no torrent owns").arg(moved());
            actions << action(QStringLiteral("pause"), tr("pause"));
        }
        line = parts.join(QStringLiteral(" · "));
        actions << action(QStringLiteral("details"), tr("details"));
        break;
    }
    case MoveJob::State::Done: {
        // E1.
        const MoveJob::Summary summary = job.summary();
        kind = QStringLiteral("green");
        glyph = QStringLiteral("✓");
        headline = tr("%1 moved to %2").arg(name, job.newPath());
        QStringList parts;
        const qint64 took = summary.startedAt.isValid() && summary.endedAt.isValid()
                                ? summary.startedAt.secsTo(summary.endedAt)
                                : -1;
        const bool carried = job.carryLoose() && m_carryDone &&
                             (m_carryResult.carried > 0 || m_carryResult.stayed > 0);
        if (carried) {
            // L2: the end, with loose files.
            headline = tr("%1 moved, loose files too").arg(name);
            parts << tr("%1 torrents and %2 at %3")
                         .arg(summary.moved)
                         .arg(counted(m_carryResult.carried, tr("%1 file"), tr("%1 files")),
                              job.newPath());
            if (m_carryResult.stayed > 0) {
                int recent = 0;
                int unreadable = 0;
                int different = 0;
                int noRoom = 0;
                int other = 0;
                for (const core::CarryFile &file : m_carryResult.files) {
                    switch (file.outcome) {
                    case core::CarryFile::Outcome::Recent:
                    case core::CarryFile::Outcome::Changed:
                        ++recent;
                        break;
                    case core::CarryFile::Outcome::Unknown:
                        ++unreadable;
                        break;
                    case core::CarryFile::Outcome::Different:
                        ++different;
                        break;
                    case core::CarryFile::Outcome::NoRoom:
                        ++noRoom;
                        break;
                    case core::CarryFile::Outcome::ToDo:
                    case core::CarryFile::Outcome::Carried:
                    case core::CarryFile::Outcome::AlreadyThere:
                        break;
                    default:
                        ++other;
                    }
                }
                QStringList stayed;
                if (recent > 0)
                    stayed << counted(recent, tr("%1 recent file"), tr("%1 recent files"));
                if (unreadable > 0)
                    stayed << tr("%1 unreadable").arg(unreadable);
                if (different > 0)
                    stayed << tr("%1 with a different file at the destination").arg(different);
                if (noRoom > 0)
                    stayed << tr("%1 for want of room").arg(noRoom);
                if (other > 0)
                    stayed << tr("%1 other").arg(other);
                parts << tr("stayed at %1: %2").arg(job.root(), stayed.join(QStringLiteral(", ")));
            }
            if (m_carryResult.foldersRemoved > 0)
                parts << counted(m_carryResult.foldersRemoved,
                                 tr("%1 empty folder removed there"),
                                 tr("%1 empty folders removed there"));
            if (took >= 0)
                parts << tr("in %1").arg(span(took));
        } else {
            parts
                << (took >= 0
                        ? tr("%1 of %2 in %3").arg(summary.moved).arg(summary.total).arg(span(took))
                        : tr("%1 of %2").arg(summary.moved).arg(summary.total));
        }
        if (summary.failed > 0)
            parts << tr("%1 failed").arg(summary.failed);
        if (summary.alreadyThere > 0)
            parts << counted(
                summary.alreadyThere, tr("%1 was already there"), tr("%1 were already there"));
        if (summary.leftOut > 0)
            parts << tr("%1 left out (a different file at the destination)").arg(summary.leftOut);
        if (summary.removed > 0)
            parts << tr("%1 removed during the move").arg(summary.removed);
        if (summary.elsewhere > 0)
            parts << tr("%1 moved elsewhere by qBittorrent").arg(summary.elsewhere);
        const int added = int(job.addedDuring().size());
        if (added > 0)
            parts << counted(added,
                             tr("%1 added during the move is still at %2"),
                             tr("%1 added during the move are still at %2"))
                         .arg(job.root());
        line = parts.join(QStringLiteral(" · "));
        failedText.clear();
        if (summary.failed > 0) {
            actions << action(QStringLiteral("recheck"), tr("recheck the %1…").arg(summary.failed))
                    << action(QStringLiteral("retry"), tr("retry the %1").arg(summary.failed));
        }
        if (carried && m_carryResult.stayed > 0)
            actions << action(QStringLiteral("stayed"), tr("show what stayed"));
        actions << action(QStringLiteral("show"), tr("show"))
                << action(QStringLiteral("details"), tr("details"))
                << action(QStringLiteral("close"), tr("close"));
        break;
    }
    case MoveJob::State::Ended: {
        // J3.
        const MoveJob::Summary summary = job.summary();
        glyph = QStringLiteral("■");
        headline = tr("The move of %1 ended early").arg(name);
        line = tr("%1 moved to %2 · %3 not moved, still at %4 · every torrent is whole on one side "
                  "or the other")
                   .arg(summary.moved)
                   .arg(job.newPath())
                   .arg(summary.notMoved)
                   .arg(job.root());
        failedText.clear();
        actions << action(QStringLiteral("moveRest"), tr("move the rest…"))
                << action(QStringLiteral("show"), tr("show"))
                << action(QStringLiteral("close"), tr("close"));
        break;
    }
    }
    band.insert(QStringLiteral("kind"), kind);
    band.insert(QStringLiteral("glyph"), glyph);
    band.insert(QStringLiteral("headline"), headline);
    band.insert(QStringLiteral("line"), line);
    band.insert(QStringLiteral("failedText"), failedText);
    band.insert(QStringLiteral("progress"), progress);
    band.insert(QStringLiteral("actions"), actions);
    band.insert(QStringLiteral("withServer"), job.withServer());
    return band;
}

QString MoveController::detailsText() const
{
    if (m_job == nullptr || m_job->state() == MoveJob::State::Idle)
        return {};
    const MoveJob &job = *m_job;
    QStringList lines;
    const QVariantMap words = band();
    lines << words.value(QStringLiteral("headline")).toString()
          << words.value(QStringLiteral("line")).toString() << QString();
    if (job.startedAt().isValid())
        lines << tr("started %1").arg(core::Format::relativeTime(job.startedAt()));
    lines << tr("from %1 to %2").arg(job.root(), job.newPath()) << QString();
    for (const MoveJob::Entry &entry : job.entries()) {
        const QString step = stepFor(entry.hash).value(QStringLiteral("text")).toString();
        lines << QStringLiteral("%1 · %2 · %3 → %4").arg(step, entry.name, entry.from, entry.to);
        if (!entry.reason.isEmpty())
            lines << QStringLiteral("    ") + entry.reason;
        if (!entry.why.isEmpty())
            lines << QStringLiteral("    ") + entry.why;
        if (!entry.line.isEmpty())
            lines << QStringLiteral("    ") + entry.line;
        if (!entry.later.isEmpty())
            lines << QStringLiteral("    ") + tr("later: %1").arg(entry.later);
    }
    return lines.join(QLatin1Char('\n'));
}

void MoveController::onFinished()
{
    bump();
    if (!m_hooks.notify || m_job == nullptr)
        return;
    // E2: one notice, after the loose files.
    const MoveJob::Summary summary = m_job->summary();
    const QString name = FolderTree::baseName(m_job->root());
    QStringList parts;
    parts << tr("%1 moved").arg(summary.moved);
    if (summary.failed > 0)
        parts << tr("%1 failed").arg(summary.failed);
    if (summary.endedEarly)
        parts << tr("%1 not moved").arg(summary.notMoved);
    if (m_carryDone && m_carryResult.carried > 0)
        parts << counted(
            m_carryResult.carried, tr("%1 loose file carried"), tr("%1 loose files carried"));
    if (m_carryDone && m_carryResult.stayed > 0)
        parts << counted(m_carryResult.stayed, tr("%1 file stayed"), tr("%1 files stayed"));
    m_hooks.notify(summary.endedEarly ? tr("The move of %1 ended early").arg(name)
                                      : tr("%1 moved").arg(name),
                   parts.join(QStringLiteral(", ")));
}

void MoveController::onPausedItself(MoveJob::PauseReason reason)
{
    Q_UNUSED(reason)
    bump();
    // E2: a job that paused itself gives one notice too, since nothing moves until someone
    // looks.
    if (!m_hooks.notify)
        return;
    const QVariantMap words = band();
    m_hooks.notify(words.value(QStringLiteral("headline")).toString(),
                   words.value(QStringLiteral("line")).toString());
}

void MoveController::onCarryWanted()
{
    // M17: the torrents are done; carry the files no torrent owns, off the main thread.
    if (m_job == nullptr || m_job->state() != MoveJob::State::Carrying)
        return;
    if (m_carryRunning != nullptr && m_carryRunning->load())
        return;
    const QString root = m_job->root();
    QString from;
    QString to;
    QList<core::CarryFile> files;
    QStringList owned;
    const bool recorded = loadCarry(root, &from, &to, &files);
    if (!recorded && m_hooks.probe != nullptr && m_hooks.probe->seesServerDisks()) {
        // No dry run kept (the job was queued behind another, or omaseed stopped before it
        // was written): walk the folder now. The torrents have left it, so only those still
        // there (failed, left out) own anything in it.
        from = m_hooks.probe->localPathFor(root);
        to = m_hooks.probe->localPathFor(m_job->newPath());
        owned = ownedLocalPaths(root);
    }
    if (from.isEmpty() || to.isEmpty()) {
        m_job->finishCarry(0, 0);
        return;
    }

    m_carryPaused = false;
    m_carryProgress = {};
    m_carryStartedAt = now();
    m_carryStartBytes = -1;
    m_carryStop = std::make_shared<std::atomic_bool>(false);
    m_carryRunning = std::make_shared<std::atomic_bool>(true);
    bump();

    QPointer<MoveController> self(this);
    const QString serverId = m_serverId;
    const QString record = carryFilePath();
    const QDateTime at = now();
    QThreadPool::globalInstance()->start([self,
                                          stop = m_carryStop,
                                          running = m_carryRunning,
                                          serverId,
                                          root,
                                          from,
                                          to,
                                          files,
                                          recorded,
                                          owned,
                                          record,
                                          at]() mutable {
        if (!recorded) {
            const core::LooseFiles::Result walk = core::LooseFiles::scan(from, owned, at);
            for (const core::LooseFile &file : walk.files) {
                // Fail closed: an incomplete walk carries nothing.
                if (file.verdict == core::LooseFile::Verdict::NeverCounts)
                    continue;
                core::CarryFile item;
                item.relative = file.relative;
                item.size = file.size;
                item.modified = file.modified;
                if (file.verdict != core::LooseFile::Verdict::Loose || !walk.complete) {
                    item.outcome = file.verdict == core::LooseFile::Verdict::Recent
                                       ? core::CarryFile::Outcome::Recent
                                       : core::CarryFile::Outcome::Unknown;
                    item.why = file.why;
                }
                files << item;
            }
        }
        QElapsedTimer since;
        since.start();
        const auto post = [self](auto &&work) {
            if (QCoreApplication *app = QCoreApplication::instance())
                QMetaObject::invokeMethod(
                    app, std::forward<decltype(work)>(work), Qt::QueuedConnection);
        };
        const core::LooseCarry::Result result = core::LooseCarry::run(
            from,
            to,
            files,
            QDateTime::currentDateTime(),
            std::chrono::minutes(15),
            [stop]() { return stop->load(); },
            [&](const core::LooseCarry::Progress &progress) {
                if (since.elapsed() < 200 && !progress.current.isEmpty())
                    return;
                since.restart();
                post([self, progress]() {
                    if (self.isNull())
                        return;
                    self->m_carryProgress = progress;
                    if (self->m_carryStartBytes < 0)
                        self->m_carryStartBytes = progress.bytesDone;
                    self->bump();
                });
            });
        // The record is written here too, so a quit right after still finds it.
        QSaveFile out(record);
        QDir().mkpath(QFileInfo(record).path());
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(QJsonObject{{QStringLiteral("root"), root},
                                                {QStringLiteral("from"), from},
                                                {QStringLiteral("to"), to},
                                                {QStringLiteral("files"),
                                                 core::LooseCarry::toJson(result.files)}})
                          .toJson(QJsonDocument::Compact));
            out.commit();
        }
        running->store(false);
        post([self, serverId, root, from, to, result]() {
            if (!self.isNull())
                self->onCarryRan(serverId, root, from, to, result);
        });
    });
}

void MoveController::onCarryRan(const QString &serverId,
                                const QString &root,
                                const QString &from,
                                const QString &to,
                                const core::LooseCarry::Result &result)
{
    Q_UNUSED(from)
    Q_UNUSED(to)
    if (m_job == nullptr || serverId != m_serverId || m_job->root() != root ||
        m_job->state() != MoveJob::State::Carrying)
        return;
    if (result.stopped) {
        m_carryPaused = true;
        bump();
        return;
    }
    m_carryResult = result;
    m_carryDone = true;
    m_job->finishCarry(result.carried, result.stayed);
}

QString MoveController::carryFilePath() const
{
    if (m_job == nullptr || m_job->filePath().isEmpty())
        return {};
    QString path = m_job->filePath();
    if (path.endsWith(QLatin1String(".json")))
        path.chop(5);
    return path + QStringLiteral(".carry.json");
}

bool MoveController::loadCarry(const QString &root,
                               QString *from,
                               QString *to,
                               QList<core::CarryFile> *files) const
{
    QFile file(carryFilePath());
    if (carryFilePath().isEmpty() || !file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("root")).toString() != root)
        return false;
    *from = object.value(QStringLiteral("from")).toString();
    *to = object.value(QStringLiteral("to")).toString();
    *files = core::LooseCarry::fromJson(object.value(QStringLiteral("files")).toArray());
    return !from->isEmpty();
}

void MoveController::stopCarry()
{
    if (m_carryStop != nullptr)
        m_carryStop->store(true);
    // The worker stops within one read (1 MiB) and removes its partial copy; wait for that,
    // so a quit never leaves a temporary file behind.
    QElapsedTimer waited;
    waited.start();
    while (m_carryRunning != nullptr && m_carryRunning->load() && waited.elapsed() < 10000)
        QThread::msleep(5);
}

QVariantList MoveController::carryRows() const
{
    QVariantList rows;
    if (m_job == nullptr)
        return rows;
    const QString name = FolderTree::baseName(m_job->root());
    QList<core::CarryFile> files = m_carryResult.files;
    std::stable_sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
        return !a.carried() && b.carried();
    });
    for (const core::CarryFile &file : files) {
        if (file.outcome == core::CarryFile::Outcome::ToDo)
            continue;
        rows << QVariantMap{{QStringLiteral("path"), name + QLatin1Char('/') + file.relative},
                            {QStringLiteral("size"), core::Format::size(file.size)},
                            {QStringLiteral("what"),
                             file.carried() ? tr("carried") : tr("stays: %1").arg(file.why)},
                            {QStringLiteral("kind"),
                             file.carried() ? QStringLiteral("ok")
                             : file.outcome == core::CarryFile::Outcome::Unknown
                                 ? QStringLiteral("unknown")
                             : file.outcome == core::CarryFile::Outcome::Recent ||
                                     file.outcome == core::CarryFile::Outcome::Changed
                                 ? QStringLiteral("warn")
                                 : QStringLiteral("unknown")}};
    }
    return rows;
}

QString MoveController::carryText() const
{
    QStringList lines;
    for (const QVariant &row : carryRows()) {
        const QVariantMap map = row.toMap();
        lines << QStringLiteral("%1\t%2\t%3")
                     .arg(map.value(QStringLiteral("path")).toString(),
                          map.value(QStringLiteral("size")).toString(),
                          map.value(QStringLiteral("what")).toString());
    }
    return lines.join(QLatin1Char('\n'));
}

// ---- E3: recheck smallest first, then start the complete ones ------------------------------

QVariantList MoveController::recheckRows(const QStringList &hashes) const
{
    QVariantList rows;
    if (m_hooks.engine == nullptr)
        return rows;
    const core::TorrentModel *torrents = m_hooks.engine->torrents();
    QList<core::Torrent> list;
    for (const QString &hash : hashes) {
        if (torrents->indexOfHash(hash) >= 0)
            list << torrents->torrent(hash);
    }
    std::stable_sort(list.begin(), list.end(), [](const core::Torrent &a, const core::Torrent &b) {
        return a.completed < b.completed;
    });
    for (const core::Torrent &torrent : list)
        rows << QVariantMap{{QStringLiteral("hash"), torrent.hash},
                            {QStringLiteral("name"), torrent.name},
                            {QStringLiteral("bytes"), core::Format::size(torrent.completed)},
                            {QStringLiteral("then"), tr("start if it comes out complete")}};
    return rows;
}

void MoveController::recheckSmallestFirst(const QStringList &hashes)
{
    m_rechecks.clear();
    for (const QVariant &row : recheckRows(hashes))
        m_rechecks << row.toMap().value(QStringLiteral("hash")).toString();
    m_recheckHash.clear();
    recheckNext();
    Q_EMIT rechecksChanged();
}

void MoveController::recheckNext()
{
    if (m_hooks.engine == nullptr || m_hooks.engine->client() == nullptr)
        return;
    core::TorrentModel *torrents = m_hooks.engine->torrents();
    if (!m_recheckHash.isEmpty()) {
        if (torrents->indexOfHash(m_recheckHash) < 0) {
            m_recheckHash.clear();
        } else {
            const core::Torrent &torrent = torrents->torrent(m_recheckHash);
            if (isChecking(torrent.state)) {
                m_recheckSeenChecking = true;
                return;
            }
            if (!m_recheckSeenChecking && ++m_recheckPolls < kRecheckQuietPolls)
                return;
            if (torrent.progress >= 1.0)
                m_hooks.engine->client()->torrentsStart({m_recheckHash});
            m_recheckHash.clear();
        }
    }
    if (m_rechecks.isEmpty()) {
        Q_EMIT rechecksChanged();
        return;
    }
    m_recheckHash = m_rechecks.takeFirst();
    m_recheckSeenChecking = false;
    m_recheckPolls = 0;
    m_hooks.engine->client()->torrentsRecheck({m_recheckHash});
    Q_EMIT rechecksChanged();
}

void MoveController::retryFailed()
{
    if (m_job == nullptr || m_hooks.engine == nullptr || m_hooks.engine->client() == nullptr)
        return;
    // M16: again to the place the job meant for each, one request per target.
    QHash<QString, QStringList> byTarget;
    for (const QString &hash : failedHashes()) {
        const MoveJob::Entry *entry = m_job->entry(hash);
        if (entry != nullptr && !entry->to.isEmpty())
            byTarget[entry->to] << hash;
    }
    for (auto it = byTarget.cbegin(); it != byTarget.cend(); ++it)
        m_hooks.engine->client()->torrentsSetLocation(it.value(), it.key());
}

} // namespace omaseed::app
