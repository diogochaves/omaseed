// MoveController's sheet half (board 42 S1–S3, board 43 Q1–Q4, P1, S1, E2; DECISIONS C15,
// C16): what a move would do, worded, before anything is sent. The job half is in
// MoveController.cpp.

#include "ui/app/MoveController.hpp"

#include "ui/app/MoveWords.hpp"

#include "core/ErrorWords.hpp"
#include "core/Format.hpp"
#include "core/FreeSpace.hpp"
#include "core/QbtClient.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include <QCoreApplication>
#include <QThreadPool>

#include <algorithm>

namespace omaseed::app {

using core::FolderTree;
using core::MoveJob;
using core::MovePlan;
using core::SharedData;
using namespace movewords;

namespace {

/// The loose files' walk is read again when it is older than this (S1 is read again on every
/// change of the field, and the files beside a folder don't change that fast).
constexpr qint64 kWalkFreshSeconds = 60;

} // namespace

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

} // namespace omaseed::app
