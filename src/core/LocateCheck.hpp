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
#include <QStringList>

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

/// `do remove`'s guard (C50 GT1): which of `files` are under `localFolder` at all, whatever
/// their size, skipped ones too (`found` of `files`; `firstMissing` stays the first absent).
/// Any found means the torrent still has files there.
Result present(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat);

/// The mount points this machine has now (/proc/self/mountinfo) and the ones /etc/fstab
/// names, for `unmountedDrive`.
struct Mounts
{
    QStringList mounted;
    QStringList listed;
};

/// Read from the two files (a test hands its own copies).
Mounts readMounts(const QString &mountinfo = QStringLiteral("/proc/self/mountinfo"),
                  const QString &fstab = QStringLiteral("/etc/fstab"));

/// Whether a local path exists (a file or a folder).
using Exists = std::function<bool(const QString &localPath)>;
bool existsLocal(const QString &localPath);

/// `do remove`'s guard (C50 GT1): "files gone" must mean gone, not a drive unplugged. When
/// `localFolder` is missing and the drive that should hold it isn't mounted, the drive's
/// folder ("/run/media/<user>/SHELF"); "" when the folder is there or its drive is mounted.
/// The rule (assumed in the build, C50 "Built for"):
/// - the folder is at or under an /etc/fstab target that isn't mounted; or
/// - it is under /run/media, /media or /mnt, and the nearest folder of it that exists is
///   not on a filesystem mounted under that root (an empty mount folder, or the top folder
///   gone entirely, as udisks leaves it). A drive mounted there with the folder missing on it
///   is files gone.
/// The drive's folder is the root's next component (two under /run/media, and under /media
/// when the first is `user`).
QString unmountedDrive(const QString &localFolder,
                       const Mounts &mounts,
                       const Exists &exists,
                       const QString &user);

/// QFileInfo on this machine's disk.
std::optional<qint64> statLocal(const QString &localPath);

/// A file as the person reads it: its path without the torrent's own top folder, when every
/// file of it sits under that one folder ("Lecture 08.mkv" for "Lectures/Lecture 08.mkv").
QString shownName(const QList<ContentFile> &files, const QString &path);

} // namespace omaseed::core::LocateCheck
