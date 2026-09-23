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

/// A speed limit: "10 KB/s", or "∞" when there is none (qBittorrent sends 0 or -1).
QString limit(qint64 bytesPerSecond);

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

// --- the details pane (spec §2 Fig. 1-4) ---------------------------------------------

/// The peers table's abbreviated speed: "1.02 M", "744 K", "12 K", "0" (spec §2 Fig. 4,
/// "Speeds abbreviate to fit"). Same three-significant-digit rule as size(), with one-letter
/// units and no "/s"; bytes per second under 1 KB print as a bare number.
QString speedShort(qint64 bytesPerSecond);

/// A count with the spec's space as the thousands separator: 1264 -> "1 264" (spec §2 Fig. 1
/// "1 264 × 1 MiB", "1 204 total").
QString count(qint64 value);

/// "1 264 × 1 MiB" — the General tab's Pieces row in Information (spec §2 Fig. 1, docs/12
/// G2). Piece sizes are powers of two, so this one row prints proper binary units
/// (KiB/MiB/GiB) rather than qBittorrent's KB/MB/GB labels. Empty when there are no pieces
/// yet (a magnet without metadata).
QString pieceLayout(int pieceCount, qint64 pieceSize);

/// "617 of 1 264" — the pieces bar's label (docs/12 G3). Empty without pieces.
QString piecesHave(int piecesHave, int pieceCount);

/// "21 connected · 1 204 total" — the General tab's Seeds and Peers rows (spec §2 Fig. 1).
QString connectedTotal(int connected, int total);

/// "global · ratio 2.0", "unlimited", "ratio 2.0 · 7d" — the General tab's Share limit row
/// (spec §2 Fig. 1). `ratioLimit` and `seedingTimeLimit` follow qBittorrent's sentinels:
/// -2 means "use the global limit", -1 "no limit"; the time limit is in minutes.
QString shareLimit(double ratioLimit, qint64 seedingTimeLimit);

// --- docs/12 G2: the General tab's new rows -------------------------------------------

/// "15 of 100" — connections over the torrent's connection limit; a limit of 0 or less
/// (qBittorrent's "none") prints "15 of ∞".
QString connections(int connected, int limit);

/// "↓ 4.12 MB/s · ↑ 820 KB/s" — the average speeds over the time active. qBittorrent sends
/// -1 for a torrent that has not been active yet; that side prints "—", and both at -1 give
/// an empty string, so the row is left out.
QString averageSpeeds(qint64 downBytesPerSecond, qint64 upBytesPerSecond);

/// "in 12 min" — the next announce. Empty at 0 or below (a stopped torrent announces
/// nothing), so the row is left out.
QString reannounceIn(qint64 seconds);

/// The magnet as the General tab draws it: the `xt` part only, cut after the first eight
/// characters of the hash — "magnet:?xt=urn:btih:3f6c9a1b…". The full link is what `y`
/// copies; this is only for the eye. Empty for an empty link.
QString shortMagnet(const QString &magnetUri);

} // namespace omaseed::core::Format
