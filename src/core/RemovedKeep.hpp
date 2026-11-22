#pragma once

// RemovedKeep — the way back for a torrent an agent removed (docs/DECISIONS.md C50 GT6; #295).
//
// Before a removal, the torrent goes into a folder of its own:
//
//   $XDG_DATA_HOME/omaseed/removed/<yyyymmdd>-<hash8>/torrent.omaseed-torrents
//
// a TorrentsBackup file holding that one torrent: the manifest (naming the server it came from,
// its engine, and `created`, the moment it was kept) and its record and .torrent, the same
// format Settings › Backup writes, so the engine's own exporter writes it (`only`) and its
// importer adds it back. A second removal of the same torrent on the same day gets `-2`, `-3`.
//
// Each is kept kDays from `created`; prune() at start deletes the older ones. A folder whose
// file can't be read (damaged, not one torrent) is left out of list() and take(); prune() then
// goes by the date in its name, so it is not kept forever either.
//
// Adding back reads the file with every record stopped (take): the importer adds it paused, at
// the save path the record holds, and checks nothing. The caller forgets the entry once the add
// went through.
//
// Folders are 0700 and files 0600: a private tracker's announce URL carries a passkey (C9).

#include "core/TorrentsBackup.hpp"
#include "core/engines/EngineKind.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace omaseed::core {

class RemovedKeep
{
public:
    static constexpr int kDays = 30;
    static inline const QString kFile = QStringLiteral("torrent.omaseed-torrents");

    struct Entry
    {
        QString id; ///< the folder's name, "20261121-1a2b3c4d"
        QString hash;
        QString name;
        QString savePath;
        QString serverName;
        QString serverUrl;
        EngineKind engine = EngineKind::Qbittorrent;
        QDateTime removedAt;
        QDateTime keptUntil;
        QString path;      ///< the file
        bool link = false; ///< kept as a magnet link (no metadata)
        bool operator==(const Entry &other) const = default;
    };

    /// `$XDG_DATA_HOME/omaseed/removed` (`~/.local/share` without it).
    static QString root();

    /// A new, empty folder (0700) for `hash` kept at `at`, and the file path in it for the
    /// exporter to write; empty with `error` when it can't be made.
    static QString prepare(const QString &hash, const QDateTime &at, QString *error = nullptr);
    /// Writes `record` (and its .torrent, empty for a link) as a one-torrent file under a new
    /// folder; `manifest` names the server. The entry, or nothing with `error`.
    static std::optional<Entry> keep(const TorrentsManifest &manifest,
                                     const TorrentRecord &record,
                                     const QByteArray &torrent,
                                     QString *error = nullptr);
    /// The entry of the folder `id`, when its file holds one torrent.
    static std::optional<Entry> entry(const QString &id);
    /// Every readable entry, newest first.
    static QList<Entry> list();
    /// Deletes what was kept before `now` - kDays; how many folders went.
    static int prune(const QDateTime &now);
    /// The file of `id` read for adding back: its one record stopped, its save path as kept.
    static std::optional<TorrentsBackup::Contents> take(const QString &id,
                                                        QString *error = nullptr);
    /// Deletes `id`'s folder (after it was added back).
    static bool forget(const QString &id);
    /// A folder name this class makes: `yyyymmdd-<8 hex>` with an optional `-N`.
    static bool isId(const QString &id);
};

} // namespace omaseed::core
