#pragma once

// SpeedSets — which way qBittorrent's alternative limits go (docs/31 §3, DECISIONS.md C4).
//
// qBittorrent keeps two sets of global limits, `dl_limit`/`up_limit` and
// `alt_dl_limit`/`alt_up_limit`, in bytes/s with 0 for "no limit". Most people use the second
// set as the slow one (a turtle); someone who caps the line by day and opens it at night uses
// it as the fast one (a burst). The set itself says which: each direction's alternative limit
// is compared with its normal one, 0 read as ∞, and the two comparisons give the kind.
//
//   Burst   at least one direction faster, none slower
//   Turtle  at least one direction slower, none faster
//   Mixed   faster one way and slower the other
//   Same    the same both ways: switching changes nothing
//
// Mixed and Same keep today's word, "alt-speed" (D2): no claim either way. Pure.

#include <QString>

namespace omaseed::core::SpeedSets {

enum class Kind { Unknown, Burst, Turtle, Mixed, Same };

/// -1 when `alt` is slower than `normal`, 1 when faster, 0 the same. A limit of 0 or less is
/// no limit, which ranks above any number.
int compare(qint64 normal, qint64 alt);

/// The kind of the alternative set against the normal one.
Kind kind(qint64 dlLimit, qint64 upLimit, qint64 altDlLimit, qint64 altUpLimit);

/// The word the app prints: "burst", "turtle", or "alt-speed" for the rest (Unknown too).
QString word(Kind kind);

/// The status file's `alt.kind` and the views' key: "burst", "turtle" or "alt".
QString key(Kind kind);

} // namespace omaseed::core::SpeedSets
