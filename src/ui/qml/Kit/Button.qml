import QtQuick
import Omaseed.Kit

// A themed clickable control — one component for every button-shaped thing in the kit (spec
// v2 §8 tokens page `.btn`, and §9 item 10: "the kit has no filled button"). States compose in
// priority order, mirroring the shell's own Button
// (/usr/share/omarchy/shell/Ui/Button.qml):
//
//   pressed         fillPressed (22 %)
//   activeFocus     accent ring (1 px), plus the hover fill
//   hasCursor/hover fillHover (8 %) + borderHover (25 %)
//   selected        fillSelected (18 %), no border unless `bordered`
//   idle            no chrome, unless `bordered`: fillIdle (4 %) + borderIdle (40 %)
//
// `primary` is an accent outline with accent text (never a fill — see §9 item 10). `danger`
// is red text; when `danger` is also `primary` or `selected` the outline turns red too (spec
// `.btn.danger.sel`). Every colour comes from `Theme`.
Rectangle {
    id: root

    property string text: ""
    property string glyph: ""
    property bool primary: false
    property bool danger: false
    property bool hasCursor: false
    property bool selected: false
    property bool bordered: false
    // The label's type size. Body everywhere (spec `.btn`), except the confirm dialog's two
    // buttons, which the spec prints at caption (`.confirm .bts .btn`).
    property int fontSize: Theme.font.body
    // Debug-only override so the ThemeGallery can show the pressed fill without a held mouse
    // button; a real press (mouseArea.pressed) always wins over this.
    property bool pressed: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    readonly property color _textColor: root.danger ? Theme.red : (root.primary ? Theme.accent :
                                                                                  Theme.foreground)
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

    Accessible.name: root.text.length > 0 ? root.text : root.glyph
    Accessible.role: Accessible.Button

    activeFocusOnTab: root.enabled
    border.color: root._borderColor
    border.width: (root.primary || root.activeFocus || root.hot || root.bordered || root.selected)
                  ? 1 : 0
    color: (mouseArea.pressed || root.pressed) ? Theme.fill(Theme.fillPressed) : (root.activeFocus
                                                                                  || root.hot)
                                                 ? Theme.fill(Theme.fillHover) : root.selected ? (
                                                                                                     root.danger
                                                                                                     ? Qt.rgba(
                                                                                                           Theme.red.r,
                                                                                                           Theme.red.g,
                                                                                                           Theme.red.b,
                                                                                                           Theme.fillSelected) :
                                                                                                       Theme.fill(
                                                                                                           Theme.fillSelected)) :
                                                                                                 root.bordered
                                                                                                 ? Theme.fill(
                                                                                                       Theme.fillIdle) :
                                                                                                   "transparent"
    height: Theme.controlHeight
    implicitWidth: label.implicitWidth + glyphLabel.implicitWidth + Theme.space(20) + (root.glyph.length
                                                                                       > 0 && root.text.length
                                                                                       > 0 ? Theme.space(
                                                                                                 6) : 0)
    opacity: root.enabled ? 1.0 : 0.4
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: Theme.space(6)

        Text {
            id: glyphLabel

            anchors.verticalCenter: parent.verticalCenter
            color: root._textColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.icon
            text: root.glyph
            visible: root.glyph.length > 0
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            color: root._textColor
            font.family: Theme.fontFamily
            font.pixelSize: root.fontSize
            text: root.text
            visible: root.text.length > 0
        }
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

    Keys.onReturnPressed: if (root.enabled)
                              root.clicked()
    Keys.onSpacePressed: if (root.enabled)
                             root.clicked()
}
