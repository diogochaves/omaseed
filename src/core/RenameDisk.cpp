#include "core/RenameDisk.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace omaseed::core {

RenameDisk::RenameDisk(const QString &localBase, const QStringList &ownPaths)
    : m_base(QDir::cleanPath(localBase))
    , m_own(ownPaths.cbegin(), ownPaths.cend())
{
    // Only an absolute folder of this machine can be read; anything else sees nothing.
    if (localBase.isEmpty() || !QDir::isAbsolutePath(m_base))
        m_base.clear();
}

QString RenameDisk::localOf(const QString &path) const
{
    return m_base.endsWith(QLatin1Char('/')) ? m_base + path : m_base + QLatin1Char('/') + path;
}

bool RenameDisk::fileTaken(const QString &path) const
{
    if (!sees() || path.isEmpty() || m_own.contains(path))
        return false;
    // exists() follows links, so a broken one reads as nothing there: rename() would still
    // replace it.
    const QFileInfo info(localOf(path));
    return info.exists() || info.isSymLink();
}

bool RenameDisk::folderTaken(const QString &folder) const
{
    if (!sees() || folder.isEmpty())
        return false;
    const QString local = localOf(folder);
    const QFileInfo info(local);
    if (!info.exists() && !info.isSymLink())
        return false;
    if (info.isSymLink() || !info.isDir())
        return true;
    // Folders are walked, never followed through a link; every other entry is a file the
    // torrent's files could land on.
    QDirIterator it(local,
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir root(local);
    while (it.hasNext()) {
        const QFileInfo entry = it.nextFileInfo();
        if (entry.isDir() && !entry.isSymLink())
            continue;
        if (!m_own.contains(folder + QLatin1Char('/') + root.relativeFilePath(entry.filePath())))
            return true;
    }
    return false;
}

} // namespace omaseed::core
