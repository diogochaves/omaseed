.pragma library

.import "Format.js" as Format

// What the widget reads off the status file (plugin/STATUS.md, schema v2):
// whether there is an app at all, which state the bar and the popup draw, and
// the few sums the widget's own options need (rareSeeds, recentCount). Pure
// functions over the parsed file, so tests/plugin/tst_Status.qml can hold them
// to the contract without a shell.

var SCHEMA = 2
var STALE_SECONDS = 30

// The parsed file, or null when it is not JSON or not an object.
function parse(text) {
  try {
    var value = JSON.parse(String(text || ""))
    return value && typeof value === "object" && !Array.isArray(value) ? value : null
  } catch (e) {
    return null
  }
}

// A file the widget may mirror: schema 2 (Q12: anything else reads as "not
// running"), stamped within the last 30 s of `now` (unix seconds). A crashed
// app therefore goes quiet within half a minute.
function live(status, now) {
  if (!status || status.v !== SCHEMA) return false
  var t = Number(status.t)
  return isFinite(t) && now - t <= STALE_SECONDS
}

function obj(value) {
  return value && typeof value === "object" && !Array.isArray(value) ? value : {}
}

function list(value) {
  return Array.isArray(value) ? value : []
}

function num(value) {
  return typeof value === "number" && isFinite(value) ? value : 0
}

// One word for what the widget draws:
//   "notRunning"   no live file
//   "unreachable"  the app cannot reach its server
//   "error"        torrents in error
//   "moving"       something downloads or uploads
//   "idle"         connected, nothing moving
function state(status, now) {
  if (!live(status, now)) return "notRunning"
  if (obj(status.server).reachable === false) return "unreachable"
  if (num(obj(status.counts).errored) > 0) return "error"
  return moving(status) ? "moving" : "idle"
}

function moving(status) {
  var speed = obj(status && status.speed)
  return num(speed.dl) > 0 || num(speed.up) > 0
}

function altOn(status) {
  return obj(status && status.alt).on === true
}

// The mark as the bar readout (contract §2): which arrows are lit and what the
// square says.
//   down, up   lit while that direction moves
//   square     "plain" | "alt" (outline, alt-speed colour) | "error" (urgent)
//              | "unreachable" (urgent outline)
//   dim        the app is not running: the whole mark at 45 %
function markState(status, now) {
  var s = state(status, now)
  if (s === "notRunning") return { down: false, up: false, square: "plain", dim: true }
  var speed = obj(status.speed)
  var reachable = s !== "unreachable"
  return {
    down: reachable && num(speed.dl) > 0,
    up: reachable && num(speed.up) > 0,
    square: s === "unreachable" ? "unreachable" : (s === "error" ? "error" : (altOn(status) ? "alt" : "plain")),
    dim: false
  }
}

// `hideWhenIdle`: the widget leaves the bar while nothing moves. An unreachable
// server is news and stays; so are torrents in error.
function hiddenWhenIdle(status, now) {
  var s = state(status, now)
  return s === "idle" || s === "notRunning"
}

// The bar options with the one rule the shell's form cannot express: mark and
// speed may both be on, never both off. Both off reads as the mark alone.
function barOptions(settings) {
  var s = settings || {}
  var showMark = s.showMark === undefined || s.showMark === null ? true : s.showMark === true
  var showSpeed = s.showSpeed === true
  if (!showMark && !showSpeed) showMark = true
  var metric = ["down", "up", "both"].indexOf(s.speedMetric) >= 0 ? s.speedMetric : "both"
  return {
    showMark: showMark,
    showSpeed: showSpeed,
    speedMetric: metric,
    markColor: s.markColor === "single" ? "single" : "directions",
    hideWhenIdle: s.hideWhenIdle === true,
    rareSeeds: clampInt(s.rareSeeds, 1, 5, 2),
    recentCount: clampInt(s.recentCount, 0, 5, 3),
    middleClick: ["altSpeed", "openApp", "none"].indexOf(s.middleClick) >= 0 ? s.middleClick : "altSpeed",
    rightClick: ["openApp", "altSpeed", "none"].indexOf(s.rightClick) >= 0 ? s.rightClick : "openApp"
  }
}

function clampInt(value, lo, hi, fallback) {
  var n = Number(value)
  if (value === undefined || value === null || value === "" || !isFinite(n)) return fallback
  return Math.max(lo, Math.min(hi, Math.round(n)))
}

// ------------------------------------------------------------------ tooltip

var CLICK_WORDS = { altSpeed: "alt-speed", openApp: "open omaseed", none: "" }

