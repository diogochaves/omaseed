#pragma once

// TorrentsBackup — a server's torrents as one file (docs/24 §2, §4; D2 a; #33), whatever its
// engine (docs/57, DECISIONS C40; #88).
//
// A plain tar (TarArchive), so any tool can open one and see what is in it:
//
//   manifest.json            { "format": "omaseed-torrents", "version": 1, "created": ISO date,
//                              "app": omaseed's version, "server": {"name", "url", "engine",
//                              "qbittorrent", "webApi", "savePath", "downloadPath"},
//                              "count", "magnets", "categories": {...}, "tags": [...] }
//   torrents/<id>.json       one torrent: what the server keeps about it beyond its .torrent
//   torrents/<id>.torrent    its .torrent (torrents/export), when it has its metadata
//
// <id> is qBittorrent's id for the torrent (TorrentFile::torrentIdFor). The manifest comes
// first, then each torrent's record and its .torrent side by side, so an export is written in
// one pass and a reader learns everything the import sheet needs from the headers and the
// small records alone, fetching a .torrent only when it is sent (TarReader).
//
// A record holds what docs/24 §2 lists: the save path (and the incomplete folder), category,
// tags, share and speed limits, started, stopped or forced, its queue position, renamed files
// (against the .torrent's own names, under the content layout that leaves fewest), file
// priorities, every tracker it announces to, and for a magnet that never got its metadata
// (torrents/export answers 409) the link instead of a .torrent.
//
// The manifest names the engine that wrote the file (C40 X1): "server"."engine" is EngineKind's
// name, and a file without it is a qBittorrent's (every file before C40). A qBittorrent's
// version stays under "qbittorrent" (with "webApi"), so an older omaseed reads the file as it
// always did; another engine's goes under "version" and those two keys are left out.
//
// A record keeps qBittorrent's two grouping fields whatever engine wrote it (C40 X4), so an
// older omaseed still imports the file sensibly:
//   - a Transmission's record has `category` empty and `tags` = its labels, in the daemon's
//     order;
//   - a Deluge's record has `category` = its one label and no tags.
// What those become on another engine is LibraryCrossing's.
//
// The file is private (0600): a private tracker's announce URL carries the account's passkey,
// in the .torrent files and the records' tracker lists alike (DECISIONS C9).

#include "core/TarArchive.hpp"
#include "core/engines/EngineKind.hpp"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

class TorrentFile;

/// What the manifest says about the export as a whole.
struct TorrentsManifest
{
    QDateTime created;
    QString appVersion;
    QString serverName;
    QString serverUrl;
    /// The engine that wrote the file (DECISIONS C40 X1); qBittorrent for a file without one.
    EngineKind engine = EngineKind::Qbittorrent;
    QString version;        ///< the engine's own: "v5.2.3", "4.1.3", "2.2.0"
    QString apiVersion;     ///< qBittorrent's Web API, "2.15.1"; empty for the other engines
    QString savePath;       ///< the server's default save path then
    QString downloadPath;   ///< its incomplete folder, when it used one
    int count = 0;          ///< torrents in the file
    int magnets = 0;        ///< of which links without metadata
    QJsonObject categories; ///< torrents/categories, keyed by name
    QStringList tags;       ///< every tag the server had

    QJsonObject toJson() const;
    static std::optional<TorrentsManifest> fromJson(const QJsonObject &object,
                                                    QString *error = nullptr);
};

/// One torrent, as its record in the file holds it.
struct TorrentRecord
{
    struct Rename
    {
        int index = 0;
        QString from; ///< the name qBittorrent gives the file under `layout`
        QString to;   ///< what it was called on the server
        bool operator==(const Rename &other) const = default;
    };

