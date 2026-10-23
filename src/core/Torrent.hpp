#pragma once

// Torrent — the value behind one row of sync/maindata's `torrents` map.
//
// The API sends the whole object on a full update and only the changed keys afterwards, so
// applyJson() merges key by key and reports which keys really moved; TorrentModel turns that
// list into the role list of a narrow dataChanged().
//
// Field names follow the JSON (snake_case) turned into camelCase; nothing here is renamed,
// so the mapping to the API docs stays obvious.

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace omaseed::core {

Q_NAMESPACE

/// Every `state` string qBittorrent 5.x can send, plus the pre-5.0 paused* spellings it
/// still emits when an older server is on the other end.
enum class TorrentState {
    Unknown,
    Downloading,
    StalledDownload,
    MetaDownload,
    ForcedMetaDownload, ///< "forcedMetaDL": a force-started magnet still fetching metadata
    ForcedDownload,
    QueuedDownload,
    CheckingDownload,
    Uploading,
    StalledUpload,
    ForcedUpload,
    QueuedUpload,
    CheckingUpload,
    StoppedDownload,
    StoppedUpload,
    PausedDownload,
    PausedUpload,
    Error,
    MissingFiles,
    CheckingResumeData,
    Moving,
    Allocating,
};
Q_ENUM_NS(TorrentState)

/// The rail's status groups (spec §1 Fig. 1). A torrent belongs to several at once, so this
/// is a flag set, not a single value.
enum class StatusGroup {
    NoGroup = 0x000,
    Downloading = 0x001,
    Seeding = 0x002,
    Completed = 0x004,
    Stopped = 0x008,
    Active = 0x010,
    Stalled = 0x020,
    Checking = 0x040,
    Errored = 0x080,
};
Q_DECLARE_FLAGS(StatusGroups, StatusGroup)
Q_FLAG_NS(StatusGroups)

/// The state string as the API spells it, e.g. "stalledUP"; "unknown" for Unknown.
QString torrentStateKey(TorrentState state);
/// The reverse; an unknown string gives TorrentState::Unknown.
TorrentState torrentStateFromKey(QStringView key);

/// The host the rail groups a tracker under: "tracker.opentrackr.org" for a URL, "DHT",
/// "PeX" or "LSD" for qBittorrent's pseudo-trackers, "trackerless" for an empty string.
QString trackerHost(const QString &trackerUrl);

/// The name of the bucket a torrent with no tracker at all falls into (Torrent::isTrackerless).
QString trackerlessKey();

struct Torrent
{
    QString hash; ///< the key of the torrents map, not a field of the object

    QString name;
    qint64 size = 0;      ///< bytes of the selected files
    qint64 totalSize = 0; ///< bytes of every file, -1 before the metadata arrives
    double progress = 0.0;
    TorrentState state = TorrentState::Unknown;

    qint64 dlspeed = 0;
    qint64 upspeed = 0;
    qint64 eta = 8640000; ///< seconds; 8640000 means "no estimate"

    int numSeeds = 0;      ///< seeds connected
    int numComplete = 0;   ///< seeds in the swarm
    int numLeechs = 0;     ///< peers connected
    int numIncomplete = 0; ///< peers in the swarm
    double ratio = 0.0;

    QString category;
    QStringList tags; ///< the comma list, split and trimmed
    QString savePath;
    QString contentPath;
    QString downloadPath; ///< the incomplete folder, when "keep incomplete in" is on

    QDateTime addedOn;
    QDateTime completionOn; ///< invalid while the torrent is incomplete
    QDateTime lastActivity; ///< the last time data moved, invalid when it never has

    QString tracker; ///< URL of the tracker currently in use, empty when there is none
    int trackersCount = 0;
    bool hasTrackerError = false; ///< qBittorrent 5.x reports this per torrent in maindata

    int priority = 0; ///< queue position, -1 when queueing is off
    bool seqDl = false;
    bool firstLastPiecePrio = false;
    bool superSeeding = false; ///< docs/12 G4: per-torrent super seeding
    bool autoTmm = false;      ///< docs/12 G4: automatic torrent management
    /// The torrent carries the private flag (maindata `private`, qBittorrent 5.0+): no DHT,
    /// PeX or LSD, its trackers only (docs/16 §5). A magnet without metadata yet has none.
    bool isPrivate = false;
    qint64 dlLimit = 0;
    qint64 upLimit = 0;
    double ratioLimit = -2.0;     ///< -2 global, -1 unlimited
    qint64 seedingTimeLimit = -2; ///< minutes; -2 global, -1 unlimited
    /// Minutes without traffic before the share limit acts (qBittorrent 4.6, API 2.9.x);
    /// -2 global, -1 unlimited. Older servers never send it, so it stays -2.
    qint64 inactiveSeedingTimeLimit = -2;
    /// What happens when a share limit is reached, as qBittorrent 5.2 (API 2.15.1) spells it:
    /// Default, Stop, Remove, RemoveWithContent or EnableSuperSeeding. Older servers never
    /// send the key, so the value stays "Default" (use the global setting).
    QString shareLimitAction = QStringLiteral("Default");

    double availability = 0.0;
    qint64 amountLeft = 0;
    qint64 completed = 0;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    qint64 uploadedSession = 0; ///< since qBittorrent started (the widget's top seeders)
    qint64 timeActive = 0;
    qint64 seedingTime = 0;

    QString infohashV1;
    QString infohashV2;
    QString magnetUri;

    /// A hybrid torrent (BEP 52 v1+v2): qBittorrent gives it both hashes. libtorrent 2.1.2
    /// errors on a recheck of one that it downloaded when a file is missing (DECISIONS C21).
    bool isHybrid() const { return !infohashV1.isEmpty() && !infohashV2.isEmpty(); }
    /// A v2-only torrent (BEP 52): a v2 hash and no v1. libtorrent 2.1.2 counts one short of
    /// 100 % for good after a recheck that found a skipped file's pieces gone (DECISIONS C29).
    bool isV2Only() const { return infohashV1.isEmpty() && !infohashV2.isEmpty(); }

    /// Merges the keys present in `json` and returns the JSON keys whose value changed.
    /// A full object and a partial diff go through the same path.
    QStringList applyJson(const QJsonObject &json);

    /// The rail groups this torrent shows up under.
    StatusGroups statusGroups() const;
    bool inGroup(StatusGroup group) const;

    /// One of downloading, seeding, stopped, stalled, checking, queued, error, missing files,
    /// completed, moving, metadata, unknown (spec §1 Status column).
    QString stateText() const;
    /// The Theme colour role for that state: stateDownloading, stateSeeding, stateStopped,
    /// stateStalled, stateChecking, stateQueued, stateError (plan §3).
    QString stateRole() const;
    /// "21 / 42" — connected seeds over connected peers (spec §1 S / P column).
    QString seedsPeersText() const;
    /// The rail bucket for this torrent's tracker.
    QString trackerHostKey() const;
    /// No tracker at all, as qBittorrent 5 counts it: `trackers_count == 0` (review Q11). A
    /// torrent whose trackers all fail is not trackerless; it stays under its hosts. The
    /// current `tracker` must also be empty, so a server that never sends `trackers_count`
    /// still leaves a torrent that is announcing somewhere out of the bucket.
    bool isTrackerless() const;
    /// True once the payload is on disk.
    bool isComplete() const;
    /// True while the ETA means something: an unfinished torrent that is downloading, or
    /// trying to (stalled reads ∞). Every other state shows an em dash, whatever `eta` says;
    /// an errored torrent keeps its last estimate.
    bool hasEta() const;

    bool operator==(const Torrent &other) const = default;
};

} // namespace omaseed::core

Q_DECLARE_OPERATORS_FOR_FLAGS(omaseed::core::StatusGroups)
Q_DECLARE_METATYPE(omaseed::core::Torrent)
