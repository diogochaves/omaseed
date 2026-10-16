#pragma once

// What the details pane shows about one torrent, in omaseed's terms (docs/36 §4.1): the
// General tab's figures, the content files, the trackers, the peers (as a delta: qBittorrent
// sends them that way), the web seeds and the pieces. Each engine's translator fills these;
// the details models never read an engine's replies.

#include "core/engines/TorrentPatch.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace omaseed::core {

/// A file's priority. The values are qBittorrent's (the reference), which App still sends as
/// they are; other engines map theirs onto these four.
enum class FilePriority {
    Mixed = -1,
    Skip = 0,
    Normal = 1,
    High = 6,
    Maximal = 7,
};

/// How a tracker is doing (the values are qBittorrent's, WebUI API v2.15).
enum class TrackerStatus {
    Disabled = 0,     ///< the pseudo-trackers when the feature is off
    NotContacted = 1, ///< the tracker has not been reached yet
    Working = 2,
    Updating = 3,
    NotWorking = 4,
    TrackerError = 5, ///< qBittorrent 5.0+: the tracker answered with an error
    Unreachable = 6,  ///< qBittorrent 5.0+: the tracker could not be reached
};

/// NotWorking, TrackerError and Unreachable: the three ways an announce can fail.
constexpr bool isTrackerFailure(int status)
{
    return status == int(TrackerStatus::NotWorking) || status == int(TrackerStatus::TrackerError) ||
           status == int(TrackerStatus::Unreachable);
}

/// The General tab's figures (spec §2 Fig. 1, docs/12 G2). A figure the server did not give
/// is empty, and the view leaves its row out.
struct TorrentProperties
{
    QString hash;
    QString name;
    QString savePath;
    QString infohashV1;
    QString infohashV2;
    QString createdBy;
    QString comment;
    qint64 totalSize = 0;
    qint64 totalDownloaded = 0;
    qint64 totalUploaded = 0;
    qint64 totalWasted = 0;
    int piecesNum = 0;
    qint64 pieceSize = 0;
    int piecesHave = 0;
    std::optional<double> availability; ///< distributed copies; empty when there is no figure
    int seeds = 0;
    int seedsTotal = 0;
    int peers = 0;
    int peersTotal = 0;
    qint64 timeElapsed = 0; ///< seconds active
    QDateTime createdOn;    ///< invalid when unknown
    std::optional<int> connections;
    int connectionsLimit = 0;
    /// When a complete copy was last seen: empty when the server does not say, an invalid
    /// date when it says never.
    std::optional<QDateTime> lastSeen;
    qint64 seedingTime = 0; ///< seconds
    QDateTime completedOn;  ///< invalid while incomplete
    std::optional<qint64> dlSpeedAvg;
    std::optional<qint64> upSpeedAvg;
    qint64 reannounce = 0;         ///< seconds to the next announce
    std::optional<bool> isPrivate; ///< empty until the metadata says

    bool operator==(const TorrentProperties &other) const = default;
};

/// One file of a torrent's content (spec §2 Fig. 2).
struct ContentFile
{
    std::optional<int> index; ///< the engine's id for it; its position when absent
    QString path;             ///< relative to the save path, '/' separated
    qint64 size = 0;
    double progress = 0.0;
    std::optional<int> priority;               ///< a FilePriority; absent keeps the one it had
    std::optional<double> availability;        ///< empty when the server has no figure
    std::optional<std::pair<int, int>> pieces; ///< first and last piece

    bool operator==(const ContentFile &other) const = default;
};

/// Where a tracker row comes from: a real announce URL, or one of the engine's own peer
/// sources, which some engines list among the trackers.
enum class TrackerSource { Announce, Dht, Pex, Lsd };

/// One row of the Trackers tab (spec §2 Fig. 3).
struct TrackerEntry
{
    QString url; ///< as the engine shows it, also for the peer sources
    TrackerSource source = TrackerSource::Announce;
    int tier = -1;
    int status = 1; ///< a TrackerStatus
    std::optional<int> seeds;
    std::optional<int> peers;
    std::optional<int> leeches;
    std::optional<int> downloaded;
    std::optional<qint64> nextAnnounce; ///< seconds since the epoch
    QString message;

    bool operator==(const TrackerEntry &other) const = default;
};

/// What changed about one peer; a partial delta may carry only a field or two.
struct PeerPatch
{
    QString address; ///< "ip:port", the peer's identity
    std::optional<QString> ip;
    std::optional<int> port;
    std::optional<QString> country;
    std::optional<QString> countryCode;
    std::optional<QString> client;
    std::optional<QString> connection;
    std::optional<QString> flags;
    std::optional<QString> flagsDesc;
    std::optional<qint64> dlSpeed;
    std::optional<qint64> upSpeed;
    std::optional<qint64> downloaded;
    std::optional<qint64> uploaded;
    std::optional<double> progress;
    std::optional<double> relevance;

    bool operator==(const PeerPatch &other) const = default;
};

/// One answer about a torrent's peers: everything (full) or what moved.
struct PeersDelta
{
    bool full = false;
    QList<PeerPatch> peers;
    QStringList removed;
    std::optional<int> cursor; ///< the engine's sync cursor (qBittorrent's rid)
};

/// Everything the pane shows at once: `--demo details` and the tests fill it in one go.
struct TorrentDetailsData
{
    QString hash;
    TorrentProperties properties;
    QList<ContentFile> files;
    QList<TrackerEntry> trackers;
    PeersDelta peers;
    QStringList webSeeds;
    QByteArray pieceStates; ///< one PieceStates::State per piece
};

} // namespace omaseed::core
