#pragma once

// MovePlan — the preflight of a folder's move, as data (board 42 §3, DECISIONS C15 M4–M9,
// M12, #96). Pure apart from the disk checks, which go through MoveDisk so the tests can say
// what the disk would.
//
// Given the library, the folder (`root`) and its new path, it works out, before anything is
// sent:
//
// - each torrent's target: the new path plus its save path below the folder (M4), and the
//   order they go in, smallest first (M12);
// - torrents from outside the folder whose files are all inside it (one with its root folder
//   named like the folder, or one added without a subfolder whose files all lie in it): they
//   move too, to the matching place (§9). One that spans the folder is not pulled in; it is
//   listed;
// - torrents inside the folder sharing data with each other move together anyway (§3 S1);
// - room (M5): bytes on disk (qBittorrent's `completed`) plus the loose files to carry, against
//   free space less the low-disk threshold, read through FreeSpaceProbe; the same drive needs
//   none;
// - writable (M6): checked here, as the owner of the folder's files, when this machine sees
//   the server's disks;
// - automatic management (M7): which torrents use it; setLocation turns it off for them;
// - a file already at a torrent's target (M8): qBittorrent would keep it and report the move
//   done, so that torrent is left out. Checked on this machine when
//   it sees the server's disks, else on the server through the folders qBittorrent listed
//   (ServerFolder, DECISIONS C16 L13, #102);
// - torrents already at the new path (counted, skipped) and what blocks the move as a whole
//   (the folder itself, a path inside it).
//
// What it can't check without a local view it says it couldn't (Check::Unknown), never "fine".
//
// A selection (DECISIONS C16 L1, #103) is the other way in: `hashes` instead of a folder, and
// a target rule (L5, L11): into one folder, keep their places below their deepest common
// folder, or each to its own path (move back). The same checks follow.

#include "core/FreeSpace.hpp"
#include "core/ServerFolder.hpp"
#include "core/Torrent.hpp"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

/// The disk checks a plan makes on this machine. real() is the one the app uses.
struct MoveDisk
{
    enum class Access {
        Yes,
        No,
        Unknown,
    };

    /// Whether something (a file, a folder, a link) is at `localPath`.
    std::function<bool(const QString &localPath)> exists;
    /// #161: what is at `localPath`, for M8's line: its size (a folder's, its files'), -1 when
    /// it can't be read; `folder` says whether it is one.
    std::function<qint64(const QString &localPath, bool *folder)> sizeOf;
    /// Whether the owner of `ownerOf`'s files could create `localTarget` (or write in it):
    /// tested on its nearest existing parent, by that owner's permissions, and No on a
    /// read-only mount.
    std::function<Access(const QString &localTarget, const QString &ownerOf)> canWrite;

    static MoveDisk real();
};

class MovePlan
{
public:
    enum class Check {
        Ok,      ///< green
        Warn,    ///< yellow: fine, but know it
        Block,   ///< red, about the whole move: the button is off
        Unknown, ///< can't tell here; the move is allowed
    };

    /// Where M8's check ran (C16 L13).
    enum class NamesCheck {
        Here,    ///< on this machine, which sees the server's disks
        Server,  ///< on the server: qBittorrent listed every target's folder
        Pending, ///< on the server, some listings still to come: don't send yet
        Cannot,  ///< nowhere: the server can't list folders, or didn't answer
    };

    enum class Blocker {
        None,
        NoPath,          ///< the field is empty
        SameFolder,      ///< "games is already there."
        InsideTheFolder, ///< "That is inside games."
        NothingToMove,
        NoRoom,
        AllThere, ///< a selection whose every torrent is already at its target
    };

    /// Where each torrent of a selection goes (C16 L5, L11).
    enum class Rule {
        KeepPlaces, ///< the new path plus its save path below the root (a folder's, C15 M4)
        OneFolder,  ///< every torrent to the new path
        EachOwn,    ///< each to its own path, from Input::targets (move back)
    };