    QString id; ///< qBittorrent's id, the record's and the .torrent's name in the file
    QString infohashV1;
    QString infohashV2;
    QString name;
    qint64 size = 0;     ///< the wanted bytes (torrents/info `size`)
    double progress = 0; ///< 0..1 at export
    QString savePath;
    QString downloadPath; ///< empty when it has no incomplete folder of its own
    QString category;
    QStringList tags;
    bool autoTmm = false;
    QString state;         ///< "started", "stopped" or "forced"
    int queuePosition = 0; ///< 0: none (complete, or queueing off)
    qint64 upLimit = 0;    ///< bytes/s; 0 none
    qint64 dlLimit = 0;
    double ratioLimit = -2;               ///< -2 the server's default, -1 none
    qint64 seedingTimeLimit = -2;         ///< minutes
    qint64 inactiveSeedingTimeLimit = -2; ///< minutes
    QString shareLimitAction;             ///< "Default", "Stop", … (API 2.15.1)
    bool sequential = false;
    bool firstLastPiecePrio = false;
    QString layout = QStringLiteral("Original"); ///< contentLayout for the add
    QList<Rename> renamed;
    /// One per file, in index order; empty when every file is normal. qBittorrent's values
    /// (0 not wanted, 1 normal, 6 high, 7 maximal), and 2 for another engine's low.
    QList<int> priorities;
    QStringList trackers; ///< every tracker, in tier order
    QString magnet;       ///< a magnet without metadata: the link; empty otherwise

    bool isMagnet() const { return !magnet.isEmpty(); }
    bool started() const { return state != QLatin1String("stopped"); }
    /// Every id the torrent can be known by on another server: its id, v1 and v2 hashes.
    QStringList ids() const;

    QJsonObject toJson() const;
    static std::optional<TorrentRecord> fromJson(const QJsonObject &object,
                                                 QString *error = nullptr);
};

class TorrentsBackup
{
public:
    static constexpr int kVersion = 1;
    static inline const QString kFormat = QStringLiteral("omaseed-torrents");
    static inline const QString kSuffix = QStringLiteral(".omaseed-torrents");
    static inline const QString kManifest = QStringLiteral("manifest.json");

    static QString recordEntry(const QString &id);
    static QString torrentEntry(const QString &id);
    /// 40 or 64 lower-case hex digits: the only ids an entry name is made from.
    static bool isTorrentId(const QString &id);

    /// The record for one torrent, from what the server says about it: its torrents/info
    /// object, torrents/files and torrents/trackers arrays, and its .torrent (empty for a
    /// magnet without metadata).
    static TorrentRecord recordFrom(const QJsonObject &info,
                                    const QJsonArray &files,
                                    const QJsonArray &trackers,
                                    const QByteArray &torrent);
    /// The names qBittorrent gives a torrent's files when it is added with `layout`
    /// ("Original", "Subfolder", "NoSubfolder"), in index order: what its torrents/files
    /// `name`s read before any rename.
    static QStringList namesUnder(const TorrentFile &parsed, const QString &layout);
    /// The content layout that leaves fewest renames between `parsed` and `names` (the files
    /// as the server names them now), and those renames.
    static QPair<QString, QList<TorrentRecord::Rename>> renamesOf(const TorrentFile &parsed,
                                                                  const QStringList &names);

    /// A file read for import: the manifest, every record in file order, and where each
    /// .torrent lies, by id.
    struct Contents
    {
        QString path;
        TorrentsManifest manifest;
        QList<TorrentRecord> records;
        QHash<QString, TarReader::Header> torrents;
    };
    /// Reads `path`'s manifest and records (never its .torrent files' bytes), or nothing with
    /// `error` saying why: not an archive, not ours, a newer version, a record without its
    /// .torrent, damage anywhere.
    static std::optional<Contents> read(const QString &path, QString *error = nullptr);
    /// One torrent's .torrent bytes from `contents`' file, through `device` (opened on it).
    static QByteArray torrentBytes(QIODevice *device,
                                   const Contents &contents,
                                   const QString &id,
                                   QString *error = nullptr);
};

} // namespace omaseed::core
