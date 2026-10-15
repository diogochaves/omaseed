#pragma once

#include "core/TorrentFile.hpp"

#include <QList>

namespace omaseed::core {

/// #63: which files of a torrent are "extras": the NFO, checksums and text that come with a
/// release, sample clips, and a tracker's adverts. Read from the file list alone (names and
/// sizes, no download, no network), so the add sheet can say a torrent carries them. Nothing is
/// unticked because of it: a seeding client keeps the whole torrent (docs/27 D2).
///
/// What counts, by #63's rules, case ignored:
/// - Info: `.nfo`, `.txt`, `.sfv`, `.md5`, `.srr`, at any depth.
/// - Sample: a file or folder named `sample` or `samples` (a file's name without its extension),
///   or a file named `*-sample.*`.
/// - Advert: `.url`, `.website`, `.html`, `.lnk`, `.exe` at the top level (directly in the
///   torrent's folder), where trackers put them.
/// - Image: `.jpg`, `.jpeg`, `.png`, only when the torrent is not an image set: fewer than 5 of
///   them, together less than 1 % of the torrent's size.
/// A file is counted once, under the first of those that fits. A torrent of one file has no
/// extras (that file is the torrent), and neither has one where every file would be one (a
/// collection of text files is its own content).
namespace TorrentExtras {

enum class Kind { Info, Sample, Advert, Image };

struct Extra
{
    int index = -1; ///< the file id: its position in the list, qBittorrent's numbering
    Kind kind = Kind::Info;

    bool operator==(const Extra &other) const = default;
};

/// The extras among `files` (TorrentFileEntry paths: below the torrent's folder, '/'
/// separated), in file-id order.
QList<Extra> find(const QList<TorrentFileEntry> &files);

} // namespace TorrentExtras
} // namespace omaseed::core
