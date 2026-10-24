import QtQuick
import Omaseed.Kit

// One key drawn as a small bordered cap (docs/12 F1's `.kc`): the `/` at the end of the Files
// tab's summary line and the `esc` inside its filter field. The border is the idle field's
// (40 %), the key bright, at the caption size; on a fill, `ink` is the fill's (C30 F1).
Rectangle {
    id: root

    property string text: ""
    /// The ground the cap sits on; null is the window.
    property Ink ink: null

    border.color: Theme.fill(Theme.borderIdle)
    border.width: 1
    color: "transparent"
    implicitHeight: label.implicitHeight
    implicitWidth: label.implicitWidth + Theme.space(10)
    radius: Theme.cornerRadius

    Text {
        id: label

        anchors.centerIn: parent
        color: root.ink !== null ? root.ink.brightForeground : Theme.brightForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: root.text
    }
}
