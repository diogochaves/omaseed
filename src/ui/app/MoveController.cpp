#include "ui/app/MoveController.hpp"

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

namespace {

/// The loose files' walk is read again when it is older than this (S1 is read again on every
/// change of the field, and the files beside a folder don't change that fast).
constexpr qint64 kWalkFreshSeconds = 60;
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

/// "about 50 min left", "about 2 h 10 min left", "" when it can't tell.
QString timeLeft(qint64 seconds)
{
    if (seconds < 0)
        return {};
    const qint64 minutes = std::max<qint64>(1, (seconds + 59) / 60);
    if (minutes < 90)
        return MoveController::tr("about %1 min left").arg(minutes);
    return MoveController::tr("about %1 h %2 min left").arg(minutes / 60).arg(minutes % 60);
}

/// "1 h 52 min", "14 min", "40 s".
QString span(qint64 seconds)
{
    if (seconds < 60)
        return MoveController::tr("%1 s").arg(std::max<qint64>(0, seconds));
    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return MoveController::tr("%1 min").arg(minutes);
    return MoveController::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

/// `one` or `many` with `n` for its %1 (the codebase's plural idiom: English, no .qm).
QString counted(int n, const QString &one, const QString &many)
{
    return (n == 1 ? one : many).arg(n);
}

QVariantMap action(const QString &id, const QString &text)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("text"), text}};
}

QVariantMap check(const QString &kind,
                  const QString &text,
                  const QString &note = QString(),
                  const QStringList &hashes = {})
{
    // Each line is green (fine), yellow (fine, but know it), red (something is left out) or
    // dim (a fact): S1.
    static const QHash<QString, QString> glyphs{{QStringLiteral("ok"), QStringLiteral("✓")},
                                                {QStringLiteral("warn"), QStringLiteral("!")},
                                                {QStringLiteral("bad"), QStringLiteral("✕")},
                                                {QStringLiteral("unknown"), QStringLiteral("?")},
                                                {QStringLiteral("fact"), QStringLiteral("·")},
                                                {QStringLiteral("busy"), QStringLiteral("…")},
                                                {QStringLiteral("loose"), QStringLiteral("▱")}};
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("glyph"), glyphs.value(kind)},
            {QStringLiteral("text"), text},
            {QStringLiteral("note"), note},
            {QStringLiteral("hashes"), hashes}};
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

// ---- the sheet (S1–S3) ----------------------------------------------------------------------

QStringList MoveController::ownedLocalPaths(const QString &root) const
{
    // M9: a file is a torrent's when it lies under the torrent's content path, or is one of
    // the files of a torrent added without a subfolder. Every torrent's content path counts
    // (a torrent saved elsewhere may still have its folder in this one).
    QStringList owned;
    if (m_hooks.probe == nullptr)
        return owned;
    const QString normalRoot = SharedData::normalize(root);
    for (const core::Torrent &torrent : library()) {
        const QString save = SharedData::normalize(torrent.savePath);
        const QString content = SharedData::normalize(torrent.contentPath);
        if (!content.isEmpty() && content != save) {
            const QString local = m_hooks.probe->localPathFor(content);
            if (!local.isEmpty())
                owned << local;
            continue;
        }
        if (!SharedData::isUnder(save, normalRoot) && !SharedData::isUnder(normalRoot, save))
            continue;
        for (const QString &file : m_files.value(torrent.hash)) {
            const QString local = m_hooks.probe->localPathFor(FolderTree::join(save, file));
            if (!local.isEmpty())
                owned << local;
        }
    }
    return owned;
}

