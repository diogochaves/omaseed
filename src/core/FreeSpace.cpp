#include "core/FreeSpace.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>
#include <cstdlib>

namespace omaseed::core {

namespace {

/// The calibration's slack: free space moves while torrents write, and the two figures are
/// read a moment apart.
constexpr qint64 kCalibrationFloor = 256LL * 1024 * 1024;

QString normalize(const QString &path)
{
    QString result = path.trimmed();
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

/// `path` is `base` or a folder under it, at a path-component boundary.
bool isUnder(const QString &path, const QString &base)
{
    if (base.isEmpty() || !path.startsWith(base))
        return false;
    if (path.size() == base.size() || base.endsWith(QLatin1Char('/')))
        return true;
    return path.at(base.size()) == QLatin1Char('/');
}

} // namespace

bool FileSystems::allocatesInFull(const QString &fileSystem)
{
    return fileSystem == QLatin1String("ntfs3") || fileSystem == QLatin1String("exfat") ||
           fileSystem == QLatin1String("vfat");
}

bool FileSystems::isFat32(const QString &fileSystem)
{
    return fileSystem == QLatin1String("vfat");
}

FreeSpaceProbe::FreeSpaceProbe(StorageReader reader)
    : m_reader(std::move(reader))
{}

StorageReading FreeSpaceProbe::readLocal(const QString &localPath)
{
    StorageReading reading;
    if (localPath.isEmpty() || !QDir::isAbsolutePath(localPath))
        return reading;

    QString probe = QDir::cleanPath(localPath);
    reading.exists = QFileInfo(probe).isDir();
    // A folder the add will create does not exist yet: its drive is its nearest parent's.
    while (!QFileInfo::exists(probe)) {
        const QString parent = QFileInfo(probe).path();
        if (parent == probe)
            return reading;
        probe = parent;
    }
    const QStorageInfo storage(probe);
    if (!storage.isValid() || !storage.isReady())
        return reading;
    reading.valid = true;
    reading.bytesFree = storage.bytesAvailable();
    reading.bytesTotal = storage.bytesTotal();
    reading.rootPath = storage.rootPath();
    reading.fileSystem = QString::fromUtf8(storage.fileSystemType());
    return reading;
}

void FreeSpaceProbe::setServer(const QString &defaultSavePath,
                               qint64 serverBytesFree,
                               const PathMap &pathMap,
                               bool mapEnabled)
{
    m_defaultSavePath = normalize(defaultSavePath);
    m_serverBytesFree = serverBytesFree;
    m_pathMap = pathMap;
    m_mapEnabled = mapEnabled;

    m_seesServerDisks = false;
    if (m_defaultSavePath.isEmpty() || serverBytesFree <= 0)
        return;
    const QString local = localFor(m_defaultSavePath);
    if (local.isEmpty())
        return;
    const StorageReading reading = m_reader(local);
    if (!reading.valid || !reading.exists)
        return;
    const qint64 slack = std::max(kCalibrationFloor, serverBytesFree / 100);
    m_seesServerDisks = std::llabs(reading.bytesFree - serverBytesFree) <= slack;
}

QString FreeSpaceProbe::localFor(const QString &serverPath) const
{
    if (m_mapEnabled && m_pathMap.count() > 0) {
        if (!m_pathMap.covers(serverPath))
            return QString();
        return m_pathMap.toLocal(serverPath);
    }
    return serverPath;
}

QString FreeSpaceProbe::localPathFor(const QString &serverPath) const
{
    const QString path = normalize(serverPath);
    if (!m_seesServerDisks || path.isEmpty())
        return QString();
    return localFor(path);
}

FreeSpaceProbe::Answer FreeSpaceProbe::answer(const QString &savePath) const
{
    const QString path = normalize(savePath);
    const bool isDefault = path.isEmpty() || path == m_defaultSavePath;

    Answer server;
    server.source = Source::Default;
    server.bytesFree = m_serverBytesFree;

    if (isDefault) {
        if (m_serverBytesFree <= 0)
            return Answer();
        // The server's own figure, and the drive's type when this machine sees it (C17 D8).
        if (m_seesServerDisks) {
            const QString local = localFor(m_defaultSavePath);
            if (!local.isEmpty())
                server.fileSystem = m_reader(local).fileSystem;
        }
        return server;
    }

    if (m_seesServerDisks) {
        const QString local = localFor(path);
        if (!local.isEmpty()) {
            const StorageReading reading = m_reader(local);
            if (reading.valid) {
                Answer answer;
                answer.source = Source::Local;
                answer.bytesFree = reading.bytesFree;
                answer.bytesTotal = reading.bytesTotal;
                answer.where = reading.rootPath;
                answer.fileSystem = reading.fileSystem;
                return answer;
            }
        }
        return Answer();
    }
    // Remote: a folder under the default one is on its drive (a mount below it is rare
    // enough not to second-guess the server's own figure).
    if (m_serverBytesFree > 0 && isUnder(path, m_defaultSavePath))
        return server;
    return Answer();
}

} // namespace omaseed::core
