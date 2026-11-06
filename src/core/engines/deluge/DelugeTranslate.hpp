#pragma once

// DelugeTranslate — the one place that reads Deluge 2.2's answers (through deluge-web's /json)
// into omaseed's engine-neutral types, and spells omaseed's values in Deluge's words on the way
// out (docs/36 §4, DECISIONS C14 E2 a, C25, issue #87). Status keys are Deluge's own
// (`core.get_torrent_status`); the tests read answers recorded from a real daemon
// (tests/fixtures/deluge/, scripts/dev/deluge-fixtures.py).
//
// The wire's sentinels end here: `progress` in percent becomes a fraction, an eta of 0 is "no
// estimate", a ratio of -1 none yet, speeds and limits in KiB/s (1024 bytes; -1 none) become
// bytes/s, a date of 0 is no date, `time_since_transfer` -1 is "never", file priorities
// 0 · 1–3 · 4 · 5–7 become Skip · Low · Normal · High, `queue` -1 is not queued. A torrent
// without metadata (a magnet) has no pieces (`num_pieces` 0). Deluge's labels are the Label
// plugin's, one per torrent, lower case: they go in the categories place (C25 B), never in
// the tags place.

#include "core/engines/DetailsData.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/EngineFeatures.hpp"
#include "core/engines/ServerInfo.hpp"
#include "core/engines/SyncDelta.hpp"
#include "core/engines/TorrentMetadata.hpp"
#include "core/engines/TorrentPatch.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace omaseed::core::DelugeTranslate {

/// What one torrent's translation needs besides its own keys.
struct Context
{
    /// The Label plugin is on: `label` is read (off, a stale `label` deluge-web still sends
    /// is ignored).
    bool labelPlugin = false;
    /// Seconds since the epoch, for `time_since_transfer`.
    qint64 nowSecs = 0;
    /// Where its content starts below its folder: its top folder, or its one file (from its
    /// `files`; contentRoot()). Empty when not known yet: the content path is left out.
    QString contentRoot;
};

/// The keys the poll asks web.update_ui for: one row of the list (TorrentPatch). `trackers`
/// and `files` are left out (a list of dictionaries each, every poll): the engine asks for
/// them now and then and keeps what they say (DelugeEngine).
QStringList listFields();
/// The General tab's (properties()), the Files tab's, the Trackers tab's, the Peers tab's and
/// the pieces' keys.
QStringList propertiesFields();
QStringList filesFields();
QStringList trackersFields();
QStringList peersFields();
QStringList piecesFields();
/// core.get_session_status' keys the poll reads (the session's totals).
QStringList sessionStatusKeys();
/// core.get_config_values' keys for the global limits and the settings the speed surfaces
/// read (sessionPreferences).
QStringList configKeys();

/// web.update_ui's answer, taken apart.
struct Poll
{
    bool connected = false; ///< deluge-web has a daemon
    /// The torrents, each with its hash put in as `hash` (the answer keys them by it).
    QList<QJsonObject> torrents;
    /// The Label plugin is on: its filter is in the tree.
    bool labelPlugin = false;
    /// Its labels, from the filter tree (every one, those on no torrent too), sorted.
    QStringList labels;
    QJsonObject stats;
};
Poll poll(const QJsonValue &updateUi);
/// A result keyed by hash (core.get_torrents_status) as rows with `hash` put in.
QList<QJsonObject> rows(const QJsonValue &byHash);

/// `state`, `is_finished`, the rates, `num_pieces` and `is_auto_managed` as a phase and flags;
/// the word is Deluge's own state.
TorrentStatus status(const QJsonObject &torrent);
/// One torrent, every key of listFields() it carries.
TorrentPatch torrent(const QJsonObject &json, const Context &context = {});
/// What `files` says the content starts with below the save path: the top folder of a
/// torrent whose files all sit in one, else its one file's path. Empty without files.
QString contentRoot(const QJsonObject &json);
/// Its trackers' announce URLs (`trackers`, or the one in use when that isn't there), for
/// the rail's tracker map.
QStringList trackerUrls(const QJsonObject &json);
/// The torrent is stopped, by its state word: Paused, or Error, which a resume doesn't clear
/// (stoppedAmong, C20; engine matrix §4.4). Its `paused` key also holds for a queued torrent
/// (libtorrent pauses those) and not for a failed move after a resume, so it isn't read.
bool isStopped(const QJsonObject &torrent);