void MoveController::fetchFiles(const QStringList &hashes)
{
    if (m_hooks.engine == nullptr || m_hooks.engine->client() == nullptr)
        return;
    for (const QString &hash : hashes) {
        if (m_files.contains(hash) || m_filesPending.contains(hash))
            continue;
        m_filesPending.insert(hash);
        const QString server = m_serverId;
        QPointer<MoveController> self(this);
        m_hooks.engine->client()->torrentsFiles(hash)->then(
            this, [self, hash, server](const core::QbtResult<QJsonArray> &result) {
                if (self.isNull() || self->m_serverId != server)
                    return;
                self->m_filesPending.remove(hash);
                QStringList names;
                if (!result.isError()) {
                    for (const QJsonValue &file : result.value())
                        names << file.toObject().value(QStringLiteral("name")).toString();
                }
                // An unreadable list is an empty one: the walk then fails closed on it (the
                // torrent's files read "unknown" at worst, never loose: LooseFiles).
                self->m_files.insert(hash, names);
                ++self->m_planRevision;
                Q_EMIT self->planChanged();
            });
    }
}

QStringList MoveController::ownedServerPaths(const QString &root) const
{
    // M9, as ownedLocalPaths() but as the server writes them (C16 L13).
    QStringList owned;
    const QString normalRoot = SharedData::normalize(root);
    for (const core::Torrent &torrent : library()) {
        const QString save = SharedData::normalize(torrent.savePath);
        const QString content = SharedData::normalize(torrent.contentPath);
        if (!content.isEmpty() && content != save) {
            owned << content;
            continue;
        }
        if (!SharedData::isUnder(save, normalRoot) && !SharedData::isUnder(normalRoot, save))
            continue;
        for (const QString &file : m_files.value(torrent.hash))
            owned << FolderTree::join(save, file);
    }
    return owned;
}

QStringList MoveController::foldersToList(const QString &root) const
{
    // The folder, every save path below it, and the folders between.
    const QString normalRoot = SharedData::normalize(root);
    QSet<QString> folders{normalRoot};
    for (const core::Torrent &torrent : library()) {
        QString folder = SharedData::normalize(torrent.savePath);
        while (!folder.isEmpty() && folder != normalRoot &&
               SharedData::isUnder(folder, normalRoot) && !folders.contains(folder)) {
            folders.insert(folder);
            folder = FolderTree::parentOf(folder);
        }
    }
    QStringList out(folders.cbegin(), folders.cend());
    out.sort();
    return out;
}

core::LooseFiles::OnServer MoveController::looseOnServer(const QString &root) const
{
    return core::LooseFiles::onServer(
        root, foldersToList(root), m_serverFolders, ownedServerPaths(root));
}

void MoveController::fetchListings(const QStringList &folders)
{
    if (!m_serverLists || m_hooks.engine == nullptr || m_hooks.engine->client() == nullptr)
        return;
    for (const QString &folder : folders) {
        if (folder.isEmpty() || m_listingsPending.contains(folder))
            continue;
        m_listingsPending.insert(folder);
        const QString server = m_serverId;
        QPointer<MoveController> self(this);
        m_hooks.engine->client()->appDirectoryContent(folder)->then(
            this, [self, folder, server](const core::QbtResult<QJsonArray> &result) {
                if (self.isNull() || self->m_serverId != server)
                    return;
                self->m_listingsPending.remove(folder);
                const core::ServerFolder listed = core::ServerFolder::fromReply(result);
                if (listed.state == core::ServerFolder::State::Unsupported)
                    self->m_serverLists = false;
                // A listing read again that fails keeps the one it had.
                if (listed.state != core::ServerFolder::State::Failed ||
                    !self->m_serverFolders.contains(folder))
                    self->m_serverFolders.insert(folder, listed);
                self->m_serverFoldersAt.insert(folder, self->now());
                ++self->m_planRevision;
                Q_EMIT self->planChanged();
            });
    }
}

