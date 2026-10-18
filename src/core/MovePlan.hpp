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
//   done (diogochaves/upstream#123), so that torrent is left out;
// - torrents already at the new path (counted, skipped) and what blocks the move as a whole
//   (the folder itself, a path inside it).
//
// What it can't check without a local view it says it couldn't (Check::Unknown), never "fine".

#include "core/FreeSpace.hpp"
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

    enum class Blocker {
        None,
        NoPath,          ///< the field is empty
        SameFolder,      ///< "games is already there."
        InsideTheFolder, ///< "That is inside games."
        NothingToMove,
        NoRoom,
    };

    struct Item
    {
        enum class Kind {
            Move,         ///< in the folder, goes to `to`
            PulledIn,     ///< saved outside, all its files inside: goes to `to`
            LeftOut,      ///< M8: a file already at its target
            AlreadyThere, ///< already under the new path: skipped
            Spans,        ///< saved outside, some of its files inside: stays, listed
        };
        QString hash;
        QString name;
        QString from; ///< its save path now
        QString to;   ///< its target, empty when it doesn't move
        qint64 bytes = 0;
        Kind kind = Kind::Move;
        QString why; ///< for LeftOut and Spans: the path that decided it
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
    };

    static MovePlan make(const Input &input);

    QString root() const { return m_root; }
    QString newPath() const { return m_newPath; }
    /// Every torrent the plan looked at, in the order it would send them.
    const QList<Item> &items() const { return m_items; }
    /// The hashes it sends, in order: smallest first (M12). The first goes alone (M6).
    QStringList toMove() const;
    const Item *item(const QString &hash) const;
    QList<PathRow> pathRows() const { return m_pathRows; }
    /// Hashes of torrents added without a subfolder whose file lists the plan needs.
    QStringList needFiles() const { return m_needFiles; }

    Blocker blocker() const { return m_blocker; }
    bool canMove() const { return m_blocker == Blocker::None; }

    // ---- room (M5) -------------------------------------------------------------------------
    Check room() const { return m_room; }
    qint64 bytesToMove() const { return m_bytesToMove; } ///< torrents and loose files
    qint64 torrentBytes() const { return m_torrentBytes; }
    qint64 sizeOfTorrents() const
    {
        return m_sizeOfTorrents;
    }                                                ///< their size, for "1.64 TB in the torrents"
    int partial() const { return m_partial; }        ///< torrents with less on disk than their size
    qint64 bytesFree() const { return m_bytesFree; } ///< -1 unknown
    qint64 bytesFreeAfter() const { return m_bytesFree < 0 ? -1 : m_bytesFree - m_bytesToMove; }
    bool sameDrive() const { return m_sameDrive; } ///< a rename for each torrent
    QString drive() const { return m_drive; }      ///< the destination's mount point, if known
    bool freeReadHere() const { return m_freeReadHere; }

    // ---- the rest of the checks ------------------------------------------------------------
    Check writable() const { return m_writable; }
    bool seesDisks() const { return m_seesDisks; } ///< loose files and collisions can be checked
    /// Groups of moving torrents that share data among themselves (they move together).
    QList<QStringList> sharedInside() const { return m_sharedInside; }
    QStringList pulledIn() const { return hashesOf(Item::Kind::PulledIn); }
    QStringList spans() const { return hashesOf(Item::Kind::Spans); }
    QStringList leftOut() const { return hashesOf(Item::Kind::LeftOut); }
    QStringList alreadyThere() const { return hashesOf(Item::Kind::AlreadyThere); }
    /// Moving torrents with automatic management on (M7), and the categories they use.
    QStringList autoManaged() const;
    QStringList autoManagedCategories() const;

private:
    QStringList hashesOf(Item::Kind kind) const;

    QString m_root;
    QString m_newPath;
    QList<Item> m_items;
    QList<PathRow> m_pathRows;
    QStringList m_needFiles;
    Blocker m_blocker = Blocker::None;
    Check m_room = Check::Unknown;
    qint64 m_bytesToMove = 0;
    qint64 m_torrentBytes = 0;
    qint64 m_sizeOfTorrents = 0;
    int m_partial = 0;
    qint64 m_bytesFree = -1;
    bool m_sameDrive = false;
    QString m_drive;
    bool m_freeReadHere = false;
    Check m_writable = Check::Unknown;
    bool m_seesDisks = false;
    QList<QStringList> m_sharedInside;
};

} // namespace omaseed::core
