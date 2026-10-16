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

// docs/31 D12: which way the alternative set goes, as the app works it out: "burst" (faster
// than the normal limits), "turtle" (slower), or "alt" (mixed, the same, not known, or an
// app from before alt.kind, which then reads as today's yellow alt-speed).
function altKind(status) {
  var kind = obj(status && status.alt).kind
  return kind === "burst" || kind === "turtle" ? kind : "alt"
}

// When the schedule ends the current state ("07:00", "Mon 07:00"), "" for none.
function altUntil(status) {
  var until = obj(status && status.alt).until
  return typeof until === "string" ? until : ""
}

// The alt-speed button's tooltip: "Burst on (A)", "Turtle (A)", or today's words.
function altButtonTip(status) {
  var kind = altKind(status)
  var on = altOn(status)
  if (kind === "burst") return on ? "Burst on (A)" : "Burst (A)"
  if (kind === "turtle") return on ? "Turtle on (A)" : "Turtle (A)"
  return on ? "Alternative speed limits on (A)" : "Alternative speed limits (A)"
}

// The mark as the bar readout (contract §2): which arrows are lit and what the
// square says.
//   down, up   lit while that direction moves
//   square     "plain" | "alt" (outline, alt-speed colour) | "burst" (outline, green,
//              docs/31 W1) | "error" (urgent) | "unreachable" (urgent outline)
//   dim        the app is not running: the whole mark at 45 %
function markState(status, now) {
  var s = state(status, now)
  if (s === "notRunning") return { down: false, up: false, square: "plain", dim: true }
  var speed = obj(status.speed)
  var reachable = s !== "unreachable"
  return {
    down: reachable && num(speed.dl) > 0,
    up: reachable && num(speed.up) > 0,
    square: s === "unreachable" ? "unreachable" : (s === "error" ? "error"
      : (altOn(status) ? (altKind(status) === "burst" ? "burst" : "alt") : "plain")),
    dim: false
  }
}

// The console convention (omarchy-console DESIGN.md §6: consoleAwake, consoleState,
// consoleReason, consoleValue), read by its rail to move a widget into its running zone.
// What wakes it is the user's (docs/27 D14-D16, the "omarchy-console's rail" settings):
// downloads (on by default: there is something to wait for), uploads and torrents in error
// (off by default: a library that seeds all day, or keeps a few torrents with missing files
// for months, would never leave the running zone). An unreachable server always wakes it,
// and a failure wins over a download. "Downloads" and "uploads" mean data moving now (speed
// above 0), not a torrent's state. The value is both speeds, awake or not; nothing while the
// app is not running.
function consoleReading(status, now, options) {
  var o = options || barOptions({})
  var s = state(status, now)
  if (s === "notRunning") return { awake: false, state: "", reason: "", value: "" }
  var speed = obj(status.speed)
  var value = "↓ " + Format.barValue(num(speed.dl)) + " ↑ " + Format.barValue(num(speed.up))
  if (s === "unreachable") return { awake: true, state: "failed", reason: "qBittorrent unreachable", value: "" }
  var errored = num(obj(status.counts).errored)
  if (o.wakeOnError && errored > 0)
    return { awake: true, state: "failed", reason: errored + " errored", value: value }
  if ((o.wakeOnDownload && num(speed.dl) > 0) || (o.wakeOnUpload && num(speed.up) > 0))
    return { awake: true, state: "running", reason: "", value: value }
  return { awake: false, state: "", reason: "", value: value }
}

// `hideWhenIdle`: the widget leaves the bar while nothing moves. An unreachable
// server is news and stays; so are torrents in error.
function hiddenWhenIdle(status, now) {
  var s = state(status, now)
  return s === "idle" || s === "notRunning"
}

// The bar options with the one rule the shell's form cannot express: mark and
// speed may both be on, never both off. Both off reads as the mark alone.
// A switch's value as `omarchy bar set` may have stored it: true, or "true" when it was
// set without --json. null when it is not set at all.
function flag(value) {
  if (value === true || value === "true") return true
  if (value === false || value === "false") return false
  return null
}

