#pragma once

// PromisedRoom — the room downloads in progress will still take, by drive (docs/DECISIONS.md
// C47 I10). The add's free line takes it from free for its colour and says it
// ("Z of it still coming for N downloads"), so an add doesn't look as if it fits on a drive
// that torrents already downloading will fill.

#include "core/Torrent.hpp"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>

namespace omaseed::core {

namespace PromisedRoom {

struct Promise
{
    qint64 bytes = 0; ///< what they still have to download, summed
    int count = 0;    ///< how many torrents

    bool operator==(const Promise &other) const = default;
};

/// Whether `torrent` still writes new data: downloading or queued to download (stalled too),
/// not stopped, not complete, with bytes left.
bool isPromising(const Torrent &torrent);

/// Where it writes: its incomplete folder while it has one, else its save path.
QString writesTo(const Torrent &torrent);

/// Every promising torrent's `amountLeft`, summed per drive. `deviceOf` names a server path's
/// drive (FreeSpaceProbe's local reading's `device`); 0 (not seen) leaves it out: omaseed
/// says nothing where it can't tell the drive (I10).
QHash<quint64, Promise> byDevice(const QList<Torrent> &torrents,
                                 const std::function<quint64(const QString &serverPath)> &deviceOf);

} // namespace PromisedRoom

} // namespace omaseed::core
