#pragma once

// TransmissionTranslate — the one place that reads Transmission 4.1's JSON-RPC answers into
// omaseed's engine-neutral types, and spells omaseed's values in Transmission's words on the
// way out (docs/36 §4, DECISIONS C14 E2 a, issue #85). Field names are 4.1's snake_case
// (rpc-spec.md at 4.1.3); the tests read answers recorded from a real daemon
// (tests/fixtures/transmission/, scripts/dev/transmission-fixtures.py).
//
// The wire's sentinels end here: an eta of -1 or -2 is "no estimate", a ratio of -1 none yet and
// -2 infinite, seed_ratio_mode / seed_idle_mode 0 · 1 · 2 the server's default · the torrent's
// own · none, speeds in kB/s (`units.speed_bytes`, 1000) become bytes/s, a date of 0 is no date,
// file priorities -1 · 0 · 1 plus `wanted` become Low · Normal · High and Skip. Transmission's
// integer ids stay inside the engine; everything here is keyed by `hash_string`.

#include "core/engines/DetailsData.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/EngineFeatures.hpp"
#include "core/engines/ServerInfo.hpp"
#include "core/engines/SyncDelta.hpp"
#include "core/engines/TorrentPatch.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace omaseed::core::TransmissionTranslate {

/// What the session says that a torrent's translation needs (session_get).
struct Session
{
    qint64 speedBytes = 1000; ///< bytes in a "kB" of a speed (`units.speed_bytes`)
    bool queueing = true;     ///< a download or a seed queue is on
    /// Where unfinished torrents sit, when `incomplete_dir_enabled`; Transmission has no
    /// per-torrent field for it (engine matrix §4.3).
    QString incompleteDir;
};

/// The session_get fields the poll asks for: the units, the queues, the incomplete folder,
/// the speed limits and turtle mode.
QStringList sessionFields();
Session session(const QJsonObject &sessionGet);

/// The torrent_get fields one row of the list needs (TorrentPatch, the tracker map).
QStringList listFields();
/// The General tab's (properties()), the Files tab's, the Trackers tab's, the Peers tab's,
/// the web seeds' and the pieces' fields.
QStringList propertiesFields();
QStringList filesFields();
QStringList trackersFields();
QStringList peersFields();
QStringList webSeedsFields();
QStringList piecesFields();

/// torrent_get's `torrents`, as objects whether it came as objects or as a table.
QList<QJsonObject> rows(const QJsonObject &result);

/// `status`, `error`, `metadata_percent_complete`, `left_until_done`, the rates and the peer
/// counts as a phase and flags; the word is Transmission's own name for the status.
TorrentStatus status(const QJsonObject &torrent);
/// One torrent, every field of listFields() it carries.
TorrentPatch torrent(const QJsonObject &json, const Session &session = {});
/// Its trackers' announce URLs (the `trackers` field), for the rail's tracker map.
QStringList trackerUrls(const QJsonObject &json);

/// session_stats, the session_get of sessionFields() and free_space's answer (when the poll
/// asked) as the figures GlobalState shows.
ServerStatePatch serverState(const QJsonObject &stats,
                             const QJsonObject &sessionGet,
                             const std::optional<QJsonObject> &freeSpace = std::nullopt);
/// The speed limits and turtle mode's, from the same session_get.
GlobalLimits globalLimits(const QJsonObject &sessionGet);
/// The same session_get as the preferences omaseed's speed surfaces read (EngineClient::
/// sessionPreferences, C25 K20): dl_limit, up_limit, alt_dl_limit, alt_up_limit in bytes/s
/// (0 none), scheduler_enabled, schedule_from_hour/min and schedule_to_hour/min from
/// alt_speed_time_*, scheduler_day_mask (alt_speed_time_day, AltSchedule's Transmission rule)
/// and save_path (download_dir). A key whose field is missing is left out.
QVariantMap speedPreferences(const QJsonObject &sessionGet);
/// What session_set takes for `changes` (keys as speedPreferences writes them), given the
/// session as it is (`sessionGet`, for the half of a time not changed and the units). Empty,
/// with `why` saying so, when a key has no place in Transmission's session.
QJsonObject
speedSessionSet(const QVariantMap &changes, const QJsonObject &sessionGet, QString *why = nullptr);

/// The General tab: a torrent of propertiesFields(). `nowSecs` (seconds since the epoch)
/// counts down to the next announce.
TorrentProperties properties(const QJsonObject &json, qint64 nowSecs);
/// The Files tab: `files` with `file_stats`.
QList<ContentFile> files(const QJsonObject &json);
/// A file's priority as a FilePriority: not wanted is Skip, -1 · 0 · 1 Low · Normal · High.
int filePriority(bool wanted, int priority);
/// The Trackers tab: `tracker_stats`, one row per tracker (no DHT/PeX/LSD rows: Transmission
/// lists none).
QList<TrackerEntry> trackers(const QJsonObject &json);
/// The Peers tab: Transmission sends the whole list each time.
PeersDelta peers(const QJsonObject &json);
QStringList webSeeds(const QJsonObject &json);
/// `pieces` (a base64 bitfield of `piece_count` bits) as PieceStates; Transmission says
/// nothing of pieces in flight.
QByteArray pieceStates(const QJsonObject &json);

/// What a Transmission 4.1 can do, by board 47's A–K table (DECISIONS C25).
EngineFeatures features(const ServerInfo &info);

// --- the way out ----------------------------------------------------------------------------

/// Bytes/s in the daemon's kB/s, rounded up so a small limit stays a limit.
qint64 speedUnits(qint64 bytesPerSec, qint64 speedBytes = 1000);
/// `tracker_list` (one URL per line, a blank line between tiers) with `urls` added, each in
/// a tier of its own after the others; those already listed are left as they are.
QString trackerListWith(const QString &list, const QStringList &urls);
/// `tracker_list` without `urls`; a tier left empty goes too.
QString trackerListWithout(const QString &list, const QStringList &urls);

} // namespace omaseed::core::TransmissionTranslate
