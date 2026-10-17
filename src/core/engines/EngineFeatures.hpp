#pragma once

// EngineFeatures — what one server can do, worked out from its engine and version after the
// probe, never from a fixed list per engine (docs/36 §4.3, docs/DECISIONS.md C14 E2). It
// carries values as well as yes or no: how torrents are grouped, which file priorities and
// share-limit actions exist. Whatever it says a server lacks is hidden wherever it would be
// offered (C14 E4); for qBittorrent at current versions everything is on.
//
// qBittorrent's record comes from QbtTranslate::features().

#include <QList>
#include <QStringList>

namespace omaseed::core {

struct EngineFeatures
{
    // How torrents are grouped (C14 E5).
    bool categories = false;        ///< one group per torrent
    bool categorySavePaths = false; ///< a category carries its own save path
    bool tags = false;              ///< any number of labels per torrent

    /// The file priorities a file can be given (FilePriority values), lowest first.
    QList<int> filePriorities;
    /// What can happen when a share limit is reached, by name ("Default", "Stop", …);
    /// empty when the server cannot be told.
    QStringList shareLimitActions;

    bool queue = false;      ///< a download queue with positions
    bool forceStart = false; ///< start past the queue
    bool sequentialDownload = false;
    bool firstLastPiece = false; ///< first and last pieces first
    bool superSeeding = false;
    bool autoManagement = false;   ///< automatic torrent management (categories move data)
    bool fetchMetadata = false;    ///< a magnet's files before the add (C5)
    bool addOptions = false;       ///< the add's content layout, skip check and rename
    bool peerControl = false;      ///< add a peer to a torrent, ban one from all
    bool editWebSeeds = false;     ///< add and remove a torrent's HTTP sources
    bool exportTorrent = false;    ///< hand back a torrent's .torrent file
    bool seedingTimeLimit = false; ///< a limit on total seeding time (ratio and idle aside)
    bool engineLog = false;        ///< the engine's own log: the log face, the problem log
    /// qBittorrent's own surfaces, reached through its engine (docs/36 §4.4, C14 E6, E7):
    /// Settings › qBittorrent with its settings export and import, the torrents export and
    /// import (C9's file holds qBittorrent's rows), RSS feeds and rules.
    bool qbtSettings = false;
    bool torrentsBackup = false;
    bool rss = false;

    bool shareLimitAction() const { return !shareLimitActions.isEmpty(); }

    bool operator==(const EngineFeatures &other) const = default;
};

} // namespace omaseed::core
