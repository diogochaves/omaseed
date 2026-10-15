#pragma once

#include "core/PathMap.hpp"

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
};

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
        Unknown,
    };

    struct Answer
    {
        Source source = Source::Unknown;
        qint64 bytesFree = 0;
        qint64 bytesTotal = 0; ///< 0 when only the free figure is known (Default)
        QString where;         ///< the mount point for Local, empty otherwise
    };

    explicit FreeSpaceProbe(StorageReader reader = readLocal);

    /// The server side, read again whenever any of it may have changed. `pathMap` is used
    /// only when `mapEnabled`; with mappings, a path none of them covers is unknown.
    void setServer(const QString &defaultSavePath,
                   qint64 serverBytesFree,
                   const PathMap &pathMap,
                   bool mapEnabled);

    /// Whether local readings are trusted for this server (the calibration above).
    bool seesServerDisks() const { return m_seesServerDisks; }
    QString defaultSavePath() const { return m_defaultSavePath; }

    Answer answer(const QString &savePath) const;

    /// The folder on this machine for `serverPath`, when local readings are trusted
    /// (seesServerDisks()) and the path map, if it has mappings, covers it; "" otherwise.
    /// The rename guard (#64) reads the server's files through it.
    QString localPathFor(const QString &serverPath) const;

    /// QStorageInfo on `localPath`, or on its nearest existing parent.
    static StorageReading readLocal(const QString &localPath);

private:
    /// The local folder for a server path, or "" when the path map says it is not reachable.
    QString localFor(const QString &serverPath) const;

    StorageReader m_reader;
    QString m_defaultSavePath;
    qint64 m_serverBytesFree = 0;
    PathMap m_pathMap;
    bool m_mapEnabled = false;
    bool m_seesServerDisks = false;
};

} // namespace omaseed::core