function barOptions(settings) {
  var s = settings || {}
  var showMark = flag(s.showMark) !== false
  var showSpeed = flag(s.showSpeed) === true
  if (!showMark && !showSpeed) showMark = true
  var metric = ["down", "up", "both"].indexOf(s.speedMetric) >= 0 ? s.speedMetric : "both"
  return {
    showMark: showMark,
    showSpeed: showSpeed,
    speedMetric: metric,
    markColor: s.markColor === "single" ? "single" : "directions",
    hideWhenIdle: flag(s.hideWhenIdle) === true,
    rareSeeds: clampInt(s.rareSeeds, 1, 5, 2),
    recentCount: clampInt(s.recentCount, 0, 5, 3),
    middleClick: ["altSpeed", "openApp", "none"].indexOf(s.middleClick) >= 0 ? s.middleClick : "altSpeed",
    rightClick: ["openApp", "altSpeed", "none"].indexOf(s.rightClick) >= 0 ? s.rightClick : "openApp",
    wakeOnDownload: flag(s.wakeOnDownload) !== false,
    wakeOnUpload: flag(s.wakeOnUpload) === true,
    wakeOnError: flag(s.wakeOnError) === true
  }
}

function clampInt(value, lo, hi, fallback) {
  var n = Number(value)
  if (value === undefined || value === null || value === "" || !isFinite(n)) return fallback
  return Math.max(lo, Math.min(hi, Math.round(n)))
}

// ------------------------------------------------------------------ settings page

// The widget's own settings page (docs/21, docs/27): the manifest's twelve, in four groups. A row
// is a switch ("bool"), a pick of a few values ("enum") or a small number ("int"). `needs`
// names the switch a row only means something with.
function settingRows() {
  return [
    { key: "showMark", group: "In the bar", title: "Show the mark", type: "bool",
      detail: "lights its lower arm while downloading, the upper one while uploading" },
    { key: "showSpeed", group: "In the bar", title: "Show the speed", type: "bool",
      detail: "beside the mark, under it in a vertical bar" },
    { key: "speedMetric", group: "In the bar", title: "Speed shows", type: "enum", needs: "showSpeed",
      detail: "", options: [["down", "↓ down"], ["up", "↑ up"], ["both", "both"]] },
    { key: "markColor", group: "In the bar", title: "Colours", type: "enum",
      detail: "the theme's blue and green, or its accent for both",
      options: [["directions", "blue and green"], ["single", "one"]] },
    { key: "hideWhenIdle", group: "In the bar", title: "Hide while nothing moves", type: "bool",
      detail: "back when something moves; an unreachable server still shows" },
    { key: "middleClick", group: "Clicks on the mark", title: "Middle click", type: "enum",
      detail: "", options: [["altSpeed", "alt-speed"], ["openApp", "omaseed"], ["none", "nothing"]] },
    { key: "rightClick", group: "Clicks on the mark", title: "Right click", type: "enum",
      detail: "", options: [["openApp", "omaseed"], ["altSpeed", "alt-speed"], ["none", "nothing"]] },
    { key: "rareSeeds", group: "In the popup", title: "Few seeds means", type: "int", min: 1, max: 5,
      detail: "Keeping alive counts torrents with this many seeds or fewer" },
    { key: "recentCount", group: "In the popup", title: "Rows in Lately", type: "int", min: 0, max: 5,
      detail: "finished downloads and milestones; 0 hides the section" },
    { key: "wakeOnDownload", group: "omarchy-console's rail", title: "Downloads wake it", type: "bool",
      detail: "while anything receives data" },
    { key: "wakeOnUpload", group: "omarchy-console's rail", title: "Uploads wake it", type: "bool",
      detail: "while anything sends data; a library that always seeds stays up" },
    { key: "wakeOnError", group: "omarchy-console's rail", title: "Errors wake it", type: "bool",
      detail: "as a failure, \"2 errored\"" }
  ]
}

// A dim line under a group's rows, "" for none. The rail's group says who reads it (stock
// Omarchy doesn't, and the widget can't tell which host it is in) and what always wakes it.
function settingGroupNote(group) {
  if (group === "omarchy-console's rail")
    return "Only omarchy-console's rail reads these. qBittorrent unreachable always wakes it, as a failure."
  return ""
}

// Mark and speed: one of them stays on (docs/21 S2). True for the one that is the last on.
function settingLocked(row, cfg) {
  var o = obj(cfg)
  if (!row || row.type !== "bool") return false
  if (row.key === "showMark") return o.showMark === true && o.showSpeed !== true
  if (row.key === "showSpeed") return o.showSpeed === true && o.showMark !== true
  return false
}

// A row that means nothing as things are (the speed's direction with the speed off).
function settingInactive(row, cfg) {
  return !!(row && row.needs && obj(cfg)[row.needs] !== true)
}