void MoveController::scanLoose(const QString &root)
{
    if (m_hooks.probe == nullptr || !m_hooks.probe->seesServerDisks())
        return;
    const QString local = m_hooks.probe->localPathFor(root);
    if (local.isEmpty())
        return;
    LooseWalk &walk = m_loose[root];
    if (walk.running)
        return;
    if (walk.at.isValid() && walk.at.secsTo(now()) < kWalkFreshSeconds && walk.local == local)
        return;
    walk.root = root;
    walk.local = local;
    walk.running = true;
    const QStringList owned = ownedLocalPaths(root);
    const QDateTime at = now();
    const int serial = ++m_walkSerial;
    QPointer<MoveController> self(this);
    QThreadPool::globalInstance()->start([self, root, local, owned, at, serial]() {
        core::LooseFiles::Result result = core::LooseFiles::scan(local, owned, at);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, root, result = std::move(result), at, serial]() {
                if (self.isNull())
                    return;
                auto found = self->m_loose.find(root);
                if (found == self->m_loose.end() || serial < self->m_walkSerial - 64)
                    return;
                found->running = false;
                found->at = at;
                found->result = result;
                ++self->m_planRevision;
                Q_EMIT self->planChanged();
                ++self->m_revision;
                Q_EMIT self->changed();
            },
            Qt::QueuedConnection);
    });
}

MovePlan MoveController::makePlan(const QString &root, const QString &newPath, bool carry) const
{
    MovePlan::Input input;
    input.torrents = library();
    input.root = root;
    input.newPath = newPath;
    input.files = m_files;
    input.probe = m_hooks.probe;
    input.serverFolders = m_serverFolders;
    // With no client to ask (a demo, a test), the server can't list either.
    input.serverLists =
        m_serverLists && m_hooks.engine != nullptr && m_hooks.engine->client() != nullptr;
    if (m_hooks.lowDiskBytes)
        input.lowDiskThreshold = m_hooks.lowDiskBytes();
    if (carry) {
        const auto walk = m_loose.constFind(SharedData::normalize(root));
        if (walk != m_loose.cend() && !walk->running)
            input.looseBytes = walk->result.looseBytes;
    }
    return MovePlan::make(input);
}

