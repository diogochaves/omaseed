#pragma once

// Every omaseed-core type QML touches, put into the `Omaseed` module's type registry.
//
// None of these is ever created from QML — they all hang off the `App` singleton — so each
// one is QML_UNCREATABLE. Registering them anyway is what lets qmllint and qmlls type-check
// `App.filtered.count`, `App.counts.downloading` or `TorrentFilterModel.Seeding` instead of
// treating them as untyped `var`, and it is what makes the enums spellable from QML at all.
//
// ---------------------------------------------------------------------------------------
// Role names a delegate over `App.torrents` or `App.filtered` gets (TorrentModel::Role, see
// TorrentModel::roleNames()). Every one is `required property <type> <name>` material:
//
//   hash            string    the infohash, the key of everything
//   name            string    torrent name (Name column)
//   category        string    "" when uncategorised (dimmed after the name)
//   tags            list      QStringList
//   tagsText        string    "private · keep"
//   size            qint64    bytes of the selected files
//   sizeText        string    "1.31 GB" (Size column)
//   totalSize       qint64    bytes of every file
//   progress        double    0…1
//   progressText    string    "48.9%" (Progress column)
//   progressPercent double    0…100, for a bar's width
//   state           enum      omaseed::core::TorrentState
//   stateText       string    "downloading" (Status column)
//   stateRole       string    the Theme colour role, e.g. "stateDownloading"
//   statusGroups    int       omaseed::core::StatusGroups flags
//   dlspeed         qint64    bytes/s          dlspeedText  string "1.84 MB/s"
//   upspeed         qint64    bytes/s          upspeedText  string "112 KB/s"
//   eta             qint64    seconds          etaText      string "2m 40s"
//                                              etaLongText  string "2 min 40 s"
//   numSeeds        int       connected seeds  numComplete  int seeds in the swarm
//   numLeechs       int       connected peers  numIncomplete int peers in the swarm
//   seedsPeersText  string    "21 / 42" (S / P column)
//   ratio           double                     ratioText    string "1.87"
//   savePath        string                     contentPath  string
//   addedOn         date                       addedOnText  string "added today 10:41"
//   completionOn    date                       completionOnText string
//   tracker         string    announce URL in use
//   trackerHost     string    "tracker.example.org" or "trackerless"
//   trackersCount   int
//   hasTrackerError bool
//   isPrivate       bool      the private flag (docs/16 §5: the marker after the category)
//   priority        int       queue position   seqDl bool   firstLastPiecePrio bool
//   dlLimit         qint64    upLimit qint64   ratioLimit double  seedingTimeLimit qint64
//   shareLimitAction string   Default | Stop | Remove | RemoveWithContent | EnableSuperSeeding
//   availability    double    amountLeft qint64  completed qint64
//   downloaded      qint64    uploaded qint64
//   timeActive      qint64    seconds          timeActiveText string
//   seedingTime     qint64    seconds          seedingTimeText string
//   infohashV1      string    infohashV2 string  magnetUri string
//   torrent         var       the whole value, for code that wants more than one field
//
// CategoryModel: name, savePath. TagModel: name.
// TrackerModel: host, urls, hashes, torrentCount, isTrackerless.
//
// ---------------------------------------------------------------------------------------
// The details pane (T3.4, spec §2 Fig. 1-4). `App.details` is a TorrentDetails; its three
// models are `App.details.files`, `.trackers` and `.peers`, and the General tab reads the
// ready strings of `App.details.properties` (a QVariantMap: name, savePath, hashV1, hashV2,
// totalSizeText, downloaded, uploaded, wasted, pieces, availability, seeds, peers,
// timeActive, shareLimit, createdBy, comment, createdOn - an empty value means the server
// sent nothing and the row is left out).
//
// TorrentFilesModel (visible rows of the file tree; folded folders drop their subtree):
//   name         string   leaf name, folders with a trailing "/"
//   path         string   full path inside the torrent (the `o` open action)
//   size         qint64   sizeText string "3.86 GB"   (a folder sums its files)
//   progress     double   progressText string "100 %" (a folder weights by size)
//   priority     int      0 skip, 1 normal, 6 high, 7 maximal, -1 mixed
//   priorityText string   "skip" / "normal" / "high" / "maximal" / "mixed"
//   isFolder     bool     depth int    expanded bool
//   fileIndexes  list     the API file ids under this row, for torrents/filePrio
//   availability double   -1 on a folder
//
// TorrentTrackersModel:
//   url string (full announce URL)      host string ("tracker.example.org", "DHT")
//   tier int      status int            statusText string ("working · 27 min")
//   healthy bool                        dotRole string ("green"/"yellow"/"dimForeground")
//   seeds int  seedsText string ("1 204" or an em dash)   peers int
//   leeches int  leechesText string   downloaded int   msg string   isPseudo bool
//
// TorrentPeersModel:
//   address string ("ip:port", the ban/copy key)   ip string   port int
//   country string  countryCode string  client string  connection string
//   dlSpeed qint64  dlSpeedText string "1.02 MB/s" upSpeed qint64  upSpeedText string
//   progress double  progressText string  flags string  flagsDesc string
//   downloaded qint64  uploaded qint64  relevance double
//
// TorrentWebSeedsModel (#10):
//   url string (the whole URL)   text string ("mirror.example/pub/": https:// dropped,
//   http:// kept)   host string
// ---------------------------------------------------------------------------------------

