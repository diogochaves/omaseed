import QtQuick
import QtQuick.Controls
import Quickshell
import qs.Commons
import qs.Ui
import "Format.js" as Format
import "Status.js" as Status

// The popup behind the mark (docs/20 §3): one view, in the shell's card style.
//
//   head      the live mark, "omaseed ↗", the server line, and three buttons:
//             alt-speed, open omaseed, settings (omaseed's own, Q11)
//   now       ↓ and ↑, the alternative limits while they are on, an hour of upload
//   sections  Add, Your share, Keeping alive, In flight, Lately; each folds by its
//             header or `space`, keeps one line while folded, and is remembered
//
// Add has no text field: it lists what is ready (what was pasted with ctrl+v anywhere
// in the popup, a magnet on the clipboard when the popup opened, today's .torrent files
// in ~/Downloads) and hands the ticked ones to omaseed, which adds them, checks
// duplicates and remembers the category.
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
  readonly property var mark: service ? service.markState : Status.markState(null, 0)

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property color accent: Color.accent
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property color blue: service ? service.blue : accent
  readonly property color green: service ? service.green : foreground
  readonly property color yellow: service ? service.yellow : urgent
  readonly property color downInk: cfg.markColor === "single" ? accent : blue
  readonly property color upInk: cfg.markColor === "single" ? accent : green

  readonly property var server: status && status.server ? status.server : ({})
  readonly property string serverName: String(server.name || "omaseed")
  readonly property var folds: service ? service.folds : ({ add: false, share: true, alive: true, flight: false, lately: true })

  // ------------------------------------------------------------- open / close

  function openPanel() {
    if (service) service.now = Date.now() / 1000
    root.controller.show()
    probeClipboard()
    Qt.callLater(function() {
      if (root.opened) setCenterHoverRevealSuppressed(true)
      keys.forceActiveFocus()
      if (root.targets.length > 0 && root.targetIndex(root.cursorKey) < 0) root.cursorKey = root.targets[0].key
    })
  }

  function close() {
    setCenterHoverRevealSuppressed(false)
    root.controller.hide()
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
                         "Downloads" + (when === "" ? "" : " · " + when), d.inLibrary ? serverName : "", ""))
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

  function probeClipboard() {
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
    if (!running) { if (service) service.openApp(); close(); return }
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
    if (!running) {
      list = [["↵", "start"], ["esc", "close"]]
    } else {
      var t = targetAt(cursorKey)
      var enter = !t ? "" : (t.section === "add" ? "add" : (t.key.indexOf("h:") === 0 ? "fold"
                  : (t.section === "lately" ? "folder" : "show")))
      list = [["a", "add"], ["o", "omaseed"], ["A", "alt-speed"], ["j k", "move"], ["space", "fold"], ["z", "fold all"]]
      if (enter !== "") list.push(["↵", enter])
      if (t && t.section === "add") list.push(["⇧↵", "add stopped"], ["m", "add sheet"], ["b", "browse"])
      else if (t && t.hash) list.push(["s", "show"])
      if (!reachable) list.push(["r", "retry"])
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
        if (event.key === Qt.Key_Escape) root.close()
        else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
          root.switchPanel(shift || event.key === Qt.Key_Backtab ? -1 : 1)
        else if (ctrl && event.key === Qt.Key_V) root.paste()
        else if (ctrl) handled = false
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) root.activate(shift)
        else if (event.key === Qt.Key_Down || event.text === "j") root.moveCursor(1)
        else if (event.key === Qt.Key_Up || event.text === "k") root.moveCursor(-1)
        else if (event.key === Qt.Key_Space) root.foldCursorSection()
        else if (event.text === "a") root.goToAdd()
        else if (event.text === "b") root.browse()
        else if (event.text === "m") root.openAddSheet()
        else if (event.text === "o") { if (root.service) root.service.openApp(); root.close() }
        else if (event.text === "A") { if (root.service && root.reachable) root.service.toggleAltSpeed() }
        else if (event.text === "z") root.toggleAllFolds()
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

            Mark {
              id: headMark
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
              squareOutline: root.mark.square === "alt" || root.mark.square === "unreachable"
              squareColor: root.mark.square === "alt" ? root.yellow
                : (root.mark.square === "error" || root.mark.square === "unreachable" ? root.urgent : root.foreground)
            }

            Column {
              id: headText
              anchors.left: headMark.right
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
                  text: "omaseed"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.title
                  font.bold: true
                }

                LineIcon {
                  anchors.verticalCenter: titleText.verticalCenter
                  name: "open"
                  size: Style.font.bodySmall
                  color: root.dim
                }
              }

              Row {
                width: parent.width
                spacing: Style.space(6)

                Rectangle {
                  anchors.verticalCenter: parent.verticalCenter
                  width: Style.space(6)
                  height: width
                  radius: width / 2
                  color: !root.running ? root.dim : (root.reachable ? root.accent : root.urgent)
                }

                Text {
                  width: parent.width - Style.space(12)
                  textFormat: Text.PlainText
                  text: root.running ? Status.serverLine(root.status) : "NOT RUNNING"
                  color: root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                  font.letterSpacing: Style.space(1)
                  elide: Text.ElideRight
                }
              }
            }

            MouseArea {
              anchors.fill: headText
              cursorShape: Qt.PointingHandCursor
              onClicked: { if (root.service) root.service.openApp(); root.close() }
            }

            Row {
              id: headButtons
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(6)

              PanelActionButton {
                bordered: true
                enabled: root.reachable
                tooltipText: root.altOn ? "Alternative speed limits on (A)" : "Alternative speed limits (A)"
                foreground: root.altOn ? root.yellow : root.dim
                hoverColor: root.altOn ? root.yellow : root.foreground
                fontFamily: root.fontFamily
                onClicked: if (root.service) root.service.toggleAltSpeed()
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "altSpeed"
                  color: parent.foreground
                  opacity: parent.enabled ? 1 : 0.5
                }
              }

              PanelActionButton {
                bordered: true
                tooltipText: root.running ? "Open omaseed (o)" : "Start omaseed"
                foreground: root.dim
                hoverColor: root.foreground
                fontFamily: root.fontFamily
                onClicked: { if (root.service) root.service.openApp(); root.close() }
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "mark"
                  color: parent.foreground
                }
              }

              PanelActionButton {
                bordered: true
                tooltipText: "omaseed settings"
                foreground: root.dim
                hoverColor: root.foreground
                fontFamily: root.fontFamily
                onClicked: { if (root.service) root.service.openSettings(); root.close() }
                LineIcon {
                  anchors.centerIn: parent
                  size: Style.font.icon
                  name: "gear"
                  color: parent.foreground
                }
              }
            }
          }

          PanelSeparator { width: parent.width; foreground: root.foreground }

          // ====================================================== not running

          Column {
            visible: !root.running
            width: parent.width
            spacing: Style.space(10)

            Text {
              width: parent.width
              textFormat: Text.PlainText
              text: "omaseed is not running."
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
              wrapMode: Text.WordWrap
            }

            Text {
              width: parent.width
              textFormat: Text.PlainText
              text: "The widget shows what the app reports."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            Button {
              text: "Start omaseed"
              bordered: true
              hasCursor: true
              foreground: root.accent
              fontFamily: root.fontFamily
              onClicked: { if (root.service) root.service.openApp(); root.close() }
            }
          }

          // ====================================================== now

          Column {
            visible: root.running
            width: parent.width
            spacing: Style.space(10)

            Row {
              spacing: Style.space(28)

              Stat {
                arrow: "↓"
                arrowColor: root.altOn ? root.yellow : root.downInk
                value: root.reachable ? Format.speed(root.speedNow.dl) : "—"
                valueColor: root.altOn ? root.yellow : root.foreground
                caption: "Download"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Stat {
                arrow: "↑"
                arrowColor: root.altOn ? root.yellow : root.upInk
                value: root.reachable ? Format.speed(root.speedNow.up) : "—"
                valueColor: root.altOn ? root.yellow : root.foreground
                caption: "Upload"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }
            }

            Row {
              visible: root.reachable && root.altOn
              spacing: Style.space(6)

              LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "altSpeed"
                size: Style.font.body
                color: root.yellow
              }

              Text {
                textFormat: Text.PlainText
                text: Status.altLine(root.status)
                color: root.yellow
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
            visible: root.reachable && root.status && root.status.reachable === "firewalled"
            width: parent.width
            title: "Peers cannot reach you"
            detail: "qBittorrent reports its port as firewalled, so only peers you reach first can download from you. Forward the listening port on your router, or turn on UPnP / NAT-PMP in omaseed › Settings › qBittorrent › Connection."
            rule: root.urgent
            foreground: root.foreground
            fontFamily: root.fontFamily
          }

          Notice {
            visible: root.running && !root.reachable
            width: parent.width
            title: Status.unreachableLine(root.server)
            detail: "Showing the last known state. Press r to retry now."
            rule: root.urgent
            foreground: root.foreground
            fontFamily: root.fontFamily
            action: "Retry"
            onActionClicked: if (root.service) root.service.retry()
          }

          // ====================================================== sections

          Repeater {
            model: root.visibleSections()

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

          PanelSeparator { visible: root.running; width: parent.width; foreground: root.foreground }

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
      hasCursor: root.cursorKey === "h:add"
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
          hasCursor: root.cursorKey === modelData.key
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

          Repeater {
            model: [""].concat(root.categories.map(function(c) { return String(c.name) }))
            delegate: Button {
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
            }
          }
        }

        Text {
          width: parent.width
          textFormat: Text.PlainText
          text: {
            var folder = Status.targetFolder(root.status, root.category)
            var line = "→ " + (folder === "" ? "the server's default folder" : folder)
            return root.addInfo.autoTmm === true ? line + " · automatic management" : line
          }
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideMiddle
        }
      }

      Row {
        width: parent.width
        spacing: Style.space(8)

        Button {
          visible: root.addRows.length > 0
          text: root.service && root.service.adding ? "Adding…" : "Add " + root.tickedSources.length
          bordered: true
          enabled: root.reachable && root.tickedSources.length > 0
          opacity: enabled ? 1 : 0.45
          foreground: root.accent
          fontFamily: root.fontFamily
          tooltipText: "Add the ticked ones (↵), stopped (⇧↵), or in omaseed's add sheet (m)"
          onClicked: root.addTicked(false)
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
      hasCursor: root.cursorKey === "h:share"
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
      }

      Text {
        visible: root.topRows.length > 0
        textFormat: Text.PlainText
        text: "TOP SEEDERS THIS SESSION"
        color: root.dim
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        font.letterSpacing: Style.space(1)
      }

      Repeater {
        model: root.topRows
        delegate: ListRow {
          required property var modelData
          title: String(modelData.name || "")
          meta: Format.size(modelData.uploaded) + " · ratio " + Format.ratio(modelData.ratio)
          readOnly: !root.reachable
          hasCursor: root.cursorKey === "top:" + modelData.hash
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
      hasCursor: root.cursorKey === "h:alive"
      foreground: root.foreground
      fontFamily: root.fontFamily
      onToggled: root.toggleFold("alive")
      onHovered: root.cursorKey = "h:alive"
      onHasCursorChanged: if (hasCursor) root.reveal(this)

      Row {
        spacing: Style.space(8)
        opacity: root.reachable ? 1 : 0.45

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
            { n: root.aliveInfo.waiting, words: "peers waiting" },
            { n: root.aliveInfo.quiet, words: "nobody has asked for in a month" }
          ]
          delegate: Rectangle {
            required property var modelData
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
          hasCursor: root.cursorKey === "alive:" + modelData.hash
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
      hasCursor: root.cursorKey === "h:flight"
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
      }

      Repeater {
        model: root.activeRows
        delegate: ListRow {
          required property var modelData
          title: String(modelData.name || "")
          meta: Format.percent(modelData.progress) + " · " + Format.eta(modelData.eta)
          progress: Number(modelData.progress) || 0
          progressColor: root.altOn ? root.yellow : root.downInk
          readOnly: !root.reachable
          hasCursor: root.cursorKey === "flight:" + modelData.hash
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
      hasCursor: root.cursorKey === "h:lately"
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
          hasCursor: root.cursorKey === key
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
