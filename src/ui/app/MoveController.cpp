#include "ui/app/MoveController.hpp"

#include "ui/app/MoveWords.hpp"

#include "core/ErrorWords.hpp"
#include "core/Format.hpp"
#include "core/FreeSpace.hpp"
#include "core/QbtClient.hpp"
#include "core/QbtMoveFeed.hpp"
#include "core/ScopeSentence.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <utility>

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
    // A start waiting for file lists or the server's listings goes once they are in.
    connect(this, &MoveController::planChanged, this, &MoveController::tryPending);
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
    m_recheckStarts.clear();
    m_recheckHash.clear();
    m_pending = QJsonObject();
    m_recents =
        !serverId.isEmpty() && m_hooks.loadRecents ? m_hooks.loadRecents(serverId) : QStringList();
    // C18 D8: that server's finished moves, what is past the limits dropped.
    m_history = !serverId.isEmpty() && m_hooks.loadHistory
                    ? core::MoveHistory::fromJson(m_hooks.loadHistory(serverId))
                    : core::MoveHistory();
    if (m_history.prune(now()))
        saveHistory();
    ++m_historyRevision;
    Q_EMIT historyChanged();
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
    // L12: a queued move is checked again when its turn comes.
    m_job->setReplan([this](const QJsonObject &spec) { return replan(spec); });
    connect(m_job.get(), &MoveJob::queuedDropped, this, [this](const QString &title) {
        Q_EMIT note(tr("%1 wasn't moved: nothing in it can move any more").arg(title), {}, true);
        Q_EMIT outcome({{QStringLiteral("action"), QStringLiteral("move")},
                        {QStringLiteral("name"), title},
                        {QStringLiteral("failedName"), title},
                        {QStringLiteral("cause"), tr("nothing in it can move any more")}});
    });
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
        // L6: a quiet move that ended unseen has nothing to show.
        if (!m_job->wasShown())
            closeQuietSummary();
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
    m_held.reset();
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
    // A held summary gives way once the job under it shows.
    if (m_held.has_value() && m_job != nullptr && m_job->wasShown())
        m_held.reset();
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
    // A selection's common folder isn't what moves: only a folder's move marks the rail.
    if (m_job != nullptr && m_job->kind() == QLatin1String("folder")) {
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
    // DECISIONS C24 H5 (board 46): the title bar's sentence for a folder, the scope line's
    // facts (C15 M1) in the board's order; its name is the title. While this folder moves, the
    // move's progress instead.
    const QString normal = SharedData::normalize(path);
    const QVariantMap figures = m_folders->figures(normal);
    if (figures.isEmpty())
        return {};
    if (m_job != nullptr && m_job->kind() == QLatin1String("folder") &&
        SharedData::normalize(m_job->root()) == normal) {
        switch (m_job->state()) {
        case MoveJob::State::Running:
        case MoveJob::State::Pausing:
        case MoveJob::State::Paused:
        case MoveJob::State::Ending:
            return core::ScopeSentence::folderMoving(
                m_job->newPath(), m_job->moved(), m_job->total());
        default:
            break;
        }
    }
    int loose = 0;
    const auto walk = m_loose.constFind(figures.value(QStringLiteral("path")).toString());
    if (walk != m_loose.cend() && !walk->running && walk->result.complete)
        loose = walk->result.loose;
    return core::ScopeSentence::folder(figures.value(QStringLiteral("count")).toInt(),
                                       figures.value(QStringLiteral("bytes")).toLongLong(),
                                       figures.value(QStringLiteral("savePaths")).toInt(),
                                       loose,
                                       figures.value(QStringLiteral("path")).toString());
}

