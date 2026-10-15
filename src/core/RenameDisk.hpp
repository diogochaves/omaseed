#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// What a rename would land on, on disk (#64). qBittorrent checks a new name only against the
/// torrent's own files (`409 The file already exists`); libtorrent's rename is a plain
/// rename(), so a file of that name that the torrent does not own is replaced without a word,
/// and a folder renamed onto an existing one takes over the files in it (upstream#85).
///
/// So a target that is on disk and is not one of the torrent's own files is taken. This can
/// only be read where this machine sees the server's disks (App asks FreeSpaceProbe, #29);
/// elsewhere a default-made RenameDisk sees nothing and never calls anything taken.
class RenameDisk
{
public:
    /// Sees nothing: nothing is ever taken.
    RenameDisk() = default;
    /// `localBase` is the folder on this machine the torrent's file names are relative to
    /// (its save path, or its download path while it downloads there), `ownPaths` every
    /// file of the torrent, as torrents/files names them ('/' separated).
    RenameDisk(const QString &localBase, const QStringList &ownPaths);

    bool sees() const { return !m_base.isEmpty(); }

    /// A file renamed to `path` would replace something that is not the torrent's: anything
    /// at that path (a file, a folder, a link, even a broken one) other than its own files.
    bool fileTaken(const QString &path) const;
    /// A folder renamed to `folder` would land on something that is not the torrent's: a
    /// file or a link of that name, or a folder holding any file the torrent does not own.
    /// An empty folder is free (qBittorrent only fills it).
    bool folderTaken(const QString &folder) const;

private:
    QString localOf(const QString &path) const;

    QString m_base;
    QSet<QString> m_own;
};

} // namespace omaseed::core
