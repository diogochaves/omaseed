import QtQuick
import Omaseed.Kit

// On/off switch with a trailing label (spec v2 §8 `.toggle`/`.sw`; proportions from
// /usr/share/omarchy/shell/Ui/ToggleSwitch.qml). Track `Theme.space(28)` × `Theme.space(14)`;
// `Space` toggles when focused; clicking anywhere in the control toggles too.
Item {
    id: root

    property bool checked: false
    property string text: ""
    property bool hasCursor: false

    signal toggled

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    readonly property int knobSize: root.trackHeight - Theme.space(4)
    readonly property int trackHeight: Theme.space(14)
    readonly property int trackWidth: Theme.space(28)

    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.name: root.text
    Accessible.role: Accessible.CheckBox

    activeFocusOnTab: true
    height: implicitHeight
    implicitHeight: Math.max(root.trackHeight, label.implicitHeight)
    implicitWidth: track.width + (label.visible ? Theme.space(8) + label.implicitWidth : 0)
    width: implicitWidth

    Rectangle {
        id: track

        anchors.verticalCenter: parent.verticalCenter
        border.color: root.hot ? Theme.fill(Theme.borderHover) : Theme.fill(Theme.borderIdle)
        border.width: root.checked ? 0 : 1
        color: root.checked ? Theme.fill(Theme.fillSelected) : Theme.fill(Theme.fillIdle)
        height: root.trackHeight
        radius: Theme.cornerRadius
        width: root.trackWidth

        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }

        Rectangle {
            id: knob

            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? Theme.brightForeground : Theme.dimForeground
            height: root.knobSize
            radius: Theme.cornerRadius
            width: root.knobSize
            x: root.checked ? track.width - width - Theme.space(2) : Theme.space(2)

            Behavior on x {
                NumberAnimation {
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }

        // The whole control holds the focus; the ring goes round the track (review Q6).
        FocusRing {
            shown: root.activeFocus
        }
    }

    Text {
        id: label

        anchors.left: track.right
        anchors.leftMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root.text
        visible: root.text.length > 0
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: {
            root.forceActiveFocus();
            root.checked = !root.checked;
            root.toggled();
        }
    }

    Keys.onSpacePressed: {
        root.checked = !root.checked;
        root.toggled();
    }
}
