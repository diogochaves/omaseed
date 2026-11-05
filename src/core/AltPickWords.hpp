#pragma once

// AltPickWords — the words of the notice a speed pick made outside the window sends once it
// has taken effect (DECISIONS C36 M9; the presets are C32 D8 a's, AltPresets).
//
// - off: "Normal speeds", no body.
// - turtle: "Turtle" / "↓ 1.00 MB/s ↑ 200 KB/s" (a direction with no limit is left out; with
//   neither, "No limits"). At qBittorrent's untouched defaults, 10 KB/s each way, a second
//   line says so and points at Settings › qBittorrent › Speed.
// - burst: "Burst for 60 min" or "Burst until stopped" / its limits, as turtle's.
//
// A limit that is not known (-1, the server's set not read yet) leaves the body empty.
// Speeds are Format::speed's. Pure Qt Core; `tst_altpickwords` has a row per case.

#include <QString>

namespace omaseed::core::AltPickWords {

/// qBittorrent's own alternative limits, never changed: 10 KiB/s each way.
inline constexpr qint64 kQbtDefaultAltLimit = 10 * 1024;

struct Notice
{
    QString summary;
    QString body;
};

/// `kind` "off", "turtle" or "burst"; `dl`/`up` in bytes/s (0 none, -1 unknown); `minutes` a
/// burst's length, 0 until stopped.
Notice notice(const QString &kind, qint64 dl, qint64 up, int minutes);

} // namespace omaseed::core::AltPickWords
