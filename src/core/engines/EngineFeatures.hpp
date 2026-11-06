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
    /// The many-per-torrent group is the engine's labels (Transmission, C25 K14): called
    /// "labels" wherever a person reads it, none exists without a torrent, no comma in one.
    bool tagsAreLabels = false;
    /// The one-per-torrent group is the engine's labels (Deluge's Label plugin, C25 B,
    /// K13–K15): called "labels" wherever a person reads it, lower case, the folder a label
    /// moves its torrents to when they finish as its path. With `categories` off the plugin
    /// is off: the section is gone and Settings says why in one line.
    bool categoriesAreLabels = false;

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
    bool autoManagement = false; ///< automatic torrent management (categories move data)
    bool fetchMetadata = false;  ///< a magnet's files before the add (C5)
    bool addOptions = false;     ///< the add's skip check and rename (with the layout below)
    /// The add's content layout (original, a subfolder, none); Deluge takes the add's other
    /// options but not this one (board 47 G, C25 K27).
    bool addContentLayout = false;
    bool peerControl = false; ///< add a peer to a torrent
    bool peerBan = false;     ///< ban a peer from every torrent (none on Deluge, board 47 H)
    /// The torrent's HTTP sources are reported (the web seeds' tab); Deluge has none (board 47
    /// H: the tab never appears).
    bool webSeeds = false;
    bool editWebSeeds = false; ///< add and remove a torrent's HTTP sources
    /// Only the tracker in use says how it is doing; the others are listed with their tiers
    /// alone (Deluge's tracker_status, board 47 H, C25 K29): the trackers tab says so once.
    bool trackerStatusInUseOnly = false;
    bool seedingTimeLimit = false; ///< a limit on total seeding time (ratio and idle aside)
    /// A limit on time without traffic (qBittorrent's inactive seeding time, Transmission's
    /// idle limit); Deluge keeps a ratio only (board 47 D, C25 K18).
    bool idleLimit = false;
    bool engineLog = false; ///< the engine's own log: the log face, the problem log
    /// The alternative speed limits, turtle mode and its clock (C4); none on Deluge (C25 K20).
    bool altSpeeds = false;
    /// Renaming a torrent renames its top folder (or its one file) on disk, not only the name
    /// shown: Transmission's torrent_rename_path (C25 K28). Renaming a file or folder inside
    /// it keeps it where it is there (a new name, never a new folder).
    bool renameOnDisk = false;
    /// A torrent known only by its v2 hash (a 64-character hash, a btmh-only magnet, a v2-only
    /// .torrent) can be added; Transmission finds torrents by their v1 hash (C25 K25, #147).
    bool v2Only = false;
    // Moves (board 47 §E, the engine matrix's §4.2; C25 K21–K23).
    /// The engine says how far one torrent's move is, wherever its disks are (Deluge's %).
    /// Without it, the growth probe measures the target when omaseed sees the disks (L10).
    bool moveProgress = false;
    /// A failed move puts the torrent in error and stops it, in the engine's own words: the
    /// failure is learnt from the torrent, not from a log (MoveFeed, board 47 §4).
    bool moveStopsOnFailure = false;
    /// The server lists a folder's names (EngineClient::folderContent): M8's check and the
    /// loose files' names where omaseed can't see the disks (C16 L13).
    bool listFolders = false;
    /// The engine answers nothing, polls included, until a move's copy ends (Transmission,
    /// C25 K22): one torrent is sent at a time, and a poll that times out meanwhile reads
    /// "busy moving", not a lost server.
    bool movesHoldAnswers = false;
    /// Free space is read on the server for any folder (EngineClient::freeSpace): a move's
    /// room is checked there where omaseed can't see the disks (K23; L13's "can't run here").
    bool roomOnServer = false;
    /// A move replaces a file already at a torrent's target (Transmission, K23, checked in
    /// board 47 §3); qBittorrent and Deluge keep it and call the move done (C15 M8).
    bool moveReplacesFile = false;
    /// qBittorrent's own surfaces, reached through its engine (docs/36 §4.4, C14 E6, E7):
    /// Settings › qBittorrent with its settings export and import, the torrents export and
    /// import (C9's file holds qBittorrent's rows), RSS feeds and rules.
    bool qbtSettings = false;
    /// The engine's settings, read and written through its session (EngineClient::
    /// sessionPreferences): Settings › ENGINE, its own sections (C25 K31, C14 E6; E5, #86).
    bool sessionSettings = false;
    bool torrentsBackup = false;
    bool rss = false;
    /// Remove a torrent keeping its files and add its own .torrent back with its settings
    /// (engines/qbt/AddAgainJob; C17 D3): what clears a hybrid's libtorrent error.
    bool addAgain = false;
    /// qBittorrent's search plugins (search/*; DECISIONS C26, #145): the search view and
    /// Settings › qBittorrent › Search plugins.
    bool search = false;
    /// qBittorrent's torrent creator (torrentcreator/*, Web API 2.10.4; DECISIONS C27, #146):
    /// "create a torrent" and its sheet.
    bool createTorrent = false;
    /// The largest speed limit the engine keeps, in bytes/s: past it qBittorrent wraps the
    /// value to "no limit" without a word (an int of bytes/s); Transmission keeps kB/s.
    qint64 maxSpeedLimit = 2147483647;

    bool shareLimitAction() const { return !shareLimitActions.isEmpty(); }

    bool operator==(const EngineFeatures &other) const = default;
};

} // namespace omaseed::core
