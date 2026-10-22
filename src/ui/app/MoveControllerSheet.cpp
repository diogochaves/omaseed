// MoveController's sheet half (board 42 S1–S3, board 43 Q1–Q4, P1, S1, E2; DECISIONS C15,
// C16): what a move would do, worded, before anything is sent. The job half is in
// MoveController.cpp.

#include "ui/app/MoveController.hpp"

#include "ui/app/MoveWords.hpp"

#include "core/CategoryModel.hpp"
#include "core/ErrorWords.hpp"
#include "core/Format.hpp"
#include "core/FreeSpace.hpp"
#include "core/QbtClient.hpp"
#include "core/SharedData.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>
#include <QThreadPool>

#include <algorithm>

namespace omaseed::app {

using core::FolderTree;
using core::MoveDisk;
using core::MoveJob;
using core::MovePlan;
using core::SharedData;
using namespace movewords;

namespace {

/// The loose files' walk is read again when it is older than this (S1 is read again on every
/// change of the field, and the files beside a folder don't change that fast).
constexpr qint64 kWalkFreshSeconds = 60;

/// A line a selection's quiet sheet keeps even when every check passed (C16 L3, L13).
QVariantMap always(QVariantMap line)
{
    line.insert(QStringLiteral("always"), true);
    return line;
}

/// A selection going to several drives, each named (L5, L11): "20 MB to /tmp, 30 GB free;
/// 5 MB to /home, 100 GB free". "" for one drive.
QString perDrive(const MovePlan &plan)
{
    QStringList parts;
    for (const MovePlan::DriveRoom &room : plan.drives())
        parts << MoveController::tr("%1 to %2, %3 free")
                     .arg(core::Format::size(room.bytes),
                          room.drive,
                          core::Format::size(std::max<qint64>(0, room.bytesFree)));
    return parts.join(QStringLiteral("; "));
}

/// "/tmp and /home", "/a, /b and /c".
QString driveNames(const MovePlan &plan)
{
    QStringList names;
    for (const MovePlan::DriveRoom &room : plan.drives())
        names << room.drive;
    const QString last = names.takeLast();
    return names.isEmpty()
               ? last
               : MoveController::tr("%1 and %2").arg(names.join(QStringLiteral(", ")), last);
}

/// #131: the writing check's subject, the destination folders the check looked at (only those
/// answering No when `failed`): "/a", "/a and 2 more". `madeIn` gets the folder a missing one
/// will be made in, when there is exactly one; "" otherwise. Empty when nothing was checked.
QString writeFolders(const MovePlan &plan, bool failed, QString *madeIn)
{
    QList<MovePlan::WriteCheck> checks;
    for (const MovePlan::WriteCheck &check : plan.writeChecks()) {
        if (!failed || check.access == MoveDisk::Access::No)
            checks << check;
    }
    *madeIn = QString();
    if (checks.isEmpty())
        return {};
    if (checks.size() == 1) {
        *madeIn = checks.constFirst().madeIn;
        return checks.constFirst().folder;
    }
    return MoveController::tr("%1 and %n more", nullptr, int(checks.size() - 1))
        .arg(checks.constFirst().folder);
}

/// The drive plan.drive() names: what goes there, -1 when it is the only one.
qint64 bytesToDrive(const MovePlan &plan)
{
    for (const MovePlan::DriveRoom &room : plan.drives()) {
        if (room.drive == plan.drive())
            return room.bytes;
    }
    return -1;
}

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

QVariantList
MoveController::checkLines(const MovePlan &plan, const QString &newPath, const QString &name)
{
    // S1's lines (board 42), for a folder and for a selection (board 43 Q2: word for word).
    const QString server = serverName();
    const bool selection = plan.isSelection();
    QVariantList checks;
    const bool pathOk = plan.blocker() != MovePlan::Blocker::NoPath &&
                        plan.blocker() != MovePlan::Blocker::SameFolder &&
                        plan.blocker() != MovePlan::Blocker::InsideTheFolder;
    if (pathOk) {
        const QString drive = plan.drive().isEmpty() ? FolderTree::parentOf(newPath) : plan.drive();
        // C16 L13: said once, so a remote move never reads as checked as a local one.
        if (!plan.seesDisks())
            checks << always(check(
                QStringLiteral("fact"),
                tr("omaseed can't see %1's disks. Checked on %1, through qBittorrent: files "
                   "already where the torrents go; the first torrent, sent alone, answers for "
                   "writing. Not checked here: room on that drive, writing, the age and size of "
                   "files no torrent owns.")
                    .arg(server),
                tr("a path map in Settings › Servers lets omaseed see them")));
        // M5 and M6, for what goes: with nothing to move there is nothing to say.
        if (!plan.toMove().isEmpty()) {
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
                const QString drives = perDrive(plan);
                const qint64 there = bytesToDrive(plan);
                switch (plan.room()) {
                case MovePlan::Check::Ok:
                case MovePlan::Check::Warn:
                    if (!drives.isEmpty()) {
                        checks << check(
                            plan.room() == MovePlan::Check::Ok ? QStringLiteral("ok")
                                                               : QStringLiteral("warn"),
                            tr("Room. %1.").arg(drives),
                            plan.freeReadHere()
                                ? tr("read on this machine, which sees %1's disks").arg(server)
                                : tr("qBittorrent's own figure for its default drive"));
                        break;
                    }
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
                    checks << check(
                        QStringLiteral("bad"),
                        tr("Not enough room. %1, %2 free on %3, and %4 is kept free.")
                            .arg(there < 0 ? toMove
                                           : tr("%1 to move there").arg(core::Format::size(there)),
                                 core::Format::size(std::max<qint64>(0, plan.bytesFree())),
                                 drive,
                                 core::Format::size(m_hooks.lowDiskBytes ? m_hooks.lowDiskBytes()
                                                                         : 0)));
                    break;
                case MovePlan::Check::Unknown:
                    checks << check(
                        QStringLiteral("unknown"),
                        tr("Room: can't tell. %1. omaseed can't see %2's disks there.")
                            .arg(toMove, server),
                        tr("a torrent that doesn't fit fails; the job pauses at the first "
                           "\"no space\" from qBittorrent"));
                    break;
                }
            }
            // M6: writable. #131: it names the destination folder (as boards 42 and 43 draw
            // it), and the folder a missing one will be made in; the drive only when there
            // is no folder to name.
            QString madeIn;
            switch (plan.writable()) {
            case MovePlan::Check::Ok: {
                const QString folders = writeFolders(plan, false, &madeIn);
                const QString line =
                    folders.isEmpty() ? tr("Writable. %1 is writable by the owner of these files.")
                                            .arg(plan.drives().isEmpty() ? drive : driveNames(plan))
                    : madeIn.isEmpty()
                        ? tr("Writable. %1 is writable by the owner of these files.").arg(folders)
                        : tr("Writable. %1 will be made in %2, which the owner of these files "
                             "can write to.")
                              .arg(folders, madeIn);
                checks << check(QStringLiteral("ok"),
                                line,
                                tr("checked here; qBittorrent's own answer comes from the first "
                                   "torrent, which goes alone"));
                break;
            }
            case MovePlan::Check::Unknown:
                checks << check(
                    QStringLiteral("unknown"),
                    tr("Writable: can't tell here. The first torrent goes alone and answers it."));
                break;
            default: {
                const QString folders = writeFolders(plan, true, &madeIn);
                const QString line =
                    folders.isEmpty()
                        ? tr("Not writable here: the owner of these files can't write to %1.")
                              .arg(plan.drives().isEmpty() ? drive : driveNames(plan))
                    : madeIn.isEmpty()
                        ? tr("Not writable here: the owner of these files can't write to %1.")
                              .arg(folders)
                        : tr("Not writable here: %1 would be made in %2, and the owner of these "
                             "files can't write there.")
                              .arg(folders, madeIn);
                checks << check(QStringLiteral("warn"),
                                line,
                                tr("the check can be wrong for a container's user; the first "
                                   "torrent goes alone and answers it"));
                break;
            }
            }
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
                parts << (selection ? counted(sharing,
                                              tr("%1 torrent shares files with another of these; "
                                                 "they move together."),
                                              tr("%1 torrents share files with each other; they "
                                                 "move together."))
                                    : counted(sharing,
                                              tr("%1 torrent shares files with others in the "
                                                 "folder; they move together."),
                                              tr("%1 torrents share files with others in the "
                                                 "folder; they move together.")));
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
            // Moving back, each goes to its own place: the first one's names it.
            const MovePlan::Item *first = plan.item(plan.leftOut().constFirst());
            const QString where =
                !newPath.isEmpty() || first == nullptr ? newPath : FolderTree::parentOf(first->why);
            const core::ErrorWords::Text words =
                core::ErrorWords::forLeftOut(int(plan.leftOut().size()), where);
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
        // C16: a selection's own reasons to leave one where it is.
        if (!plan.movedSince().isEmpty())
            checks << check(QStringLiteral("warn"),
                            counted(int(plan.movedSince().size()),
                                    tr("%1 was moved since by something else: it stays where it is "
                                       "now."),
                                    tr("%1 were moved since by something else: they stay where "
                                       "they are now.")),
                            QString(),
                            plan.movedSince());
        if (!plan.intoItself().isEmpty())
            checks << check(QStringLiteral("bad"),
                            counted(int(plan.intoItself().size()),
                                    tr("%1 would go inside its own folder: left out."),
                                    tr("%1 would go inside their own folders: left out.")),
                            QString(),
                            plan.intoItself());
        if (!plan.alreadyThere().isEmpty())
            checks << always(check(
                QStringLiteral("fact"),
                newPath.isEmpty()
                    ? tr("%1 already where it was. Skipped.").arg(int(plan.alreadyThere().size()))
                    : tr("%1 already there, at %2. Skipped.")
                          .arg(int(plan.alreadyThere().size()))
                          .arg(newPath),
                QString(),
                plan.alreadyThere()));
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
    return checks;
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

    out.insert(QStringLiteral("checks"), checkLines(plan, newPath, name));

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

// ---- a selection (board 43, DECISIONS C16) --------------------------------------------------

namespace {

MovePlan::Rule ruleOf(const QString &rule)
{
    if (rule == QLatin1String("keepPlaces"))
        return MovePlan::Rule::KeepPlaces;
    if (rule == QLatin1String("eachOwn"))
        return MovePlan::Rule::EachOwn;
    return MovePlan::Rule::OneFolder; // L5's default
}

QJsonObject pathsJson(const QHash<QString, QString> &paths)
{
    QJsonObject out;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QHash<QString, QString> pathsOf(const QJsonValue &json)
{
    QHash<QString, QString> out;
    const QJsonObject object = json.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

/// C18 D7: when a finished move ended, "16:24" today, "20 Oct 16:24" another day.
QString whenText(const QDateTime &at, const QDateTime &now)
{
    const QDateTime local = at.toLocalTime();
    const QString time = QLocale().toString(local.time(), QLocale::ShortFormat);
    if (local.date() == now.toLocalTime().date())
        return time;
    return QLocale().toString(local.date(), QStringLiteral("d MMM")) + QLatin1Char(' ') + time;
}

/// Where a finished move went, for its line: the folder, or "their places below ROOT".
QString whereOf(const core::MoveRecord &record)
{
    return record.newPath.isEmpty() ? MoveController::tr("their places") : record.newPath;
}

} // namespace

MovePlan MoveController::planFor(const QJsonObject &spec) const
{
    const QString kind = spec.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("folder") || kind.isEmpty())
        return makePlan(spec.value(QStringLiteral("root")).toString(),
                        spec.value(QStringLiteral("newPath")).toString(),
                        spec.value(QStringLiteral("carry")).toBool());
    MovePlan::Input input;
    input.torrents = library();
    input.hashes = spec.value(QStringLiteral("hashes")).toVariant().toStringList();
    input.rule = ruleOf(spec.value(QStringLiteral("rule")).toString());
    input.newPath = spec.value(QStringLiteral("newPath")).toString();
    input.targets = pathsOf(spec.value(QStringLiteral("targets")));
    input.expectedAt = pathsOf(spec.value(QStringLiteral("expectedAt")));
    input.files = m_files;
    input.probe = m_hooks.probe;
    input.serverFolders = m_serverFolders;
    input.serverLists =
        m_serverLists && m_hooks.engine != nullptr && m_hooks.engine->client() != nullptr;
    if (m_hooks.lowDiskBytes)
        input.lowDiskThreshold = m_hooks.lowDiskBytes();
    return MovePlan::make(input);
}

bool MoveController::fetchFor(const MovePlan &plan)
{
    fetchFiles(plan.needFiles());
    fetchListings(plan.needListings());
    const bool files = std::any_of(plan.needFiles().cbegin(),
                                   plan.needFiles().cend(),
                                   [this](const QString &hash) { return !m_files.contains(hash); });
    return files || plan.namesCheck() == MovePlan::NamesCheck::Pending;
}

QJsonObject MoveController::selectionSpec(const QStringList &hashes,
                                          const QString &newPath,
                                          const QString &rule) const
{
    // The title the band and the summary use: the torrent's name, or "6 torrents".
    QString title = counted(int(hashes.size()), tr("%1 torrent"), tr("%1 torrents"));
    if (hashes.size() == 1 && m_hooks.engine != nullptr &&
        m_hooks.engine->torrents()->indexOfHash(hashes.constFirst()) >= 0)
        title = m_hooks.engine->torrents()->torrent(hashes.constFirst()).name;
    return {{QStringLiteral("kind"), QStringLiteral("selection")},
            {QStringLiteral("title"), title},
            {QStringLiteral("hashes"), QJsonArray::fromStringList(hashes)},
            {QStringLiteral("rule"),
             rule == QLatin1String("keepPlaces") ? QStringLiteral("keepPlaces")
                                                 : QStringLiteral("oneFolder")},
            {QStringLiteral("newPath"),
             newPath.trimmed().isEmpty() ? QString() : SharedData::normalize(newPath.trimmed())}};
}

QJsonObject MoveController::backSpec() const
{
    // L11: each torrent the last job moved, to where it was; one moved since stays.
    if (holding())
        return m_held->back;
    if (m_job == nullptr)
        return {};
    const QHash<QString, QString> from = m_job->movedFrom();
    if (from.isEmpty())
        return {};
    QStringList hashes = from.keys();
    hashes.sort();
    return {
        {QStringLiteral("kind"), QStringLiteral("back")},
        {QStringLiteral("title"), counted(int(hashes.size()), tr("%1 torrent"), tr("%1 torrents"))},
        {QStringLiteral("hashes"), QJsonArray::fromStringList(hashes)},
        {QStringLiteral("rule"), QStringLiteral("eachOwn")},
        {QStringLiteral("targets"), pathsJson(from)},
        {QStringLiteral("expectedAt"), pathsJson(m_job->movedTo())},
        {QStringLiteral("backOf"), m_job->title()},
        {QStringLiteral("backFrom"), m_job->newPath()},
        {QStringLiteral("startedAt"), m_job->startedAt().toString(Qt::ISODate)},
        // C18 D7: what goes back leaves the job's record in the history.
        {QStringLiteral("historyId"), recordIdOfJob()}};
}

QJsonObject MoveController::backSpecOf(const QString &id, const QString &only) const
{
    // C18 D7: a finished move's torrents, each to where it was, after its summary closed.
    const core::MoveRecord *record = m_history.find(id);
    if (record == nullptr)
        return {};
    QStringList hashes = record->from.keys();
    if (!only.isEmpty())
        hashes = hashes.contains(only) ? QStringList{only} : QStringList();
    if (hashes.isEmpty())
        return {};
    hashes.sort();
    QHash<QString, QString> from;
    QHash<QString, QString> to;
    for (const QString &hash : std::as_const(hashes)) {
        from.insert(hash, record->from.value(hash));
        to.insert(hash, record->to.value(hash));
    }
    QStringList hadAutoTmm;
    for (const QString &hash : record->hadAutoTmm) {
        if (from.contains(hash))
            hadAutoTmm << hash;
    }
    const QString title = hashes.size() == 1 && record->names.contains(hashes.constFirst())
                              ? record->names.value(hashes.constFirst())
                              : counted(int(hashes.size()), tr("%1 torrent"), tr("%1 torrents"));
    return {{QStringLiteral("kind"), QStringLiteral("back")},
            {QStringLiteral("title"), title},
            {QStringLiteral("hashes"), QJsonArray::fromStringList(hashes)},
            {QStringLiteral("rule"), QStringLiteral("eachOwn")},
            {QStringLiteral("targets"), pathsJson(from)},
            {QStringLiteral("expectedAt"), pathsJson(to)},
            {QStringLiteral("backOf"), record->title},
            {QStringLiteral("backFrom"), record->newPath},
            {QStringLiteral("startedAt"), record->startedAt.toString(Qt::ISODate)},
            {QStringLiteral("historyId"), id},
            {QStringLiteral("hadAutoTmm"), QJsonArray::fromStringList(hadAutoTmm)},
            {QStringLiteral("carried"), only.isEmpty() ? record->carried : 0}};
}

QVariantMap MoveController::planBackOf(const QString &id, const QString &only)
{
    const QJsonObject spec = backSpecOf(id, only);
    if (spec.isEmpty())
        return {};
    const MovePlan plan = planFor(spec);
    fetchFor(plan);
    return selectionWords(plan, spec);
}

QVariantMap MoveController::lastMoveOf(const QString &hash) const
{
    const core::MoveRecord *record = m_history.lastFor(hash);
    if (record == nullptr)
        return {};
    const core::MoveHistory::BackCheck all = m_history.check(record->id, savePaths());
    const int others = int(record->from.size()) - 1;
    return {{QStringLiteral("id"), record->id},
            {QStringLiteral("title"), record->title},
            {QStringLiteral("when"), whenText(record->finishedAt, now())},
            {QStringLiteral("today"),
             record->finishedAt.toLocalTime().date() == now().toLocalTime().date()},
            {QStringLiteral("from"), record->from.value(hash)},
            {QStringLiteral("to"), record->to.value(hash)},
            {QStringLiteral("others"), others},
            {QStringLiteral("hashes"), QStringList(record->from.keys())},
            {QStringLiteral("carried"), record->carried > 0 ? tr("loose files too") : QString()},
            {QStringLiteral("canBack"), !all.goBack.isEmpty()},
            {QStringLiteral("canBackThis"), all.goBack.contains(hash)}};
}

QVariantMap MoveController::newestMove() const
{
    const core::MoveRecord *record = m_history.newest();
    if (record == nullptr)
        return {};
    const core::MoveHistory::BackCheck all = m_history.check(record->id, savePaths());
    return {{QStringLiteral("id"), record->id},
            {QStringLiteral("title"), record->title},
            {QStringLiteral("when"), whenText(record->finishedAt, now())},
            {QStringLiteral("where"), whereOf(*record)},
            {QStringLiteral("count"), int(record->from.size())},
            {QStringLiteral("canBack"), !all.goBack.isEmpty()}};
}

QVariantList MoveController::moveLines() const
{
    // C18 D7: one line per move in the log, oldest first, worded as qBittorrent's are: a name
    // in quotes (the torrent's, so the log's torrent chip finds it, or the folder's), a
    // selection by its count alone (its title is the count).
    QVariantList out;
    const QList<core::MoveRecord> &records = m_history.records();
    for (auto it = records.crbegin(); it != records.crend(); ++it) {
        const core::MoveRecord &record = *it;
        const int count = int(record.from.size());
        const QString torrents = counted(count, tr("%1 torrent"), tr("%1 torrents"));
        QString text;
        if (count == 1)
            text = tr("omaseed moved \"%1\" to %2")
                       .arg(record.names.value(record.from.keys().constFirst(), record.title),
                            whereOf(record));
        else if (record.kind == QLatin1String("folder"))
            text = tr("omaseed moved the folder \"%1\", %2, to %3")
                       .arg(record.title, torrents, whereOf(record));
        else
            text = tr("omaseed moved %1 to %2").arg(torrents, whereOf(record));
        if (record.carried > 0)
            text += QLatin1Char(' ') +
                    counted(record.carried, tr("and %1 loose file"), tr("and %1 loose files"));
        out << QVariantMap{{QStringLiteral("at"), record.finishedAt.toSecsSinceEpoch()},
                           {QStringLiteral("message"), text},
                           {QStringLiteral("id"), record.id}};
    }
    return out;
}

QJsonObject MoveController::replan(const QJsonObject &spec) const
{
    // L12: checked again now; one that can't move any more is dropped.
    const MovePlan plan = planFor(spec);
    if (!plan.canMove() || plan.namesCheck() == MovePlan::NamesCheck::Pending)
        return {};
    return MoveJob::planJson(plan, spec.value(QStringLiteral("carry")).toBool(), 0, spec);
}

QVariantMap MoveController::planSelection(const QStringList &hashes,
                                          const QString &newPath,
                                          const QString &rule)
{
    const QJsonObject spec = selectionSpec(hashes, newPath, rule);
    const MovePlan plan = planFor(spec);
    fetchFor(plan);
    return selectionWords(plan, spec);
}

QVariantMap MoveController::planBack()
{
    const QJsonObject spec = backSpec();
    if (spec.isEmpty())
        return {};
    const MovePlan plan = planFor(spec);
    fetchFor(plan);
    return selectionWords(plan, spec);
}

QVariantMap MoveController::selectionWords(const MovePlan &plan, const QJsonObject &spec)
{
    const QString kind = spec.value(QStringLiteral("kind")).toString();
    const bool back = kind == QLatin1String("back");
    const QString title = spec.value(QStringLiteral("title")).toString();
    const QString newPath = plan.newPath();
    const QString server = serverName();
    const QStringList hashes = spec.value(QStringLiteral("hashes")).toVariant().toStringList();
    const int count = int(hashes.size());
    const int folders = int(plan.savePaths().size());

    QVariantMap out;
    out.insert(QStringLiteral("title"),
               back ? tr("Move %1 back").arg(title) : tr("Move %1").arg(title));
    {
        qint64 onDisk = 0;
        for (const MovePlan::Item &item : plan.items())
            onDisk += item.bytes;
        QStringList parts;
        if (back) {
            const QDateTime at = QDateTime::fromString(
                spec.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
            parts << (at.isValid() ? tr("each to where it was before %1")
                                         .arg(QLocale().toString(at.toLocalTime().time(),
                                                                 QLocale::ShortFormat))
                                   : tr("each to where it was"));
        } else {
            parts << (folders > 1 ? tr("%1 from %2 folders")
                                        .arg(counted(count, tr("%1 torrent"), tr("%1 torrents")))
                                        .arg(folders)
                                  : counted(count, tr("%1 torrent"), tr("%1 torrents")));
        }
        QString disk = tr("%1 on disk").arg(core::Format::size(onDisk));
        if (plan.partial() > 0)
            disk += QStringLiteral(" ") +
                    tr("(%1 in the torrents: %2)")
                        .arg(core::Format::size(plan.sizeOfTorrents()),
                             counted(plan.partial(), tr("%1 is partial"), tr("%1 are partial")));
        parts << disk;
        if (!server.isEmpty())
            parts << tr("on %1").arg(server);
        out.insert(QStringLiteral("header"), parts.join(QStringLiteral(" · ")));
    }
    // L5: the choice, only when they come from two save paths or more.
    out.insert(QStringLiteral("savePaths"), folders);
    out.insert(QStringLiteral("commonRoot"), plan.root());
    out.insert(QStringLiteral("ruleOffered"), !back && folders > 1);
    out.insert(QStringLiteral("rule"), spec.value(QStringLiteral("rule")).toString());
    if (!back && !newPath.isEmpty()) {
        out.insert(QStringLiteral("explain"),
                   plan.rule() == MovePlan::Rule::KeepPlaces
                       ? tr("each goes to the same place below %1").arg(newPath)
                   : count == 1 ? tr("it goes into %1").arg(newPath)
                                : tr("all %1 go into %2").arg(count).arg(newPath));
        out.insert(QStringLiteral("keepText"), tr("keep their places below %1").arg(plan.root()));
    }
    QVariantList rows;
    if (back || (folders > 1 && plan.rule() == MovePlan::Rule::KeepPlaces)) {
        for (const MovePlan::PathRow &row : plan.pathRows())
            rows << QVariantMap{{QStringLiteral("from"), row.from},
                                {QStringLiteral("to"), row.to},
                                {QStringLiteral("count"), row.count}};
    }
    out.insert(QStringLiteral("pathRows"), rows);

    QString blocker;
    switch (plan.blocker()) {
    case MovePlan::Blocker::None:
    case MovePlan::Blocker::SameFolder:
    case MovePlan::Blocker::InsideTheFolder:
        break;
    case MovePlan::Blocker::NoPath:
        blocker = tr("Type the new path.");
        break;
    case MovePlan::Blocker::AllThere: {
        // Each to its own place (a retry, a move back) has no one path: the torrent's own.
        QString at = newPath;
        for (const MovePlan::Item &item : plan.items()) {
            if (at.isEmpty() && item.kind == MovePlan::Item::Kind::AlreadyThere)
                at = item.to;
        }
        if (count == 1)
            blocker = tr("It is already at %1.").arg(at);
        else if (newPath.isEmpty())
            blocker = tr("All %1 are already where they would go.").arg(count);
        else
            blocker = tr("All %1 are already at %2.").arg(count).arg(newPath);
        break;
    }
    case MovePlan::Blocker::NothingToMove:
        blocker = tr("Nothing to move: every torrent is already there or left out.");
        break;
    case MovePlan::Blocker::NoRoom:
        blocker = tr("Not enough room on %1.").arg(plan.drive().isEmpty() ? newPath : plan.drive());
        break;
    }
    out.insert(QStringLiteral("blocker"), blocker);

    // L3: a line shows only when it says something; when every check passed, one green line
    // stands for them.
    QVariantList all = checkLines(plan, newPath, title);
    // C18 D7: a finished move's way back says who moved the others since.
    const QString historyId = back ? spec.value(QStringLiteral("historyId")).toString() : QString();
    const core::MoveHistory::BackCheck since =
        historyId.isEmpty() ? core::MoveHistory::BackCheck()
                            : m_history.check(historyId, savePaths(), hashes);
    if (!historyId.isEmpty() && m_history.find(historyId) != nullptr) {
        const QStringList generic = plan.movedSince();
        all.removeIf([&generic](const QVariant &value) {
            const QVariantMap line = value.toMap();
            return !generic.isEmpty() &&
                   line.value(QStringLiteral("hashes")).toStringList() == generic;
        });
        for (const auto &group : since.movedAgain) {
            const core::MoveRecord *later = m_history.find(group.first);
            if (later == nullptr)
                continue;
            QVariantMap line = check(
                QStringLiteral("warn"),
                counted(int(group.second.size()),
                        tr("%1 was moved again since, by the move to %2 (%3): it stays where it "
                           "is now."),
                        tr("%1 were moved again since, by the move to %2 (%3): they stay where "
                           "they are now."))
                    .arg(whereOf(*later), whenText(later->finishedAt, now())),
                tr("Moving that one back first puts them where this move left them."),
                group.second);
            line.insert(QStringLiteral("link"), tr("move that one back first"));
            line.insert(QStringLiteral("backFirst"), later->id);
            all << line;
        }
        if (!since.outside.isEmpty())
            all << check(QStringLiteral("warn"),
                         counted(int(since.outside.size()),
                                 tr("%1 was moved since outside omaseed: it stays where it is "
                                    "now."),
                                 tr("%1 were moved since outside omaseed: they stay where they "
                                    "are now.")),
                         QString(),
                         since.outside);
        if (!since.gone.isEmpty())
            all << check(QStringLiteral("fact"),
                         counted(int(since.gone.size()),
                                 tr("%1 is no longer in the library."),
                                 tr("%1 are no longer in the library.")));
    }
    QVariantList checks;
    bool says = false;
    for (const QVariant &value : all) {
        const QString lineKind = value.toMap().value(QStringLiteral("kind")).toString();
        if (lineKind == QLatin1String("warn") || lineKind == QLatin1String("bad") ||
            lineKind == QLatin1String("unknown") || lineKind == QLatin1String("busy"))
            says = true;
    }
    if (plan.blocker() == MovePlan::Blocker::NoRoom) {
        for (const QVariant &value : all) {
            if (value.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("bad"))
                checks << value;
        }
    } else if (says) {
        checks = all;
    } else if (plan.blocker() == MovePlan::Blocker::None) {
        for (const QVariant &value : all) {
            if (value.toMap().value(QStringLiteral("always")).toBool())
                checks << value;
        }
        if (plan.sameDrive())
            checks.prepend(check(QStringLiteral("ok"),
                                 tr("Same drive: a rename, it lands at once. Room and access "
                                    "are fine.")));
        else if (!plan.drives().isEmpty())
            checks.prepend(check(QStringLiteral("ok"),
                                 tr("Room and access are fine: %1.").arg(perDrive(plan))));
        else if (plan.bytesFree() >= 0)
            checks.prepend(check(QStringLiteral("ok"),
                                 tr("Room and access are fine on %1: %2 to move, %3 free.")
                                     .arg(plan.drive(),
                                          core::Format::size(plan.bytesToMove()),
                                          core::Format::size(plan.bytesFree()))));
        else
            checks.prepend(check(QStringLiteral("ok"), tr("Room and access are fine.")));
    } else {
        for (const QVariant &value : all) {
            if (value.toMap().value(QStringLiteral("always")).toBool())
                checks << value;
        }
    }
    if (back) {
        // L11: what moving back doesn't put back. A finished move's (C18 D7) from its record.
        const QStringList had =
            spec.contains(QStringLiteral("hadAutoTmm"))
                ? spec.value(QStringLiteral("hadAutoTmm")).toVariant().toStringList()
            : m_job == nullptr ? QStringList()
                               : m_job->hadAutoTmm();
        int hadHere = 0;
        for (const QString &hash : had)
            hadHere += plan.toMove().contains(hash) ? 1 : 0;
        if (hadHere > 0)
            checks << check(QStringLiteral("warn"),
                            counted(hadHere,
                                    tr("Automatic management stays off for the %1 that had it. "
                                       "Turning it on again is up to you (the torrent's menu)."),
                                    tr("Automatic management stays off for the %1 that had it. "
                                       "Turning it on again is up to you (the torrent's menu).")),
                            QString(),
                            had);
        const int carried = spec.contains(QStringLiteral("carried"))
                                ? spec.value(QStringLiteral("carried")).toInt()
                            : m_carryDone ? m_carryResult.carried
                                          : 0;
        if (carried > 0)
            checks << check(QStringLiteral("fact"),
                            counted(carried,
                                    tr("The %1 loose file carried stays at %2."),
                                    tr("The %1 loose files carried stay at %2."))
                                .arg(spec.value(QStringLiteral("backFrom")).toString()));
    }
    // Q4: a move running now goes first.
    const bool behind = m_job != nullptr && m_job->isActive();
    if (behind) {
        const QString left = timeLeft(m_job->secondsLeft());
        QString line = tr("The move of %1 is running (%2 of %3). These go when it ends.")
                           .arg(m_job->title())
                           .arg(m_job->moved())
                           .arg(m_job->total());
        if (!left.isEmpty())
            line += QLatin1Char(' ') + tr("That is %1.").arg(left);
        checks << always(check(QStringLiteral("fact"), line));
    }
    out.insert(QStringLiteral("checks"), checks);
    out.insert(QStringLiteral("loose"), QVariantMap());
    out.insert(QStringLiteral("carryOffered"), false);
    out.insert(QStringLiteral("carryDefault"), false);

    const int moveCount = int(plan.toMove().size());
    out.insert(QStringLiteral("moveCount"), moveCount);
    out.insert(QStringLiteral("moveHashes"), plan.toMove());
    out.insert(QStringLiteral("canMove"),
               plan.canMove() && plan.namesCheck() != MovePlan::NamesCheck::Pending);
    out.insert(QStringLiteral("queuedBehind"), behind ? m_job->title() : QString());
    out.insert(
        QStringLiteral("moveText"),
        behind                     ? tr("move after %1").arg(m_job->title())
        : back && since.stay() > 0 ? tr("move %1 back · %2 stay").arg(moveCount).arg(since.stay())
        : back                     ? tr("move %1 back").arg(moveCount)
        : count == 1               ? tr("move")
        : moveCount == 0 ? tr("move")
                         : counted(moveCount, tr("move %1 torrent"), tr("move %1 torrents")));
    return out;
}

QVariantList MoveController::places(const QString &typed) const
{
    // P1: recent destinations, the library's folders, the categories' folders not among
    // them; `typed` narrows all three.
    const QString needle = typed.trimmed();
    const auto wanted = [&needle](const QString &path) {
        return needle.isEmpty() || path.contains(needle, Qt::CaseInsensitive);
    };
    QVariantList out;
    QSet<QString> seen;
    for (const QString &path : m_recents) {
        if (!wanted(path) || seen.contains(path))
            continue;
        seen.insert(path);
        out << QVariantMap{{QStringLiteral("path"), path},
                           {QStringLiteral("kind"), QStringLiteral("recent")}};
    }
    QList<core::FolderNode> nodes = m_folders->nodes();
    for (const core::FolderNode &node : std::as_const(nodes)) {
        if (!node.isSavePath || !wanted(node.path) || seen.contains(node.path))
            continue;
        seen.insert(node.path);
        out << QVariantMap{{QStringLiteral("path"), node.path},
                           {QStringLiteral("kind"), QStringLiteral("folder")},
                           {QStringLiteral("count"), node.countHere}};
    }
    if (m_hooks.engine != nullptr) {
        const core::CategoryModel *categories = m_hooks.engine->categories();
        for (const QString &name : categories->names()) {
            const QString path = SharedData::normalize(categories->savePathOf(name));
            if (path.isEmpty() || !wanted(path) || seen.contains(path))
                continue;
            seen.insert(path);
            out << QVariantMap{{QStringLiteral("path"), path},
                               {QStringLiteral("kind"), QStringLiteral("category")},
                               {QStringLiteral("name"), name}};
        }
    }
    return out;
}

} // namespace omaseed::app
