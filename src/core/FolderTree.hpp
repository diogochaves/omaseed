#pragma once

// FolderTree — the library's folders, as board 42 §2 draws them in the rail (DECISIONS C15
// M1–M3, #96). Pure: torrents in, rows out, no model and no disk.
//
// Every save path the library uses is a row. A save path under another sits under it
// ("handheld" under "games"). A top save path (one under no other) hangs from a heading: the
// drive's top when this machine can tell it (FreeSpaceProbe's mount point, through the path
// map), otherwise its parent folder. A heading holds no torrents of its own and is shown dim;
// a heading that is itself a save path is just a row ("4 here"). A save path whose parent is
// "/" has no heading.
//
// Paths are compared as the server writes them (SharedData::normalize: `\` read as `/`, a
// trailing `/` trimmed, case-sensitive). Bytes are what is on disk, qBittorrent's `completed`,
// never the torrent's size: a partial pack holds far less than it would.

#include "core/Torrent.hpp"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

struct FolderNode
{
    QString path;            ///< the server's path, normalised
    QString name;            ///< the row's words: the path below its parent row, or the whole path
    QString parentPath;      ///< the parent row's path, empty for a top row
    int depth = 0;           ///< 0 for a top row
    bool isSavePath = false; ///< some torrent is saved exactly here; false: a heading
    bool hasChildren = false;
    int countHere = 0; ///< torrents saved exactly here
    int count = 0;     ///< torrents here and below
    qint64 bytesHere = 0;
    qint64 bytes = 0;  ///< bytes on disk here and below
    int savePaths = 0; ///< save paths here and below (this one included when it is one)

    bool isHeading() const { return !isSavePath; }
    bool operator==(const FolderNode &other) const = default;
};

class FolderTree
{
public:
    /// The mount point of a server path, as this machine sees it ("/mnt/fast"), or "" when it
    /// can't tell. Tests pass their own; the app passes FreeSpaceProbe through the path map.
    using MountRoot = std::function<QString(const QString &serverPath)>;

    /// The rows, depth first, children by name (case-insensitive, then as written).
    static QList<FolderNode> build(const QList<Torrent> &torrents, const MountRoot &mountRoot = {});

    /// The node for `path` in `nodes`, or nullptr.
    static const FolderNode *find(const QList<FolderNode> &nodes, const QString &path);

    /// M4: where a torrent saved at `savePath` goes when the folder `root` becomes `newPath`:
    /// `newPath` plus its path below `root`. Empty when `savePath` is not `root` or under it.
    static QString targetFor(const QString &savePath, const QString &root, const QString &newPath);

    /// The last part of a path: "games" for "/mnt/archive/games".
    static QString baseName(const QString &path);
    /// The path without its last part: "/mnt/archive" for "/mnt/archive/games", "/" for
    /// "/games", "" for "/" or a bare name.
    static QString parentOf(const QString &path);
    /// `parent` and `child` joined with one `/`.
    static QString join(const QString &parent, const QString &child);
};

} // namespace omaseed::core