#include "core/AddFilesModel.hpp"
#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerPreferences.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TagModel.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "core/TrackerModel.hpp"

#include <QQmlEngine>

struct TorrentModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentModel)
    QML_NAMED_ELEMENT(TorrentModel)
    QML_UNCREATABLE("TorrentModel is App.torrents; it is not created from QML.")
};

/// The rail's status rows are this type's `StatusFilter` enum: `TorrentFilterModel.All`,
/// `.Downloading`, `.Seeding`, `.Completed`, `.Stopped`, `.Active`, `.Stalled`, `.Checking`,
/// `.Errored`, assigned to `App.filtered.statusFilter`.
struct TorrentFilterModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentFilterModel)
    QML_NAMED_ELEMENT(TorrentFilterModel)
    QML_UNCREATABLE("TorrentFilterModel is App.filtered; it is not created from QML.")
};

struct FilterCountsForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::FilterCounts)
    QML_NAMED_ELEMENT(FilterCounts)
    QML_UNCREATABLE("FilterCounts is App.counts; it is not created from QML.")
};

struct CategoryModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::CategoryModel)
    QML_NAMED_ELEMENT(CategoryModel)
    QML_UNCREATABLE("CategoryModel is App.categories; it is not created from QML.")
};

struct TagModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TagModel)
    QML_NAMED_ELEMENT(TagModel)
    QML_UNCREATABLE("TagModel is App.tags; it is not created from QML.")
};

struct TrackerModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TrackerModel)
    QML_NAMED_ELEMENT(TrackerModel)
    QML_UNCREATABLE("TrackerModel is App.trackers; it is not created from QML.")
};

struct GlobalStateForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::GlobalState)
    QML_NAMED_ELEMENT(GlobalState)
    QML_UNCREATABLE("GlobalState is App.globalState; it is not created from QML.")
};

struct ServerPreferencesForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::ServerPreferences)
    QML_NAMED_ELEMENT(ServerPreferences)
    QML_UNCREATABLE("ServerPreferences is App.serverPrefs; it is not created from QML.")
};

struct SyncEngineForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::SyncEngine)
    QML_NAMED_ELEMENT(SyncEngine)
    QML_UNCREATABLE("SyncEngine is App.engine; it is not created from QML.")
};

struct ServerStoreForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::ServerStore)
    QML_NAMED_ELEMENT(ServerStore)
    QML_UNCREATABLE("ServerStore is App.servers; it is not created from QML.")
};

/// The details pane's controller and its three tab models (T3.4). `App.details` owns them;
/// nothing here is created from QML.
struct TorrentDetailsForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentDetails)
    QML_NAMED_ELEMENT(TorrentDetails)
    QML_UNCREATABLE("TorrentDetails is App.details; it is not created from QML.")
};

struct TorrentFilesModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentFilesModel)
    QML_NAMED_ELEMENT(TorrentFilesModel)
    QML_UNCREATABLE("TorrentFilesModel is App.details.files; it is not created from QML.")
};

/// The add sheet's file picker (docs/11 D1): TorrentFilesModel's roles plus `wanted`
/// (0 every file under the row skipped, 1 some, 2 none).
struct AddFilesModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::AddFilesModel)
    QML_NAMED_ELEMENT(AddFilesModel)
    QML_UNCREATABLE("AddFilesModel is App.addFiles; it is not created from QML.")
};

struct TorrentTrackersModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentTrackersModel)
    QML_NAMED_ELEMENT(TorrentTrackersModel)
    QML_UNCREATABLE("TorrentTrackersModel is App.details.trackers; it is not created from QML.")
};

struct TorrentWebSeedsModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentWebSeedsModel)
    QML_NAMED_ELEMENT(TorrentWebSeedsModel)
    QML_UNCREATABLE("TorrentWebSeedsModel is App.details.webSeeds; it is not created from QML.")
};