QVariantMap MoveController::plan(const QString &rootIn, const QString &newPathIn, bool carry)
{
    const QString root = SharedData::normalize(rootIn);
    const QString newPath =
        newPathIn.trimmed().isEmpty() ? QString() : SharedData::normalize(newPathIn.trimmed());
    const QString name = FolderTree::baseName(root);
    const QString server = serverName();

    // The file lists first: the plan and the walk both need those of torrents added without a
    // subfolder; nothing is called loose before they are in (fail closed, M9).
    MovePlan plan = makePlan(root, newPath, carry);
    QStringList need = plan.needFiles();
    for (const core::Torrent &torrent : library()) {
        const QString save = SharedData::normalize(torrent.savePath);
        if (SharedData::normalize(torrent.contentPath) == save && SharedData::isUnder(save, root) &&
            !m_files.contains(torrent.hash))
            need << torrent.hash;
    }
    need.removeDuplicates();
    fetchFiles(need);
    const bool filesIn = std::none_of(need.cbegin(), need.cend(), [this](const QString &hash) {
        return !m_files.contains(hash);
    });
    const bool seesDisks = m_hooks.probe != nullptr && m_hooks.probe->seesServerDisks() &&
                           !m_hooks.probe->localPathFor(root).isEmpty();
    if (filesIn && seesDisks)
        scanLoose(root);
    // C16 L13 (#102): without a view of the disks, the server lists the folders M8's check and
    // the loose files' names need; a listing a minute old is read again behind the old one.
    core::LooseFiles::OnServer onServer;
    const bool serverLists =
        m_serverLists && m_hooks.engine != nullptr && m_hooks.engine->client() != nullptr;
    if (!seesDisks && serverLists) {
        onServer = looseOnServer(root);
        QStringList listings = plan.needListings() + onServer.needListings;
        for (auto at = m_serverFoldersAt.cbegin(); at != m_serverFoldersAt.cend(); ++at) {
            if (at.value().secsTo(now()) >= kWalkFreshSeconds)
                listings << at.key();
        }
        listings.removeDuplicates();
        fetchListings(listings);
    }

    QVariantMap out;
    const QVariantMap figures = m_folders->figures(root);
    const int count = figures.value(QStringLiteral("count")).toInt();
    out.insert(QStringLiteral("title"), tr("Move %1").arg(name));
    {
        // A plan that can't move (the field is the folder itself) counts nothing: the folder's
        // own figure then.
        const qint64 onDisk = plan.torrentBytes() > 0
                                  ? plan.torrentBytes()
                                  : figures.value(QStringLiteral("bytes")).toLongLong();
        QString header = counted(count, tr("%1 torrent"), tr("%1 torrents")) +
                         QStringLiteral(" · ") + tr("%1 on disk").arg(core::Format::size(onDisk));
        if (plan.partial() > 0)
            header += QStringLiteral(" ") +
                      tr("(%1 in the torrents: %2)")
                          .arg(core::Format::size(plan.sizeOfTorrents()),
                               counted(plan.partial(), tr("%1 is partial"), tr("%1 are partial")));
        if (!server.isEmpty())
            header += QStringLiteral(" · ") + tr("on %1").arg(server);
        out.insert(QStringLiteral("header"), header);
    }
    if (!newPath.isEmpty()) {
        const QString parent = FolderTree::parentOf(newPath);
        out.insert(
            QStringLiteral("explain"),
            FolderTree::baseName(newPath) == name
                ? tr("%1 goes inside %2. Each torrent keeps its place below %1.").arg(name, parent)
                : tr("%1 becomes %2. Each torrent keeps its place below it.").arg(name, newPath));
    }

    QVariantList rows;
    for (const MovePlan::PathRow &row : plan.pathRows())
        rows << QVariantMap{{QStringLiteral("from"), row.from},
                            {QStringLiteral("to"), row.to},
                            {QStringLiteral("count"), row.count}};
    out.insert(QStringLiteral("pathRows"), rows);

    QString blocker;
    switch (plan.blocker()) {
    case MovePlan::Blocker::None:
        break;
    case MovePlan::Blocker::NoPath:
        blocker = tr("Type the folder's new path.");
        break;
    case MovePlan::Blocker::SameFolder:
        blocker = tr("%1 is already there.").arg(name);
        break;
    case MovePlan::Blocker::InsideTheFolder:
        blocker = tr("That is inside %1.").arg(name);
        break;
    case MovePlan::Blocker::NothingToMove:
        blocker = tr("Nothing to move: every torrent is already there or left out.");
        break;
    case MovePlan::Blocker::NoRoom:
        blocker = tr("Not enough room on %1.").arg(plan.drive().isEmpty() ? newPath : plan.drive());
        break;
    case MovePlan::Blocker::AllThere:
        blocker = tr("All %1 are already at %2.").arg(plan.alreadyThere().size()).arg(newPath);
        break;
    }
    out.insert(QStringLiteral("blocker"), blocker);

    QVariantList checks;
    const bool pathOk = plan.blocker() != MovePlan::Blocker::NoPath &&
                        plan.blocker() != MovePlan::Blocker::SameFolder &&
                        plan.blocker() != MovePlan::Blocker::InsideTheFolder;
    if (pathOk) {
        const QString drive = plan.drive().isEmpty() ? FolderTree::parentOf(newPath) : plan.drive();
        // C16 L13: said once, so a remote move never reads as checked as a local one.
        if (!plan.seesDisks())
            checks << check(
                QStringLiteral("fact"),
                tr("omaseed can't see %1's disks. Checked on %1, through qBittorrent: files "
                   "already where the torrents go; the first torrent, sent alone, answers for "
                   "writing. Not checked here: room on that drive, writing, the age and size of "
                   "files no torrent owns.")
                    .arg(server),
                tr("a path map in Settings › Servers lets omaseed see them"));
        // M5: room.
        if (plan.sameDrive()) {
            checks << check(
                QStringLiteral("ok"),
                tr("Room. The same drive: each torrent is renamed, nothing is copied."));
        } else {
            const QString what = plan.bytesToMove() > plan.torrentBytes()
                                     ? tr("%1 to move (torrents and loose files)")
                                     : tr("%1 to move");
            const QString toMove = what.arg(core::Format::size(plan.bytesToMove()));
            switch (plan.room()) {
            case MovePlan::Check::Ok:
            case MovePlan::Check::Warn:
                checks << check(
                    plan.room() == MovePlan::Check::Ok ? QStringLiteral("ok")
                                                       : QStringLiteral("warn"),
                    tr("Room. %1, %2 free on %3. %4 left after.")
                        .arg(toMove,
                             core::Format::size(plan.bytesFree()),
                             drive,
                             core::Format::size(std::max<qint64>(0, plan.bytesFreeAfter()))),
                    plan.freeReadHere()
                        ? tr("read on this machine, which sees %1's disks").arg(server)
                        : tr("qBittorrent's own figure for its default drive"));
                break;
            case MovePlan::Check::Block:
                checks << check(QStringLiteral("bad"),
                                tr("Not enough room. %1, %2 free on %3, and %4 is kept free.")
                                    .arg(toMove,
                                         core::Format::size(std::max<qint64>(0, plan.bytesFree())),
                                         drive,
                                         core::Format::size(
                                             m_hooks.lowDiskBytes ? m_hooks.lowDiskBytes() : 0)));
                break;
            case MovePlan::Check::Unknown:
                checks << check(QStringLiteral("unknown"),
                                tr("Room: can't tell. %1. omaseed can't see %2's disks there.")
                                    .arg(toMove, server),
                                tr("a torrent that doesn't fit fails; the job pauses at the first "
                                   "\"no space\" from qBittorrent"));
                break;
            }
        }
        // M6: writable.
        switch (plan.writable()) {
        case MovePlan::Check::Ok:
            checks << check(
                QStringLiteral("ok"),
                tr("Writable. %1 is writable by the owner of these files.").arg(drive),
                tr("checked here; qBittorrent's own answer comes from the first torrent, "
                   "which goes alone"));
            break;
        case MovePlan::Check::Unknown:
            checks << check(
                QStringLiteral("unknown"),
                tr("Writable: can't tell here. The first torrent goes alone and answers it."));
            break;
        default:
            checks << check(
                QStringLiteral("warn"),
                tr("Not writable here: the owner of these files can't write to %1.").arg(drive),
                tr("the check can be wrong for a container's user; the first torrent "
                   "goes alone and answers it"));
            break;
        }
        // Shared data, pulled in, spans.
        int sharing = 0;
        QStringList sharers;
        for (const QStringList &group : plan.sharedInside()) {
            sharing += int(group.size());
            sharers << group;
        }
        if (sharing > 0 || !plan.pulledIn().isEmpty()) {
            QStringList parts;
            if (sharing > 0)
                parts << counted(
                    sharing,
                    tr("%1 torrent shares files with others in the folder; they move together."),
                    tr("%1 torrents share files with others in the folder; they move together."));
            if (!plan.pulledIn().isEmpty())
                parts << counted(
                             int(plan.pulledIn().size()),
                             tr("%1 saved outside has all its files inside %2: it moves too."),
                             tr("%1 saved outside have all their files inside %2: they move too."))
                             .arg(name);
            checks << check(QStringLiteral("ok"),
                            tr("Shared data.") + QLatin1Char(' ') + parts.join(QLatin1Char(' ')),
                            QString(),
                            sharers + plan.pulledIn());
        }
        if (!plan.spans().isEmpty())
            checks << check(
                QStringLiteral("warn"),
                counted(int(plan.spans().size()),
                        tr("%1 spans %2: its files inside would be left without it. It stays."),
                        tr("%1 span %2: their files inside would be left without them. They stay."))
                    .arg(name),
                QString(),
                plan.spans());
        // M7: automatic management.
        if (!plan.autoManaged().isEmpty()) {
            const QStringList categories = plan.autoManagedCategories();
            checks << check(
                QStringLiteral("warn"),
                counted(
                    int(plan.autoManaged().size()),
                    tr("Automatic management. %1 torrent uses it%2. Moving turns it off for it, as "
                       "qBittorrent does for any set location, and the category still points at "
                       "the old folder."),
                    tr("Automatic management. %1 torrents use it%2. Moving turns it off for them, "
                       "as "
                       "qBittorrent does for any set location, and the category still points at "
                       "the old folder."))
                    .arg(categories.isEmpty()
                             ? QString()
                             : QStringLiteral(" (") +
                                   tr("category %1").arg(categories.join(QStringLiteral(", "))) +
                                   QLatin1Char(')')),
                QString(),
                plan.autoManaged());
        }
        // M8: left out.
        if (!plan.leftOut().isEmpty()) {
            const core::ErrorWords::Text words =
                core::ErrorWords::forLeftOut(int(plan.leftOut().size()), newPath);
            checks << check(QStringLiteral("bad"),
                            words.headline + QStringLiteral(": ") + words.line,
                            QString(),
                            plan.leftOut());
        }
        switch (plan.namesCheck()) {
        case MovePlan::NamesCheck::Here:
            break;
        case MovePlan::NamesCheck::Server:
            if (plan.leftOut().isEmpty())
                checks << check(
                    QStringLiteral("ok"),
                    tr("Nothing already there. %1 listed where the torrents go.").arg(server));
            break;
        case MovePlan::NamesCheck::Pending:
            checks << check(QStringLiteral("busy"),
                            tr("Asking %1 whether files with these torrents' names are already "
                               "where they go…")
                                .arg(server));
            break;
        case MovePlan::NamesCheck::Cannot:
            checks << check(
                QStringLiteral("unknown"),
                m_serverLists
                    ? tr("Files already there: can't tell. %1 didn't answer when asked to list "
                         "where the torrents go.")
                          .arg(server)
                    : tr("Files already there: can't tell. %1's qBittorrent can't list its "
                         "folders to omaseed.")
                          .arg(server),
                tr("qBittorrent keeps a file already at a torrent's place and reports the move "
                   "done; the torrent then has the wrong data. Look there first."));
            break;
        }
        if (!plan.alreadyThere().isEmpty())
            checks << check(QStringLiteral("fact"),
                            tr("%1 already there, at %2. Skipped.")
                                .arg(int(plan.alreadyThere().size()))
                                .arg(newPath),
                            QString(),
                            plan.alreadyThere());
        // M6, M12: the order.
        const QStringList order = plan.toMove();
        if (!order.isEmpty()) {
            const MovePlan::Item *first = plan.item(order.first());
            const QString firstText =
                first == nullptr
                    ? QString()
                    : QStringLiteral("%1 (%2)").arg(first->name, core::Format::size(first->bytes));
            checks << check(
                QStringLiteral("fact"),
                order.size() == 1
                    ? tr("Order: %1 goes alone.").arg(firstText)
                    : tr("Order: smallest first. %1 goes alone; the rest follow, 3 at a "
                         "time in qBittorrent's queue.")
                          .arg(firstText));
        }
    }
    out.insert(QStringLiteral("checks"), checks);

    // The files no torrent owns: a dry run (M9).
    QVariantMap loose;
    QVariantList looseLines;
    bool carryOffered = false;
    if (!seesDisks && serverLists && filesIn && onServer.needListings.isEmpty() &&
        onServer.complete) {
        // C16 L13: their names, as the server lists them; nothing is carried.
        loose.insert(QStringLiteral("state"), QStringLiteral("onServer"));
        loose.insert(QStringLiteral("count"), int(onServer.names.size()));
        if (onServer.names.isEmpty()) {
            looseLines << check(
                QStringLiteral("ok"),
                tr("No files no torrent owns in %1, as %2 lists it.").arg(name, server));
        } else {
            looseLines << check(
                QStringLiteral("warn"),
                counted(int(onServer.names.size()),
                        tr("%1 file or folder no torrent owns in %2 stays where it is: omaseed "
                           "can't see %3's disks, so it can't carry it."),
                        tr("%1 files and folders no torrent owns in %2 stay where they are: "
                           "omaseed can't see %3's disks, so it can't carry them."))
                    .arg(name, server),
                tr("names as %1 lists them; their size and age can't be read from here")
                    .arg(server));
        }
        if (onServer.neverCounts > 0)
            looseLines << check(
                QStringLiteral("fact"),
                counted(onServer.neverCounts, tr("%1 never counts: %2."), tr("%1 never count: %2."))
                    .arg(core::LooseFiles::neverCountPatterns().join(QStringLiteral(", "))));
    } else if (!seesDisks && serverLists && (!filesIn || !onServer.needListings.isEmpty())) {
        loose.insert(QStringLiteral("state"), QStringLiteral("reading"));
        looseLines << check(QStringLiteral("busy"),
                            tr("Asking %1 what is in %2…").arg(server, name));
    } else if (!seesDisks) {
        loose.insert(QStringLiteral("state"), QStringLiteral("cantSee"));
        looseLines << check(
            QStringLiteral("fact"),
            serverLists
                ? tr("Files no torrent owns: %1 couldn't list all of %2. Whatever is there stays "
                     "where it is.")
                      .arg(server, name)
                : tr("Files no torrent owns: can't see them. They stay where they are. A path map "
                     "in Settings › Servers lets omaseed see them."));
    } else {
        const auto walk = m_loose.constFind(root);
        if (!filesIn || walk == m_loose.cend() || walk->running || !walk->at.isValid()) {
            loose.insert(QStringLiteral("state"), QStringLiteral("reading"));
            looseLines << check(QStringLiteral("busy"),
                                tr("Looking for files no torrent owns in %1…").arg(name));
        } else {
            const core::LooseFiles::Result &result = walk->result;
            loose.insert(QStringLiteral("state"),
                         result.complete ? QStringLiteral("done") : QStringLiteral("incomplete"));
            loose.insert(QStringLiteral("count"), result.loose);
            loose.insert(QStringLiteral("bytes"), result.looseBytes);
            if (!result.complete) {
                looseLines << check(QStringLiteral("unknown"),
                                    tr("%1 couldn't be read whole: none of its files count as "
                                       "loose, so they stay where they are.")
                                        .arg(name));
            } else if (result.loose == 0 && result.recent == 0 && result.unknown == 0) {
                looseLines << check(QStringLiteral("ok"),
                                    tr("No files no torrent owns in %1.").arg(name));
            } else {
                if (result.loose > 0) {
                    carryOffered = true;
                    const QString files =
                        counted(result.loose,
                                tr("%1 file, %2, in %3, beside the torrents' folders."),
                                tr("%1 files, %2, in %3, beside the torrents' folders."))
                            .arg(core::Format::size(result.looseBytes), name);
                    looseLines << check(
                        QStringLiteral("loose"),
                        files + QLatin1Char(' ') +
                            (carry
                                 ? tr("Carried to the same place below %1 after the torrents land: "
                                      "copied, checked, then removed here. Never deleted.")
                                       .arg(newPath.isEmpty() ? tr("the new path") : newPath)
                                 : tr("Left where they are, and listed in the summary.")));
                }
                if (result.recent > 0)
                    looseLines << check(
                        QStringLiteral("warn"),
                        counted(result.recent,
                                tr("%1 changed in the last 15 min: left where it is, "
                                   "listed in the summary."),
                                tr("%1 changed in the last 15 min: left where they are, "
                                   "listed in the summary.")));
                if (result.unknown > 0) {
                    QString first;
                    for (const core::LooseFile &file : result.files) {
                        if (file.verdict == core::LooseFile::Verdict::Unknown) {
                            first = file.relative;
                            break;
                        }
                    }
                    looseLines << check(
                        QStringLiteral("unknown"),
                        counted(result.unknown,
                                tr("%1 couldn't be read (%2): unknown, so left where it is."),
                                tr("%1 couldn't be read (%2 and more): unknown, so left "
                                   "where they are."))
                            .arg(first));
                }
            }
            if (result.neverCounts > 0)
                looseLines << check(
                    QStringLiteral("fact"),
                    counted(
                        result.neverCounts, tr("%1 never counts: %2."), tr("%1 never count: %2."))
                        .arg(core::LooseFiles::neverCountPatterns().join(QStringLiteral(", "))));
        }
    }
    loose.insert(QStringLiteral("rows"), looseLines);
    out.insert(QStringLiteral("loose"), loose);
    out.insert(QStringLiteral("carryOffered"), carryOffered);
    out.insert(QStringLiteral("carryDefault"), carryOffered);

    const int moveCount = int(plan.toMove().size());
    out.insert(QStringLiteral("moveCount"), moveCount);
    out.insert(QStringLiteral("moveHashes"), plan.toMove());
    // #102: nothing is sent before the server has said what is already there.
    out.insert(QStringLiteral("canMove"),
               plan.canMove() && plan.namesCheck() != MovePlan::NamesCheck::Pending);
    const bool behind = m_job != nullptr && m_job->state() != MoveJob::State::Idle &&
                        m_job->state() != MoveJob::State::Done &&
                        m_job->state() != MoveJob::State::Ended;
    out.insert(QStringLiteral("queuedBehind"),
               behind ? FolderTree::baseName(m_job->root()) : QString());
    out.insert(QStringLiteral("moveText"),
               behind ? tr("move after %1").arg(FolderTree::baseName(m_job->root()))
               : moveCount == 0
                   ? tr("move")
                   : counted(moveCount, tr("move %1 torrent"), tr("move %1 torrents")));
    return out;
}

