#pragma once

#include "core/PathMap.hpp"

#include <QHash>
#include <QPair>
#include <QString>

#include <functional>

namespace omaseed::core {

/// What this machine reads for one folder: the drive under it, found from the folder or
/// (when it does not exist yet) its nearest existing parent.
struct StorageReading
{
    bool valid = false;  ///< something on the way up exists and could be measured
    bool exists = false; ///< the folder itself exists (not only a parent)
    qint64 bytesFree = 0;
    qint64 bytesTotal = 0;
    QString rootPath; ///< the drive's mount point, e.g. "/mnt/media"
    /// `st_dev` of the folder (or its nearest existing parent); 0 when it couldn't be read. Two
    /// btrfs subvolumes share a mount point but not a device, and rename(2) between them
    /// copies (#240), so "the same drive" compares this before rootPath.
    quint64 device = 0;
    /// QStorageInfo::fileSystemType(): the driver, not the format ("ntfs3", "ntfs", "exfat",
    /// "vfat", "ext4", "btrfs"), as #116 read it. DECISIONS C17 D8 decides by it.
    QString fileSystem;
};

/// Which filesystems make a tick cost a file's full size at once (DECISIONS C17 D8–D9, as #116
/// measured them). Wanting a file makes libtorrent create it at its full length; on `ntfs3`,
/// `exfat` and `vfat` that allocates it all, and skipping it later keeps it. The newer in-kernel
/// `ntfs`, ext4 and btrfs keep it sparse. Types nobody measured (`fuseblk`, xfs, network mounts)
/// get no line: omaseed won't guess.
namespace FileSystems {

/// `ntfs3`, `exfat` or `vfat`: every ticked file takes its full size at once.
bool allocatesInFull(const QString &fileSystem);
/// FAT32 (`vfat`) can't hold a file of 4 GiB or more (D9).
bool isFat32(const QString &fileSystem);
/// The smallest file FAT32 refuses: 4 GiB.
constexpr qint64 kFat32TooBig = 4LL * 1024 * 1024 * 1024;

} // namespace FileSystems

/// Measures a local folder. Replaceable so tests never depend on this machine's disks.
using StorageReader = std::function<StorageReading(const QString &localPath)>;

/// How much room the add sheet's Save to has (docs/25 §4, #29). Three sources, in order:
///
/// - Save to empty, or the server's default save path (or under it while this machine
///   cannot read the server's disks): `free_space_on_disk`, which qBittorrent measures on its
///   default save path. Exact for every server, remote ones too.
/// - Any other folder, when this machine sees the server's disks: read here, on the folder
///   the path map turns it into. "Sees" is checked, not assumed: the default save path must
///   exist here (after the map) and read within 1 % (or 256 MiB) of the server's own figure.
///   A qBittorrent in Docker on 127.0.0.1 fails that, since `/downloads` is not a folder here.
/// - Otherwise: unknown (docs/25 D8 a, the caller shows the default folder's figure for scale).
class FreeSpaceProbe
{
public:
    enum class Source {
        Default, ///< the server's own figure
        Local,   ///< read on this machine
        Server,  ///< read on the server for that folder (EngineClient::freeSpace, C25 K23)
        Unknown,
    };

    struct Answer
    {
        Source source = Source::Unknown;
        qint64 bytesFree = 0;
        qint64 bytesTotal = 0; ///< 0 when only the free figure is known (Default)
        QString where;         ///< the mount point for Local, empty otherwise
        quint64 device = 0;    ///< StorageReading::device for Local, 0 otherwise
        /// The drive's filesystem (StorageReading::fileSystem) wherever this machine reads
        /// it: Local, and Default while it sees the server's disks. Empty otherwise: the API
        /// doesn't say, and omaseed won't guess (DECISIONS C17 D8).
        QString fileSystem;
    };

    explicit FreeSpaceProbe(StorageReader reader = readLocal);

    /// The server side, read again whenever any of it may have changed. `pathMap` is used
    /// only when `mapEnabled`; with mappings, a path none of them covers is unknown.
    void setServer(const QString &defaultSavePath,
                   qint64 serverBytesFree,
                   const PathMap &pathMap,
                   bool mapEnabled);

    /// Free space the server read for folders, by folder as it writes them (an engine that
    /// reads any folder's: EngineFeatures::roomOnServer). Where this machine can't see the
    /// server's disks, a folder read so is answered as Server, `where` the folder it was read
    /// on. The value is {bytes free, the folder read}.
    void setServerReadings(const QHash<QString, QPair<qint64, QString>> &readings)
    {
        m_serverReadings = readings;
    }
    /// Whether local readings are trusted for this server (the calibration above).
    bool seesServerDisks() const { return m_seesServerDisks; }
    QString defaultSavePath() const { return m_defaultSavePath; }

    Answer answer(const QString &savePath) const;

    /// The folder on this machine for `serverPath`, when local readings are trusted
    /// (seesServerDisks()) and the path map, if it has mappings, covers it; "" otherwise.
    /// The rename guard (#64) reads the server's files through it.
    QString localPathFor(const QString &serverPath) const;

    /// QStorageInfo and stat(2) on `localPath`, or on its nearest existing parent.
    static StorageReading readLocal(const QString &localPath);

    /// Whether a move between two folders is a rename (#240): both read on this machine, on
    /// one device when both devices are known, else under one mount point.
    static bool sameDrive(const Answer &a, const Answer &b);

private:
    /// The local folder for a server path, or "" when the path map says it is not reachable.
    QString localFor(const QString &serverPath) const;

    StorageReader m_reader;
    QString m_defaultSavePath;
    qint64 m_serverBytesFree = 0;
    PathMap m_pathMap;
    bool m_mapEnabled = false;
    bool m_seesServerDisks = false;
    QHash<QString, QPair<qint64, QString>> m_serverReadings;
};

} // namespace omaseed::core
