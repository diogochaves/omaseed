#pragma once

// FilesViewState — what the details pane's Files tab remembers between torrents and runs
// (#9), in state.json's "files" section:
//
//   {"hideSkipped": true, "filters": [{"hash": "…", "text": "*.srt"}, …],
//    "views": {"add": "flat"}}
//
// `hideSkipped` is a preference for every torrent. A filter belongs to the torrent it was
// typed for: coming back to that torrent brings it back. The list is most recent first and
// keeps the last kMaxFilters torrents, so it cannot grow with the library; an emptied filter
// drops its torrent from the list. DECISIONS C47 I6: tree · flat is kept per window ("add",
// "details"); a window not in "views" is in the tree. Every call reads the file fresh
// (StateFile), and a write that would change nothing does not touch the disk.

#include <QString>

namespace omaseed::core::FilesViewState {

inline constexpr int kMaxFilters = 100;

bool hideSkipped();
void setHideSkipped(bool hide);

/// The filter last typed for `hash`, or "".
QString filterFor(const QString &hash);
/// Remembers `text` for `hash` (first in the list); "" forgets it. An empty hash is ignored.
void rememberFilter(const QString &hash, const QString &text);

/// C47 I6: "tree" or "flat" for `scope` ("add" or "details"); "tree" when none was kept, or
/// for anything else found there.
QString view(const QString &scope);
/// Keeps `view` for `scope`: "flat" is written, anything else is the tree and drops the
/// entry. An empty scope is ignored.
void setView(const QString &scope, const QString &view);

} // namespace omaseed::core::FilesViewState
