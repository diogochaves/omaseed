import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import "Status.js" as Status

// Everything the widget knows and everything it can do, for one bar surface.
//
// Read: the status file omaseed writes (plugin/STATUS.md), watched, never polled; the
// theme's blue, green and yellow from its colors.toml (contract §1 C4: the shell's
// palette has no such roles); the popup's fold states, which the widget remembers itself.
//
// Act: methods on omaseed's `io.github.diogochaves.omaseed.Status` D-Bus interface,
// fire-and-forget, the result arriving with the next write of the file (contract §5).
// Two calls answer with data the popup needs at once (DescribeSources, AddSources), so
// they run as a Process and are read back.
//
// A bar surface exists per monitor, so there may be several of these: each is a reader,
// and the only thing they write (the heartbeat, the folds) is idempotent.
Item {
  id: root

  visible: false

  readonly property string runtimeDir: Quickshell.env("XDG_RUNTIME_DIR") || ""
  readonly property string appDir: runtimeDir === "" ? "" : runtimeDir + "/omaseed"
  readonly property string statusPath: appDir === "" ? "" : appDir + "/status.json"
  readonly property string stateHome: Quickshell.env("XDG_STATE_HOME") || (Quickshell.env("HOME") || "") + "/.local/state"
  readonly property string stateDir: stateHome + "/omarchy/omaseed"

  readonly property string busName: "io.github.diogochaves.omaseed"
  readonly property string busPath: "/io/github/diogochaves/omaseed"
  readonly property string busInterface: "io.github.diogochaves.omaseed.Status"
  readonly property string desktopId: "io.github.diogochaves.omaseed"

  // ------------------------------------------------------------- status

  property var status: null
  property real now: Date.now() / 1000

  readonly property string state: Status.state(status, now)
  readonly property bool running: state !== "notRunning"
  readonly property bool reachable: running && state !== "unreachable"
  readonly property bool altOn: running && Status.altOn(status)
  readonly property var markState: Status.markState(status, now)

  FileView {
    id: statusFile
    path: root.statusPath
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: {
      root.status = Status.parse(text())
      root.now = Date.now() / 1000
    }
    onLoadFailed: root.status = null
  }

  // The clock that ages the file: a crashed app reads as not running within 30 s. A file
  // that did not exist when the widget loaded is not watched until it does, so the same
  // tick reads it again while there is nothing to show.
  Timer {
    interval: 5000
    running: true
    repeat: true
    onTriggered: {
      root.now = Date.now() / 1000
      if (!root.running && root.statusPath !== "") statusFile.reload()
    }
  }

  // ------------------------------------------------------------- heartbeat

  // Q7: while this file is under 90 s old, omaseed hides its own tray icon, so the bar
  // does not carry two marks. Touched every 30 s.
  Timer {
    interval: 30000
    running: root.appDir !== ""
    repeat: true
    triggeredOnStart: true
    onTriggered: Quickshell.execDetached(["sh", "-c", 'mkdir -p -m 0700 "$1" && touch "$1/widget.alive"', "sh", root.appDir])
  }

  // ------------------------------------------------------------- theme colours

  // The theme's blue, green and yellow, as omaseed reads them (named keys first, the
  // terminal palette's numbered ones second). Without them: the accent for blue, the
  // foreground for green, and the ink between foreground and urgent for yellow.
  property color themeBlue: "transparent"
  property color themeGreen: "transparent"
  property color themeYellow: "transparent"

  readonly property color blue: themeBlue.a > 0 ? themeBlue : Color.accent
  readonly property color green: themeGreen.a > 0 ? themeGreen : Color.foreground
  readonly property color yellow: themeYellow.a > 0 ? themeYellow
    : Qt.rgba((Color.foreground.r + Color.urgent.r) / 2, (Color.foreground.g + Color.urgent.g) / 2, (Color.foreground.b + Color.urgent.b) / 2, 1)

  FileView {
    path: Color.currentThemePath + "/colors.toml"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: root.readTheme(text())
    onLoadFailed: root.readTheme("")
  }

  function readTheme(raw) {
    var lines = String(raw || "").split("\n")
    var found = {}
    for (var i = 0; i < lines.length; i++) {
      var match = lines[i].match(/^\s*([A-Za-z0-9_-]+)\s*=\s*["']?(#[0-9A-Fa-f]{6})/)
      if (match) found[match[1]] = match[2]
    }
    themeBlue = found.blue || found.color4 || "transparent"
    themeGreen = found.green || found.color2 || "transparent"
    themeYellow = found.yellow || found.color3 || "transparent"
  }

  // ------------------------------------------------------------- folds

  // C3: Add and In flight open the first time, the rest folded; afterwards the popup
  // opens the way it was left. Kept in the widget's own state, not in shell.json.
  readonly property var defaultFolds: ({ add: false, share: true, alive: true, flight: false, lately: true })
  property var folds: defaultFolds

  FileView {
    id: foldFile
    path: root.stateDir + "/folds.json"
    atomicWrites: true
    printErrors: false
    onLoaded: {
      var saved = Status.parse(text())
      var next = {}
      for (var key in root.defaultFolds)
        next[key] = saved && typeof saved[key] === "boolean" ? saved[key] : root.defaultFolds[key]
      root.folds = next
    }
  }

  function setFolds(next) {
    folds = next
    Quickshell.execDetached(["mkdir", "-p", root.stateDir])
    foldWrite.restart()
  }

  Timer {
    id: foldWrite
    interval: 300
    onTriggered: foldFile.setText(JSON.stringify(root.folds) + "\n")
  }

  // ------------------------------------------------------------- acting

  function call(method, signature, args) {
    var command = ["busctl", "--user", "call", busName, busPath, busInterface, method]
    if (signature) command = command.concat([signature]).concat(args || [])
    Quickshell.execDetached(command)
  }

  function toggleAltSpeed() { call("ToggleAltSpeed") }
  function showTorrent(hash) { if (hash) call("ShowTorrent", "s", [String(hash)]) }
  function openFolder(hash) { if (hash) call("OpenFolder", "s", [String(hash)]) }
  function openAddSheet(sources) {
    var list = sources || []
    call("OpenAddSheet", "as", [String(list.length)].concat(list))
  }

  function appAction(name) {
    Quickshell.execDetached(["busctl", "--user", "call", busName, busPath, "org.freedesktop.Application",
                             "ActivateAction", "sava{sv}", name, "0", "0"])
  }

  function openSettings() { appAction("settings") }

  // A setting of the widget's own, where the shell's scoped call is missing (Widget.qml).
  function barSet(id, key, value) {
    Quickshell.execDetached(["omarchy", "bar", "set", id, key, JSON.stringify(value), "--json"])
  }
  function retry() { appAction("retry") }

  // Raise a running omaseed; start one that is not, through its desktop file, so it
  // lands in the session's own scope (uwsm), the way the launcher starts it.
  function openApp() {
    if (running) {
      Quickshell.execDetached(["busctl", "--user", "call", busName, busPath, "org.freedesktop.Application",
                               "Activate", "a{sv}", "0"])
      return
    }
    Quickshell.execDetached(["sh", "-c",
      'uwsm app -- "$1.desktop" >/dev/null 2>&1 || gtk-launch "$1" >/dev/null 2>&1 || exec omaseed', "sh", desktopId])
  }

  // DescribeSources(as) → s: what the app makes of each source (name, kind, size,
  // duplicate). `done(list)` gets the parsed array, or null when the app did not answer.
  property var describeQueue: []

  function describe(sources, done) {
    if (!sources || sources.length === 0) return
    describeQueue = describeQueue.concat([{ sources: sources, done: done }])
    pumpDescribe()
  }

  function pumpDescribe() {
    if (describeProcess.running || describeQueue.length === 0) return
    var job = describeQueue[0]
    describeQueue = describeQueue.slice(1)
    describeProcess.job = job
    describeProcess.command = ["timeout", "--kill-after=0.5s", "10s", "busctl", "--user", "--json=short", "call",
      busName, busPath, busInterface, "DescribeSources", "as", String(job.sources.length)].concat(job.sources)
    describeProcess.running = true
  }

  Process {
    id: describeProcess
    property var job: null
    environment: ({ LC_ALL: "C" })
    stdout: StdioCollector { id: describeOut; waitForEnd: true }
    onExited: function(exitCode) {
      var job = describeProcess.job
      describeProcess.job = null
      var answer = exitCode === 0 ? Status.busString(describeOut.text) : null
      if (job && typeof job.done === "function") job.done(Array.isArray(answer) ? answer : null)
      root.pumpDescribe()
    }
  }

  // AddSources(as, s category, b stopped) → s: {added, duplicates, failed}. One add at a
  // time; `done(result)` gets the parsed object, or null when the app did not answer.
  property bool adding: addProcess.running

  function addSources(sources, category, stopped, done) {
    if (addProcess.running || !sources || sources.length === 0) return false
    addProcess.done = done
    addProcess.command = ["timeout", "--kill-after=0.5s", "30s", "busctl", "--user", "--json=short", "call",
      busName, busPath, busInterface, "AddSources", "assb", String(sources.length)].concat(sources)
      .concat([String(category || ""), stopped ? "true" : "false"])
    addProcess.running = true
    return true
  }

  Process {
    id: addProcess
    property var done: null
    environment: ({ LC_ALL: "C" })
    stdout: StdioCollector { id: addOut; waitForEnd: true }
    stderr: StdioCollector { id: addErr; waitForEnd: true }
    onExited: function(exitCode) {
      var done = addProcess.done
      addProcess.done = null
      var answer = exitCode === 0 ? Status.busString(addOut.text) : null
      if (answer === null && exitCode !== 0)
        console.warn("chaves.omaseed", "AddSources failed:", String(addErr.text || "").trim() || "exit " + exitCode)
      if (typeof done === "function") done(answer)
    }
  }

  // The clipboard as text (`wl-paste -n -t text`), for ctrl+v and for the probe when the
  // popup opens. `done(text)` gets "" when there is no text.
  property var clipboardDone: null

  function readClipboard(done) {
    if (clipboardProcess.running) return
    clipboardDone = done
    clipboardProcess.running = true
  }

  Process {
    id: clipboardProcess
    command: ["timeout", "--kill-after=0.5s", "2s", "wl-paste", "-n", "-t", "text"]
    environment: ({ LC_ALL: "C" })
    stdout: StdioCollector { id: clipboardOut; waitForEnd: true }
    onExited: function(exitCode) {
      var done = root.clipboardDone
      root.clipboardDone = null
      if (typeof done === "function") done(exitCode === 0 ? String(clipboardOut.text || "") : "")
    }
  }

  // Browse…: the system file picker through the portal. `done(paths)`.
  property var browseDone: null

  function browse(done) {
    if (browseProcess.running) return
    browseDone = done
    browseProcess.running = true
  }

  Process {
    id: browseProcess
    command: ["zenity", "--file-selection", "--multiple", "--separator=\n", "--title=Add torrents",
              "--file-filter=Torrent files | *.torrent", "--file-filter=All files | *"]
    environment: ({ GTK_USE_PORTAL: "1" })
    stdout: StdioCollector { id: browseOut; waitForEnd: true }
    onExited: function(exitCode) {
      var done = root.browseDone
      root.browseDone = null
      if (exitCode !== 0 || typeof done !== "function") return
      var paths = String(browseOut.text || "").split("\n").filter(function(p) { return p.trim() !== "" })
      done(paths)
    }
  }
}
