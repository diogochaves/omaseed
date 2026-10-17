#pragma once

// A torrent's metadata as the add sheet needs it, looked up by the engine before the add
// (docs/29, C5): the files with the root folder stripped (TorrentFileEntry's own convention,
// so AddFilesModel and LibraryMatch take them as a .torrent's), in the engine's file order.

#include "core/TorrentFile.hpp"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

struct TorrentMetadata
{
    QString torrentId; ///< the id the library keys the torrent by
    QString infoHashV1;
    QString infoHashV2;
    QString name;
    qint64 totalSize = 0;
    qint64 pieceLength = 0;
    bool isPrivate = false;
    /// The files sit in a folder named `name`.
    bool multiFile = false;
    QList<TorrentFileEntry> files;
    QStringList trackers;
};

/// One answer to a lookup: still looking, or done. `metadata` is empty when the engine said
/// it had the metadata but sent nothing usable.
struct FetchedMetadata
{
    bool ready = false;
    std::optional<TorrentMetadata> metadata;
};

} // namespace omaseed::core
