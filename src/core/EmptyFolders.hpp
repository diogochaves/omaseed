#pragma once

// EmptyFolders — the folders a move left empty at its source, removed after qBittorrent has
// moved the torrents (DECISIONS C15 M17, #203). qBittorrent (libtorrent) removes each
// torrent's own subfolders as it moves them, never the save path it leaves; a folder's move
// empties those too (games/handheld, games). LooseCarry removes what the carry empties below
// the folder; this covers what the torrents' own move empties, both ways.
//
// Only `rmdir`: a folder that holds anything (a file the user put there, a hidden file, a
// link) stays, and nothing is ever deleted that isn't an empty folder. On this machine's
// paths only (the caller maps the server's through the path map).

#include <QString>
#include <QStringList>

namespace omaseed::core {

class EmptyFolders
{
public:
    /// Removes each folder of `emptied` that is empty, then each folder above it while it is
    /// empty, up to `root`: the folders below `root` only, or `root` too when `withRoot`.
    /// Never a folder outside `root` or above it, never one of `keep` nor a folder that holds
    /// one of `keep` (a move's destination, qBittorrent's default save path). Deepest first.
    /// Returns how many went.
    static int remove(const QString &root,
                      const QStringList &emptied,
                      bool withRoot,
                      const QStringList &keep = {});
};

} // namespace omaseed::core
