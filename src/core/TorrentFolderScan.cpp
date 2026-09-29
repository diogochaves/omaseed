#include "core/TorrentFolderScan.hpp"

#include "core/TorrentFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace omaseed::core {

QList<FolderTorrent> TorrentFolderScan::scan(const QString &folder, int limit)
{
    QList<FolderTorrent> found;
    if (folder.isEmpty() || limit <= 0)
        return found;

    // QDir's name filters ignore case, so "Film.TORRENT" counts too. Newest first.
    const QDir dir(folder);
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.torrent")}, QDir::Files | QDir::Readable, QDir::Time);
    for (const QFileInfo &info : files) {
        if (found.size() >= limit)
            break;
        if (info.size() <= 0 || info.size() > maxFileSize)
            continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const TorrentFile parsed = TorrentFile::parse(file.readAll());
        if (!parsed.isValid())
            continue;

        FolderTorrent torrent;
        torrent.path = info.absoluteFilePath();
        torrent.name = parsed.name.isEmpty() ? info.completeBaseName() : parsed.name;
        torrent.size = parsed.totalSize;
        torrent.modified = info.lastModified();
        torrent.infoHashV1 = parsed.infoHashV1;
        torrent.infoHashV2 = parsed.infoHashV2;
        found.append(torrent);
    }
    return found;
}

} // namespace omaseed::core
