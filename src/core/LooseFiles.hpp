#pragma once

// LooseFiles — the files in a folder that no torrent owns (board 42 §3 and §7, DECISIONS C15
// M9, #96). The move carries them after the torrents (M17); a later "files no torrent owns"
// view on a folder can use this as it is.
//
// It walks a folder on this machine (the caller maps the server's path first) and sorts every
// file it finds:
//
// - owned: under some torrent's content path, or one of the files of a torrent added without
//   a subfolder (the caller lists those). Not reported; an owned folder is not even walked.
// - never counts: qBittorrent's own `.parts` and `*.!qB` files (they belong to a torrent and
//   move with it) and system clutter: Thumbs.db, desktop.ini, .DS_Store. Shown, never carried.
// - recent: changed less than the minimum age ago (15 min): it may still be being written.
//   Stays, and is listed.
// - unknown: anything that can't be read, a folder that can't be listed, a link, anything
//   that is not a plain file. Fail closed: unknown is never loose (qbit_manage #1379 counted an
//   unreadable path as having no links). Stays, and is listed.
// - loose: everything else. The preflight's list is the dry run: these are what would move.
//
// Matching is on the content path, never only the save path (qui fixed that false positive in
// April 2026). Paths are compared as written, case-sensitive.
//
// For a server whose disks this machine can't see, onServer() names what qBittorrent lists of
// the folder that no torrent owns (DECISIONS C16 L13, #102): names only, so nothing is recent,
// unknown or carried; they stay where they are.

#include "core/ServerFolder.hpp"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

namespace omaseed::core {

struct LooseFile
{
    enum class Verdict {
        Loose,
        NeverCounts,
        Recent,
        Unknown,
    };

    QString path;     ///< on this machine
    QString relative; ///< below the folder that was walked
    qint64 size = 0;  ///< bytes; 0 for a folder or what couldn't be read
    QDateTime modified;
    bool isFolder = false; ///< a folder that couldn't be listed (Unknown)
    Verdict verdict = Verdict::Loose;
    QString why; ///< "changed 3 min ago", "can't read", "a link", "qBittorrent's own"…

    bool operator==(const LooseFile &other) const = default;
};

class LooseFiles
{
public:
    struct Result
    {
        QList<LooseFile> files; ///< every file that isn't owned, in walk order (by path)
        int loose = 0;
        qint64 looseBytes = 0;
        int neverCounts = 0;
        int recent = 0;
        int unknown = 0;
        /// False when the walk was stopped (`stop` said so) or the folder itself can't be
        /// listed: then nothing in it may be called loose.
        bool complete = true;
    };

    /// The names that never count (M9's visible list), as patterns: "*.parts", "*.!qB",
    /// "Thumbs.db", "desktop.ini", ".DS_Store".
    static QStringList neverCountPatterns();
    /// Whether a file named `fileName` is on that list.
    static bool neverCounts(const QString &fileName);

    /// Walks `folder`. `owned` are local paths: a torrent's content path (a folder covers
    /// everything below it, a file only itself) and the files of torrents added without a
    /// subfolder. `now` and `minAge` decide what is recent. `stop`, called now and then, ends
    /// the walk early when it returns true.
    static Result scan(const QString &folder,
                       const QStringList &owned,
                       const QDateTime &now,
                       std::chrono::seconds minAge = std::chrono::minutes(15),
                       const std::function<bool()> &stop = {});

    struct OnServer
    {
        /// Below the folder, as qBittorrent listed them: "manual.pdf", "handheld/config". A
        /// folder no torrent owns is one name; what is inside it isn't listed.
        QStringList names;
        int neverCounts = 0;
        /// Folders to ask the server for first.
        QStringList needListings;
        /// False while listings are missing or one failed: then nothing may be called loose.
        bool complete = true;
    };

    /// The entries of `folders` (the folder being moved, every save path below it and the
    /// folders between, as the server writes them) that no torrent owns, from what the server
    /// listed. `owned` are server paths, as scan()'s. An entry that is one of `folders` or
    /// holds one is not named.
    static OnServer onServer(const QString &root,
                             const QStringList &folders,
                             const QHash<QString, ServerFolder> &listed,
                             const QStringList &owned);
};

} // namespace omaseed::core