/// web.update_ui's stats and core.get_session_status' answer as the figures GlobalState
/// shows. Deluge keeps no all-time totals (K34): those are left out.
ServerStatePatch serverState(const QJsonObject &stats, const QJsonObject &sessionStatus);
/// The server's speed limits from core.get_config_values; Deluge has no alternative ones.
GlobalLimits globalLimits(const QJsonObject &config);
/// The same config as the preferences omaseed's speed surfaces and K31's Basics read
/// (EngineClient::sessionPreferences): dl_limit, up_limit (bytes/s, 0 none), save_path
/// (download_location), listen_port (the first of listen_ports), dht, max_ratio_enabled,
/// max_ratio and max_ratio_act (stop_seed_at_ratio, stop_seed_ratio, remove_seed_at_ratio:
/// 0 stop, 1 remove). No alt_* and no scheduler (K20). Settings › Deluge (E6) reads the rest
/// of core.get_config by Deluge's own names (utpex, lsd, max_active_seeding…, -1 for no
/// limit), `encryption` (forced · enabled · disabled, the outgoing policy) and
/// `enabled_plugins` (sorted, read only; K15). A key whose setting is missing is left out.
QVariantMap speedPreferences(const QJsonObject &config);
/// What core.set_config takes for `changes` (keys as speedPreferences writes them; `encryption`
/// sets both ways). Empty, with `why` saying so, when a key has no place in it (the plugins
/// among them: Deluge's own preferences turn those on or off, K15) or a value is out of range.
QJsonObject configSet(const QVariantMap &changes, QString *why = nullptr);

/// The General tab: a torrent of propertiesFields().
TorrentProperties properties(const QJsonObject &json);
/// The Files tab: `files` with `file_priorities`, `file_progress` and `piece_length`.
QList<ContentFile> files(const QJsonObject &json);
/// A Deluge file priority as a FilePriority: 0 Skip, 1–3 Low, 4 Normal, 5–7 High (C25 K17).
int filePriority(int delugePriority);
/// A FilePriority in Deluge's numbers: Skip 0, Low 1, Normal 4, High 7; Maximal is High (its
/// highest), Mixed Normal.
int delugePriority(FilePriority priority);
/// The Trackers tab: each tracker of `trackers` by URL and tier. Deluge reports only the one
/// in use (`tracker_status`): that row carries it, the others read not contacted (board 47 H).
QList<TrackerEntry> trackers(const QJsonObject &json);
/// The Peers tab: Deluge sends the whole list each time.
PeersDelta peers(const QJsonObject &json);
/// `pieces` (0 missing, 1 missing but a peer has it, 2 downloading, 3 have; null while seeding
/// or before the metadata) as PieceStates.
QByteArray pieceStates(const QJsonObject &json);

/// One label and its options (label.get_options): its path is the folder it moves finished
/// torrents to, only while it does (`apply_move_completed` and `move_completed`).
CategoryPatch category(const QString &label, const QJsonObject &options);
/// A label as Deluge keeps it: lower case, only a–z, 0–9, `_` and `-` (the Label plugin's
/// check). Empty, with `why` saying so, when it can't be one.
QString labelName(const QString &name, QString *why = nullptr);

/// core.prefetch_magnet_metadata's answer, `[hash, base64 of the bencoded info dictionary]`
/// (scripts/dev/research/87-deluge-prefetch): the metadata, with the magnet's own trackers.
/// Empty when the answer holds none yet (`[hash, ""]`, the call's timeout passed) or holds
/// something that doesn't read as an info dictionary (`*unreadable` then says so).
std::optional<TorrentMetadata>
metadata(const QJsonValue &answer, const QString &magnet, bool *unreadable = nullptr);

/// What a Deluge 2.1 or later can do, by board 47's A–K table (DECISIONS C25), with the
/// Label plugin on or off.
EngineFeatures features(const ServerInfo &info, bool labelPlugin);

// --- the way out ----------------------------------------------------------------------------

/// Bytes/s in Deluge's KiB/s; 0 (none) is -1.
double speedKiB(qint64 bytesPerSec);
/// `trackers` (as set_torrent_trackers takes them: url and tier) with `urls` added, each in a
/// tier of its own after the others; those already listed are left as they are.
QJsonArray trackersWith(const QJsonArray &trackers, const QStringList &urls);
/// `trackers` without `urls`.
QJsonArray trackersWithout(const QJsonArray &trackers, const QStringList &urls);

} // namespace omaseed::core::DelugeTranslate
