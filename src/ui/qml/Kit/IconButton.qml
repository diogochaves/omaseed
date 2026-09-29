import QtQuick
import Omaseed.Kit

// A square icon-only control, `Theme.controlHeight` on each side (spec v2 §1 Fig. 1 `.acts`:
// add, alt-speed, settings, help). Same state machine as `Button`; see that file's header for
// the priority order. `tooltip` shows the kit's `Tooltip` under the button on hover or
// keyboard focus.
Rectangle {
    id: root

    property string glyph: ""
    property string tooltip: ""
    property bool primary: false
    property bool danger: false
    property bool hasCursor: false
    property bool selected: false
    property bool bordered: false
    /// Spec `.btn.icon.on`: the glyph alone turns accent, with no border or fill (the hero's
    /// `+`, and its alt-speed glyph while the server is on alternative limits).
    property bool accent: false
    // Debug-only override so the ThemeGallery can show the pressed fill without a held mouse
    // button; a real press (mouseArea.pressed) always wins over this.
    property bool pressed: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// The button's tooltip, which lives on the window's content item (the tests read it).
    readonly property alias tooltipItem: tip
    readonly property color _glyphColor: root.danger ? Theme.red : (root.primary || root.selected
                                                                    || root.accent) ? Theme.accent :
                                                                                      Theme.dimForeground
    readonly property color _borderColor: (root.danger && (root.selected || root.primary))
                                          ? Theme.red : root.primary ? Theme.accent : root.hot
                                                                       ? Theme.fill(
                                                                             Theme.borderHover) : (
                                                                             root.bordered
                                                                             || root.selected)
                                                                         ? Theme.fill(
                                                                               Theme.borderIdle) :
                                                                           "transparent"

    // An SVG glyph ("icon:gear") is no name to read out.
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.glyph.startsWith("icon:") ? "" :
                                                                                               root.glyph
    Accessible.role: Accessible.Button

    activeFocusOnTab: root.enabled
    border.color: root._borderColor
    border.width: (root.primary || root.hot || root.bordered || root.selected) ? 1 : 0
    color: (mouseArea.pressed || root.pressed) ? Theme.fill(Theme.fillPressed) : (root.activeFocus
                                                                                  || root.hot)
                                                 ? Theme.fill(Theme.fillHover) : root.selected
                                                   ? Theme.fill(Theme.fillSelected) : root.bordered
                                                     ? Theme.fill(Theme.fillIdle) : "transparent"
    // Implicit, not fixed: a layout (the hero's `room`) reads `implicitWidth`, and a fixed
    // `width` left it at 0.
    implicitHeight: Theme.controlHeight
    implicitWidth: Theme.controlHeight
    opacity: root.enabled ? 1.0 : 0.4
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Glyph {
        anchors.centerIn: parent
        color: root._glyphColor
        pixelSize: Theme.font.icon
        text: root.glyph
    }

    FocusRing {}

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        enabled: root.enabled
        hoverEnabled: true

        // A click leaves the keyboard where it was (see Button's header).
        onClicked: root.clicked()
    }

    // On the window's content item, not in the button: clamped inside a button one control
    // tall, the panel would have nowhere to go (the hero's server tip does the same, KIT-8).
    Tooltip {
        id: tip

        readonly property bool wanted: root.visible && root.tooltip.length > 0 && (mouseArea.containsMouse
                                                                                   || root.activeFocus)

        objectName: "iconButtonTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000

        onWantedChanged: tip.wanted ? tip.showFor(root, root.tooltip) : tip.hide()
    }

    onTooltipChanged: tip.wanted ? tip.showFor(root, root.tooltip) : tip.hide()

    // Keypad `enter` is its own key (`Qt.Key_Enter`); without it the key went on to the
    // Sheet, which ran its primary action instead of this button (KIT-7).
    Keys.onEnterPressed: if (root.enabled)
                             root.clicked()
    Keys.onReturnPressed: if (root.enabled)
                              root.clicked()
    Keys.onSpacePressed: if (root.enabled)
                             root.clicked()
}
