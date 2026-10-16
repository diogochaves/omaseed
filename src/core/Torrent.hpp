#pragma once

// Torrent — the value behind one row of the library, in omaseed's terms (docs/36 §4.1).
//
// Engines describe torrents in their own words; each engine's translator turns that into
// TorrentPatches (engines/TorrentPatch.hpp), and apply() merges one field by field and
// reports which fields really moved, which TorrentModel turns into the role list of a narrow
// dataChanged(). A full object and a partial diff go through the same path. Sentinels are
// gone by the time a value gets here: "no estimate" is an empty eta, an infinite ratio is
// infinity, "the server's default" is a LimitMode (docs/DECISIONS.md C14).
//
// The fields follow qBittorrent's model, which is the reference (docs/36 §4), renamed only
// where its spelling was a wire detail.

#include "core/engines/ShareLimits.hpp"
#include "core/engines/TorrentFields.hpp"
#include "core/engines/TorrentStatus.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

Q_NAMESPACE

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

/// The host the rail groups a tracker under: "tracker.opentrackr.org" for a URL, "DHT",
/// "PeX" or "LSD" for qBittorrent's pseudo-trackers, "trackerless" for an empty string.
QString trackerHost(const QString &trackerUrl);

/// The name of the bucket a torrent with no tracker at all falls into (Torrent::isTrackerless).
QString trackerlessKey();

struct TorrentPatch;

struct Torrent
{
    QString hash; ///< the info-hash that identifies it, never changed by apply()

    QString name;
    qint64 size = 0;      ///< bytes of the selected files
    qint64 totalSize = 0; ///< bytes of every file, -1 before the metadata arrives
    double progress = 0.0;
    TorrentStatus status;

    qint64 dlspeed = 0;
    qint64 upspeed = 0;
    std::optional<qint64> eta; ///< seconds; empty when there is no estimate

    int numSeeds = 0;      ///< seeds connected
    int numComplete = 0;   ///< seeds in the swarm
    int numLeechs = 0;     ///< peers connected
    int numIncomplete = 0; ///< peers in the swarm
    double ratio = 0.0;    ///< infinity when uploaded with nothing downloaded

    QString category;
    QStringList tags; ///< its labels, many per torrent
    QString savePath;
    QString contentPath;
    QString downloadPath; ///< the incomplete folder, when "keep incomplete in" is on

    QDateTime addedOn;
    QDateTime completionOn; ///< invalid while the torrent is incomplete
    QDateTime lastActivity; ///< the last time data moved, invalid when it never has

    QString tracker; ///< URL of the tracker currently in use, empty when there is none
    int trackersCount = 0;
    bool hasTrackerError = false; ///< qBittorrent 5.x reports this per torrent in maindata

    int priority = 0; ///< queue position, 0 when not queued, -1 when queueing is off
    bool seqDl = false;
    bool firstLastPiecePrio = false;
    bool superSeeding = false; ///< docs/12 G4: per-torrent super seeding
    bool autoTmm = false;      ///< docs/12 G4: automatic torrent management
    /// The torrent carries the private flag (maindata `private`, qBittorrent 5.0+): no DHT,
    /// PeX or LSD, its trackers only (docs/16 §5). A magnet without metadata yet has none.
    bool isPrivate = false;
    qint64 dlLimit = 0; ///< bytes/s, 0 for none
    qint64 upLimit = 0; ///< bytes/s, 0 for none
    RatioLimit ratioLimit;
    MinutesLimit seedingTimeLimit;
    /// Minutes without traffic before the share limit acts (qBittorrent 4.6, API 2.9.x).
    /// Older servers never send it, so it stays on the server's default.
    MinutesLimit inactiveSeedingTimeLimit;
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

    /// Merges the fields present in `patch` and returns the ones whose value changed.
    TorrentFields apply(const TorrentPatch &patch);

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
