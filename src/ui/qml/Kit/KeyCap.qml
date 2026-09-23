import QtQuick
import Omaseed.Kit

// One key drawn as a small bordered cap (docs/12 F1's `.kc`): the `/` at the end of the Files
// tab's summary line and the `esc` inside its filter field. The border is the idle field's
// (40 %), the key bright, at the caption size.
Rectangle {
    id: root

    property string text: ""

    border.color: Theme.fill(Theme.borderIdle)
    border.width: 1
    color: "transparent"
    implicitHeight: label.implicitHeight
    implicitWidth: label.implicitWidth + Theme.space(10)
    radius: Theme.cornerRadius

    Text {
        id: label

        anchors.centerIn: parent
        color: Theme.brightForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: root.text
    }
}
