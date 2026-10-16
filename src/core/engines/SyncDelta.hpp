#pragma once

// SyncDelta — one poll's worth of change, in omaseed's terms: the contract between an engine
// and SyncEngine (docs/36 §4.2, docs/DECISIONS.md C14).
//
// A full delta replaces everything the models hold; a partial one merges. qBittorrent's
// sync/maindata converts 1:1 (QbtTranslate::syncDelta), so it keeps its deltas and its cost;
// an engine without deltas will come through a differ that produces the same shape.

#include "core/engines/TorrentPatch.hpp"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

/// A category, or what changed in one: a partial delta may leave the save path out.
struct CategoryPatch
{
    QString name;
    std::optional<QString> savePath;

    bool operator==(const CategoryPatch &other) const = default;
};

/// The server-wide figures GlobalState shows. Each is absent when the poll did not carry it.
struct ServerStatePatch
{
    std::optional<qint64> dlSpeed;   ///< bytes/s, every torrent together
    std::optional<qint64> upSpeed;   ///< bytes/s
    std::optional<qint64> dlSession; ///< bytes downloaded this session
    std::optional<qint64> upSession; ///< bytes uploaded this session
    std::optional<qint64> freeSpaceOnDisk;
    std::optional<bool> altSpeedLimits; ///< the alternative limits are on
    std::optional<QString> connectionStatus;
    std::optional<int> dhtNodes;
    std::optional<int> peerConnections;
    std::optional<qint64> dlRateLimit; ///< bytes/s in force, 0 for none
    std::optional<qint64> upRateLimit;
    std::optional<bool> queueing;
    std::optional<int> refreshInterval; ///< ms the server suggests between polls
    std::optional<qint64> allTimeUpload;
    std::optional<qint64> allTimeDownload;
    /// The all-time ratio; -1 when the server sent something that is not a number.
    std::optional<double> globalRatio;

    bool operator==(const ServerStatePatch &other) const = default;
};

struct SyncDelta
{
    /// Everything is listed: what a full delta leaves out is gone.
    bool full = false;

    /// In the order the engine listed them (new rows are appended in that order).
    QList<TorrentPatch> torrents;
    QStringList removedTorrents;

    QList<CategoryPatch> categories;
    QStringList removedCategories;

    QStringList tags; ///< added (or, in a full delta, all of them)
    QStringList removedTags;

    /// Announce URL → the torrents announcing to it. In a partial delta only the URLs whose
    /// list moved; an empty list drops the URL.
    QHash<QString, QStringList> trackers;
    QStringList removedTrackers;

    std::optional<ServerStatePatch> serverState;

    /// The engine's own sync cursor (qBittorrent's rid), when it has one.
    std::optional<int> cursor;
};

} // namespace omaseed::core
