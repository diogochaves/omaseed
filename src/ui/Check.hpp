#pragma once

// Check — `omaseed check <skin> [--theme NAME]` (DECISIONS C37 U5, docs/55 §6): what a skin
// reads and drops, the fonts it lacks, where the C30 floor lifted text that Omarchy's skin
// doesn't need lifted, and a picture of it, drawn offscreen on the demo library's invented
// torrents. It needs no display server, no session bus and no qBittorrent, and the picture
// (`~/.cache/omaseed/check/<name>[@<theme>].png`) is the only file it writes (herdr's lesson:
// a check with side effects is a bug).

#include <QByteArray>
#include <QString>

namespace omaseed::ui {

/// What a check found: `json` its answer when `code` is "", else the failure's code and
/// words. `exit` is check's own (0 drawn, 1 drawn whole as Omarchy's); a failure's is its
/// code's.
struct CheckResult
{
    QString code;
    QString message;
    QByteArray json;
    int exit = 0;
};

/// Runs the check of `skin` under the installed theme `theme` ("" for the person's current
/// one). It makes its own QGuiApplication on the offscreen platform, so no Qt application
/// may exist yet; it sets the environment it needs (the platform, the software scene graph,
/// no bus, OMASEED_SKIN) before making it.
CheckResult runCheck(int argc, char *argv[], const QString &skin, const QString &theme);

} // namespace omaseed::ui
