import QtQuick
import Omaseed.Kit

// On/off switch with a trailing label (spec v2 §8 `.toggle`/`.sw`; proportions from
// /usr/share/omarchy/shell/Ui/ToggleSwitch.qml). Track `Theme.space(28)` × `Theme.space(14)`;
// `Space` toggles when focused; clicking anywhere in the control toggles too. On fills the
// track at the pressed step of `accent` (Q25), so on and off differ by more than the knob.
// A click leaves the keyboard where it was, as Button's does (docs/DECISIONS.md M8): the focus
// ring stayed round the track after the pointer left. `tab` still focuses and rings it.
Item {
    id: root

    property bool checked: false
    property string text: ""
    property bool hasCursor: false

    signal toggled

    /// Flip the switch and say so: what a click, `space` and a screen reader's press or
    /// toggle all do (#75).
    function toggle(): void {
        root.checked = !root.checked;
        root.toggled();
    }

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    readonly property int knobSize: root.trackHeight - Theme.space(4)
    readonly property int trackHeight: Theme.space(14)
    readonly property int trackWidth: Theme.space(28)

    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.name: root.text
    Accessible.role: Accessible.CheckBox

    // A screen reader's press or toggle is the click (#75); disabled, it does nothing, as the
    // pointer. Handled here, Qt does not flip `checked` behind the owner's back.
    Accessible.onPressAction: if (root.enabled)
                                  root.toggle()
    Accessible.onToggleAction: if (root.enabled)
                                   root.toggle()

    activeFocusOnTab: true
    height: implicitHeight
    implicitHeight: Math.max(root.trackHeight, label.implicitHeight)
    implicitWidth: track.width + (label.visible ? Theme.space(8) + label.implicitWidth : 0)
    width: implicitWidth

    Rectangle {
        id: track

        objectName: "toggleTrack"
        anchors.verticalCenter: parent.verticalCenter
        border.color: root.hot ? Theme.fill(Theme.borderHover) : Theme.fill(Theme.borderIdle)
        border.width: root.checked ? 0 : Theme.hairWidth
        // The ticked tint and the idle fill (docs/DECISIONS.md C30 F5); the knob is no text,
        // but takes the ground's colour so it reads as the ✓ on a ticked box does.
        color: root.checked ? Theme.on.ticked.fill : Theme.on.idle.fill
        height: root.trackHeight
        radius: Theme.cornerRadius
        width: root.trackWidth

        Behavior on color {
            ColorAnimation {
                duration: Theme.fillChangeDuration
            }
        }

        Rectangle {
            id: knob

            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? Theme.on.ticked.brightForeground : Theme.on.idle.dimForeground
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

        onClicked: root.toggle()
    }

    Keys.onSpacePressed: root.toggle()
}
