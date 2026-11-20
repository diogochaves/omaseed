#pragma once

// TempFolder — where a server downloads a torrent before it moves it to Save to, in one shape
// for every engine (docs/DECISIONS.md C47 I8–I12, on the facts #271 checked). qBittorrent's
// "keep incomplete torrents in" and Transmission's incomplete folder are a temp folder in front
// of Save to; Deluge's is the other way round: it downloads to `download_location` and, with
// `move_completed` on, ends in `move_completed_path` (#271 (4)).

#include "core/engines/EngineKind.hpp"

#include <QString>
#include <QVariantMap>

namespace omaseed::core {

struct TempFolder
{
    bool on = false; ///< the server downloads there first
    QString path;    ///< the folder it downloads to first ("" when off or not said)
    /// The add can turn it off (or point it elsewhere) for one torrent: qBittorrent's
    /// `useDownloadPath`, Deluge's `download_location`/`move_completed` (#271 (1), (4)).
    /// Transmission's `torrent_add` ignores an `incomplete_dir` (#271 (5)): session-wide.
    bool perTorrent = false;

    bool operator==(const TempFolder &other) const = default;
};

namespace TempFolders {

/// The server's temp folder from its preferences as ServerPreferences holds them:
/// qBittorrent's `temp_path_enabled`/`temp_path` (Transmission's are translated into the same
/// keys), Deluge's `move_completed` with `save_path` (its `download_location`).
TempFolder fromPreferences(const QVariantMap &prefs, EngineKind engine);

/// Where a torrent ends when the add leaves Save to empty: the server's default save path, or
/// for Deluge with `move_completed` on, `move_completed_path` (DECISIONS C47 I8, #277).
QString defaultSavePath(const QVariantMap &prefs, EngineKind engine);

/// The server makes each file at its full size when the torrent is added (qBittorrent's
/// `preallocate_all`, Deluge's `pre_allocate_storage`; Transmission none, #271 (6)): the room
/// promised to downloads is then already taken from free (C47 I10).
bool preallocates(const QVariantMap &prefs, EngineKind engine);

} // namespace TempFolders

} // namespace omaseed::core
