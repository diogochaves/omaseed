#pragma once

// Format — the one place that turns numbers into the strings the spec draws.
//
// Every rule here comes from docs/05-ui-spec-v2.html; the figure that fixes it is named on
// each function. Sizes and speeds are binary (1 KB = 1024 B) with qBittorrent's KB/MB/GB
// labels, shown to three significant digits, which is what every figure in the spec does.

#include <QDateTime>
#include <QString>

namespace omaseed::core::Format {

/// "4.21 MB/s", "812 KB/s", "0" for nothing moving (spec §1 hero and table).
QString speed(qint64 bytesPerSecond);

/// "1.31 GB", "412 MB", "9 KB", "0" (spec §1 Size column, §2 strip).
QString size(qint64 bytes);

/// The details strip's long form: "2 min 40 s", "2h 41m", "∞" for 8640000, "—" when
/// negative or absent (spec §1 Fig. 1 strip "eta 2 min 40 s").
QString eta(qint64 seconds);

/// The table column's short form: "2m 40s", "7m 05s", "19m", "2h 41m", "∞", "—"
/// (spec §1 Fig. 1 ETA column).
QString etaShort(qint64 seconds);

/// "0.15" — two decimals; a negative (qBittorrent's "no ratio yet") gives "0.00" and
/// anything at or above 10000 gives "∞" the way the WebUI does.
QString ratio(double value);

/// 0.489 → "48.9 %", 1.0 → "100 %" (spec §2 strip "done 48.9 %").
QString percent(double fraction);

/// The same number without the space, for the tight progress column: "48.9%", "100%".
QString percentCompact(double fraction);

/// "today 10:41", "yesterday 22:05", "12 Sep 09:41", "12 Sep 2025 09:41" for another year.
QString relativeTime(const QDateTime &when);

/// The meta line's form: "added today 10:41" (spec §1 Fig. 1 "linux · keep · added today
/// 10:41"). Empty when the timestamp is unset.
QString added(const QDateTime &when);

/// Seconds a torrent has been active: "14 min 22 s", "2h 41m", "3d 4h", "0 s".
QString duration(qint64 seconds);

/// "21 / 42" — connected seeds over connected peers (spec §1 S / P column).
QString seedsPeers(int seeds, int peers);

} // namespace omaseed::core::Format
