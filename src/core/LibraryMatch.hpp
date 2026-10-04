#pragma once

#include "core/TorrentFile.hpp"

#include <QList>
#include <QString>

namespace omaseed::core {

/// docs/25 §3 (#27): a `.torrent` being added whose files are already on disk under another
/// torrent of the library (the same release from another tracker: another infohash, the same
/// files). Saved where those files are, the new torrent checks them and seeds both swarms
/// from one copy.
///
/// Only exact matches count (D4 a): every file at the same relative path with the same size,
/// as the server lists the candidate's files (`torrents/files`, renames included). The root
/// folder is made to line up through the save path and the content layout:
///
/// - candidate in folder R, new torrent named R: its save path, layout "Original";
/// - candidate in folder R, new torrent named otherwise: save path + "/R", "NoSubfolder";
/// - candidate without a folder of its own: its save path, "NoSubfolder";
/// - single files: the same file name and size, its save path, "Original".
namespace LibraryMatch {

/// One library torrent, as the server describes it.
struct Candidate
{
    QString savePath;
    QString contentPath;
    /// `torrents/files`: each file's `name` (relative to the save path, '/' separated) and
    /// `size`.
    QList<TorrentFileEntry> files;
};

struct Result
{
    bool exact = false;
    QString savePath; ///< where to save the new torrent
    QString layout;   ///< "Original" | "NoSubfolder"
};

/// The new torrent (`name`, `multiFile`, `files` as TorrentFile parses them) against one
/// candidate.
Result match(const QString &name,
             bool multiFile,
             const QList<TorrentFileEntry> &files,
             const Candidate &candidate);

} // namespace LibraryMatch

} // namespace omaseed::core
