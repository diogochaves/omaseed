import QtQuick
import Omaseed.Kit

// What the window shows while files or text are dragged over it (#51 D7): an accent outline
// around the content, washed in the accent at the idle fill, and "drop to add" in the middle
// on a popup's ground (plan §3: background opaque, 1 px accent edge). The host's DropArea
// says when (`shown`); the overlay never takes the pointer or the drop itself.
//
// Hand-formatted to qmlformat's own style.
Item {
    id: root

    /// A drag is over the window.
    property bool shown: false

    enabled: false
    visible: root.shown

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.space(6)
        border.color: Theme.accent
        border.width: 1
        // The accent's idle tint, a token a skin can move; the words sit on the opaque
        // label below, not on it (docs/DECISIONS.md C30 F5).
        color: Theme.on.accentIdle.fill
        objectName: "dropOutline"
        radius: Theme.cornerRadius
    }

    Rectangle {
        anchors.centerIn: parent
        border.color: Theme.accent
        border.width: 1
        color: Theme.background
        height: label.implicitHeight + 2 * Theme.space(12)
        radius: Theme.cornerRadius
        width: label.implicitWidth + 2 * Theme.space(24)

        Text {
            id: label

            anchors.centerIn: parent
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.title
            objectName: "dropLabel"
            text: qsTr("drop to add")
        }
    }
}