struct TorrentPeersModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentPeersModel)
    QML_NAMED_ELEMENT(TorrentPeersModel)
    QML_UNCREATABLE("TorrentPeersModel is App.details.peers; it is not created from QML.")
};

/// The `omaseed::core` namespace enums, as the QML namespace `Torrent`. Both enums are
/// scoped and both have a `Downloading`, so QML spells the enum out:
///   `Torrent.TorrentState.StalledUpload` — the `state` role's value
///   `Torrent.StatusGroups.Errored`       — a flag of the `statusGroups` role
/// (the flag enum answers to its Q_DECLARE_FLAGS name, `StatusGroups`, not `StatusGroup`).
/// The flattened spellings `Torrent.StalledUpload` and `Torrent.Errored` work too, but the
/// qualified ones say which enum they mean, so views use those.
namespace TorrentEnumsForeign {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(omaseed::core)
QML_NAMED_ELEMENT(Torrent)
} // namespace TorrentEnumsForeign

// --- T4.5: the settings modal's server list -----------------------------------------------
// `App.serverList`, a `QAbstractListModel` over `App.servers` (ServerStore). Role names a
// delegate over it gets:
//
//   id              string  the stable server id, the key of everything (secrets included)
//   name            string  "home"
//   url             string  "http://127.0.0.1:8080"
//   urlText         string  "127.0.0.1:8080" — host:port for the dim line
//   authMode        string  "apiKey" | "cookie" | "none"
//   authModeText    string  "API key" | "cookie" | "none"
//   username        string  "" unless authMode is "cookie"
//   isDefault       bool    the store's defaultId points here
//   pathMapText     string  "/downloads → /data/torrents", "" when there is none
//   reachable       bool    the last App.testServer() verdict; false until one ran
//   reachableText   string  "yes · 3 ms" / "no · connection refused"; "" until one ran
//   torrentCount    int     -1 until a test counted them
//   lastError       string  the last failed test's message
//   version         string  "5.2.3" from the last successful test; "" until one ran
//   apiVersion      string  "2.15.1" likewise
//   versionText     string  "v5.2.3" for the dim line; "" until a test ran (Q23)
#include "core/ServerListModel.hpp"

struct ServerListModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::ServerListModel)
    QML_NAMED_ELEMENT(ServerListModel)
    QML_UNCREATABLE("ServerListModel is App.serverList; it is not created from QML.")
};

// ---------------------------------------------------------------------------------------
// T4.2, the add flow (spec §3 Fig. 5 and Fig. 10). Nothing new is registered here; these are
// the shapes the `App` singleton's add methods pass through QML, documented next to the
// model roles because `Views/AddSheet.qml` reads them the same way a delegate reads a role.
//
// `App.parseAddable(text)` -> QVariantMap, and the same map again through
// `App.addableParsed(map)` once a `.torrent` has been read and hashed on the thread pool
// (a file that cannot be read answers there too, with `error`):
//   kind         string   "magnet" | "url" | "file" | "none"
//   valid        bool     false when nothing usable was found (see `error`)
//   pending      bool     true while a `.torrent` is still being read and hashed
//   name         string   the magnet's `dn`, the torrent's name, a URL's last path segment
//   sizeText     string   "2.34 GB"; empty when the source does not say
//   infoHashV1   string   lowercase hex; infoHashV2 string, for a v2/hybrid torrent
//   value        string   what goes to the server (the magnet, the URL, the file path)
//   token        string   the key `App.addTorrent` uses to find a picked file's bytes
//   error        string   why `valid` is false
//   fileCount    int      a `.torrent`'s number of files (its tree: `App.loadAddFiles(token)`)
//
// `App.addTorrent(request)` takes that map plus the sheet's rows: `category` string, `tags`
// list<string>, `savePath` string, `autoTMM` bool, `contentLayout` "Original" | "Subfolder" |
// "NoSubfolder", `upLimitKb` and `dlLimitKb` ints (0 = unlimited), `sequentialDownload`,
// `skipChecking`, `firstLastPiecePrio` and `stopped` bools, `rename` string (empty keeps the
// torrent's own name) and, for a `.torrent`, `filePriorities` (one per file id). It answers
// with `App.addFinished(ok, message, hash)`, `hash` being the id the server keys the torrent
// by (its v2 hash cut to 40 characters when it has one, else its v1 hash).
//
// `App.findDuplicate(hashV1, hashV2)` -> `App.torrentRow`'s map for the torrent already in
// the library, or an empty map.
