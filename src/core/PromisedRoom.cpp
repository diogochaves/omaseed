#include "core/PromisedRoom.hpp"

namespace omaseed::core::PromisedRoom {

bool isPromising(const Torrent &torrent)
{
    if (torrent.amountLeft <= 0)
        return false;
    if (torrent.status.has(TorrentFlag::Finished))
        return false;
    switch (torrent.status.phase) {
    case TorrentPhase::Downloading: // stalled and queued are flags on it
    case TorrentPhase::Allocating:
        return true;
    default:
        return false;
    }
}

QString writesTo(const Torrent &torrent)
{
    return torrent.downloadPath.isEmpty() ? torrent.savePath : torrent.downloadPath;
}

QHash<quint64, Promise> byDevice(const QList<Torrent> &torrents,
                                 const std::function<quint64(const QString &serverPath)> &deviceOf)
{
    QHash<quint64, Promise> out;
    QHash<QString, quint64> seen; // one reading per folder
    for (const Torrent &torrent : torrents) {
        if (!isPromising(torrent))
            continue;
        const QString path = writesTo(torrent);
        if (path.isEmpty())
            continue;
        auto known = seen.constFind(path);
        if (known == seen.cend())
            known = seen.insert(path, deviceOf(path));
        if (*known == 0)
            continue;
        Promise &promise = out[*known];
        promise.bytes += torrent.amountLeft;
        ++promise.count;
    }
    return out;
}

} // namespace omaseed::core::PromisedRoom