bool MoveController::start(const QString &rootIn, const QString &newPathIn, bool carry)
{
    // A folder's move (C15); its spec lets a queued one be planned again (C16 L12).
    if (m_job == nullptr)
        return false;
    const QString root = SharedData::normalize(rootIn);
    const QString newPath = SharedData::normalize(newPathIn.trimmed());
    MovePlan plan = makePlan(root, newPath, carry);
    // C28 (#176): "replace it": the empty files in the way go first, then the plan again.
    const QStringList replaced = removeReplaced(plan, std::exchange(m_replace, {}));
    if (!replaced.isEmpty())
        plan = makePlan(root, newPath, carry);
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
    rememberDestination(newPath);
    QJsonObject spec{{QStringLiteral("kind"), QStringLiteral("folder")},
                     {QStringLiteral("title"), FolderTree::baseName(root)},
                     {QStringLiteral("root"), root},
                     {QStringLiteral("newPath"), newPath},
                     {QStringLiteral("carry"), carry}};
    if (!replaced.isEmpty())
        spec.insert(QStringLiteral("replaced"), QJsonArray::fromStringList(replaced));
    const bool started = m_job->start(plan, m_serverId, carry, looseBytes, spec);
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

bool MoveController::startSelection(const QStringList &hashes,
                                    const QString &newPath,
                                    const QString &rule)
{
    return startSpec(selectionSpec(hashes, newPath, rule));
}

bool MoveController::startBackOf(const QString &id, const QString &only, bool carry)
{
    QJsonObject spec = backSpecOf(id, only);
    if (!carry)
        spec.insert(QStringLiteral("carry"), false);
    return !spec.isEmpty() && startSpec(spec);
}

bool MoveController::startBack(bool carry)
{
    QJsonObject spec = backSpec();
    if (!carry)
        spec.insert(QStringLiteral("carry"), false);
    return !spec.isEmpty() && startSpec(spec);
}

bool MoveController::startSpec(const QJsonObject &specIn)
{
    const QStringList asked = std::exchange(m_replace, {});
    if (m_job == nullptr)
        return false;
    QJsonObject spec = specIn;
    if (!asked.isEmpty())
        spec.insert(QStringLiteral("replace"), QJsonArray::fromStringList(asked));
    MovePlan plan = planFor(spec);
    // The file lists and the server's listings first (M8, #102): it starts once they are in.
    if (fetchFor(plan)) {
        m_pending = spec;
        return true;
    }
    m_pending = QJsonObject();
    // C28 (#176): "replace it": the empty files in the way go first, then the plan again.
    const QStringList replace = spec.take(QStringLiteral("replace")).toVariant().toStringList();
    const QStringList replaced = removeReplaced(plan, replace);
    if (!replaced.isEmpty()) {
        const QStringList before =
            spec.value(QStringLiteral("replaced")).toVariant().toStringList();
        spec.insert(QStringLiteral("replaced"), QJsonArray::fromStringList(before + replaced));
        plan = planFor(spec);
    }
    if (!plan.canMove()) {
        const QVariantMap words = selectionWords(plan, spec);
        QString why = words.value(QStringLiteral("blocker")).toString();
        if (why.isEmpty())
            why = tr("nothing to move");
        const QString title = spec.value(QStringLiteral("title")).toString();
        const QStringList about = plan.leftOut() + plan.intoItself() + plan.movedSince();
        Q_EMIT note(tr("%1 wasn't moved: %2").arg(title, why), about, true);
        // C18 D1: the strip names it by its title ("games", "3 torrents"), never a list.
        Q_EMIT outcome(
            {{QStringLiteral("action"), QStringLiteral("move")},
             {QStringLiteral("hashes"), about},
             {QStringLiteral("failedHashes"), about},
             {QStringLiteral("name"), title},
             {QStringLiteral("failedName"), title},
             {QStringLiteral("target"), spec.value(QStringLiteral("newPath")).toString()},
             {QStringLiteral("cause"), why}});
        return false;
    }
    // The last job's summary: move back and retry are its own actions and take its place
    // (L11, M16); any other move runs under it, and it stays until closed (#105).
    const QString kind = spec.value(QStringLiteral("kind")).toString();
    const bool fromSummary = kind == QLatin1String("back") || kind == QLatin1String("retry");
    if (fromSummary)
        m_held.reset();
    if (m_job->state() == MoveJob::State::Done || m_job->state() == MoveJob::State::Ended) {
        if (!fromSummary && m_job->wasShown())
            holdSummary();
        else
            forgetSummary();
    }
    if (m_hooks.lowDiskBytes)
        m_job->setLowDiskThreshold(m_hooks.lowDiskBytes());
    m_restoredAt = QDateTime();
    if (!plan.newPath().isEmpty())
        rememberDestination(plan.newPath());
    // C32 D19: a move back carries back the loose files on the list its spec holds (the one
    // the move kept), after the torrents, as a folder's move carries them (M17).
    const bool carry =
        kind == QLatin1String("back") && spec.value(QStringLiteral("carry")).toBool();
    qint64 looseBytes = 0;
    if (carry) {
        for (const core::CarryFile &file :
             core::LooseCarry::fromJson(spec.value(QStringLiteral("carryFiles")).toArray())) {
            if (file.outcome == core::CarryFile::Outcome::ToDo)
                looseBytes += file.size;
        }
    }
    if (m_job->start(plan, m_serverId, carry, looseBytes, spec)) {
        // Not queued: the last job's carry is over and done with.
        QFile::remove(carryFilePath());
        m_carryResult = {};
        m_carryDone = false;
        m_carryPaused = false;
    }
    bump();
    return true;
}

QStringList MoveController::removeReplaced(const MovePlan &plan, const QStringList &paths) const
{
    QStringList gone;
    if (paths.isEmpty() || m_hooks.probe == nullptr)
        return gone;
    // Only what this plan, made now, still finds an empty file no torrent owns.
    const QStringList replaceable = plan.replaceable();
    const core::MoveDisk disk = core::MoveDisk::real();
    for (const QString &asked : paths) {
        const QString path = SharedData::normalize(asked);
        if (!replaceable.contains(path) || gone.contains(path))
            continue;
        const QString local = m_hooks.probe->localPathFor(path);
        if (local.isEmpty() || !disk.removeEmpty(local))
            continue;
        qInfo().noquote()
            << QStringLiteral("move: deleted the empty file no torrent owned at %1").arg(path);
        gone << path;
    }
    return gone;
}

void MoveController::tryPending()
{
    if (m_pending.isEmpty())
        return;
    const QJsonObject spec = m_pending;
    m_pending = QJsonObject();
    startSpec(spec);
}

void MoveController::rememberDestination(const QString &path)
{
    // L4: the last 8 per server, newest first.
    const QString normal = SharedData::normalize(path);
    if (normal.isEmpty() || m_serverId.isEmpty())
        return;
    m_recents.removeAll(normal);
    m_recents.prepend(normal);
    while (m_recents.size() > 8)
        m_recents.removeLast();
    if (m_hooks.saveRecents)
        m_hooks.saveRecents(m_serverId, m_recents);
}

// ---- the history (board 45 D7–D8, C18, #133) ------------------------------------------------

QString MoveController::recordIdOfJob() const
{
    if (m_job == nullptr || !m_job->startedAt().isValid())
        return {};
    for (const core::MoveRecord &record : m_history.records()) {
        if (record.startedAt == m_job->startedAt() && record.title == m_job->title())
            return record.id;
    }
    return {};
}

void MoveController::recordFinished()
{
    const QHash<QString, QString> from = m_job->movedFrom();
    if (m_job->kind() == QLatin1String("back")) {
        // A move back of a record: what went back leaves it. The move back itself isn't
        // recorded: its summary offers moving them again while it shows.
        const QString of = m_job->spec().value(QStringLiteral("historyId")).toString();
        // C32 D19: and the loose files it carried back.
        QStringList filesBack;
        if (m_job->carryLoose() && m_carryDone) {
            for (const core::CarryFile &file : m_carryResult.files) {
                if (file.carried())
                    filesBack << file.relative;
            }
        }
        if (!of.isEmpty() && m_history.backDone(of, from.keys(), filesBack))
            saveHistory();
        return;
    }
    if (from.isEmpty() || !recordIdOfJob().isEmpty())
        return;
    core::MoveRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.kind = m_job->kind();
    record.title = m_job->title();
    record.newPath = m_job->newPath();
    record.startedAt = m_job->startedAt();
    record.finishedAt = now();
    record.from = from;
    record.to = m_job->movedTo();
    for (const MoveJob::Entry &entry : m_job->entries()) {
        if (from.contains(entry.hash))
            record.names.insert(entry.hash, entry.name);
    }
    record.hadAutoTmm = m_job->hadAutoTmm();
    record.replaced = m_job->spec().value(QStringLiteral("replaced")).toVariant().toStringList();
    if (m_carryDone && m_carryResult.carried > 0) {
        record.carried = m_carryResult.carried;
        record.carriedTo = m_job->newPath();
        // C32 D19: the list move back carries back, as each file lies at the new place now.
        record.carriedFrom = m_job->root();
        const QString at =
            m_hooks.probe == nullptr ? QString() : m_hooks.probe->localPathFor(record.carriedTo);
        if (!at.isEmpty())
            record.carriedFiles = core::LooseCarry::asCarried(at, m_carryResult.files);
    }
    m_history.add(std::move(record), now());
    saveHistory();
}

void MoveController::saveHistory()
{
    if (!m_serverId.isEmpty() && m_hooks.saveHistory)
        m_hooks.saveHistory(m_serverId, m_history.toJson());
    ++m_historyRevision;
    Q_EMIT historyChanged();
}

QHash<QString, QString> MoveController::savePaths() const
{
    QHash<QString, QString> out;
    for (const core::Torrent &torrent : library())
        out.insert(torrent.hash, torrent.savePath);
    return out;
}

void MoveController::closeQuietSummary()
{
    // Not from inside the job's own signal: the next queued move may start from here.
    QPointer<MoveController> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (self.isNull() || self->m_job == nullptr || self->m_job->wasShown())
            return;
        if (self->m_job->state() == MoveJob::State::Done ||
            self->m_job->state() == MoveJob::State::Ended)
            self->forgetSummary();
    });
}