    struct Item
    {
        enum class Kind {
            Move,         ///< in the folder, goes to `to`
            PulledIn,     ///< saved outside, all its files inside: goes to `to`
            LeftOut,      ///< M8: a file already at its target
            AlreadyThere, ///< already under the new path: skipped
            Spans,        ///< saved outside, some of its files inside: stays, listed
            MovedSince,   ///< move back: no longer where the move put it (`why`)
            IntoItself,   ///< its target lies inside its own content folder (`why`)
        };
        QString hash;
        QString name;
        QString from; ///< its save path now
        QString to;   ///< its target, empty when it doesn't move
        qint64 bytes = 0;
        Kind kind = Kind::Move;
        QString why; ///< for LeftOut and Spans: the path that decided it
        /// LeftOut (#161 a): what is at `why`. Its size, -1 unknown (the disks unseen).
        qint64 thereBytes = -1;
        bool thereIsFolder = false;
        /// Whose it is: a torrent of the library's name, or empty.
        QString thereOwner;
        /// False: can't tell whether a torrent owns it (a torrent without a subfolder whose
        /// files aren't known saves around it). True and no owner: no torrent owns it.
        bool thereOwnerKnown = false;
        bool autoTmm = false;
        QString category;
    };

    /// One row of the sheet's "goes to" list: a save path and where it goes.
    struct PathRow
    {
        QString from;
        QString to;
        int count = 0;
    };

    struct Input
    {
        QList<Torrent> torrents; ///< the whole library
        QString root;            ///< the folder, as the server writes it
        QString newPath;         ///< its new path
        /// torrents/files names (relative to the save path) of torrents added without a
        /// subfolder, by hash. Those the plan needed and didn't get are in needFiles().
        QHash<QString, QStringList> files;
        const FreeSpaceProbe *probe = nullptr;              ///< room, the local view, the drive
        qint64 lowDiskThreshold = 2LL * 1024 * 1024 * 1024; ///< Q16
        qint64 looseBytes = 0; ///< loose files to carry (M17), when the tick is on
        MoveDisk disk = MoveDisk::real();
        /// Folders qBittorrent listed, by folder as the server writes it: M8's check when this
        /// machine doesn't see the server's disks. Those the plan needed and didn't get are in
        /// needListings().
        QHash<QString, ServerFolder> serverFolders;
        /// False once the server answered that it can't list folders.
        bool serverLists = true;

        /// C16 L1: a selection. Empty: the folder `root` and what is saved under it (C15). For
        /// a selection `root` may stay empty: KeepPlaces then uses their deepest common folder.
        QStringList hashes;
        Rule rule = Rule::KeepPlaces;
        /// EachOwn: each torrent's target, by hash (`newPath` is not used).
        QHash<QString, QString> targets;
        /// EachOwn: where each torrent should be now (where a move put it); one that isn't was
        /// moved since and is left out (L11).
        QHash<QString, QString> expectedAt;
    };

    /// The deepest folder every one of `paths` lies in (itself included): "/" at worst, ""
    /// for none.
    static QString commonFolder(const QStringList &paths);

    static MovePlan make(const Input &input);

    QString root() const { return m_root; }
    QString newPath() const { return m_newPath; }
    bool isSelection() const { return m_selection; }
    Rule rule() const { return m_rule; }
    /// A selection's save paths, distinct (L5 offers its choice when there are two or more).
    QStringList savePaths() const { return m_savePaths; }
    /// A selection's torrents that share files with torrents outside it (they are not in the
    /// plan: the shared-data guard asks, #28).
    QStringList sharedOutside() const { return m_sharedOutside; }
    /// Every torrent the plan looked at, in the order it would send them.
    const QList<Item> &items() const { return m_items; }
    /// The hashes it sends, in order: smallest first (M12). The first goes alone (M6).
    QStringList toMove() const;
    const Item *item(const QString &hash) const;
    QList<PathRow> pathRows() const { return m_pathRows; }
    /// Hashes of torrents added without a subfolder whose file lists the plan needs.
    QStringList needFiles() const { return m_needFiles; }
    /// Folders on the server whose listings M8's check needs (when the disks aren't seen).
    QStringList needListings() const { return m_needListings; }
    NamesCheck namesCheck() const { return m_namesCheck; }