// Line 1 the numbers, line 2 the session's share, a news line only when there
// is news, the click map last (contract §2 Tooltip).
function tooltip(status, now, options) {
  var lines = []
  var s = state(status, now)
  if (s === "notRunning") {
    lines.push("omaseed is not running")
  } else {
    var server = obj(status.server)
    var counts = obj(status.counts)
    var speed = obj(status.speed)
    var name = String(server.name || "omaseed")
    if (s === "unreachable") {
      lines.push(name + " · " + num(counts.downloading) + " downloading · " + num(counts.seeding) + " seeding")
    } else {
      lines.push(name + " · ↓ " + Format.speed(num(speed.dl)) + " ↑ " + Format.speed(num(speed.up))
                 + " · " + num(counts.downloading) + " downloading · " + num(counts.seeding) + " seeding")
    }
    var share = obj(status.share)
    lines.push("gave " + Format.size(num(share.sessionUp)) + " this session · ratio " + Format.ratio(share.sessionRatio))
    if (s === "unreachable") lines.push(unreachableLine(server))
    else if (altOn(status)) lines.push(altLine(status))
  }
  var clicks = ["L: panel"]
  var o = options || barOptions({})
  if (CLICK_WORDS[o.middleClick]) clicks.push("M: " + CLICK_WORDS[o.middleClick])
  if (CLICK_WORDS[o.rightClick]) clicks.push("R: " + CLICK_WORDS[o.rightClick])
  lines.push(clicks.join("  "))
  return lines.join("\n")
}

function unreachableLine(server) {
  var parts = ["unreachable"]
  if (server.error) parts.push(String(server.error))
  if (num(server.retryIn) > 0) parts.push("retrying in " + num(server.retryIn) + " s")
  return parts.join(" · ")
}

// "alternative limits · ↓ 1 MB/s · ↑ 200 KB/s"
function altLine(status) {
  var alt = obj(status && status.alt)
  return "alternative limits · ↓ " + Format.limit(num(alt.dlLimit)) + " · ↑ " + Format.limit(num(alt.upLimit))
}

// "HOME · QBITTORRENT 5.2.3"
function serverLine(status) {
  var server = obj(status && status.server)
  var parts = [String(server.name || "omaseed")]
  if (server.version) parts.push("qBittorrent " + server.version)
  return parts.join(" · ").toUpperCase()
}

// ------------------------------------------------------------------ sections

// Your share, the folded line: "14.2 GB given this session · ratio 4.58".
function shareSummary(status) {
  var share = obj(status && status.share)
  return Format.size(num(share.sessionUp)) + " given this session · ratio " + Format.ratio(share.sessionRatio)
}

// The week strip: seven bars oldest first, today last, each with its day's
// letters and a height from 0 to 1 against the week's best day.
function weekBars(week) {
  var days = list(week).slice(-7)
  var best = 0
  for (var i = 0; i < days.length; i++) best = Math.max(best, num(days[i].up))
  var out = []
  for (var j = 0; j < days.length; j++) {
    var up = num(days[j].up)
    out.push({ day: Format.weekday(days[j].day), up: up, height: best > 0 ? up / best : 0, today: j === days.length - 1 })
  }
  return out
}

function weekTotal(week) {
  var total = 0
  var days = list(week).slice(-7)
  for (var i = 0; i < days.length; i++) total += num(days[i].up)
  return total
}

// "96.4 GB seeded this week · 118 seeding · 2.10 TB on offer · all time ratio 2.31"
function shareFacts(status) {
  var share = obj(status && status.share)
  return Format.size(weekTotal(status && status.week)) + " seeded this week · "
    + num(obj(status && status.counts).seeding) + " seeding · "
    + Format.size(num(share.onOffer)) + " on offer · all time ratio " + Format.ratio(share.allTimeRatio)
}

// Keeping alive, from the per-seed buckets up to `threshold` seeds:
//   held, onlyYou (1 seed), plusOne (2 seeds), waiting, quiet, and the rarest
//   rows under the threshold, at most three.
function alive(status, threshold) {
  var a = obj(status && status.alive)
  var buckets = list(a.buckets)
  var out = { threshold: threshold, held: 0, onlyYou: 0, plusOne: 0, waiting: 0, quiet: 0, rows: [] }
  for (var i = 0; i < buckets.length; i++) {
    var b = obj(buckets[i])
    var seeds = num(b.seeds)
    if (seeds < 1 || seeds > threshold) continue
    out.held += num(b.count)
    out.waiting += num(b.waiting)
    out.quiet += num(b.quiet)
    if (seeds === 1) out.onlyYou += num(b.count)
    if (seeds === 2) out.plusOne += num(b.count)
  }
  var rows = list(a.rows)
  for (var j = 0; j < rows.length && out.rows.length < 3; j++) {
    var r = obj(rows[j])
    if (num(r.seeds) >= 1 && num(r.seeds) <= threshold) out.rows.push(r)
  }
  return out
}

// "7 with 2 or fewer seeds · 2 only you"
function aliveSummary(summary) {
  if (summary.held === 0) return "nothing with " + summary.threshold + " or fewer seeds"
  var text = summary.held + " with " + summary.threshold + " or fewer seeds"
  if (summary.onlyYou > 0) text += " · " + summary.onlyYou + " only you"
  return text
}

