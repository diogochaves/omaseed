#pragma once

#include "core/TorrentFile.hpp"

#include <QList>
#include <QString>

#include <optional>

namespace omaseed::core {

/// DECISIONS C47 I5: what kind of file each file of a torrent is, for the file pane's chips,
/// its `kind` column and sort, and the kind filter. The table is board 59's A1
/// (docs/59-the-add-sheets-own.html §3), taken alone: board 59 itself is **not** ruled, so its
/// program check, guessed category and magnet checklist are not here.
///
/// Read from the paths alone, case ignored:
/// - extras first: whatever TorrentExtras::find counts (NFOs, checksums, samples, adverts,
///   stray images). Programs stay among them as TorrentExtras counts them (a top-level `.exe`
///   or `.lnk` is an advert); a program deeper in has no kind of its own and is Other.
/// - then 59 A1's extensions: video, audio, subtitles, images, books, archives (a split set,
///   `.r00`…, `.partN.rar`, `.001`…, is archives too), disc images.
/// - anything else, or no extension: Other.
namespace FileKinds {

/// 59 A1's order, extras after the content kinds, other last: the kind sort follows it.
enum class Kind { Video, Audio, Subtitles, Images, Books, Archives, DiscImages, Extras, Other };

/// Every kind, in the table's order.
QList<Kind> all();

/// The kind's name, the one QML and the kind filter use: "video", "audio", "subtitles",
/// "images", "books", "archives", "discImages", "extras", "other".
QString name(Kind kind);
/// The kind named `name`, or nothing for a name that is none of the above.
std::optional<Kind> fromName(const QString &name);

/// 59 A1's word, one or many: "video" / "videos", "audio file" / "audio files",
/// "subtitle" / "subtitles", "disc image" / "disc images", "other file" / "other files"…
QString word(Kind kind, bool plural);

/// The kind of each of `files` (paths below the torrent's folder, '/' separated), in order.
QList<Kind> classify(const QList<TorrentFileEntry> &files);

/// How many of `kind` the torrent has: a split archive set (`x.rar` with `x.r00`…,
/// `x.part1.rar`…, `x.7z.001`… in one folder) counts as one archive; every other kind counts
/// its files. `kinds` is classify(files).
int units(Kind kind, const QList<TorrentFileEntry> &files, const QList<Kind> &kinds);

} // namespace FileKinds
} // namespace omaseed::core