bool MoveController::holding() const
{
    return m_held.has_value() && (m_job == nullptr || !m_job->wasShown());
}

void MoveController::holdSummary()
{
    if (m_job == nullptr || !m_job->wasShown() ||
        (m_job->state() != MoveJob::State::Done && m_job->state() != MoveJob::State::Ended))
        return;
    Held held;
    held.band = band();
    held.kind = kind();
    held.title = title();
    held.state = stateName();
    held.root = root();
    held.newPath = newPath();
    held.details = detailsText();
    held.hashes = jobHashes();
    held.failed = failedHashes();
    for (const QString &hash : std::as_const(held.hashes)) {
        if (wasRunning(hash))
            held.running.insert(hash);
    }
    held.carryRows = carryRows();
    for (const QString &hash : std::as_const(held.hashes))
        held.steps.insert(hash, stepFor(hash));
    held.back = backSpec();
    held.retry = retrySpec();
    forgetSummary();
    // A folder's move shows at once, and takes the band.
    if (m_job == nullptr || !m_job->wasShown())
        m_held = std::move(held);
    bump();
}

bool MoveController::noticeWanted() const
{
    // M16: a folder's move always gives one; L8: another only when its band showed and it
    // ran past a minute or the window was hidden at its end.
    if (m_job == nullptr)
        return false;
    if (!m_job->isQuiet())
        return true;
    if (!m_job->wasShown())
        return false;
    const bool hidden = m_hooks.engine != nullptr && !m_hooks.engine->isVisible();
    const qint64 ran = m_job->startedAt().isValid() ? m_job->startedAt().secsTo(now()) : 0;
    return hidden || ran > 60;
}

