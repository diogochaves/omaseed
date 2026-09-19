import QtQuick
import Omaseed.Kit

// Small pill-less rectangle for category/tag chips (spec v2 Fig. 5 `.chip`). Bordered idle
// (4 %/40 %), hover/cursor fill (8 %/25 %), selected fill (18 %) with no border, matching
// `.chip.on { background:var(--fill-selected); border-color:transparent }`.
Rectangle {
    id: root

    property string text: ""
    property bool selected: false
    property bool hasCursor: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse

    Accessible.name: root.text
    Accessible.role: Accessible.Button

    border.color: root.hot ? Theme.fill(Theme.borderHover) : Theme.fill(Theme.borderIdle)
    border.width: root.selected ? 0 : 1
    color: root.selected ? Theme.fill(Theme.fillSelected) : root.hot ? Theme.fill(Theme.fillHover) :
                                                                       Theme.fill(Theme.fillIdle)
    height: Theme.space(22)
    implicitWidth: label.implicitWidth + Theme.space(16)
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Text {
        id: label

        anchors.centerIn: parent
        color: root.selected ? Theme.brightForeground : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: root.text
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }
}
