#pragma once

// CreatePlan — the pure half of creating a torrent from a folder (#146, DECISIONS C27, board 49):
// the piece size qBittorrent will choose, the override's rows, the folder's check before any
// hashing (CR17), the band's figures while it hashes (CR13), and the private .torrent's name
// (CR15). No I/O except checkLocal(), which reads a folder this machine sees.
//
// The automatic piece size (CR11) is what qBittorrent 5.3.0 chose for 17 sizes from 100 KB to
// 32 GiB (board 49 §1 and #146's build plan): the smallest power of two from 16 KiB whose
// square, over 100, is at least the content's size, that is about ten times the square root
// of the size, rounded up. Measured in v1 and hybrid alike.

#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core::CreatePlan {

constexpr qint64 kMinPiece = 16 * 1024;
constexpr qint64 kMaxPiece = 64 * 1024 * 1024;
/// The largest piece qBittorrent picks by itself.
constexpr qint64 kMaxAutoPiece = 16 * 1024 * 1024;

/// The piece size qBittorrent's "automatic" gives `totalBytes` (see the top).
qint64 autoPieceSize(qint64 totalBytes);
/// How many pieces `totalBytes` makes at `pieceSize`.
qint64 pieceCount(qint64 totalBytes, qint64 pieceSize);
/// About how big the v1 .torrent is: 20 bytes a piece, a line per file and the rest.
qint64 torrentBytes(qint64 totalBytes, qint64 pieceSize, int fileCount);

/// One row of the piece size's list (board 49 §5): 16 KiB to 64 MiB, each with the count and
/// the .torrent's size; `automatic` on the one qBittorrent would choose.
struct PieceRow
{
    qint64 size = 0;
    qint64 count = 0;
    qint64 torrentBytes = 0;
    bool automatic = false;
};
QList<PieceRow> pieceRows(qint64 totalBytes, int fileCount);

/// What a folder holds, read before anything is hashed (CR17): qBittorrent's own words blame
/// "Ignore dotfiles" for a folder that is missing, unreadable or empty alike.
struct FolderCheck
{
    enum class State {
        Unknown,    ///< not asked yet, or the server didn't answer
        Missing,    ///< nothing at that path
        Unreadable, ///< there, but it can't be listed (or a file in it read)
        Empty,      ///< nothing to share
        OnlyHidden, ///< only hidden files, which are left out
        Ok
    };
    State state = State::Unknown;
    /// The path is one file: a one-file torrent (C27 CR3).
    bool isFile = false;
    /// What this machine read (true), or only the names qBittorrent listed (false: no sizes).
    bool sized = false;
    int files = 0;
    qint64 bytes = 0;
    /// Hidden files (a dot first) and their bytes; left out while ignoreDotfiles is on.
    int hidden = 0;
    qint64 hiddenBytes = 0;
    QStringList hiddenNames;
    /// The first file that couldn't be read ("" for none).
    QString unreadableFile;
};

/// Reads `path` on this machine: every file under it with its size, hidden ones (a name
/// starting with a dot, or under a folder that does) counted apart. `ignoreDotfiles` decides
/// whether hidden files count toward what is shared.
FolderCheck checkLocal(const QString &path, bool ignoreDotfiles = true);
/// From the server's own list (app/getDirectoryContent, names only, one level): `found` false
/// for its 404; `names` the full paths it listed.
FolderCheck fromListing(bool found, const QStringList &names, bool ignoreDotfiles = true);

/// The private .torrent's file name (C27 CR15): "NAME [HOST].torrent", with characters a file
/// name can't hold replaced.
QString savedName(const QString &name, const QString &host);
/// `fileName` in `dir`, or "NAME (2).torrent" and on while one is taken.
QString freeName(const QString &dir, const QString &fileName);
/// The torrent's name for a folder or file path: its last part.
QString nameOf(const QString &path);
/// The folder a made torrent is added at: the path's parent ("/" for a top folder).
QString parentOf(const QString &path);

/// While it hashes (CR13): seconds left from the % at `elapsedMs`, or -1 while there is too
/// little to tell (under 5 % or under 2 s).
qint64 secondsLeft(int percent, qint64 elapsedMs);

} // namespace omaseed::core::CreatePlan
