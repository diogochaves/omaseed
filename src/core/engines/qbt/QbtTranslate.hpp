#pragma once

// QbtTranslate — the one place that turns qBittorrent's WebUI API v2 replies into omaseed's
// engine-neutral types (docs/36 §4, docs/DECISIONS.md C14). Nothing behind it reads
// qBittorrent's JSON: SyncEngine gets a SyncDelta, the details models get their typed rows,
// and the demo data comes through here too.
//
// The wire's sentinels end here: an ETA of 8640000 is "no estimate", a ratio of 9999 is
// infinite, -2 and -1 share limits are the server's default and none, file priorities
// 0/1/6/7 and piece states 0/1/2 become named values, and the `** [DHT] **` rows of the
// Trackers tab become peer sources. A key qBittorrent did not send stays absent.

#include "core/engines/DetailsData.hpp"
#include "core/engines/EngineFeatures.hpp"
#include "core/engines/LogEntry.hpp"
#include "core/engines/SyncDelta.hpp"
#include "core/engines/TorrentMetadata.hpp"
#include "core/engines/TorrentPatch.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringView>

namespace omaseed::core::QbtTranslate {

/// A `state` word ("stalledUP") as a phase and flags, the word kept. Unknown words give an
/// Unknown phase.
TorrentStatus status(QStringView word);

/// One object of the `torrents` map (or of torrents/info), every key it carries.
TorrentPatch torrent(const QString &hash, const QJsonObject &json);
/// The `torrents` map of sync/maindata, in the map's (hash) order.
QList<TorrentPatch> torrents(const QJsonObject &map);
/// The `server_state` object.
ServerStatePatch serverState(const QJsonObject &json);
/// The `categories` map.
QList<CategoryPatch> categories(const QJsonObject &map);
/// The `trackers` map: announce URL → hashes.
QHash<QString, QStringList> trackerMap(const QJsonObject &map);
/// A whole sync/maindata answer.
SyncDelta syncDelta(const QJsonObject &maindata);

/// torrents/properties.
TorrentProperties properties(const QJsonObject &json);
/// torrents/files.
QList<ContentFile> files(const QJsonArray &array);
/// A file priority on the wire as a FilePriority; nothing for a value that is not one.
std::optional<int> filePriority(int wire);
/// torrents/trackers, the DHT, PeX and LSD rows included as peer sources. qBittorrent 5.x
/// sends next_announce as a time; older servers sent the seconds left, counted from
/// `nowSecs` (seconds since the epoch; now when absent).
QList<TrackerEntry> trackers(const QJsonArray &array, std::optional<qint64> nowSecs = {});
/// sync/torrentPeers.
PeersDelta peers(const QJsonObject &answer);
/// torrents/webseeds: the URLs.
QStringList webSeeds(const QJsonArray &array);
/// torrents/pieceStates: one PieceStates::State per piece; an unknown value is missing.
QByteArray pieceStates(const QJsonArray &array);
/// log/main.
QList<LogEntry> log(const QJsonArray &array);
/// `{hash, properties, files, trackers, peers, pieceStates, webSeeds}`, each as its endpoint
/// answers: the demo's and the tests' details.
TorrentDetailsData details(const QJsonObject &json);

/// torrents/fetchMetadata's 200: the metadata as the add sheet needs it; nothing when it
/// holds no usable `info`.
std::optional<TorrentMetadata> metadata(const QJsonObject &body);

/// What a qBittorrent with this Web API version ("2.15.1") can do.
EngineFeatures features(const QString &apiVersion);

} // namespace omaseed::core::QbtTranslate
