#include "core/MovePlan.hpp"

#include "core/FolderTree.hpp"
#include "core/SharedData.hpp"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace omaseed::core {

namespace {

QString norm(const QString &path)
{
    return SharedData::normalize(path);
}

/// `path` below `parent`, without the separator: "a/b" for "/x/a/b" under "/x".
QString below(const QString &path, const QString &parent)
{
    if (path == parent)
        return {};
    return path.mid(parent == QLatin1String("/") ? 1 : parent.size() + 1);
}

/// `path` with `suffix` (a path below some folder) taken off its end, or "" when it doesn't
/// end with it.
QString strip(const QString &path, const QString &suffix)
{
    if (suffix.isEmpty())
        return path;
    const QString tail = QLatin1Char('/') + suffix;
    if (!path.endsWith(tail))
        return {};
    const QString left = path.left(path.size() - tail.size());
    return left.isEmpty() ? QStringLiteral("/") : left;
}

bool lexists(const QString &path)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0;
}

MoveDisk::Access canWriteAs(const QString &localTarget, const QString &ownerOf)
{
    struct stat owner{};
    if (::stat(QFile::encodeName(ownerOf).constData(), &owner) != 0)
        return MoveDisk::Access::Unknown;

    QString at = QDir::cleanPath(localTarget);
    struct stat st{};
    while (::stat(QFile::encodeName(at).constData(), &st) != 0) {
        const QString up = QFileInfo(at).path();
        if (up == at)
            return MoveDisk::Access::Unknown;
        at = up;
    }
    if (!S_ISDIR(st.st_mode))
        return MoveDisk::Access::No;
    struct statvfs fs{};
    if (::statvfs(QFile::encodeName(at).constData(), &fs) == 0 && (fs.f_flag & ST_RDONLY))
        return MoveDisk::Access::No;

    const uid_t uid = owner.st_uid;
    if (uid == ::geteuid())
        return ::access(QFile::encodeName(at).constData(), W_OK | X_OK) == 0 ? MoveDisk::Access::Yes
                                                                             : MoveDisk::Access::No;
    if (uid == 0)
        return MoveDisk::Access::Yes;
    if (st.st_uid == uid)
        return (st.st_mode & S_IWUSR) && (st.st_mode & S_IXUSR) ? MoveDisk::Access::Yes
                                                                : MoveDisk::Access::No;
    // The owner's groups, when this machine knows the owner by name.
    bool inGroup = false;
    if (const passwd *pw = ::getpwuid(uid)) {
        int count = 64;
        QList<gid_t> groups(count);
        if (::getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &count) < 0) {
            groups.resize(count);
            ::getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &count);
        }
        groups.resize(std::max(0, count));
        inGroup = groups.contains(st.st_gid);
    } else {
        // A container's user this machine has no name for: only its own group is known.
        inGroup = owner.st_gid == st.st_gid;
    }
    if (inGroup)
        return (st.st_mode & S_IWGRP) && (st.st_mode & S_IXGRP) ? MoveDisk::Access::Yes
                                                                : MoveDisk::Access::No;
    return (st.st_mode & S_IWOTH) && (st.st_mode & S_IXOTH) ? MoveDisk::Access::Yes
                                                            : MoveDisk::Access::No;
}

} // namespace

MoveDisk MoveDisk::real()
{
    MoveDisk disk;
    disk.exists = lexists;
    disk.canWrite = canWriteAs;
    return disk;
}