// The line under a row's name: why it cannot change, or what it does.
function settingDetail(row, cfg) {
  if (settingLocked(row, cfg))
    return "one of mark and speed stays on: switch the " + (row.key === "showMark" ? "speed" : "mark") + " on first"
  if (settingInactive(row, cfg)) return "with the speed on"
  return row ? String(row.detail || "") : ""
}

// The row's value after a step: a switch flips, a pick or a number moves by `dir` (a
// pick wraps, a number stops at its ends). null when nothing changes.
function settingStep(row, cfg, dir) {
  var o = obj(cfg)
  if (!row || settingLocked(row, o) || settingInactive(row, o)) return null
  var current = o[row.key]
  if (row.type === "bool") return current !== true
  if (row.type === "enum") {
    var values = row.options.map(function(opt) { return opt[0] })
    var i = values.indexOf(current)
    var next = values[((i < 0 ? 0 : i) + (dir < 0 ? -1 : 1) + values.length) % values.length]
    return next === current ? null : next
  }
  if (row.type === "int") {
    var n = Math.max(row.min, Math.min(row.max, num(current) + (dir < 0 ? -1 : 1)))
    return n === current ? null : n
  }
  return null
}

// A value as the page's "saved" line says it: the chip's label, on / off, or the number.
function settingValueText(row, value) {
  if (!row) return String(value)
  if (row.type === "bool") return value === true ? "on" : "off"
  if (row.type === "enum") {
    for (var i = 0; i < row.options.length; i++) if (row.options[i][0] === value) return row.options[i][1]
  }
  return String(value)
}

// The widget's whole entry with one setting changed, as the shell's updateEntryInline
// wants it (it replaces the entry). Everything else the entry holds is kept.
function settingsWith(current, key, value) {
  var out = {}
  var c = obj(current)
  for (var k in c) if (k !== "id") out[k] = c[k]
  out[key] = value
  return out
}

// ------------------------------------------------------------------ tooltip

var CLICK_WORDS = { altSpeed: "alt-speed", openApp: "open omaseed", none: "" }

// A click's words in the tooltip: the alt-speed click named by what it does now, "burst" or
// "turtle" (docs/31 W3). Widget Settings keeps "alt-speed" as the option's name.
function clickWord(action, status) {
  if (action === "altSpeed" && altKind(status) !== "alt") return altKind(status)
  return CLICK_WORDS[action] || ""
}

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
  var known = s === "notRunning" ? null : status
  if (clickWord(o.middleClick, known)) clicks.push("M: " + clickWord(o.middleClick, known))
  if (clickWord(o.rightClick, known)) clicks.push("R: " + clickWord(o.rightClick, known))
  lines.push(clicks.join("  "))
  return lines.join("\n")
}

function unreachableLine(server) {
  var parts = ["unreachable"]
  if (server.error) parts.push(String(server.error))
  if (num(server.retryIn) > 0) parts.push("retrying in " + num(server.retryIn) + " s")
  return parts.join(" · ")
}

// "alternative limits · ↓ 1 MB/s · ↑ 200 KB/s"; for a set that goes one way, its word and
// end: "burst · ↓ ∞ · ↑ ∞ · until 07:00" (docs/31 W2). `withUntil` false leaves the end off
// (the popup draws it dimmer, `altUntilText`).
function altLine(status, withUntil) {
  var alt = obj(status && status.alt)
  var limits = "↓ " + Format.limit(num(alt.dlLimit)) + " · ↑ " + Format.limit(num(alt.upLimit))
  var kind = altKind(status)
  if (kind === "alt") return "alternative limits · " + limits
  var line = kind + " · " + limits
  if (withUntil !== false && altUntilText(status) !== "") line += " " + altUntilText(status)
  return line
}

// "· until 07:00" for a burst or a turtle with an end, "" otherwise.
function altUntilText(status) {
  if (altKind(status) === "alt" || altUntil(status) === "") return ""
  return "· until " + altUntil(status)
}

// "HOME · QBITTORRENT 5.2.3"
function serverLine(status) {
  var server = obj(status && status.server)
  var parts = [String(server.name || "omaseed")]
  if (server.version) parts.push("qBittorrent " + server.version)
  return parts.join(" · ").toUpperCase()
}

// ------------------------------------------------------------------ the shared menu

// docs/33 §11 (D24, DECISIONS C13): the app writes its servers and the menu the tray shows
// too (`servers`, `menu`, added within v2). An app that does not write them leaves both
// empty, and the popup is as it was: no ⋯, no ▾.

