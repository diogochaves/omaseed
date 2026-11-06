#include "core/FreeSpace.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>
#include <cstdlib>

#include <sys/stat.h>

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
    struct stat st{};
    if (::stat(QFile::encodeName(probe).constData(), &st) == 0)
        reading.device = quint64(st.st_dev);
    return reading;
}

bool FreeSpaceProbe::sameDrive(const Answer &a, const Answer &b)
{
    if (a.source != Source::Local || b.source != Source::Local)
        return false;
    if (a.device != 0 && b.device != 0)
        return a.device == b.device;
    return !a.where.isEmpty() && a.where == b.where;
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
                answer.device = reading.device;
                answer.fileSystem = reading.fileSystem;
                return answer;
            }
        }
        return Answer();
    }
    // Remote: what the server read for that folder (K23); else a folder under the default
    // one is on its drive (a mount below it is rare enough not to second-guess the server's
    // own figure).
    const auto read = m_serverReadings.constFind(path);
    if (read != m_serverReadings.cend() && read->first >= 0) {
        Answer answer;
        answer.source = Source::Server;
        answer.bytesFree = read->first;
        answer.where = read->second.isEmpty() ? path : read->second;
        return answer;
    }
    if (m_serverBytesFree > 0 && isUnder(path, m_defaultSavePath))
        return server;
    return Answer();
}

} // namespace omaseed::core