MovePlan MovePlan::make(const Input &input)
{
    MovePlan plan;
    plan.m_root = norm(input.root);
    plan.m_newPath = norm(input.newPath);
    const QString &root = plan.m_root;
    const QString &to = plan.m_newPath;

    if (to.isEmpty())
        plan.m_blocker = Blocker::NoPath;
    else if (to == root)
        plan.m_blocker = Blocker::SameFolder;
    else if (SharedData::isUnder(to, root))
        plan.m_blocker = Blocker::InsideTheFolder;

    const FreeSpaceProbe *probe = input.probe;
    plan.m_seesDisks = probe != nullptr && probe->seesServerDisks();
    const auto local = [&](const QString &serverPath) {
        return plan.m_seesDisks ? probe->localPathFor(serverPath) : QString();
    };
    const bool newPathAboveRoot = !to.isEmpty() && SharedData::isUnder(root, to);

    QHash<QString, const Torrent *> byHash;
    for (const Torrent &torrent : input.torrents)
        byHash.insert(torrent.hash, &torrent);

    QList<SharedDataRow> rows;
    QList<Item> moving;
    QList<Item> others;
    QSet<QString> needFiles;
    for (const Torrent &torrent : input.torrents) {
        const QString save = norm(torrent.savePath);
        const QString content = norm(torrent.contentPath);
        rows << SharedDataRow{
            torrent.hash, torrent.savePath, torrent.contentPath, torrent.completed};
        if (save.isEmpty() || root.isEmpty())
            continue;

        Item item;
        item.hash = torrent.hash;
        item.name = torrent.name;
        item.from = save;
        item.bytes = std::max<qint64>(0, torrent.completed);
        item.autoTmm = torrent.autoTmm;
        item.category = torrent.category;
        const bool noRoot = content.isEmpty() || content == save;

        if (SharedData::isUnder(save, root)) {
            item.to = to.isEmpty() ? QString() : FolderTree::targetFor(save, root, to);
            moving << item;
            continue;
        }
        if (!to.isEmpty() && !newPathAboveRoot && SharedData::isUnder(save, to)) {
            item.kind = Item::Kind::AlreadyThere;
            others << item;
            continue;
        }
        if (!SharedData::isUnder(root, save))
            continue; // elsewhere in the library

        // Saved above the folder: does it hold files in it?
        const QString rel = below(root, save);
        if (!noRoot) {
            if (content == root) {
                // Its root folder is the folder: it goes along, under the same name.
                const QString parent = to.isEmpty() ? QString() : strip(to, rel);
                if (!to.isEmpty() && parent.isEmpty()) {
                    item.kind = Item::Kind::Spans;
                    item.why = content;
                    others << item;
                } else {
                    item.kind = Item::Kind::PulledIn;
                    item.to = parent;
                    moving << item;
                }
            } else if (SharedData::isUnder(root, content)) {
                item.kind = Item::Kind::Spans; // its content holds the whole folder
                item.why = content;
                others << item;
            }
            continue;
        }
        const auto files = input.files.constFind(torrent.hash);
        if (files == input.files.cend()) {
            needFiles.insert(torrent.hash);
            continue;
        }
        int inside = 0;
        for (const QString &file : *files)
            inside += SharedData::isUnder(norm(file), rel) ? 1 : 0;
        if (inside == 0)
            continue;
        const QString parent = to.isEmpty() ? QString() : strip(to, rel);
        if (inside == files->size() && (to.isEmpty() || !parent.isEmpty())) {
            item.kind = Item::Kind::PulledIn;
            item.to = parent;
            moving << item;
        } else {
            item.kind = Item::Kind::Spans;
            item.why = root;
            others << item;
        }
    }

    // Shared data among the ones that move: they move together (docs/26, #28). A sharer from
    // outside that the walk above didn't take in spans the folder.
    QStringList movingHashes;
    for (const Item &item : std::as_const(moving))
        movingHashes << item.hash;
    const SharedData::Result shared = SharedData::find(movingHashes, rows, input.files);
    plan.m_sharedInside = shared.inside;
    for (const QString &hash : shared.needFiles)
        needFiles.insert(hash);
    QSet<QString> listed;
    for (const Item &item : std::as_const(others))
        listed.insert(item.hash);
    for (const QString &hash : shared.outside) {
        const Torrent *torrent = byHash.value(hash);
        if (listed.contains(hash) || torrent == nullptr)
            continue;
        Item item;
        item.hash = hash;
        item.name = torrent->name;
        item.from = norm(torrent->savePath);
        item.bytes = torrent->completed;
        item.kind = Item::Kind::Spans;
        item.why = norm(torrent->contentPath);
        others << item;
    }

    // M8: a file already at a torrent's target. Here when this machine sees the server's
    // disks; else in the folders qBittorrent listed (C16 L13, #102).
    const bool askServer = !plan.m_seesDisks && input.serverLists;
    plan.m_namesCheck = plan.m_seesDisks    ? NamesCheck::Here
                        : input.serverLists ? NamesCheck::Server
                                            : NamesCheck::Cannot;
    QSet<QString> needListings;
    bool failed = false;
    bool namesNeedFiles = false;
    if ((plan.m_seesDisks && input.disk.exists) || askServer) {
        for (Item &item : moving) {
            const Torrent *torrent = byHash.value(item.hash);
            if (torrent == nullptr || item.to.isEmpty() || to.isEmpty())
                continue;
            const QString save = norm(torrent->savePath);
            const QString content = norm(torrent->contentPath);
            QStringList targets;
            if (!content.isEmpty() && content != save) {
                targets << FolderTree::join(item.to, below(content, save));
            } else if (const auto files = input.files.constFind(item.hash);
                       files != input.files.cend()) {
                for (const QString &file : *files)
                    targets << FolderTree::join(item.to, norm(file));
            } else {
                needFiles.insert(item.hash);
                namesNeedFiles = true;
            }
            for (const QString &target : std::as_const(targets)) {
                bool there = false;
                if (plan.m_seesDisks) {
                    const QString here = local(target);
                    there = !here.isEmpty() && input.disk.exists(here);
                } else {
                    const QString folder = FolderTree::parentOf(target);
                    const auto listed = input.serverFolders.constFind(folder);
                    if (listed == input.serverFolders.cend()) {
                        needListings.insert(folder);
                        continue;
                    }
                    if (listed->state == ServerFolder::State::Unsupported)
                        plan.m_namesCheck = NamesCheck::Cannot;
                    else if (listed->state == ServerFolder::State::Failed)
                        failed = true;
                    there = listed->has(FolderTree::baseName(target));
                }
                if (there) {
                    item.kind = Item::Kind::LeftOut;
                    item.why = target;
                    break;
                }
            }
        }
    }
    if (askServer && plan.m_namesCheck == NamesCheck::Server) {
        if (!needListings.isEmpty() || namesNeedFiles)
            plan.m_namesCheck = NamesCheck::Pending;
        else if (failed)
            plan.m_namesCheck = NamesCheck::Cannot;
    }
    plan.m_needListings = QStringList(needListings.cbegin(), needListings.cend());
    plan.m_needListings.sort();

    // M12: smallest first, then by name.
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    const auto bySize = [&collator](const Item &a, const Item &b) {
        if (a.bytes != b.bytes)
            return a.bytes < b.bytes;
        return collator.compare(a.name, b.name) < 0;
    };
    std::stable_sort(moving.begin(), moving.end(), bySize);
    QList<Item> going;
    for (const Item &item : std::as_const(moving)) {
        if (item.kind == Item::Kind::LeftOut)
            others << item;
        else
            going << item;
    }
    std::stable_sort(others.begin(), others.end(), [&collator](const Item &a, const Item &b) {
        if (a.kind != b.kind)
            return int(a.kind) < int(b.kind);
        return collator.compare(a.name, b.name) < 0;
    });
    plan.m_items = going + others;

    QMap<QPair<QString, QString>, int> paths;
    for (const Item &item : std::as_const(going)) {
        plan.m_torrentBytes += item.bytes;
        if (const Torrent *torrent = byHash.value(item.hash)) {
            plan.m_sizeOfTorrents += torrent->size;
            plan.m_partial += torrent->completed < torrent->size ? 1 : 0;
        }
        ++paths[{item.from, item.to}];
    }
    for (auto it = paths.cbegin(); it != paths.cend(); ++it)
        plan.m_pathRows << PathRow{it.key().first, it.key().second, it.value()};
    plan.m_bytesToMove = plan.m_torrentBytes + std::max<qint64>(0, input.looseBytes);
    plan.m_needFiles = QStringList(needFiles.cbegin(), needFiles.cend());
    plan.m_needFiles.sort();

    if (plan.m_blocker == Blocker::None && going.isEmpty())
        plan.m_blocker = Blocker::NothingToMove;
    if (to.isEmpty())
        return plan;

    // M5: room.
    if (probe != nullptr) {
        const FreeSpaceProbe::Answer there = probe->answer(to);
        if (plan.m_seesDisks) {
            const FreeSpaceProbe::Answer here = probe->answer(root);
            plan.m_sameDrive = here.source == FreeSpaceProbe::Source::Local &&
                               there.source == FreeSpaceProbe::Source::Local &&
                               !here.where.isEmpty() && here.where == there.where;
        }
        if (there.source != FreeSpaceProbe::Source::Unknown) {
            plan.m_bytesFree = there.bytesFree;
            plan.m_drive = there.where;
            plan.m_freeReadHere = there.source == FreeSpaceProbe::Source::Local;
        }
    }
    if (plan.m_sameDrive) {
        plan.m_room = Check::Ok;
    } else if (plan.m_bytesFree < 0) {
        plan.m_room = Check::Unknown;
    } else if (plan.m_bytesFree - plan.m_bytesToMove < input.lowDiskThreshold) {
        plan.m_room = Check::Block;
        if (plan.m_blocker == Blocker::None)
            plan.m_blocker = Blocker::NoRoom;
    } else {
        plan.m_room = Check::Ok;
    }

    // M6: writable, as the owner of the folder's files. The local check can be wrong both
    // ways (a container's user), so No is a warning: the first torrent goes alone and answers.
    if (plan.m_seesDisks && input.disk.canWrite) {
        const QString target = local(to);
        const QString source = local(root);
        if (!target.isEmpty() && !source.isEmpty()) {
            switch (input.disk.canWrite(target, source)) {
            case MoveDisk::Access::Yes:
                plan.m_writable = Check::Ok;
                break;
            case MoveDisk::Access::No:
                plan.m_writable = Check::Warn;
                break;
            case MoveDisk::Access::Unknown:
                plan.m_writable = Check::Unknown;
                break;
            }
        }
    }
    return plan;
}