// Every server the app knows, in its Settings' order.
function servers(status) {
  return list(status && status.servers).filter(function(s) { return s && typeof s.id === "string" && s.id !== "" })
}

// D19: one server never offers to switch.
function canSwitch(status) {
  return servers(status).length >= 2
}

// The menu's rows as written, minus anything the widget cannot draw. `part` "servers" keeps
// the server rows only (the server line's ▾, W2); anything else is the whole menu (⋯, W1).
// Separators never lead, trail or come twice.
var MENU_KINDS = { action: true, check: true, radio: true, caption: true, separator: true }

function menuRows(status, part) {
  var rows = list(status && status.menu).filter(function(r) {
    return r && typeof r.id === "string" && MENU_KINDS[r.kind] === true
  })
  if (part === "servers")
    rows = rows.filter(function(r) { return r.kind === "radio" && r.id.indexOf("server:") === 0 })
  var out = []
  for (var i = 0; i < rows.length; i++) {
    var r = rows[i]
    if (r.kind === "separator" && (out.length === 0 || out[out.length - 1].kind === "separator")) continue
    out.push(r)
  }
  while (out.length > 0 && out[out.length - 1].kind === "separator") out.pop()
  return out
}

// The rows a cursor stops on: enabled actions, checks and radios.
function menuTargets(rows) {
  var out = []
  for (var i = 0; i < rows.length; i++) {
    var r = rows[i]
    if ((r.kind === "action" || r.kind === "check" || r.kind === "radio") && r.enabled !== false) out.push(i)
  }
  return out
}

// The popup's own key for a row, shown at its right: A alt-speed, m the add sheet.
function menuKey(id) {
  if (id === "alt-speed") return "A"
  if (id === "add") return "m"
  return ""
}

// What picking a row does: "switch" (a server: the popup stays and refills, W1), "stay"
// (alt-speed, stop and start all: the menu closes, the popup stays), "close" (anything that
// brings the window up or quits: the popup closes too).
function menuEffect(id) {
  var key = String(id || "")
  if (key.indexOf("server:") === 0) return "switch"
  if (key === "alt-speed" || key === "stop-all" || key === "start-all") return "stay"
  return "close"
}

// The id a server row stands for, "" for any other row.
function menuServerId(id) {
  var key = String(id || "")
  return key.indexOf("server:") === 0 ? key.slice(7) : ""
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
//   held, onlyYou (1 seed), plusOne (2 seeds), waiting (torrents), peers (people waiting),
//   quiet, and the rarest
//   rows under the threshold, at most three.
function alive(status, threshold) {
  var a = obj(status && status.alive)
  var buckets = list(a.buckets)
  var out = { threshold: threshold, held: 0, onlyYou: 0, plusOne: 0, waiting: 0, peers: 0, quiet: 0, rows: [] }
  for (var i = 0; i < buckets.length; i++) {
    var b = obj(buckets[i])
    var seeds = num(b.seeds)
    if (seeds < 1 || seeds > threshold) continue
    out.held += num(b.count)
    out.waiting += num(b.waiting)
    out.peers += num(b.peers)
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

// What an AddSources answer handed over, added to `previous`: each source that was
// added or was already there, mapped to the server it went to. The popup marks those
// Downloads rows at once instead of waiting for the next status write to say inLibrary.
function handedOver(previous, result, serverName) {
  var out = {}
  var before = obj(previous)
  for (var key in before) out[key] = before[key]
  var r = obj(result)
  var lists = [list(r.added), list(r.duplicates)]
  for (var i = 0; i < lists.length; i++) {
    for (var j = 0; j < lists[i].length; j++) {
      var source = obj(lists[i][j]).source
      if (typeof source === "string" && source !== "") out[source] = serverName
    }
  }
  return out
}

// The server a ~/Downloads row is already on ("" when none): the status file's
// inLibrary, or what the popup just handed over to this same server.
function downloadDuplicateOn(download, serverName, handed) {
  var d = obj(download)
  if (d.inLibrary === true) return serverName
  return obj(handed)[String(d.path || "")] === serverName ? serverName : ""
}

// Where the widget's add puts a torrent with `category` (name or ""). AddSources
// always adds with automatic management, so the category decides: its savePath,
// or the default folder plus its name; no category is the default folder.
function targetFolder(status, category) {
  var add = obj(status && status.add)
  var base = String(add.savePath || "")
  if (!category) return base
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