void MoveController::dropQueued(int index)
{
    if (m_job != nullptr)
        m_job->dropQueued(index);
    bump();
}

bool MoveController::shown() const
{
    if (holding())
        return true;
    return m_job != nullptr && m_job->state() != MoveJob::State::Idle && m_job->wasShown();
}

bool MoveController::active() const
{
    // A quiet move with no band yet is with qBittorrent already: nothing to ask (board 43 §9).
    return m_job != nullptr && m_job->isActive() && m_job->wasShown();
}

QString MoveController::kind() const
{
    if (holding())
        return m_held->kind;
    return m_job == nullptr || m_job->state() == MoveJob::State::Idle ? QString() : m_job->kind();
}

QString MoveController::title() const
{
    if (holding())
        return m_held->title;
    return m_job == nullptr ? QString() : m_job->title();
}

QStringList MoveController::queued() const
{
    return m_job == nullptr ? QStringList() : m_job->queuedTitles();
}

QString MoveController::stateName() const
{
    if (holding())
        return m_held->state;
    return m_job == nullptr ? QStringLiteral("idle") : stateKey(m_job->state());
}

QString MoveController::root() const
{
    if (holding())
        return m_held->root;
    return m_job == nullptr ? QString() : m_job->root();
}

QString MoveController::newPath() const
{
    if (holding())
        return m_held->newPath;
    return m_job == nullptr ? QString() : m_job->newPath();
}

QStringList MoveController::jobHashes() const
{
    if (holding())
        return m_held->hashes;
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
    if (m_held.has_value()) {
        // The held summary goes; the move under it goes on.
        const bool wasHolding = holding();
        m_held.reset();
        bump();
        if (wasHolding)
            return;
    }
    forgetSummary();
}

void MoveController::forgetSummary()
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
        return holding() ? m_held->steps.value(hash) : QVariantMap();
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
    // L10 (B3): how much of the moving one is at its target, when this machine can tell.
    const qint64 live = m_job->liveBytes(hash);
    const int progress = live >= 0 && entry->bytes > 0 ? int(live * 100 / entry->bytes) : -1;
    return {{QStringLiteral("text"), text},
            {QStringLiteral("glyph"), glyph},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("reason"), entry->reason},
            {QStringLiteral("progress"), progress}};
}

