#include "core/PartialCopies.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace omaseed::core {

PartialCopies PartialCopies::read(const QString &localPath)
{
    PartialCopies copies;
    if (localPath.isEmpty())
        return copies;
    const QFileInfo info(localPath);
    if (!info.dir().exists())
        return copies;
    copies.seen = true;
    if (info.isFile()) {
        copies.files = 1;
        copies.bytes = info.size();
        return copies;
    }
    if (!info.isDir())
        return copies;
    QDirIterator it(
        localPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++copies.files;
        copies.bytes += it.fileInfo().size();
    }
    return copies;
}

} // namespace omaseed::core