    Blocker blocker() const { return m_blocker; }
    bool canMove() const { return m_blocker == Blocker::None; }

    // ---- room (M5) -------------------------------------------------------------------------
    Check room() const { return m_room; }
    qint64 bytesToMove() const { return m_bytesToMove; } ///< torrents and loose files
    qint64 torrentBytes() const { return m_torrentBytes; }
    /// Their size, for "1.64 TB in the torrents".
    qint64 sizeOfTorrents() const { return m_sizeOfTorrents; }
    /// Torrents with less on disk than their size.
    int partial() const { return m_partial; }
    qint64 bytesFree() const { return m_bytesFree; } ///< -1 unknown
    /// What is left on that drive after the move, -1 unknown.
    qint64 bytesFreeAfter() const { return m_bytesFreeAfter; }
    bool sameDrive() const { return m_sameDrive; } ///< a rename for each torrent
    QString drive() const { return m_drive; }      ///< the destination's mount point, if known
    /// A selection's destination drives, when it goes to more than one (moving back, keeping
    /// places across drives): each with what goes there and what it has free, the one with
    /// the least left after first. Empty for one drive, which drive() and bytesFree() tell.
    struct DriveRoom
    {
        QString drive;
        qint64 bytes = 0;
        qint64 bytesFree = -1;
    };
    QList<DriveRoom> drives() const { return m_drives; }
    bool freeReadHere() const { return m_freeReadHere; }

    // ---- the rest of the checks ------------------------------------------------------------
    Check writable() const { return m_writable; }
    /// M6, #131: what the writing check looked at, by the server's paths: the destination
    /// folder itself and, when it doesn't exist yet, the existing folder it will be made in.
    struct WriteCheck
    {
        QString folder;
        QString madeIn; ///< "" when the folder exists
        MoveDisk::Access access = MoveDisk::Access::Unknown;
    };
    /// Every destination checked, in order; writable() is their sum.
    QList<WriteCheck> writeChecks() const { return m_writeChecks; }
    bool seesDisks() const { return m_seesDisks; } ///< loose files and collisions can be checked
    /// Groups of moving torrents that share data among themselves (they move together).
    QList<QStringList> sharedInside() const { return m_sharedInside; }
    QStringList pulledIn() const { return hashesOf(Item::Kind::PulledIn); }
    QStringList movedSince() const { return hashesOf(Item::Kind::MovedSince); }
    QStringList intoItself() const { return hashesOf(Item::Kind::IntoItself); }
    QStringList spans() const { return hashesOf(Item::Kind::Spans); }
    QStringList leftOut() const { return hashesOf(Item::Kind::LeftOut); }
    QStringList alreadyThere() const { return hashesOf(Item::Kind::AlreadyThere); }
    /// Moving torrents with automatic management on (M7), and the categories they use.
    QStringList autoManaged() const;
    QStringList autoManagedCategories() const;

private:
    QStringList hashesOf(Item::Kind kind) const;
    void roomForEach(const Input &input);

    QString m_root;
    QString m_newPath;
    bool m_selection = false;
    Rule m_rule = Rule::KeepPlaces;
    QStringList m_savePaths;
    QStringList m_sharedOutside;
    QList<Item> m_items;
    QList<PathRow> m_pathRows;
    QStringList m_needFiles;
    QStringList m_needListings;
    NamesCheck m_namesCheck = NamesCheck::Here;
    Blocker m_blocker = Blocker::None;
    Check m_room = Check::Unknown;
    qint64 m_bytesToMove = 0;
    qint64 m_torrentBytes = 0;
    qint64 m_sizeOfTorrents = 0;
    int m_partial = 0;
    qint64 m_bytesFree = -1;
    qint64 m_bytesFreeAfter = -1;
    bool m_sameDrive = false;
    QString m_drive;
    QList<DriveRoom> m_drives;
    bool m_freeReadHere = false;
    Check m_writable = Check::Unknown;
    QList<WriteCheck> m_writeChecks;
    bool m_seesDisks = false;
    QList<QStringList> m_sharedInside;
};

} // namespace omaseed::core