QVariantList MoveController::looseRows(const QString &rootIn) const
{
    QVariantList rows;
    const auto walk = m_loose.constFind(SharedData::normalize(rootIn));
    if (walk == m_loose.cend() && m_serverLists && !m_serverFolders.isEmpty()) {
        // C16 L13: names only, as the server lists them.
        const QString root = SharedData::normalize(rootIn);
        const core::LooseFiles::OnServer onServer = looseOnServer(root);
        if (!onServer.complete)
            return rows;
        const QString name = FolderTree::baseName(root);
        const QString what = tr("stays: omaseed can't see %1's disks").arg(serverName());
        for (const QString &entry : onServer.names)
            rows << QVariantMap{{QStringLiteral("path"), name + QLatin1Char('/') + entry},
                                {QStringLiteral("size"), QStringLiteral("—")},
                                {QStringLiteral("what"), what},
                                {QStringLiteral("kind"), QStringLiteral("warn")}};
        return rows;
    }
    if (walk == m_loose.cend() || walk->running)
        return rows;
    const QString name = FolderTree::baseName(walk->root);
    for (const core::LooseFile &file : walk->result.files) {
        QString what;
        QString kind;
        switch (file.verdict) {
        case core::LooseFile::Verdict::Loose:
            what = tr("carried");
            kind = QStringLiteral("ok");
            break;
        case core::LooseFile::Verdict::NeverCounts:
            what = tr("never counts: %1").arg(file.why);
            kind = QStringLiteral("dim");
            break;
        case core::LooseFile::Verdict::Recent:
            what = tr("%1: stays").arg(file.why);
            kind = QStringLiteral("warn");
            break;
        case core::LooseFile::Verdict::Unknown:
            what = tr("unknown: %1").arg(file.why);
            kind = QStringLiteral("unknown");
            break;
        }
        rows << QVariantMap{{QStringLiteral("path"), name + QLatin1Char('/') + file.relative},
                            {QStringLiteral("size"),
                             file.isFolder ? QStringLiteral("—") : core::Format::size(file.size)},
                            {QStringLiteral("what"), what},
                            {QStringLiteral("kind"), kind}};
    }
    return rows;
}

QString MoveController::looseText(const QString &root) const
{
    QStringList lines;
    for (const QVariant &row : looseRows(root)) {
        const QVariantMap map = row.toMap();
        lines << QStringLiteral("%1\t%2\t%3")
                     .arg(map.value(QStringLiteral("path")).toString(),
                          map.value(QStringLiteral("size")).toString(),
                          map.value(QStringLiteral("what")).toString());
    }
    return lines.join(QLatin1Char('\n'));
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
