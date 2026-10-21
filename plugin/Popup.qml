import QtQuick
import QtQuick.Controls
import Quickshell
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Format.js" as Format
import "Status.js" as Status

// The popup behind the mark (docs/20 §3): one view, in the shell's card style.
//
//   head      the live mark, "omaseed ↗", the server line (▾ with two servers or more:
//             the servers), and four buttons: alt-speed, open omaseed, the widget's own
//             settings (docs/21), and ⋯, the app's shared menu (docs/33 §11, D24)
//   now       ↓ and ↑, the alternative limits while they are on, an hour of upload
//   sections  Add, Your share, Keeping alive, In flight, Lately; each folds by its
//             header or `space`, keeps one line while folded, and is remembered
//
// Add has no text field: it lists what is ready (what was pasted with ctrl+v anywhere
// in the popup, a magnet on the clipboard when the popup opened, today's .torrent files
// in ~/Downloads) and hands the ticked ones to omaseed, which adds them, checks
// duplicates and remembers the category.
//
// The gear (or `,`) swaps the sections for the widget's settings page (SettingsPage.qml,
// docs/21): the manifest's nine settings, saved through Widget.qml as they change, and a
// row for omaseed's own Settings. `esc` comes back.
//
// Widget.qml owns the bar and the Service; this panel reads `host` and acts through it.
Panel {
  id: root
  moduleName: "chaves.omaseed"
  manageIpc: false

  property var anchorItem: null
  property var host: null
  property var hostWidget: null
  readonly property var barIdentity: hostWidget || root

  readonly property var service: host ? host.service : null
  readonly property var status: service ? service.status : null
  readonly property real now: service ? service.now : Date.now() / 1000
  readonly property var cfg: host ? host.cfg : Status.barOptions({})
  readonly property string state: service ? service.state : "notRunning"
  readonly property bool running: state !== "notRunning"
  readonly property bool reachable: running && state !== "unreachable"
  readonly property bool altOn: service ? service.altOn : false
  // docs/31: burst is green and holds nothing back; turtle and plain alt-speed are yellow.
  readonly property string altKind: service ? service.altKind : "alt"
  readonly property bool altSlows: altOn && altKind !== "burst"
  readonly property var mark: service ? service.markState : Status.markState(null, 0)

  readonly property color foreground: bar ? bar.foreground : Commons.Color.foreground
  readonly property color urgent: bar ? bar.urgent : Commons.Color.urgent
  readonly property color accent: Commons.Color.accent
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property color blue: service ? service.blue : accent
  readonly property color green: service ? service.green : foreground
  readonly property color yellow: service ? service.yellow : urgent
  readonly property color altInk: altKind === "burst" ? green : yellow
  readonly property color downInk: cfg.markColor === "single" ? accent : blue
  readonly property color upInk: cfg.markColor === "single" ? accent : green

  readonly property var server: status && status.server ? status.server : ({})
  readonly property string serverName: String(server.name || "omaseed")
  readonly property var folds: service ? service.folds : ({ add: false, share: true, alive: true, flight: false, lately: true })

  // ------------------------------------------------------------- open / close

  function openPanel() {
    if (service) service.now = Date.now() / 1000
    // Each opening starts on the sections; `omarchy-shell chaves.omaseed settings` asks
    // for the page after this.
    page = "main"
    menuPart = ""
    root.controller.show()
    // "Added 2" answers the last add; a later opening has nothing to answer.
    addNote = ""
    probeClipboard()
    Qt.callLater(function() {
      if (root.opened) setCenterHoverRevealSuppressed(true)
      keys.forceActiveFocus()
      // Each opening shows the top (onOpenedChanged), so the cursor starts there too: one
      // left on a row further down would be out of sight, and j k would seem to do nothing.
      if (root.targets.length > 0) root.cursorKey = root.targets[0].key
    })
  }

  function close() {
    setCenterHoverRevealSuppressed(false)
    root.controller.hide()
  }

  // Open (or start) omaseed and let the popup go: the open button, Start omaseed, `o`, the
  // title, and ↵ while the app is not running.
  function openApp() {
    if (service) service.openApp()
    close()
  }

  function toggle() { root.opened ? root.close() : root.openPanel() }

  function switchPanel(direction) {
    if (root.bar && typeof root.bar.switchPanelFrom === "function")
      return root.bar.switchPanelFrom(root.barIdentity, direction)
    return false
  }

  function setCenterHoverRevealSuppressed(value) {
    if (root.bar && typeof root.bar.setCenterHoverRevealSuppressed === "function")
      root.bar.setCenterHoverRevealSuppressed(value)
  }

  onOpenedChanged: if (opened && flick) flick.contentY = 0

  // ------------------------------------------------------------- the shared menu

  // docs/33 §11 (D24, W1, W2; DECISIONS C13): ⋯ (ctrl+.) opens the app's menu, the one the
  // tray shows; the server line's ▾ (ctrl+s) opens just its servers. "" while neither is.
  property string menuPart: ""
  property int menuCursor: -1
  readonly property bool hasMenu: page === "main" && running && service !== null && service.menuRows.length > 0
  readonly property bool canSwitch: page === "main" && service !== null && service.canSwitch
  readonly property var menuShown: !service || menuPart === "" ? []
    : (menuPart === "servers" ? service.serverRows : service.menuRows)

  function toggleMenu(part) {
    if (part === "menu" && !hasMenu) return
    if (part === "servers" && !canSwitch) return
    if (menuPart === part) { closeMenu(); return }
    menuPart = part
    // The cursor starts on the server the app is on, or on the first row.
    var targets = Status.menuTargets(menuShown)
    menuCursor = targets.length > 0 ? targets[0] : -1
    for (var i = 0; i < targets.length; i++)
      if (menuShown[targets[i]].kind === "radio" && menuShown[targets[i]].checked === true) { menuCursor = targets[i]; break }
    if (flick) flick.contentY = 0
    keys.forceActiveFocus()
  }

  function closeMenu() {
    menuPart = ""
    menuCursor = -1
    keys.forceActiveFocus()
  }

  function moveMenuCursor(dy) {
    var targets = Status.menuTargets(menuShown)
    if (targets.length === 0) return
    var at = targets.indexOf(menuCursor)
    at = at < 0 ? 0 : Math.max(0, Math.min(targets.length - 1, at + dy))
    menuCursor = targets[at]
  }

  // A server switches the app and the popup stays to refill (W1); alt-speed, stop and start
  // all close the menu only; the rest bring the window up, or quit, and the popup goes.
  function pickMenu(row) {
    if (!row || !service || row.enabled === false) return
    var effect = Status.menuEffect(row.id)
    if (effect === "switch") service.switchServer(Status.menuServerId(row.id))
    else service.invoke(row.id)
    closeMenu()
    if (effect === "close") close()
  }

  // ------------------------------------------------------------- settings page

  // "main" (the sections) or "settings" (the widget's own settings page).
  property string page: "main"
  property int settingsCursor: 0
  property string settingsNote: ""
  readonly property var settingRows: Status.settingRows()

  function openSettingsPage() {
    menuPart = ""
    page = "settings"
    settingsCursor = 0
    settingsNote = ""
    if (flick) flick.contentY = 0
    keys.forceActiveFocus()
  }

  function backToMain() {
    page = "main"
    if (flick) flick.contentY = 0
    keys.forceActiveFocus()
  }

  function moveSettingsCursor(dy) {
    settingsCursor = Math.max(0, Math.min(settingRows.length, settingsCursor + dy))
  }

  function saveSetting(index, value) {
    var r = settingRows[index]
    if (!r || value === null || value === undefined || value === cfg[r.key]) return
    if (!host || typeof host.saveSetting !== "function") return
    host.saveSetting(r.key, value)
    settingsNote = "saved · " + r.title.toLowerCase() + ": " + Status.settingValueText(r, value)
  }

  function stepSetting(index, dir) {
    settingsCursor = index
    if (index >= settingRows.length) { openAppSettings(); return }
    saveSetting(index, Status.settingStep(settingRows[index], cfg, dir))
  }

  function chooseSetting(index, value) {
    settingsCursor = index
    var r = settingRows[index]
    if (!r || Status.settingInactive(r, cfg)) return
    saveSetting(index, value)
  }

  function openAppSettings() {
    if (service) service.openSettings()
    close()
  }

  // ------------------------------------------------------------- folds

  function folded(id) { return folds[id] === true }

  function setFolded(id, value) {
    if (!service) return
    var next = {}
    for (var k in folds) next[k] = folds[k]
    next[id] = value
    service.setFolds(next)
  }

  function toggleFold(id) {
    setFolded(id, !folded(id))
    cursorKey = "h:" + id
  }

  // `z`: fold every section, or unfold them all when all are folded already.
  function toggleAllFolds() {
    if (!service) return
    var anyOpen = false
    var ids = visibleSections()
    for (var i = 0; i < ids.length; i++) if (!folded(ids[i])) anyOpen = true
    var next = {}
    for (var k in folds) next[k] = folds[k]
    for (var j = 0; j < ids.length; j++) next[ids[j]] = anyOpen
    service.setFolds(next)
    var t = targetAt(cursorKey)
    if (t) cursorKey = "h:" + t.section
  }

  function visibleSections() {
    if (!running) return []
    var ids = ["add", "share", "alive", "flight"]
    if (cfg.recentCount > 0) ids.push("lately")
    return ids
  }

  // ------------------------------------------------------------- data

  readonly property var speedNow: status && status.speed ? status.speed : ({ dl: 0, up: 0 })
  readonly property var share: status && status.share ? status.share : ({})
  readonly property var weekBars: Status.weekBars(status ? status.week : [])
  readonly property var hour: status && Array.isArray(status.uploadHour) ? status.uploadHour : []
  readonly property var aliveInfo: Status.alive(status, cfg.rareSeeds)
  readonly property var activeRows: status && Array.isArray(status.active) ? status.active.slice(0, 3) : []
  readonly property var recentRows: Status.recent(status, cfg.recentCount)
  readonly property var topRows: status && Array.isArray(status.topSeeders) ? status.topSeeders.slice(0, 2) : []
  readonly property var categories: status && Array.isArray(status.categories) ? status.categories : []
  readonly property var addInfo: status && status.add ? status.add : ({})

  // ------------------------------------------------------------- add

  // What was pasted, browsed, or found on the clipboard: { source, origin, name, kind,
  // size, duplicateOn, invalid }. Today's ~/Downloads files come from the status file.
  property var extraRows: []
  // Ticks the user changed, by source; everything else is ticked unless it is a duplicate.
  property var tickOverrides: ({})
  // Sources the last adds handed over, by source → server: a Downloads row reads "already
  // on" at once, before the status file's next write says inLibrary.
  property var handed: ({})
  // The category chip picked here; null follows the one the last add used.
  property var pickedCategory: null
  property string addNote: ""
  property bool addNoteUrgent: false

  readonly property string category: pickedCategory !== null ? pickedCategory : String(addInfo.category || "")

  readonly property var addRows: {
    var out = []
    var seen = {}
    for (var i = 0; i < extraRows.length; i++) {
      var r = extraRows[i]
      seen[r.source] = true
      out.push(addRowFor(r.source, r.name, originText(r), r.duplicateOn, r.invalid))
    }
    var downloads = status && Array.isArray(status.downloads) ? status.downloads : []
    for (var j = 0; j < downloads.length; j++) {
      var d = downloads[j]
      if (!d || !d.path || seen[d.path]) continue
      var when = Format.ago(d.at, now)
      out.push(addRowFor(String(d.path), String(d.name || Status.sourceName(d.path)),
                         "Downloads" + (when === "" ? "" : " · " + when),
                         Status.downloadDuplicateOn(d, serverName, handed), ""))
    }
    return out
  }

  function addRowFor(source, name, origin, duplicateOn, invalid) {
    var ticked = source in tickOverrides ? tickOverrides[source] : (duplicateOn === "" && !invalid)
    return { key: "add:" + source, source: source, name: name, origin: origin,
             duplicateOn: duplicateOn || "", invalid: invalid || "", ticked: ticked && !invalid }
  }

  function originText(r) {
    var detail = r.size > 0 ? Format.size(r.size) : (r.kind === "magnet" ? "magnet" : (r.kind === "link" ? "link" : "file"))
    return r.origin + " · " + detail
  }

  readonly property var tickedSources: {
    var out = []
    for (var i = 0; i < addRows.length; i++) if (addRows[i].ticked) out.push(addRows[i].source)
    return out
  }

  function addSummary() {
    if (addRows.length === 0) return "nothing to add"
    var origins = []
    for (var i = 0; i < addRows.length; i++) {
      var o = addRows[i].origin.split(" · ")[0]
      if (origins.indexOf(o) < 0) origins.push(o)
    }
    return tickedSources.length + " ready: " + origins.join(" and ")
  }

  function toggleTick(source) {
    var row = null
    for (var i = 0; i < addRows.length; i++) if (addRows[i].source === source) row = addRows[i]
    if (!row || row.invalid) return
    var next = {}
    for (var k in tickOverrides) next[k] = tickOverrides[k]
    next[source] = !row.ticked
    tickOverrides = next
  }

  // Text from ctrl+v (`origin` "pasted") or the clipboard probe ("clipboard"): every
  // magnet, link or .torrent path in it becomes a row, described by the app.
  function addText(text, origin, unfold) {
    var sources = Status.addable(text)
    if (sources.length === 0) {
      if (origin === "pasted") { addNote = "Nothing to add on the clipboard"; addNoteUrgent = false }
      return
    }
    addSources(sources, origin)
    if (unfold) {
      if (folded("add")) setFolded("add", false)
      cursorKey = "add:" + sources[0]
    }
  }

  function addSources(sources, origin) {
    var next = extraRows.slice()
    var fresh = []
    for (var i = 0; i < sources.length; i++) {
      var s = sources[i]
      var at = -1
      for (var j = 0; j < next.length; j++) if (next[j].source === s) at = j
      if (at >= 0) {
        // Pasted beats "on the clipboard".
        if (origin === "pasted" && next[at].origin !== "pasted") {
          var copy = Object.assign({}, next[at])
          copy.origin = "pasted"
          next[at] = copy
        }
        continue
      }
      next.push({ source: s, origin: origin, name: Status.sourceName(s), kind: Status.sourceKind(s), size: 0,
                  duplicateOn: "", invalid: "" })
      fresh.push(s)
    }
    extraRows = next
    addNote = ""
    if (fresh.length > 0 && service) service.describe(fresh, function(answer) { root.applyDescription(fresh, answer) })
  }

  // DescribeSources: one entry per source, in order.
  function applyDescription(sources, answer) {
    if (!Array.isArray(answer)) return
    var next = extraRows.slice()
    for (var i = 0; i < sources.length && i < answer.length; i++) {
      var a = answer[i] || {}
      for (var j = 0; j < next.length; j++) {
        if (next[j].source !== sources[i]) continue
        var row = Object.assign({}, next[j])
        if (a.name) row.name = String(a.name)
        if (a.kind) row.kind = String(a.kind)
        if (typeof a.size === "number" && a.size > 0) row.size = a.size
        row.duplicateOn = a.duplicate ? String(a.duplicateOn || serverName) : ""
        row.invalid = a.valid === false ? String(a.error || "not a torrent") : ""
        next[j] = row
      }
    }
    extraRows = next
  }

  function browse() {
    if (!service || !reachable) return
    service.browse(function(paths) {
      root.addSources(paths, "browsed")
      if (root.folded("add")) root.setFolded("add", false)
    })
  }

  // A "clipboard" row is what is on the clipboard now: the last opening's goes, and the probe
  // brings it back (described again) if it is still there. Pasted and browsed rows stay.
  function probeClipboard() {
    extraRows = extraRows.filter(function(r) { return r.origin !== "clipboard" })
    if (service && reachable) service.readClipboard(function(text) { root.addText(text, "clipboard", false) })
  }

  function paste() {
    if (service && reachable) service.readClipboard(function(text) { root.addText(text, "pasted", true) })
  }

  function count(value) {
    return Array.isArray(value) ? value.length : (Number(value) || 0)
  }

  function addTicked(stopped) {
    if (!service || !reachable || service.adding) return
    var sources = tickedSources
    if (sources.length === 0) { addNote = "Tick something to add"; addNoteUrgent = false; return }
    addNote = "Adding…"
    addNoteUrgent = false
    var started = service.addSources(sources, category, stopped, function(result) {
      if (result === null) {
        root.addNote = "omaseed did not answer"
        root.addNoteUrgent = true
        return
      }
      var parts = []
      var added = root.count(result.added)
      var dupes = root.count(result.duplicates)
      var failed = root.count(result.failed)
      parts.push("Added " + added + (stopped ? " stopped" : ""))
      if (dupes > 0) parts.push(dupes + " already on " + root.serverName)
      if (failed > 0) parts.push(failed + " failed")
      root.addNote = parts.join(" · ")
      root.addNoteUrgent = failed > 0
      root.handed = Status.handedOver(root.handed, result, root.serverName)
      // What was handed over leaves the list; the next status write moves Downloads.
      root.extraRows = root.extraRows.filter(function(r) { return sources.indexOf(r.source) < 0 })
      var ticks = {}
      for (var k in root.tickOverrides) if (sources.indexOf(k) < 0) ticks[k] = root.tickOverrides[k]
      root.tickOverrides = ticks
    })
    if (!started) addNote = ""
  }

  function openAddSheet() {
    if (!service || !reachable) return
    service.openAddSheet(tickedSources)
    close()
  }

  // ------------------------------------------------------------- cursor

  // Every cursor stop in order: each section's header, then its rows while unfolded.
  readonly property var targets: {
    var out = []
    if (!running) return out
    var ids = visibleSections()
    for (var i = 0; i < ids.length; i++) {
      var id = ids[i]
      out.push({ key: "h:" + id, section: id, hash: "" })
      if (folded(id)) continue
      if (id === "add")
        for (var a = 0; a < addRows.length; a++) out.push({ key: addRows[a].key, section: id, hash: "", source: addRows[a].source })
      else if (id === "share")
        for (var t = 0; t < topRows.length; t++) out.push({ key: "top:" + topRows[t].hash, section: id, hash: topRows[t].hash })
      else if (id === "alive")
        for (var v = 0; v < aliveInfo.rows.length; v++) out.push({ key: "alive:" + aliveInfo.rows[v].hash, section: id, hash: aliveInfo.rows[v].hash })
      else if (id === "flight")
        for (var f = 0; f < activeRows.length; f++) out.push({ key: "flight:" + activeRows[f].hash, section: id, hash: activeRows[f].hash })
      else if (id === "lately")
        for (var l = 0; l < recentRows.length; l++) out.push({ key: latelyKey(recentRows[l]), section: id, hash: recentRows[l].hash })
    }
    return out
  }

  property string cursorKey: ""
  // While the shared menu is open its cursor is the only one drawn; the sections keep theirs.
  readonly property string shownCursorKey: menuPart === "" ? cursorKey : ""

  function latelyKey(row) { return "lately:" + row.kind + ":" + row.hash }

  function targetIndex(key) {
    for (var i = 0; i < targets.length; i++) if (targets[i].key === key) return i
    return -1
  }

  function targetAt(key) {
    var i = targetIndex(key)
    return i >= 0 ? targets[i] : null
  }

  function moveCursor(dy) {
    if (targets.length === 0) return
    var i = targetIndex(cursorKey)
    i = i < 0 ? 0 : Math.max(0, Math.min(targets.length - 1, i + dy))
    cursorKey = targets[i].key
  }

  // Keep the cursor on screen: rows report themselves when the cursor lands on them.
  function reveal(item) {
    if (!item || !flick) return
    var p = item.mapToItem(column, 0, 0)
    var margin = Style.space(8)
    if (p.y - margin < flick.contentY) flick.contentY = Math.max(0, p.y - margin)
    else if (p.y + item.height + margin > flick.contentY + flick.height)
      flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), p.y + item.height + margin - flick.height)
  }

  // ↵: Add on Add; open the folder on a Lately row; show the torrent on any other row;
  // fold on any other header.
  function activate(stopped) {
    if (!running) { openApp(); return }
    var t = targetAt(cursorKey)
    if (!t) return
    if (t.section === "add") { addTicked(stopped); return }
    if (t.key.indexOf("h:") === 0) { toggleFold(t.section); return }
    if (!reachable) return
    if (t.section === "lately") { service.openFolder(t.hash); close() }
    else { service.showTorrent(t.hash); close() }
  }

  function showCursorTorrent() {
    var t = targetAt(cursorKey)
    if (!t || !t.hash || !reachable) return
    service.showTorrent(t.hash)
    close()
  }

  // x: tick or untick the Add row under the cursor, as in omaseed's Files tab.
  function tickCursorRow() {
    var t = targetAt(cursorKey)
    if (t && t.section === "add" && t.source) toggleTick(t.source)
  }

  function foldCursorSection() {
    var t = targetAt(cursorKey)
    if (t) toggleFold(t.section)
  }

  function goToAdd() {
    if (!running) return
    if (folded("add")) setFolded("add", false)
    cursorKey = "h:add"
  }

  // The key line under the card: every key the popup answers, the ↵ word following the
  // cursor. Keys bright, words dim, unboxed (v2 delta 05).
  function hints() {
    var list
    if (page === "settings") {
      list = [["j k", "move"], ["space ↵", "change"], ["h l", "step"], ["o", "omaseed settings"], ["esc", "back"]]
    } else if (!running) {
      list = [["↵", "start"], ["esc", "close"]]
    } else if (menuPart !== "") {
      list = [["j k", "move"], ["↵", "pick"], [menuPart === "servers" ? "ctrl+s" : "ctrl+.", "close"], ["esc", "back"]]
    } else {
      var t = targetAt(cursorKey)
      var enter = !t ? "" : (t.section === "add" ? "add" : (t.key.indexOf("h:") === 0 ? "fold"
                  : (t.section === "lately" ? "folder" : "show")))
      list = [["a", "add"], ["o", "omaseed"], ["A", "alt-speed"], ["j k", "move"], ["space", "fold"], ["z", "fold all"], [",", "widget settings"]]
      if (enter !== "") list.push(["↵", enter])
      if (t && t.section === "add") {
        if (t.source) list.push(["x", "tick"])
        list.push(["⇧↵", "add stopped"], ["m", "add sheet"], ["b", "browse"])
      }
      else if (t && t.hash) list.push(["s", "show"])
      if (!reachable) list.push(["r", "retry"])
      if (hasMenu) list.push(["ctrl+.", "menu"])
      if (canSwitch) list.push(["ctrl+s", "servers"])
      list.push(["esc", "close"])
    }
    var bright = foreground.toString()
    var faint = dim.toString()
    var parts = []
    for (var i = 0; i < list.length; i++)
      parts.push("<font color=\"" + bright + "\">" + list[i][0] + "</font> <font color=\"" + faint + "\">" + list[i][1] + "</font>")
    return parts.join("<font color=\"" + faint + "\"> · </font>")
  }

  // ------------------------------------------------------------- card

  KeyboardPanel {
    id: panel
    anchorItem: root.anchorItem
    owner: root.barIdentity
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(380))
    contentHeight: panel.fittedContentHeight(column.implicitHeight, Style.space(900))

    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Keys.priority: Keys.BeforeItem
      Keys.onPressed: function(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var shift = (event.modifiers & Qt.ShiftModifier) !== 0
        var handled = true
        if (root.page === "settings") {
          if (event.key === Qt.Key_Escape) root.backToMain()
          else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
            root.switchPanel(shift || event.key === Qt.Key_Backtab ? -1 : 1)
          else if (ctrl) handled = false
          else if (event.key === Qt.Key_Down || event.text === "j") root.moveSettingsCursor(1)
          else if (event.key === Qt.Key_Up || event.text === "k") root.moveSettingsCursor(-1)
          else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Right || event.text === "l")
            root.stepSetting(root.settingsCursor, 1)
          else if (event.key === Qt.Key_Left || event.text === "h") {
            if (root.settingsCursor < root.settingRows.length && root.settingRows[root.settingsCursor].type !== "bool")
              root.stepSetting(root.settingsCursor, -1)
          }
          else if (event.text === "o") root.openAppSettings()
          else handled = false
          event.accepted = handled
          return
        }
        // docs/33 §11 change 4: ctrl+. the menu, ctrl+s the servers, the app's own keys.
        if (ctrl && event.key === Qt.Key_Period) { root.toggleMenu("menu"); event.accepted = true; return }
        if (ctrl && event.key === Qt.Key_S) { root.toggleMenu("servers"); event.accepted = true; return }
        if (root.menuPart !== "") {
          var menuKey = true
          if (event.key === Qt.Key_Escape) root.closeMenu()
          else if (event.key === Qt.Key_Down || event.text === "j") root.moveMenuCursor(1)
          else if (event.key === Qt.Key_Up || event.text === "k") root.moveMenuCursor(-1)
          else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)
            root.pickMenu(root.menuShown[root.menuCursor])
          else menuKey = false
          if (menuKey) { event.accepted = true; return }
        }
        if (event.key === Qt.Key_Escape) root.close()
        else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
          root.switchPanel(shift || event.key === Qt.Key_Backtab ? -1 : 1)
        else if (ctrl && event.key === Qt.Key_V) root.paste()
        else if (ctrl) handled = false
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) root.activate(shift)
        else if (event.key === Qt.Key_Down || event.text === "j") root.moveCursor(1)
        else if (event.key === Qt.Key_Up || event.text === "k") root.moveCursor(-1)
        else if (event.key === Qt.Key_Space) root.foldCursorSection()
        else if (event.text === "x") root.tickCursorRow()
        else if (event.text === "a") root.goToAdd()
        else if (event.text === "b") root.browse()
        else if (event.text === "m") root.openAddSheet()
        else if (event.text === "o") root.openApp()
        else if (event.text === "A") { if (root.service && root.reachable) root.service.toggleAltSpeed() }
        else if (event.text === "z") root.toggleAllFolds()
        else if (event.text === ",") root.openSettingsPage()
        else if (event.text === "s") root.showCursorTorrent()
        else if (event.text === "r") { if (root.service && root.running && !root.reachable) root.service.retry() }
        else handled = false
        event.accepted = handled
      }

      Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
          id: column
          width: flick.width
          spacing: Style.space(12)

          // ====================================================== head

          Item {
            width: parent.width
            implicitHeight: Math.max(headMark.height, headText.implicitHeight, headButtons.implicitHeight)

            PanelActionButton {
              id: backButton
              visible: root.page === "settings"
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
              bordered: true
              tooltipText: "Back (esc)"
              foreground: root.dim
              hoverColor: root.foreground
              fontFamily: root.fontFamily
              onClicked: root.backToMain()
              // A screen reader (#80): named without the key, which goes in the description.
              // Not `accessibleName` (an Omarchy without PR #13212 would fail to load the
              // widget); where the kit has it, the kit presses and this steps aside, so one
              // press is one click. The same on every kit button below.
              Accessible.role: Accessible.Button
              Accessible.name: "Back"
              Accessible.description: "shortcut esc"
              Accessible.onPressAction: if (!("accessibleName" in backButton)) backButton.clicked()
              LineIcon {
                anchors.centerIn: parent
                size: Style.font.icon
                name: "back"
                color: parent.foreground
              }
            }

            Mark {
              id: headMark
              visible: root.page === "main"
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
              size: Style.font.display
              width: size
              height: size
              opacity: root.mark.dim ? 0.45 : 1
              downLit: root.mark.down
              upLit: root.mark.up
              downColor: root.downInk
              upColor: root.upInk
              offColor: root.mark.dim ? root.foreground : Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.3)
              squareOutline: root.mark.square === "alt" || root.mark.square === "burst" || root.mark.square === "unreachable"
              squareColor: root.mark.square === "alt" ? root.yellow : root.mark.square === "burst" ? root.green
                : (root.mark.square === "error" || root.mark.square === "unreachable" ? root.urgent : root.foreground)
            }

            Column {
              id: headText
              anchors.left: root.page === "settings" ? backButton.right : headMark.right
              anchors.leftMargin: Style.space(12)
              anchors.right: headButtons.left
              anchors.rightMargin: Style.space(8)
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(3)

              Row {
                spacing: Style.space(4)

                Text {
                  id: titleText
                  textFormat: Text.PlainText
                  text: root.page === "settings" ? "Widget settings" : "omaseed"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.title
                  font.bold: true
                  // Its click opens omaseed, as the open button does: a screen reader has that
                  // button, so the title is the heading it reads.
                  Accessible.role: Accessible.Heading
                  Accessible.name: text
                }

                LineIcon {
                  visible: root.page === "main"
                  anchors.verticalCenter: titleText.verticalCenter
                  name: "open"
                  size: Style.font.bodySmall
                  color: root.dim
                }
              }

              Row {
                id: serverLine
                width: parent.width
                spacing: Style.space(6)

                // A screen reader (#80): the line as written; with two servers or more it is
                // the servers button its ▾ shows (ctrl+s), and says whether they are open.
                Accessible.role: root.canSwitch ? Accessible.Button : Accessible.StaticText
                // a11y-ignore A6: the server's name and version: it changes when the server does
                Accessible.name: root.page === "settings" ? "omaseed in the bar"
                                 : (root.running ? Status.serverWords(root.status) : "not running")
                Accessible.description: !root.canSwitch ? ""
                                        : (root.menuPart === "servers" ? "servers, open, shortcut ctrl+s" : "servers, shortcut ctrl+s")
                Accessible.onPressAction: root.toggleMenu("servers")

                Rectangle {
                  visible: root.page === "main"
                  anchors.verticalCenter: parent.verticalCenter
                  width: Style.space(6)
                  height: width
                  radius: width / 2
                  color: !root.running ? root.dim : (root.reachable ? root.accent : root.urgent)
                }

                Text {
                  id: serverText
                  width: Math.min(implicitWidth, parent.width - Style.space(12) - (chevron.visible ? chevron.width + parent.spacing : 0))
                  textFormat: Text.PlainText
                  text: root.page === "settings" ? "OMASEED IN THE BAR"
                        : (root.running ? Status.serverLine(root.status) : "NOT RUNNING")
                  color: root.menuPart === "servers" ? root.foreground : root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                  font.letterSpacing: Style.space(1)
                  elide: Text.ElideRight
                }

                // W2: with two servers or more the line takes a click (and ctrl+s) and opens
                // the servers. One server: no ▾ (D19).
                Text {
                  id: chevron
                  visible: root.canSwitch
                  anchors.verticalCenter: parent.verticalCenter
                  textFormat: Text.PlainText
                  text: root.menuPart === "servers" ? "▴" : "▾"
                  color: root.menuPart === "servers" ? root.accent : root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                }
              }
            }

            // Not a dismiss area: the title's and the server line's click. A screen reader has
            // both without it: the open button opens omaseed, the server line is a button itself.
            // a11y-ignore A2: its two targets are published (the open button, the server line)
            MouseArea {
              anchors.fill: headText
              enabled: root.page === "main"
              cursorShape: Qt.PointingHandCursor
              onClicked: function(mouse) {
                var p = mapToItem(serverLine, mouse.x, mouse.y)
                if (root.canSwitch && p.y >= 0 && p.y <= serverLine.height) { root.toggleMenu("servers"); return }
                root.openApp()
              }
            }

            Row {
              id: headButtons
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(6)

              PanelActionButton {
                id: altButton
                visible: root.page === "main"
                bordered: true
                enabled: root.reachable
                tooltipText: Status.altButtonTip(root.status)
                foreground: root.altOn ? root.altInk : root.dim
                hoverColor: root.altOn ? root.altInk : root.foreground
                fontFamily: root.fontFamily
                onClicked: if (root.service) root.service.toggleAltSpeed()
                Accessible.role: Accessible.Button
                // a11y-ignore A6: one of three words, by the kind of set the app has; it changes when that does
                Accessible.name: Status.altButtonName(root.status)
                Accessible.description: (root.altOn ? "on, " : "") + "shortcut A"
                Accessible.onPressAction: if (!("accessibleName" in altButton) && altButton.enabled) altButton.clicked()
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: root.altKind === "burst" ? "burst" : "altSpeed"
                  color: parent.foreground
                  opacity: parent.enabled ? 1 : 0.5
                }
              }

              PanelActionButton {
                id: openButton
                visible: root.page === "main"
                bordered: true
                tooltipText: root.running ? "Open omaseed (o)" : "Start omaseed"
                foreground: root.dim
                hoverColor: root.foreground
                fontFamily: root.fontFamily
                onClicked: root.openApp()
                Accessible.role: Accessible.Button
                Accessible.name: root.running ? "Open omaseed" : "Start omaseed"
                Accessible.description: root.running ? "shortcut o" : ""
                Accessible.onPressAction: if (!("accessibleName" in openButton)) openButton.clicked()
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "mark"
                  color: parent.foreground
                }
              }

              PanelActionButton {
                id: gearButton
                bordered: true
                tooltipText: root.page === "settings" ? "Back to the popup (esc)" : "Widget settings (,)"
                foreground: root.page === "settings" ? root.accent : root.dim
                hoverColor: root.page === "settings" ? root.accent : root.foreground
                fontFamily: root.fontFamily
                onClicked: root.page === "settings" ? root.backToMain() : root.openSettingsPage()
                Accessible.role: Accessible.Button
                Accessible.name: root.page === "settings" ? "Back to the popup" : "Widget settings"
                Accessible.description: root.page === "settings" ? "shortcut esc" : "shortcut ,"
                Accessible.onPressAction: if (!("accessibleName" in gearButton)) gearButton.clicked()
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "gear"
                  color: parent.foreground
                }
              }

              // docs/33 §11 change 3 (W1): ⋯, the app's shared menu. Not there for an app that
              // writes no menu.
              PanelActionButton {
                id: menuButton
                visible: root.hasMenu
                bordered: true
                tooltipText: "Menu (ctrl+.)"
                foreground: root.menuPart === "menu" ? root.accent : root.dim
                hoverColor: root.menuPart === "menu" ? root.accent : root.foreground
                fontFamily: root.fontFamily
                onClicked: root.toggleMenu("menu")
                Accessible.role: Accessible.Button
                Accessible.name: "Menu"
                Accessible.description: root.menuPart === "menu" ? "omaseed's menu, open, shortcut ctrl+."
                                                                 : "omaseed's menu, shortcut ctrl+."
                Accessible.onPressAction: if (!("accessibleName" in menuButton)) menuButton.clicked()
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "more"
                  color: parent.foreground
                }
              }
            }
          }

          PanelSeparator { width: parent.width; foreground: root.foreground }

          // ====================================================== the shared menu

          MenuList {
            visible: root.page === "main" && root.menuPart !== ""
            width: parent.width
            rows: root.menuShown
            cursorIndex: root.menuCursor
            foreground: root.foreground
            dim: root.dim
            urgent: root.urgent
            accent: root.accent
            fontFamily: root.fontFamily
            onPicked: function(row) { root.pickMenu(row) }
            onHoveredRow: function(index) { root.menuCursor = index }
            onCursorItem: function(item) { root.reveal(item) }
          }

          PanelSeparator {
            visible: root.page === "main" && root.menuPart !== ""
            width: parent.width
            foreground: root.foreground
          }

          // ====================================================== settings page

          SettingsPage {
            visible: root.page === "settings"
            width: parent.width
            cfg: root.cfg
            cursorIndex: root.settingsCursor
            note: root.settingsNote
            foreground: root.foreground
            dim: root.dim
            warn: root.yellow
            fontFamily: root.fontFamily
            onStepped: function(index, dir) { root.stepSetting(index, dir) }
            onChosen: function(index, value) { root.chooseSetting(index, value) }
            onHoveredRow: function(index) { root.settingsCursor = index }
            onAppSettings: root.openAppSettings()
            onCursorItem: function(item) { root.reveal(item) }
          }

          // ====================================================== not running

          Column {
            visible: root.page === "main" && !root.running
            width: parent.width
            spacing: Style.space(10)

            Text {
              width: parent.width
              textFormat: Text.PlainText
              Accessible.role: Accessible.StaticText
              Accessible.name: text
              text: "omaseed is not running."
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
              wrapMode: Text.WordWrap
            }

            Text {
              width: parent.width
              textFormat: Text.PlainText
              Accessible.role: Accessible.StaticText
              Accessible.name: text
              text: "The widget shows what the app reports."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            Button {
              id: startButton
              text: "Start omaseed"
              bordered: true
              hasCursor: true
              foreground: root.accent
              fontFamily: root.fontFamily
              onClicked: root.openApp()
              Accessible.role: Accessible.Button
              Accessible.name: "Start omaseed"
              Accessible.description: "shortcut enter"
              Accessible.onPressAction: if (!("accessibleName" in startButton)) startButton.clicked()
            }
          }

          // ====================================================== now

          Column {
            visible: root.page === "main" && root.running
            width: parent.width
            spacing: Style.space(10)

            Row {
              spacing: Style.space(28)

              Stat {
                arrow: "↓"
                arrowColor: root.altSlows ? root.yellow : root.downInk
                value: root.reachable ? Format.speed(root.speedNow.dl) : "—"
                valueColor: root.altSlows ? root.yellow : root.foreground
                caption: "Download"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Stat {
                arrow: "↑"
                arrowColor: root.altSlows ? root.yellow : root.upInk
                value: root.reachable ? Format.speed(root.speedNow.up) : "—"
                valueColor: root.altSlows ? root.yellow : root.foreground
                caption: "Upload"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }
            }

            Row {
              visible: root.reachable && root.altOn
              spacing: Style.space(6)
              Accessible.role: Accessible.StaticText
              // a11y-ignore A6: a label, not a control; a screen reader reads it when it gets there, and its changes are not announced
              Accessible.name: Status.spoken(Status.altLine(root.status, true))

              LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.altKind === "burst" ? "burst" : "altSpeed"
                size: Style.font.body
                color: root.altInk
              }

              Text {
                textFormat: Text.PlainText
                text: Status.altLine(root.status, false)
                color: root.altInk
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
              }

              // docs/31 W2: the end, dimmer.
              Text {
                visible: text !== ""
                textFormat: Text.PlainText
                text: Status.altUntilText(root.status)
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
              }
            }

            Column {
              visible: root.reachable && Status.sparkVisible(root.hour)
              width: parent.width
              spacing: Style.space(4)

              Sparkline {
                width: parent.width
                height: Style.space(34)
                points: root.hour
                color: root.upInk
              }

              Item {
                width: parent.width
                height: sparkCaption.implicitHeight
                // The two lines under the sparkline, in words; the sparkline draws the same.
                Accessible.role: Accessible.StaticText
                // a11y-ignore A6: a label, not a control; a screen reader reads it when it gets there, and its changes are not announced
                Accessible.name: "upload, last hour: now " + Format.speed(root.hour.length > 0 ? root.hour[root.hour.length - 1] : 0)
                                 + ", peak " + Format.speed(Status.peak(root.hour))

                Text {
                  id: sparkCaption
                  textFormat: Text.PlainText
                  text: "upload · last hour"
                  color: root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                }

                Text {
                  anchors.right: parent.right
                  textFormat: Text.PlainText
                  text: "now " + Format.speed(root.hour.length > 0 ? root.hour[root.hour.length - 1] : 0)
                        + " · peak " + Format.speed(Status.peak(root.hour))
                  color: root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                }
              }
            }
          }

          // ====================================================== warnings

          // I7: peers cannot connect in. The fix, in words, and where.
          Notice {
            visible: root.page === "main" && root.reachable && root.status && root.status.reachable === "firewalled"
            width: parent.width
            title: "Peers cannot reach you"
            detail: "qBittorrent reports its port as firewalled, so only peers you reach first can download from you. Forward the listening port on your router, or turn on UPnP / NAT-PMP in omaseed › Settings › qBittorrent › Connection."
            rule: root.urgent
            foreground: root.foreground
            fontFamily: root.fontFamily
          }

          Notice {
            visible: root.page === "main" && root.running && !root.reachable
            width: parent.width
            title: Status.unreachableLine(root.server)
            spokenTitle: root.server.error ? "unreachable, " + root.server.error : "unreachable"
            detail: "Showing the last known state. Press r to retry now."
            rule: root.urgent
            foreground: root.foreground
            fontFamily: root.fontFamily
            action: "Retry"
            onActionClicked: if (root.service) root.service.retry()
          }

          // ====================================================== sections

          Repeater {
            model: root.page === "main" ? root.visibleSections() : []

            delegate: Column {
              id: sectionBlock
              required property string modelData
              width: column.width
              spacing: Style.space(12)

              PanelSeparator { width: parent.width; foreground: root.foreground }

              Loader {
                width: parent.width
                sourceComponent: sectionBlock.modelData === "add" ? addSection
                  : sectionBlock.modelData === "share" ? shareSection
                  : sectionBlock.modelData === "alive" ? aliveSection
                  : sectionBlock.modelData === "flight" ? flightSection
                  : latelySection
              }
            }
          }

          PanelSeparator { visible: root.page === "main" && root.running; width: parent.width; foreground: root.foreground }

          // ====================================================== keys

          Text {
            width: parent.width
            textFormat: Text.StyledText
            text: root.hints()
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            wrapMode: Text.WordWrap
            lineHeight: 1.3
          }
        }
      }
    }
  }

  // ---------------------------------------------------------------- Add

  Component {
    id: addSection

    Section {
      title: "Add"
      summary: root.addSummary()
      folded: root.folded("add")
      hasCursor: root.shownCursorKey === "h:add"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("add")
      onHovered: root.cursorKey = "h:add"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Repeater {
        model: root.addRows
        delegate: ListRow {
          required property var modelData
          title: modelData.name
          meta: modelData.invalid !== "" ? modelData.invalid
            : (modelData.duplicateOn !== "" ? "already on " + modelData.duplicateOn : modelData.origin)
          metaColor: modelData.invalid !== "" || modelData.duplicateOn !== "" ? root.urgent : root.dim
          leading: "tick"
          ticked: modelData.ticked
          leadingColor: root.accent
          readOnly: !root.reachable
          hasCursor: root.shownCursorKey === modelData.key
          foreground: root.foreground
          fontFamily: root.fontFamily
          onHovered: root.cursorKey = modelData.key
          onClicked: root.toggleTick(modelData.source)
          onHasCursorChanged: if (hasCursor) root.reveal(this)
        }
      }

      // Category chips, then where the add will land.
      Column {
        visible: root.addRows.length > 0
        width: parent.width
        spacing: Style.space(6)

        Flow {
          width: parent.width
          spacing: Style.space(6)
          opacity: root.reachable ? 1 : 0.45

          Repeater {
            model: [""].concat(root.categories.map(function(c) { return String(c.name) }))
            delegate: Button {
              id: chip
              required property string modelData
              text: modelData === "" ? "none" : modelData
              bordered: true
              selected: root.category === modelData
              enabled: root.reachable
              foreground: root.foreground
              fontFamily: root.fontFamily
              fontSize: Style.font.bodySmall
              verticalPadding: Style.space(3)
              onClicked: root.pickedCategory = modelData
              Accessible.role: Accessible.Button
              Accessible.name: chip.modelData === "" ? "No category" : chip.modelData
              Accessible.description: "category"
              Accessible.selectable: true
              Accessible.selected: chip.selected
              Accessible.onPressAction: if (!("accessibleName" in chip) && chip.enabled) chip.clicked()
            }
          }
        }

        Text {
          width: parent.width
          textFormat: Text.PlainText
          text: {
            var folder = Status.targetFolder(root.status, root.category)
            // AddSources always adds with automatic management (docs/20 §3's "whether
            // automatic management applies": for the widget's add it always does).
            return "→ " + (folder === "" ? "the server's default folder" : folder) + " · automatic management"
          }
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideMiddle
          Accessible.role: Accessible.StaticText
          // a11y-ignore A6: a label, not a control; a screen reader reads it when it gets there, and its changes are not announced
          Accessible.name: "adds " + Status.spoken(text)
        }
      }

      Row {
        width: parent.width
        spacing: Style.space(8)

        Button {
          id: addButton
          visible: root.addRows.length > 0
          text: root.service && root.service.adding ? "Adding…" : "Add " + root.tickedSources.length
          bordered: true
          enabled: root.reachable && root.tickedSources.length > 0
          opacity: enabled ? 1 : 0.45
          foreground: root.accent
          fontFamily: root.fontFamily
          tooltipText: "Add the ticked ones (↵) · stopped (⇧↵)"
          onClicked: root.addTicked(false)
          Accessible.role: Accessible.Button
          Accessible.name: addButton.text
          Accessible.description: "add the ticked ones, shortcut enter; stopped, shift+enter"
          Accessible.onPressAction: if (!("accessibleName" in addButton) && addButton.enabled) addButton.clicked()
        }

        Button {
          id: browseButton
          text: "Browse…"
          iconText: "󰉋"
          bordered: true
          enabled: root.reachable
          opacity: enabled ? 1 : 0.45
          foreground: root.foreground
          fontFamily: root.fontFamily
          tooltipText: "Pick .torrent files (b)"
          onClicked: root.browse()
          Accessible.role: Accessible.Button
          Accessible.name: "Browse…"
          Accessible.description: "pick .torrent files, shortcut b"
          Accessible.onPressAction: if (!("accessibleName" in browseButton) && browseButton.enabled) browseButton.clicked()
        }

        Text {
          anchors.verticalCenter: parent.verticalCenter
          width: Math.max(0, parent.width - browseButton.width - parent.spacing * 2
                          - (root.addRows.length > 0 ? parent.children[0].width + parent.spacing : 0))
          textFormat: Text.PlainText
          text: root.addRows.length > 0 ? "ctrl+v pastes a link here" : "or copy a magnet and press ctrl+v here"
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          elide: Text.ElideRight
        }
      }

      Text {
        visible: root.addNote !== ""
        width: parent.width
        textFormat: Text.PlainText
        text: root.addNote
        color: root.addNoteUrgent ? root.urgent : root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.WordWrap
        // What the last add did, or why it could not.
        Accessible.role: root.addNoteUrgent ? Accessible.AlertMessage : Accessible.StaticText
        Accessible.name: text
      }
    }
  }

  // ---------------------------------------------------------------- Your share

  Component {
    id: shareSection

    Section {
      title: "Your share"
      summary: Status.shareSummary(root.status)
      folded: root.folded("share")
      hasCursor: root.shownCursorKey === "h:share"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("share")
      onHovered: root.cursorKey = "h:share"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Row {
        spacing: Style.space(28)
        opacity: root.reachable ? 1 : 0.45

        Stat {
          value: Format.size(root.share.sessionUp)
          caption: "Given"
          valueSize: Style.font.title
          foreground: root.foreground
          fontFamily: root.fontFamily
        }

        Stat {
          value: Format.size(root.share.sessionDown)
          caption: "Taken"
          valueSize: Style.font.title
          foreground: root.foreground
          fontFamily: root.fontFamily
        }

        Stat {
          value: Format.ratio(root.share.sessionRatio)
          caption: "Ratio"
          valueSize: Style.font.title
          foreground: root.foreground
          fontFamily: root.fontFamily
        }
      }

      // The week: seven days of upload, today in the accent.
      Row {
        id: week
        visible: root.weekBars.length > 0
        width: parent.width
        spacing: Style.space(4)
        opacity: root.reachable ? 1 : 0.45
        readonly property real barWidth: (width - spacing * (Math.max(1, root.weekBars.length) - 1)) / Math.max(1, root.weekBars.length)

        Repeater {
          model: root.weekBars
          delegate: Column {
            required property var modelData
            width: week.barWidth
            spacing: Style.space(4)

            Item {
              width: parent.width
              height: Style.space(32)

              Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.max(Style.space(2), parent.height * modelData.height)
                color: modelData.today ? root.upInk : Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.18)
              }
            }

            Text {
              width: parent.width
              horizontalAlignment: Text.AlignHCenter
              textFormat: Text.PlainText
              text: modelData.day
              color: modelData.today ? root.foreground : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
            }
          }
        }
      }

      Text {
        width: parent.width
        textFormat: Text.PlainText
        text: Status.shareFacts(root.status)
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.WordWrap
        opacity: root.reachable ? 1 : 0.45
        Accessible.role: Accessible.StaticText
        // a11y-ignore A6: a label, not a control; a screen reader reads it when it gets there, and its changes are not announced
        Accessible.name: Status.spoken(text)
      }

      Text {
        visible: root.topRows.length > 0
        textFormat: Text.PlainText
        text: "TOP SEEDERS TODAY"
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        font.letterSpacing: Style.space(1)
        Accessible.role: Accessible.Heading
        Accessible.name: "Top seeders today"
      }

      Repeater {
        model: root.topRows
        delegate: ListRow {
          required property var modelData
          title: String(modelData.name || "")
          meta: Format.size(modelData.uploaded) + " · ratio " + Format.ratio(modelData.ratio)
          readOnly: !root.reachable
          hasCursor: root.shownCursorKey === "top:" + modelData.hash
          foreground: root.foreground
          fontFamily: root.fontFamily
          onHovered: root.cursorKey = "top:" + modelData.hash
          onClicked: { root.service.showTorrent(modelData.hash); root.close() }
          onHasCursorChanged: if (hasCursor) root.reveal(this)
        }
      }
    }
  }

  // ---------------------------------------------------------------- Keeping alive

  Component {
    id: aliveSection

    Section {
      title: "Keeping alive"
      summary: Status.aliveSummary(root.aliveInfo)
      folded: root.folded("alive")
      hasCursor: root.shownCursorKey === "h:alive"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("alive")
      onHovered: root.cursorKey = "h:alive"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Row {
        spacing: Style.space(8)
        opacity: root.reachable ? 1 : 0.45
        Accessible.role: Accessible.StaticText
        Accessible.name: String(root.aliveInfo.held) + " " + heldWords.text

        Text {
          anchors.baseline: heldWords.baseline
          textFormat: Text.PlainText
          text: String(root.aliveInfo.held)
          color: root.accent
          font.family: root.fontFamily
          font.pixelSize: Style.font.heading
        }

        Text {
          id: heldWords
          textFormat: Text.PlainText
          text: (root.aliveInfo.held === 1 ? "torrent" : "torrents") + " you hold with "
                + root.cfg.rareSeeds + " or fewer seeds"
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
        }
      }

      Flow {
        visible: root.aliveInfo.held > 0
        width: parent.width
        spacing: Style.space(6)
        opacity: root.reachable ? 1 : 0.45

        Repeater {
          model: [
            { n: root.aliveInfo.onlyYou, words: "only you" },
            { n: root.aliveInfo.plusOne, words: "you + one" },
            { n: root.aliveInfo.peers, words: "peers waiting" },
            { n: root.aliveInfo.quiet, words: "nobody has asked for in a month" }
          ]
          delegate: Rectangle {
            required property var modelData
            Accessible.role: Accessible.StaticText
            Accessible.name: String(modelData.n) + " " + modelData.words
            width: tallyRow.implicitWidth + Style.space(12)
            height: tallyRow.implicitHeight + Style.space(6)
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.25)
            radius: Style.cornerRadius

            Row {
              id: tallyRow
              anchors.centerIn: parent
              spacing: Style.space(5)

              Text {
                textFormat: Text.PlainText
                text: String(modelData.n)
                color: root.accent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
              }

              Text {
                textFormat: Text.PlainText
                text: modelData.words
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
              }
            }
          }
        }
      }

      Repeater {
        model: root.aliveInfo.rows
        delegate: ListRow {
          required property var modelData
          title: String(modelData.name || "")
          meta: Status.aliveRowText(modelData)
          metaColor: modelData.seeds <= 1 ? root.accent : root.dim
          readOnly: !root.reachable
          hasCursor: root.shownCursorKey === "alive:" + modelData.hash
          foreground: root.foreground
          fontFamily: root.fontFamily
          onHovered: root.cursorKey = "alive:" + modelData.hash
          onClicked: { root.service.showTorrent(modelData.hash); root.close() }
          onHasCursorChanged: if (hasCursor) root.reveal(this)
        }
      }
    }
  }

  // ---------------------------------------------------------------- In flight

  Component {
    id: flightSection

    Section {
      title: "In flight"
      summary: Status.activeSummary(root.status)
      folded: root.folded("flight")
      hasCursor: root.shownCursorKey === "h:flight"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("flight")
      onHovered: root.cursorKey = "h:flight"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Text {
        visible: root.activeRows.length === 0
        textFormat: Text.PlainText
        text: "nothing in flight"
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        leftPadding: Style.space(8)
        Accessible.role: Accessible.StaticText
        Accessible.name: text
      }

      Repeater {
        model: root.activeRows
        delegate: ListRow {
          required property var modelData
          title: String(modelData.name || "")
          meta: Format.percent(modelData.progress) + " · " + Format.eta(modelData.eta)
          progress: Number(modelData.progress) || 0
          progressColor: root.altSlows ? root.yellow : root.downInk
          readOnly: !root.reachable
          hasCursor: root.shownCursorKey === "flight:" + modelData.hash
          foreground: root.foreground
          fontFamily: root.fontFamily
          onHovered: root.cursorKey = "flight:" + modelData.hash
          onClicked: { root.service.showTorrent(modelData.hash); root.close() }
          onHasCursorChanged: if (hasCursor) root.reveal(this)
        }
      }
    }
  }

  // ---------------------------------------------------------------- Lately

  Component {
    id: latelySection

    Section {
      title: "Lately"
      summary: Status.recentSummary(root.status, root.cfg.recentCount, root.now)
      folded: root.folded("lately")
      hasCursor: root.shownCursorKey === "h:lately"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("lately")
      onHovered: root.cursorKey = "h:lately"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Text {
        visible: root.recentRows.length === 0
        textFormat: Text.PlainText
        text: "nothing finished yet"
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        leftPadding: Style.space(8)
        Accessible.role: Accessible.StaticText
        Accessible.name: text
      }

      Repeater {
        model: root.recentRows
        delegate: ListRow {
          required property var modelData
          readonly property string key: root.latelyKey(modelData)
          title: String(modelData.name || "")
          subline: Status.recentMeta(modelData, root.now)
          leading: modelData.kind === "milestone" ? "mark" : "check"
          leadingColor: modelData.kind === "milestone" ? root.upInk : root.green
          badge: modelData.kind === "milestone" ? Format.milestone(Number(modelData.ratio)) : ""
          badgeColor: root.accent
          readOnly: !root.reachable
          hasCursor: root.shownCursorKey === key
          foreground: root.foreground
          fontFamily: root.fontFamily
          onHovered: root.cursorKey = key
          onClicked: {
            if (modelData.path) root.service.openFolder(modelData.hash)
            else root.service.showTorrent(modelData.hash)
            root.close()
          }
          onHasCursorChanged: if (hasCursor) root.reveal(this)
        }
      }
    }
  }
}
