#pragma once

// AnnounceMask — the one function that hides a private tracker's passkey (#146, DECISIONS C27
// CR5, board 49 §6). A private announce URL carries the passkey somewhere in its path or its
// query; omaseed never needs to know where, because it never shows any of it: the scheme and
// the host stay, the path's "announce" words stay, and every other part of the path and the
// whole query become eight dots. A public URL holds no secret, and comes out the same unless it
// has a path part or a query of its own.
//
//   https://tracker-a.example/announce/k3y        → https://tracker-a.example/announce/••••••••
//   https://tracker-b.example/k3y/announce        → https://tracker-b.example/••••••••/announce
//   http://tracker-c.example:2710/a.php?pk=k3y    → http://tracker-c.example:2710/••••••••?••••••••
//   udp://open.tracker-d.example:6969/announce    → the same
//
// maskText() runs it over every URL in a text (an error's details, a log line, a notice), so
// anything that could hold an announce URL goes through it before it is shown or written.

#include <QString>

namespace omaseed::core::AnnounceMask {

/// The eight dots that stand for a hidden part.
QString dots();
/// `url` with everything that could be the passkey hidden (see the top); a text that isn't a
/// URL with a host is all dots.
QString maskUrl(const QString &url);
/// `text` with every URL in it (http, https, udp, ws, wss) masked as maskUrl() does.
QString maskText(const QString &text);

} // namespace omaseed::core::AnnounceMask
