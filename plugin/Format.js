.pragma library

// How the widget writes numbers. One rule everywhere (docs/07 §2), and the same
// units as the app: 1024-based, B · K · M · G · T.
//
// The bar uses the short form, the popup full units. The status file only ever
// carries raw numbers (plugin/STATUS.md), so every string the widget shows is
// made here, and tests/plugin/tst_Format.qml holds it to the rules.

var UNIT = 1024

// True for a number the file actually reported. null, undefined, NaN and a
// negative value all mean "unknown".
function known(value) {
  return typeof value === "number" && isFinite(value) && value >= 0
}

// ------------------------------------------------------------------ the bar

// A speed in at most four characters: "0", "842K", "4.2M", "112M", "1.1G".
// Under 1 KB/s is "0" (a live zero, never "0.0K"). Under 10 of a unit one
// decimal, ten and over none. A value that would read 1000 or more of a unit
// moves to the next one ("1.0M", never "1000K"), which is what keeps every
// value inside four characters. Unknown is "": the bar shows nothing rather
// than a dash.
function barValue(bytesPerSecond) {
  if (!known(bytesPerSecond)) return ""
  if (bytesPerSecond < UNIT) return "0"
  var units = ["K", "M", "G", "T"]
  var value = bytesPerSecond / UNIT
  var unit = 0
  for (;;) {
    var text = value < 10 ? (Math.round(value * 10) / 10).toFixed(1) : String(Math.round(value))
    if (text === "10.0") text = "10"
    if (Number(text) < 1000 || unit === units.length - 1) return text + units[unit]
    value /= UNIT
    unit++
  }
}

// The horizontal bar's field: the arrow and the value right-aligned in four
// characters, exactly five characters in all, so the widget never changes
// width: "↓4.2M", "↑842K", "↓   0". Unknown is "".
function barField(bytesPerSecond, arrow) {
  var value = barValue(bytesPerSecond)
  if (value === "") return ""
  while (value.length < 4) value = " " + value
  return String(arrow || "").charAt(0) + value
}

// The vertical bar stacks the arrow over the value, so the value alone is the
// line: at most four characters, centred by the widget.
function verticalValue(bytesPerSecond) {
  return barValue(bytesPerSecond)
}

// ------------------------------------------------------------------ the popup

// Three significant figures from MB up, whole numbers for B and KB: how the app
// prints a byte count (core/Format.cpp `scaled`). "0 B", "812 KB", "4.21 MB",
// "14.2 GB", "2.10 TB". Unknown is "—".
function size(bytes) {
  if (!known(bytes)) return "—"
  return scaled(bytes, "")
}

// "0 B/s" when nothing moves (the popup is explicit, contract §3 Idle),
// "812 KB/s", "4.21 MB/s". Unknown is "—".
function speed(bytesPerSecond) {
  if (!known(bytesPerSecond)) return "—"
  return scaled(bytesPerSecond, "/s")
}

// A limit in force, shortest form: "1 MB/s", "200 KB/s", "1.5 MB/s"; 0 is "∞".
function limit(bytesPerSecond) {
  if (!known(bytesPerSecond)) return "—"
  if (bytesPerSecond === 0) return "∞"
  return speed(bytesPerSecond).replace(/\.(\d*?)0+ /, function(match, keep) {
    return keep === "" ? " " : "." + keep + " "
  })
}

function scaled(bytes, suffix) {
  var units = ["B", "KB", "MB", "GB", "TB", "PB"]
  var value = bytes
  var unit = 0
  while (value >= UNIT && unit + 1 < units.length) {
    value /= UNIT
    unit++
  }
  var decimals = decimalsFor(unit, value)
  // Rounding can push 1023.7 KB to "1024 KB": carry it into the next unit.
  if (unit + 1 < units.length && Number(value.toFixed(decimals)) >= UNIT) {
    value /= UNIT
    unit++
    decimals = decimalsFor(unit, value)
  }
  return value.toFixed(decimals) + " " + units[unit] + suffix
}

function decimalsFor(unit, value) {
  if (unit <= 1) return 0
  return value < 10 ? 2 : (value < 100 ? 1 : 0)
}

// Download progress as the app prints it: truncated, never "100 %" before it
// is done. 0.489 is "48.9 %".
function percent(fraction) {
  if (!known(fraction)) return "—"
  if (fraction >= 1) return "100 %"
  var value = Math.floor(fraction * 1000 + 1e-6) / 10
  return Math.min(value, 99.9).toFixed(1) + " %"
}

// Time left, short: "45s", "2m 40s", "3m", "1h 05m", "2d 4h". Unknown (-1) and
// qBittorrent's "no estimate" (8640000 s and over) are "∞".
function eta(seconds) {
  if (typeof seconds !== "number" || !isFinite(seconds) || seconds < 0 || seconds >= 8640000) return "∞"
  seconds = Math.floor(seconds)
  if (seconds < 60) return seconds + "s"
  if (seconds < 3600) {
    var rest = seconds % 60
    return Math.floor(seconds / 60) + "m" + (rest === 0 ? "" : " " + pad2(rest) + "s")
  }
  if (seconds < 86400) return Math.floor(seconds / 3600) + "h " + pad2(Math.floor(seconds % 3600 / 60)) + "m"
  return Math.floor(seconds / 86400) + "d " + Math.floor(seconds % 86400 / 3600) + "h"
}

function pad2(n) {
  return n < 10 ? "0" + n : String(n)
}

// How long ago a unix time was, from `now` (unix seconds): "just now",
// "12 min ago", "3 h ago", "yesterday", "4 days ago". "" when unknown.
function ago(at, now) {
  if (!known(at) || at === 0) return ""
  var seconds = Math.max(0, Math.floor(now - at))
  if (seconds < 60) return "just now"
  if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
  if (seconds < 86400) return Math.floor(seconds / 3600) + " h ago"
  var days = Math.floor(seconds / 86400)
  return days === 1 ? "yesterday" : days + " days ago"
}

// A ratio to two decimals, "—" before there is one (-1). 9999 and over is "∞".
function ratio(value) {
  if (!known(value)) return "—"
  if (value >= 9999) return "∞"
  return value.toFixed(2)
}

// A ratio milestone's words: "paid back" at 1, "ratio 2.0", "ratio 5.0".
function milestone(value) {
  return value === 1 ? "paid back" : "ratio " + Number(value).toFixed(1)
}

// A count with its noun: "1 torrent", "7 torrents".
function plural(count, noun, nouns) {
  return count + " " + (count === 1 ? noun : (nouns || noun + "s"))
}

// "Fri" for "2026-10-02". Local calendar day, as the app writes it.
function weekday(day) {
  var parts = String(day || "").split("-")
  if (parts.length !== 3) return ""
  var date = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
  if (isNaN(date.getTime())) return ""
  return ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"][date.getDay()]
}