// "only you · 9 waiting", "you + 1 · 14 waiting"
function aliveRowText(row) {
  var seeds = num(row.seeds)
  var who = seeds <= 1 ? "only you" : "you + " + (seeds - 1)
  return who + " · " + num(row.waiting) + " waiting"
}

// In flight, the folded line: "2 · archlinux 48.9 % · Cosmos 87.2 %".
function activeSummary(status) {
  var rows = list(status && status.active)
  if (rows.length === 0) return "nothing downloading"
  var parts = [String(rows.length)]
  for (var i = 0; i < rows.length && i < 2; i++)
    parts.push(firstWord(rows[i].name) + " " + Format.percent(num(rows[i].progress)))
  return parts.join(" · ")
}

function firstWord(name) {
  var text = String(name || "")
  var match = text.match(/^[^\s._-]+/)
  return match ? match[0] : text
}

// Lately: completions and milestones, newest first, `count` rows.
function recent(status, count) {
  return list(status && status.recent).slice(0, Math.max(0, count))
}

// "finished · 6.11 GB · linux · 12 min ago" or "reached ratio 1.0 · 1 h ago".
function recentMeta(row, now) {
  var when = Format.ago(row.at, now)
  if (row.kind === "milestone") {
    var text = "reached ratio " + Number(num(row.ratio)).toFixed(1)
    return when === "" ? text : text + " · " + when
  }
  var parts = ["finished"]
  if (Format.known(row.size)) parts.push(Format.size(row.size))
  if (row.category) parts.push(String(row.category))
  if (when !== "") parts.push(when)
  return parts.join(" · ")
}

// "ubuntu-26.04.1 finished 12 min ago"
function recentSummary(status, count, now) {
  var rows = recent(status, count)
  if (rows.length === 0) return "nothing yet"
  var row = rows[0]
  var verb = row.kind === "milestone" ? Format.milestone(num(row.ratio)) : "finished"
  var when = Format.ago(row.at, now)
  return firstWord(row.name) + " " + verb + (when === "" ? "" : " " + when)
}

// The upload sparkline: hidden when nothing has moved for an hour.
function sparkVisible(hour) {
  var points = list(hour)
  for (var i = 0; i < points.length; i++) if (num(points[i]) > 0) return true
  return false
}

function peak(hour) {
  var points = list(hour)
  var best = 0
  for (var i = 0; i < points.length; i++) best = Math.max(best, num(points[i]))
  return best
}

// ------------------------------------------------------------------ add

// The sources in a piece of text, one per line: magnet links, http(s) links
// and .torrent files (a path or a file:// URI). Everything else is dropped.
function addable(text) {
  var out = []
  var lines = String(text || "").split(/[\r\n]+/)
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i].trim()
    if (line === "") continue
    if (/^magnet:\?/i.test(line) || /^https?:\/\/\S+$/i.test(line)) out.push(line)
    else if (/^file:\/\/\S+\.torrent$/i.test(line)) out.push(decodeURIComponent(line.replace(/^file:\/\//i, "")))
    else if (/^\/.+\.torrent$/i.test(line)) out.push(line)
    if (out.length >= 20) break
  }
  return out
}

// The kind of a source before the app has described it.
function sourceKind(source) {
  if (/^magnet:/i.test(source)) return "magnet"
  if (/^https?:/i.test(source)) return "link"
  return "file"
}

// A name to show before the app has described a source: a magnet's dn, a
// link's or file's last path segment.
function sourceName(source) {
  var text = String(source || "")
  if (/^magnet:/i.test(text)) {
    var dn = text.match(/[?&]dn=([^&]+)/i)
    if (dn) {
      try { return decodeURIComponent(dn[1].replace(/\+/g, " ")) } catch (e) { return dn[1] }
    }
    var hash = text.match(/xt=urn:bt[im]h:([0-9a-z]+)/i)
    return hash ? "magnet " + hash[1].slice(0, 12).toLowerCase() : "magnet link"
  }
  var path = text.replace(/[?#].*$/, "").replace(/\/+$/, "")
  var last = path.slice(path.lastIndexOf("/") + 1)
  try { return decodeURIComponent(last) || text } catch (e) { return last || text }
}

// Where a torrent goes when added with `category` (name or ""): with
// automatic management the category's folder (its savePath, or the default
// folder plus its name), without it the server's default folder.
function targetFolder(status, category) {
  var add = obj(status && status.add)
  var base = String(add.savePath || "")
  if (!category || add.autoTmm !== true) return base
  var cats = list(status.categories)
  for (var i = 0; i < cats.length; i++) {
    if (cats[i].name === category) {
      if (cats[i].savePath) return String(cats[i].savePath)
      break
    }
  }
  return base === "" ? "" : base.replace(/\/+$/, "") + "/" + category
}

// The answer of `busctl --json=short call … s`: the string it carried, parsed
// as JSON, or null.
function busString(text) {
  var reply = parse(text)
  if (!reply || !Array.isArray(reply.data) || typeof reply.data[0] !== "string") return null
  try {
    return JSON.parse(reply.data[0])
  } catch (e) {
    return null
  }
}