QStringList MovePlan::toMove() const
{
    QStringList out;
    for (const Item &item : m_items) {
        if (item.kind == Item::Kind::Move || item.kind == Item::Kind::PulledIn)
            out << item.hash;
    }
    return out;
}

const MovePlan::Item *MovePlan::item(const QString &hash) const
{
    for (const Item &one : m_items) {
        if (one.hash == hash)
            return &one;
    }
    return nullptr;
}

QStringList MovePlan::hashesOf(Item::Kind kind) const
{
    QStringList out;
    for (const Item &item : m_items) {
        if (item.kind == kind)
            out << item.hash;
    }
    return out;
}

QStringList MovePlan::autoManaged() const
{
    QStringList out;
    for (const Item &item : m_items) {
        if (item.autoTmm && (item.kind == Item::Kind::Move || item.kind == Item::Kind::PulledIn))
            out << item.hash;
    }
    return out;
}

QStringList MovePlan::autoManagedCategories() const
{
    QStringList out;
    for (const Item &item : m_items) {
        if (item.autoTmm && (item.kind == Item::Kind::Move || item.kind == Item::Kind::PulledIn) &&
            !item.category.isEmpty() && !out.contains(item.category))
            out << item.category;
    }
    out.sort();
    return out;
}

} // namespace omaseed::core
