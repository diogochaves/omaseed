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
//   priority        int       queue position   seqDl bool   firstLastPiecePrio bool
//   dlLimit         qint64    upLimit qint64   ratioLimit double  seedingTimeLimit qint64
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
//   dlSpeed qint64  dlSpeedText string "1.02 M"    upSpeed qint64  upSpeedText string
//   progress double  progressText string  flags string  flagsDesc string
//   downloaded qint64  uploaded qint64  relevance double
// ---------------------------------------------------------------------------------------

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
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

struct TorrentTrackersModelForeign
{
    Q_GADGET
    QML_FOREIGN(omaseed::core::TorrentTrackersModel)
    QML_NAMED_ELEMENT(TorrentTrackersModel)
    QML_UNCREATABLE("TorrentTrackersModel is App.details.trackers; it is not created from QML.")
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
