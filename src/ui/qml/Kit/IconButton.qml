import QtQuick
import QtQuick.Controls
import Omaseed.Kit

// A square icon-only control, `Theme.controlHeight` on each side (spec v2 §1 Fig. 1 `.acts`:
// add, alt-speed, settings, help). Same state machine as `Button`; see that file's header for
// the priority order. `tooltip` shows a themed popup on hover or keyboard focus.
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
    readonly property color _glyphColor: root.danger ? Theme.red : (root.primary || root.selected
                                                                    || root.accent) ? Theme.accent :
                                                                                      Theme.dimForeground
    readonly property color _borderColor: (root.danger && (root.selected || root.primary))
                                          ? Theme.red : root.primary ? Theme.accent :
                                                                       root.activeFocus
                                                                       ? Theme.accent : root.hot
                                                                         ? Theme.fill(
                                                                               Theme.borderHover) : (
                                                                               root.bordered
                                                                               || root.selected)
                                                                           ? Theme.fill(
                                                                                 Theme.borderIdle) :
                                                                             "transparent"

    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.glyph
    Accessible.role: Accessible.Button

    activeFocusOnTab: root.enabled
    border.color: root._borderColor
    border.width: (root.primary || root.activeFocus || root.hot || root.bordered || root.selected)
                  ? 1 : 0
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

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        enabled: root.enabled
        hoverEnabled: true

        onClicked: {
            root.forceActiveFocus();
            root.clicked();
        }
    }

    ToolTip {
        background: Rectangle {
            border.color: Theme.accent
            border.width: 1
            color: Theme.background
            radius: Theme.cornerRadius
        }
        contentItem: Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.tooltip
        }
        delay: 400
        padding: Theme.space(6)
        text: root.tooltip
        visible: root.tooltip.length > 0 && (mouseArea.containsMouse || root.activeFocus)
    }

    Keys.onReturnPressed: if (root.enabled)
                              root.clicked()
    Keys.onSpacePressed: if (root.enabled)
                             root.clicked()
}
