#pragma once

// BackupFormat — which of omaseed's three files a file is, from what is inside it (docs/34
// D8 a, #70).
//
// omaseed writes two tars, `.omaseed` (manifest format "omaseed-backup") and
// `.omaseed-torrents` ("omaseed-torrents"), and one JSON object, a qBittorrent's settings
// (format "omaseed-qbittorrent-settings"). A picker shows them by name, but the name never
// decides how a file is read: a tar is known by its manifest.json, a JSON file by its own
// "format". A renamed file still opens, and a file that only wears one of the names is
// refused by the reader its contents lead to.

#include <QString>

class QIODevice;

namespace omaseed::core::BackupFormat {

enum class Kind {
    Unknown,         ///< none of omaseed's files (or a damaged one)
    OmaseedBackup,   ///< OmaseedBackup's tar
    OmaseedTorrents, ///< TorrentsBackup's tar
    QbtSettings,     ///< QbtSettingsExport's JSON
};

/// What `device` (open, seekable, at any position) holds. Reads the tar's headers up to its
/// manifest, or a JSON file of at most 64 MiB whole; leaves the position where it was.
Kind detect(QIODevice *device);
/// The same for the file at `path`; Unknown when it cannot be opened.
Kind detectFile(const QString &path);

/// The manifest's (or the JSON object's) "format" for each kind; empty for Unknown.
QString formatName(Kind kind);

} // namespace omaseed::core::BackupFormat