QStringList MoveController::failedHashes() const
{
    if (holding())
        return m_held->failed;
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
    if (m_job == nullptr || !m_job->wasShown())
        return {};
    const QString name = m_job->title();
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
    if (holding())
        return m_held->band;
    QVariantMap band;
    // C16 L6: a quiet move shows no band until it has run 3 s.
    if (m_job == nullptr || m_job->state() == MoveJob::State::Idle || !m_job->wasShown())
        return band;
    const MoveJob &job = *m_job;
    const QString name = job.title();
    const bool folder = job.kind() == QLatin1String("folder");
    const bool back = job.kind() == QLatin1String("back");
    // Board 43 B1/B2: one torrent, across drives: its own progress, or none.
    const bool single = !folder && job.total() == 1 && !job.sameDrive();
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
        headline = back ? tr("Moving %1 back").arg(name)
                        : (job.sameDrive() ? tr("Renaming %1 to %2") : tr("Moving %1 to %2"))
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
            const QString current = job.current();
            const qint64 live = job.liveBytes(current);
            if (single && !current.isEmpty() && live < 0) {
                // B2: qBittorrent says nothing between "moving" and "moved".
                const MoveJob::Entry *entry = job.entry(current);
                parts.clear();
                parts << core::Format::size(entry->bytes);
                if (entry->movingSince.isValid())
                    parts << tr("moving for %1").arg(span(entry->movingSince.secsTo(now())));
                parts << (m_hooks.probe != nullptr && m_hooks.probe->seesServerDisks()
                              ? tr("qBittorrent doesn't report a single move's progress")
                              : tr("qBittorrent doesn't report a single move's progress, and "
                                   "omaseed can't see %1's disks")
                                    .arg(server));
                progress = -1;
            } else {
                if (!single)
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
                if (single && live >= 0 && m_hooks.probe != nullptr) {
                    // B1: measured at the destination.
                    const QString where = m_hooks.probe->answer(job.entry(current)->to).where;
                    if (!where.isEmpty())
                        parts << tr("measured at %1").arg(where);
                } else if (!current.isEmpty() && !single) {
                    const MoveJob::Entry *entry = job.entry(current);
                    parts << tr("now %1 (%2)").arg(entry->name, core::Format::size(entry->bytes));
                }
            }
            // J3: what waits behind it.
            const QList<QJsonObject> queued = job.queuedSpecs();
            if (!queued.isEmpty()) {
                const QJsonObject next = queued.constFirst();
                const QString to = next.value(QStringLiteral("newPath")).toString();
                const QString title = job.queuedTitles().constFirst();
                parts << (to.isEmpty() ? tr("next: %1").arg(title)
                                       : tr("next: %1 to %2").arg(title, to));
            }
        }
        line = parts.join(QStringLiteral(" · "));
        // B1: one torrent is already with qBittorrent and can't be taken back (M11).
        if (!single)
            actions << action(QStringLiteral("pause"), tr("pause"));
        actions << action(QStringLiteral("show"), tr("show"))
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
                       ? (folder ? tr("The first torrent, %1, didn't move: “%2”. Nothing else was "
                                      "sent; everything is still at %3.")
                                       .arg(first == nullptr ? QString() : first->name,
                                            first == nullptr ? QString() : first->reason,
                                            job.root())
                                 : tr("The first torrent, %1, didn't move: “%2”. Nothing else was "
                                      "sent; everything is still where it was.")
                                       .arg(first == nullptr ? QString() : first->name,
                                            first == nullptr ? QString() : first->reason))
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
            headline = back ? tr("Carrying the loose files back to %1: paused").arg(carryTarget())
                            : tr("Carrying the loose files to %1: paused").arg(job.newPath());
            parts << tr("nothing is being copied");
            actions << action(QStringLiteral("resume"), tr("resume"));
        } else {
            glyph = QStringLiteral("↻");
            headline = back && carry.total > 0 ? counted(carry.total,
                                                         tr("Carrying %1 loose file back to %2"),
                                                         tr("Carrying %1 loose files back to %2"))
                                                     .arg(carryTarget())
                       : back ? tr("Carrying the loose files back to %1").arg(carryTarget())
                       : carry.total > 0 ? counted(carry.total,
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
        headline =
            back ? tr("%1 moved back").arg(name) : tr("%1 moved to %2").arg(name, job.newPath());
        QStringList parts;
        const qint64 took = summary.startedAt.isValid() && summary.endedAt.isValid()
                                ? summary.startedAt.secsTo(summary.endedAt)
                                : -1;
        const bool carried = job.carryLoose() && m_carryDone &&
                             (m_carryResult.carried > 0 || m_carryResult.stayed > 0);
        if (carried) {
            // L2: the end, with loose files; a move back's are carried back (C32 D19).
            headline = back ? tr("%1 moved back, loose files too").arg(name)
                            : tr("%1 moved, loose files too").arg(name);
            parts << (back ? tr("%1 torrents and %2 back at %3") : tr("%1 torrents and %2 at %3"))
                         .arg(summary.moved)
                         .arg(counted(m_carryResult.carried, tr("%1 file"), tr("%1 files")),
                              carryTarget());
            if (m_carryResult.stayed > 0) {
                int recent = 0;
                int unreadable = 0;
                int different = 0;
                int noRoom = 0;
                int other = 0;
                QStringList changed; // a move back's: changed at the new place since the move
                for (const core::CarryFile &file : m_carryResult.files) {
                    switch (file.outcome) {
                    case core::CarryFile::Outcome::Recent:
                    case core::CarryFile::Outcome::Changed:
                        if (back)
                            changed << file.relative;
                        else
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
                // C32 D19: a file changed since the move stays, and is named.
                if (changed.size() > 0 && changed.size() <= 3)
                    stayed << tr("%1 (changed after the move)")
                                  .arg(changed.join(QStringLiteral(", ")));
                else if (changed.size() > 3)
                    stayed << tr("%1 changed after the move").arg(changed.size());
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
                parts
                    << tr("stayed at %1: %2").arg(carrySource(), stayed.join(QStringLiteral(", ")));
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
        // L11: every job's summary.
        if (summary.moved > 0)
            actions << action(QStringLiteral("moveBack"), tr("move back…"));
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
        line =
            (folder ? tr("%1 moved to %2 · %3 not moved, still at %4 · every torrent is whole on "
                         "one side or the other")
                          .arg(summary.moved)
                          .arg(job.newPath())
                          .arg(summary.notMoved)
                          .arg(job.root())
                    : tr("%1 moved · %2 not moved, still where they were · every torrent is "
                         "whole on one side or the other")
                          .arg(summary.moved)
                          .arg(summary.notMoved));
        failedText.clear();
        actions << action(QStringLiteral("moveRest"), tr("move the rest…"));
        // L11: every job's summary.
        if (summary.moved > 0)
            actions << action(QStringLiteral("moveBack"), tr("move back…"));
        actions << action(QStringLiteral("show"), tr("show"))
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

namespace {

/// #174: what became of a moved torrent later (MoveJob::Entry::later, M15), in the word the
/// table's status cell uses (Torrent::stateText) and what fixes it (docs/28's actions for
/// each), never qBittorrent's own state name.
QString laterText(const QString &later)
{
    if (later == QLatin1String("missingFiles"))
        return MoveController::tr("later: missing files · set location… or recheck it");
    if (later == QLatin1String("error"))
        return MoveController::tr("later: error · its details say why");
    return MoveController::tr("later: %1").arg(later);
}

} // namespace

QString MoveController::detailsText() const
{
    if (holding())
        return m_held->details;
    if (m_job == nullptr || m_job->state() == MoveJob::State::Idle)
        return {};
    const MoveJob &job = *m_job;
    QStringList lines;
    // #174: the band above already says its headline and line; the record starts after them.
    if (job.startedAt().isValid())
        lines << tr("started %1").arg(core::Format::relativeTime(job.startedAt()));
    if (job.kind() == QLatin1String("folder")) {
        lines << tr("from %1 to %2").arg(job.root(), job.newPath());
    } else {
        // A selection's torrents can come from, and go to, several folders (L5, L11).
        QStringList froms;
        QStringList tos;
        for (const MoveJob::Entry &entry : job.entries()) {
            if (!entry.from.isEmpty() && !froms.contains(entry.from))
                froms << entry.from;
            if (!entry.to.isEmpty() && !tos.contains(entry.to))
                tos << entry.to;
        }
        const QString from =
            froms.size() == 1 ? froms.constFirst() : tr("%1 places").arg(froms.size());
        const QString to = tos.size() == 1 ? tos.constFirst()
                           : job.newPath().isEmpty()
                               ? tr("%1 places").arg(tos.size())
                               : tr("%1 places below %2").arg(tos.size()).arg(job.newPath());
        lines << tr("from %1 to %2").arg(from, to);
    }
    lines << QString();
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
            lines << QStringLiteral("    ") + laterText(entry.later);
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap MoveController::quietOutcome(const MoveJob::Summary &summary,
                                         const QString &cause,
                                         const QString &details) const
{
    // C18 D1, D3: the torrents it sent, the ones that didn't land, and the first of those
    // by name for the line. Left-out ones were never sent: the plan said so before.
    QStringList hashes;
    QString name;
    QString failedName;
    for (const MoveJob::Entry &entry : m_job->entries()) {
        const bool failed = summary.failedHashes.contains(entry.hash);
        if (entry.step != MoveJob::Step::Moved && !failed)
            continue;
        hashes << entry.hash;
        name = entry.name;
    }
    if (!summary.failedHashes.isEmpty()) {
        if (const MoveJob::Entry *first = m_job->entry(summary.failedHashes.constFirst()))
            failedName = first->name;
    }
    return {{QStringLiteral("action"),
             m_job->kind() == QLatin1String("back") ? QStringLiteral("moveBack")
                                                    : QStringLiteral("move")},
            {QStringLiteral("hashes"), hashes},
            {QStringLiteral("failedHashes"), summary.failedHashes},
            {QStringLiteral("name"), hashes.size() == 1 ? name : QString()},
            {QStringLiteral("failedName"), failedName},
            {QStringLiteral("target"), m_job->newPath()},
            {QStringLiteral("cause"), cause},
            {QStringLiteral("details"), details}};
}

void MoveController::onFinished()
{
    bump();
    if (m_job == nullptr)
        return;
    recordFinished();
    const MoveJob::Summary summary = m_job->summary();
    if (!m_job->wasShown()) {
        // C16 L7: a quiet move is told in the footer's note, and leaves no summary.
        if (summary.failed == 0) {
            const QString where = m_job->kind() == QLatin1String("back")
                                      ? tr("moved back")
                                      : tr("moved to %1").arg(m_job->newPath());
            // The one that moved by its name: the title counts the selection, left-out
            // ones too.
            QString one = summary.title;
            for (const MoveJob::Entry &entry : m_job->entries()) {
                if (entry.step == MoveJob::Step::Moved)
                    one = entry.name;
            }
            Q_EMIT note(summary.moved == 1 ? tr("%1 %2").arg(one, where)
                                           : tr("%1 %2").arg(summary.moved).arg(where),
                        m_job->hashes(),
                        false);
            Q_EMIT outcome(quietOutcome(summary, QString(), QString()));
        } else {
            const MoveJob::Entry *first = m_job->entry(summary.failedHashes.constFirst());
            QString why = first == nullptr ? QString() : first->reason;
            // M19: what qBittorrent refused is the folder, never the sign-in.
            if (first != nullptr && first->causeKey == QLatin1String("cantCreate"))
                why = tr("qBittorrent can't create %1").arg(first->to);
            else if (first != nullptr && (first->causeKey == QLatin1String("noAccess") ||
                                          first->causeKey == QLatin1String("readOnly")))
                why = tr("qBittorrent can't write to %1").arg(first->to);
            Q_EMIT note(summary.total == 1 && first != nullptr
                            ? tr("%1 didn't move: %2").arg(first->name, why)
                            : tr("%1 of %2 didn't move: %3")
                                  .arg(summary.failed)
                                  .arg(summary.total)
                                  .arg(why),
                        summary.failedHashes,
                        true);
            Q_EMIT outcome(quietOutcome(summary, why, first == nullptr ? QString() : first->line));
        }
        closeQuietSummary();
        return;
    }
    if (!m_job->queuedTitles().isEmpty()) {
        // L12 (Q4): what waits behind it goes now, under its summary. Not from inside the
        // job's own signal.
        QPointer<MoveController> self(this);
        QTimer::singleShot(0, this, [self]() {
            if (!self.isNull())
                self->holdSummary();
        });
    }
    if (!m_hooks.notify || !noticeWanted())
        return;
    // E2: one notice, after the loose files.
    const QString name = summary.title;
    QStringList parts;
    parts << tr("%1 moved").arg(summary.moved);
    if (summary.failed > 0)
        parts << tr("%1 failed").arg(summary.failed);
    if (summary.endedEarly)
        parts << tr("%1 not moved").arg(summary.notMoved);
    if (m_carryDone && m_carryResult.carried > 0)
        parts << (summary.kind == QLatin1String("back") ? counted(m_carryResult.carried,
                                                                  tr("%1 loose file carried back"),
                                                                  tr("%1 loose files carried back"))
                                                        : counted(m_carryResult.carried,
                                                                  tr("%1 loose file carried"),
                                                                  tr("%1 loose files carried")));
    if (m_carryDone && m_carryResult.stayed > 0)
        parts << counted(m_carryResult.stayed, tr("%1 file stayed"), tr("%1 files stayed"));
    m_hooks.notify(summary.endedEarly ? tr("The move of %1 ended early").arg(name)
                   : summary.kind == QLatin1String("back") ? tr("%1 moved back").arg(name)
                                                           : tr("%1 moved").arg(name),
                   parts.join(QStringLiteral(", ")));
}

void MoveController::onPausedItself(MoveJob::PauseReason reason)
{
    Q_UNUSED(reason)
    bump();
    // E2: a job that paused itself gives one notice too, since nothing moves until someone
    // looks (for a selection, L8's rule).
    if (!m_hooks.notify || !noticeWanted())
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
    bool recorded = loadCarry(root, &from, &to, &files);
    if (!recorded && m_job->kind() == QLatin1String("back")) {
        // C32 D19: a move back carries the list its spec holds, the other way; it never walks
        // a folder, so nothing the move didn't carry goes.
        const QJsonObject spec = m_job->spec();
        if (m_hooks.probe != nullptr && m_hooks.probe->seesServerDisks()) {
            from = m_hooks.probe->localPathFor(spec.value(QStringLiteral("carryFrom")).toString());
            to = m_hooks.probe->localPathFor(spec.value(QStringLiteral("carryTo")).toString());
        }
        files = core::LooseCarry::fromJson(spec.value(QStringLiteral("carryFiles")).toArray());
        recorded = true;
    } else if (!recorded && m_hooks.probe != nullptr && m_hooks.probe->seesServerDisks()) {
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

QString MoveController::carrySource() const
{
    if (m_job == nullptr)
        return {};
    return m_job->kind() == QLatin1String("back")
               ? m_job->spec().value(QStringLiteral("carryFrom")).toString()
               : m_job->root();
}

QString MoveController::carryTarget() const
{
    if (m_job == nullptr)
        return {};
    return m_job->kind() == QLatin1String("back")
               ? m_job->spec().value(QStringLiteral("carryTo")).toString()
               : m_job->newPath();
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
    if (holding())
        return m_held->carryRows;
    QVariantList rows;
    if (m_job == nullptr)
        return rows;
    const QString name = FolderTree::baseName(carrySource());
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

bool MoveController::wasRunning(const QString &hash) const
{
    if (holding())
        return m_held->running.contains(hash);
    if (m_job == nullptr)
        return false;
    const MoveJob::Entry *entry = m_job->entry(hash);
    return entry != nullptr && entry->wasRunning;
}

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
    // DECISIONS C20 (#138): only one running when the job sent it starts again.
    for (const core::Torrent &torrent : list) {
        // C21 (#144): a failed move may have split a hybrid's files between the two places,
        // and libtorrent 2.1.2 answers its recheck with an error only adding it again clears.
        const bool hybrid = torrent.isHybrid();
        const bool starts = !hybrid && wasRunning(torrent.hash);
        QString then = tr("stays stopped");
        if (hybrid)
            then = tr("not rechecked");
        else if (starts)
            then = tr("starts if complete");
        rows << QVariantMap{
            {QStringLiteral("hash"), torrent.hash},
            {QStringLiteral("name"), torrent.name},
            {QStringLiteral("bytes"), core::Format::size(torrent.completed)},
            {QStringLiteral("starts"), starts},
            {QStringLiteral("hybrid"), hybrid},
            {QStringLiteral("then"), then},
            {QStringLiteral("why"),
             hybrid ? tr("Its files may be split between the two places, and on a hybrid torrent "
                         "(v1 and v2) a recheck would put it in an error that only adding it "
                         "again clears.")
                    : QString()}};
    }
    return rows;
}

void MoveController::recheckSmallestFirst(const QStringList &hashes)
{
    recheckListed(recheckRows(hashes));
}

void MoveController::recheckListed(const QVariantList &rows)
{
    m_rechecks.clear();
    // Decided now: the job or its summary may be gone by the time a recheck ends.
    m_recheckStarts.clear();
    const core::TorrentModel *torrents =
        m_hooks.engine == nullptr ? nullptr : m_hooks.engine->torrents();
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString hash = row.value(QStringLiteral("hash")).toString();
        // C21: never a hybrid, whatever the row says.
        if (hash.isEmpty() || row.value(QStringLiteral("hybrid")).toBool() ||
            (torrents != nullptr && torrents->indexOfHash(hash) >= 0 &&
             torrents->torrent(hash).isHybrid()))
            continue;
        m_rechecks << hash;
        if (row.value(QStringLiteral("starts")).toBool())
            m_recheckStarts.insert(hash);
    }
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
            if (torrent.progress >= 1.0 && m_recheckStarts.contains(m_recheckHash))
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
    const QJsonObject spec = holding() ? m_held->retry : retrySpec();
    if (!spec.isEmpty())
        startSpec(spec);
}

bool MoveController::moveAgain(const QString &hash)
{
    const QJsonObject spec = failedSpec(hash, false);
    return !spec.isEmpty() && startSpec(spec);
}

bool MoveController::moveFailedBack(const QString &hash)
{
    const QJsonObject spec = failedSpec(hash, true);
    return !spec.isEmpty() && startSpec(spec);
}

QJsonObject MoveController::failedSpec(const QString &hash, bool back) const
{
    // C21 (#144): one failed torrent, read from the retry the summary offers (its targets are
    // where the job meant each to go, its expectedAt where each was): again to that target,
    // as retry, or back to where it was, as move back.
    const QJsonObject retry = holding() ? m_held->retry : retrySpec();
    const QString to = retry.value(QStringLiteral("targets")).toObject().value(hash).toString();
    const QString from =
        retry.value(QStringLiteral("expectedAt")).toObject().value(hash).toString();
    if (to.isEmpty() || from.isEmpty())
        return {};
    QString title = tr("1 torrent");
    if (m_hooks.engine != nullptr && m_hooks.engine->torrents()->indexOfHash(hash) >= 0)
        title = m_hooks.engine->torrents()->torrent(hash).name;
    if (!back)
        return {{QStringLiteral("kind"), QStringLiteral("retry")},
                {QStringLiteral("title"), title},
                {QStringLiteral("hashes"), QJsonArray{hash}},
                {QStringLiteral("rule"), QStringLiteral("eachOwn")},
                {QStringLiteral("targets"), QJsonObject{{hash, to}}},
                {QStringLiteral("expectedAt"), QJsonObject{{hash, from}}}};
    // Back from wherever qBittorrent has it now: no expectedAt, so it isn't left out as moved
    // since.
    return {{QStringLiteral("kind"), QStringLiteral("back")},
            {QStringLiteral("title"), title},
            {QStringLiteral("hashes"), QJsonArray{hash}},
            {QStringLiteral("rule"), QStringLiteral("eachOwn")},
            {QStringLiteral("targets"), QJsonObject{{hash, from}}},
            {QStringLiteral("backOf"), this->title()},
            {QStringLiteral("backFrom"), newPath()}};
}

QJsonObject MoveController::retrySpec() const
{
    if (m_job == nullptr)
        return {};
    // M16: again to the place the job meant for each; C16 L9: as a job of its own, so its log
    // is read for its whole life.
    QStringList hashes;
    QJsonObject targets;
    QJsonObject expected;
    for (const QString &hash : failedHashes()) {
        const MoveJob::Entry *entry = m_job->entry(hash);
        if (entry == nullptr || entry->to.isEmpty())
            continue;
        hashes << hash;
        targets.insert(hash, entry->to);
        expected.insert(hash, entry->from);
    }
    if (hashes.isEmpty())
        return {};
    return {
        {QStringLiteral("kind"), QStringLiteral("retry")},
        {QStringLiteral("title"), counted(int(hashes.size()), tr("%1 torrent"), tr("%1 torrents"))},
        {QStringLiteral("hashes"), QJsonArray::fromStringList(hashes)},
        {QStringLiteral("rule"), QStringLiteral("eachOwn")},
        {QStringLiteral("targets"), targets},
        {QStringLiteral("expectedAt"), expected}};
}

} // namespace omaseed::app
