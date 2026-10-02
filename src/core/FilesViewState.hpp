#pragma once

// FilesViewState — what the details pane's Files tab remembers between torrents and runs
// (#9), in state.json's "files" section:
//
//   {"hideSkipped": true, "filters": [{"hash": "…", "text": "*.srt"}, …]}
//
// `hideSkipped` is a preference for every torrent. A filter belongs to the torrent it was
// typed for: coming back to that torrent brings it back. The list is most recent first and
// keeps the last kMaxFilters torrents, so it cannot grow with the library; an emptied filter
// drops its torrent from the list. Every call reads the file fresh (StateFile), and a write
// that would change nothing does not touch the disk.

#include <QString>

namespace omaseed::core::FilesViewState {

inline constexpr int kMaxFilters = 100;

bool hideSkipped();
void setHideSkipped(bool hide);

/// The filter last typed for `hash`, or "".
QString filterFor(const QString &hash);
/// Remembers `text` for `hash` (first in the list); "" forgets it. An empty hash is ignored.
void rememberFilter(const QString &hash, const QString &text);

} // namespace omaseed::core::FilesViewState
