#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace omaseed::core {

/// One `.torrent` file found in a folder, parsed.
struct FolderTorrent
{
    /// The file's absolute path.
    QString path;
    /// The torrent's name from its metainfo, or the file's name without `.torrent` when the
    /// metainfo has none.
    QString name;
    /// The torrent's total size in bytes (what it downloads, not the .torrent file's size).
    qint64 size = 0;
    /// When the file arrived: its last modification time.
    QDateTime modified;
    /// Lowercase hex, as `TorrentFile` gives them; `infoHashV2` is empty for a v1 torrent.
    QString infoHashV1;
    QString infoHashV2;

    bool operator==(const FolderTorrent &other) const = default;
};

/// The add sheet's "in Downloads" (manual box M7): the newest `.torrent` files in one folder,
/// so a file the browser just saved is one click away. Not recursive. Files that cannot be
/// read or do not parse as a torrent are skipped and do not count towards the limit.
///
/// Reads and hashes files, so callers on the GUI thread run it on the thread pool.
class TorrentFolderScan
{
public:
    /// A `.torrent` larger than this is not read: real ones are kilobytes, a few megabytes at
    /// most, and the scan must not stall on something else wearing the suffix.
    static constexpr qint64 maxFileSize = 16 * 1024 * 1024;

    /// Up to `limit` torrents from `folder`, newest first. Empty when the folder is missing.
    static QList<FolderTorrent> scan(const QString &folder, int limit = 5);
};

} // namespace omaseed::core
