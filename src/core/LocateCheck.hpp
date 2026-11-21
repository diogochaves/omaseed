#pragma once

// LocateCheck — whether a torrent's files are in a folder (DECISIONS C50 GT2, `do locate`):
// each wanted file's path, relative to the save path as the torrent lists it, looked for under
// the folder (as this machine sees it), and its size compared. Pure: the disk is read through
// a callback, so a test hands it a table.
//
// What counts as found (PLAN Q9): a file the torrent wants (priority above 0, or none given)
// exists there; a complete one (progress 1) is exactly the torrent's size, one not complete
// yet is at most that size. A skipped file (priority 0) is not required.

#include "core/engines/DetailsData.hpp"

#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace omaseed::core::LocateCheck {

/// The size of the regular file at a local path; nullopt when nothing (or a folder) is there.
using Stat = std::function<std::optional<qint64>(const QString &localPath)>;

struct Result
{
    int found = 0;        ///< wanted files there, at a size that fits
    int files = 0;        ///< wanted files
    QString firstMissing; ///< the first one not found, as the torrent lists it; "" when none
    qint64 bytes = 0;     ///< the found files' sizes on disk

    bool all() const { return files > 0 && found == files; }
};

/// `files` looked for under `localFolder` (no trailing '/' needed).
Result check(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat);

/// QFileInfo on this machine's disk.
std::optional<qint64> statLocal(const QString &localPath);

/// A file as the person reads it: its path without the torrent's own top folder, when every
/// file of it sits under that one folder ("Lecture 08.mkv" for "Lectures/Lecture 08.mkv").
QString shownName(const QList<ContentFile> &files, const QString &path);

} // namespace omaseed::core::LocateCheck
