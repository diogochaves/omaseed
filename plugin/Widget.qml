import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui
import "Format.js" as Format
import "Status.js" as Status

// omaseed in the bar (docs/20 §2).
//
// The exchange mark is the activity readout: its lower arrow lights while something
// downloads, its upper arrow while something uploads, and its square carries the link
// (an outline in the alt-speed colour under alternative limits, urgent with torrents in
// error, an urgent outline while the server is unreachable, everything at 45 % when the
// app is not running). The speeds are a second, independent switch: beside the mark in a
// horizontal bar, stacked under it in a vertical one. Every slot is measured once in the
// bar's font, so the widget never changes width.
//
// Left click opens the popup (Popup.qml), middle and right are options, scroll does
// nothing. Everything comes from omaseed's status file through Service.qml; the widget
// never talks to qBittorrent.
BarWidget {
  id: root
  moduleName: "chaves.omaseed"

  readonly property var cfg: Status.barOptions(settings)

  Service { id: service }
  readonly property alias service: service

  readonly property var status: service.status
  readonly property var mark: service.markState
  readonly property bool running: service.running

  readonly property color foreground: bar ? bar.barForeground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property color offInk: Qt.rgba(foreground.r, foreground.g, foreground.b, 0.3)

  // A lit arrow: the theme's blue in and green out, or the accent for both.
  readonly property color downInk: cfg.markColor === "single" ? Color.accent : service.blue
  readonly property color upInk: cfg.markColor === "single" ? Color.accent : service.green
  readonly property color speedInk: service.altOn ? service.yellow : foreground

  readonly property bool showDown: cfg.showSpeed && cfg.speedMetric !== "up"
  readonly property bool showUp: cfg.showSpeed && cfg.speedMetric !== "down"
  // With nothing to report (unreachable, not running) there is no value at all.
  readonly property bool hasSpeeds: service.reachable
  readonly property var speed: status && status.speed ? status.speed : ({ dl: 0, up: 0 })

  visible: !(cfg.hideWhenIdle && Status.hiddenWhenIdle(status, service.now))
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  // The bar's open-popup line: as long as the painted content.
  readonly property real openPanelIndicatorWidth: root.vertical ? 0 : content.implicitWidth
  readonly property real openPanelIndicatorHeight: root.vertical ? content.implicitHeight : 0

  // ------------------------------------------------------------- popup

  function injectPanel() {
    var target = panelLoader.item
    if (!target) return
    if ("bar" in target) target.bar = root.bar
    if ("settings" in target) target.settings = root.settings
    if ("anchorItem" in target) target.anchorItem = button
    if ("hostWidget" in target) target.hostWidget = root
    if ("host" in target) target.host = root
  }

  // The shape the shell's summon/hide/toggle routing expects on a bar widget's root.
  readonly property bool opened: panelLoader.item ? panelLoader.item.opened === true : false
  readonly property bool popoutSwitchClosing: panelLoader.item ? panelLoader.item.popoutSwitchClosing === true : false

  function open() { if (panelLoader.item) panelLoader.item.openPanel() }
  function close() { if (panelLoader.item) panelLoader.item.close() }
  function toggle() { if (panelLoader.item) panelLoader.item.toggle() }
  function closeForPopoutSwitch() { if (panelLoader.item) panelLoader.item.closeForPopoutSwitch() }

  onBarChanged: injectPanel()
  onSettingsChanged: injectPanel()

  Loader {
    id: panelLoader
    active: true
    source: Qt.resolvedUrl("Popup.qml")
    visible: false
    onLoaded: {
      root.injectPanel()
      Qt.callLater(root.injectPanel)
    }
  }

  // `omarchy-shell chaves.omaseed open|close|toggle`, for a key binding. A bar exists per
  // monitor, and Quickshell keeps one handler per target: the first live instance claims it.
  readonly property bool ipcOwner: {
    if (!bar || typeof bar.moduleWidgets !== "function") return true
    var peers = bar.moduleWidgets(moduleName)
    return peers.length > 0 && peers[0] === root
  }

  IpcHandler {
    target: root.moduleName
    enabled: root.ipcOwner
    Component.onDestruction: enabled = false
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.toggle() }
    function altSpeed(): void { service.toggleAltSpeed() }
  }

  function act(kind) {
    if (kind === "altSpeed") { if (service.reachable) service.toggleAltSpeed() }
    else if (kind === "openApp") service.openApp()
  }

  // ------------------------------------------------------------- the readout

  FontMetrics { id: valueMetrics; font.family: root.fontFamily; font.pixelSize: Style.font.body; font.features: { "tnum": 1 } }
  FontMetrics { id: arrowMetrics; font.family: root.fontFamily; font.pixelSize: root.vertical ? Style.font.caption : Style.font.body }

  // The widest value a slot can ever hold, measured once in the bar's font with tabular
  // digits: four characters (Format.barValue), whose dot and unit letters may still be
  // proportional.
  readonly property int valueSlot: {
    var texts = ["888M", "8.8M", "999K", "1.0G", "0"]
    var w = 0
    for (var i = 0; i < texts.length; i++) w = Math.max(w, valueMetrics.advanceWidth(texts[i]))
    return Math.ceil(w)
  }
  readonly property int arrowSlot: Math.ceil(Math.max(arrowMetrics.advanceWidth("↓"), arrowMetrics.advanceWidth("↑")))
  readonly property int markSize: Style.space(20)

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "omaseed"
    labelVisible: false
    hasVisualContent: true
    tooltipText: Status.tooltip(root.status, service.now, root.cfg)
    fixedWidth: root.vertical ? -1 : content.implicitWidth + button.scaledHorizontalMargin * 2
    fixedHeight: root.vertical ? content.implicitHeight + button.scaledVerticalPadding * 2 : -1
    onPressed: function(buttonCode) {
      if (buttonCode === Qt.LeftButton) root.toggle()
      else if (buttonCode === Qt.MiddleButton) root.act(root.cfg.middleClick)
      else if (buttonCode === Qt.RightButton) root.act(root.cfg.rightClick)
    }

    Grid {
      id: content
      anchors.centerIn: parent
      columns: root.vertical ? 1 : 3
      rowSpacing: Style.space(1)
      columnSpacing: Style.space(5)
      horizontalItemAlignment: Grid.AlignHCenter
      verticalItemAlignment: Grid.AlignVCenter

      Mark {
        visible: root.cfg.showMark
        size: root.markSize
        width: root.markSize
        height: root.markSize
        opacity: root.mark.dim ? 0.45 : 1
        downLit: root.mark.down
        upLit: root.mark.up
        downColor: root.downInk
        upColor: root.upInk
        offColor: root.mark.dim ? root.foreground : root.offInk
        squareOutline: root.mark.square === "alt" || root.mark.square === "unreachable"
        squareColor: root.mark.square === "alt" ? service.yellow
          : (root.mark.square === "error" || root.mark.square === "unreachable" ? root.urgent : root.foreground)
      }

      SpeedField {
        visible: root.showDown
        vertical: root.vertical
        arrow: "↓"
        value: root.hasSpeeds ? Format.barValue(root.speed.dl) : ""
        ink: root.speedInk
        fontFamily: root.fontFamily
        valueSlot: root.valueSlot
        arrowSlot: root.arrowSlot
        animate: !root.bar || root.bar.foregroundAnimationEnabled
      }

      SpeedField {
        visible: root.showUp
        vertical: root.vertical
        arrow: "↑"
        value: root.hasSpeeds ? Format.barValue(root.speed.up) : ""
        ink: root.speedInk
        fontFamily: root.fontFamily
        valueSlot: root.valueSlot
        arrowSlot: root.arrowSlot
        animate: !root.bar || root.bar.foregroundAnimationEnabled
      }
    }
  }
}
