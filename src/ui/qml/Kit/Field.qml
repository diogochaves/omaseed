import QtQuick
import Omaseed.Kit

// Single-line filter/search field (spec v2 §1 Fig. 1 `.field.search`: a leading `/` hint, a
// placeholder, and a trailing live count). Bordered idle (4 %/40 %), hover/cursor (8 %/25 %),
// accent ring on focus — spec §8 tokens page. `Escape` clears the text and emits `cleared`;
// `Return` emits `accepted`. Typing updates `text` directly (it is a plain alias over the
// inner `TextInput`), so a caller that wants single-source-of-truth state should listen for
// `onTextChanged` and write back through a `Connections`/binding of its own — assigning
// `text` imperatively from outside while focused would fight the user's keystrokes.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property string hint: ""
    property string trailing: ""
    property bool hasCursor: false

    signal accepted
    signal cleared

    readonly property bool hot: root.hasCursor || hoverHandler.hovered || input.activeFocus

    border.color: input.activeFocus ? Theme.accent : root.hot ? Theme.fill(Theme.borderHover) :
                                                                Theme.fill(Theme.borderIdle)
    border.width: 1
    color: root.hot ? Theme.fill(Theme.fillHover) : Theme.fill(Theme.fillIdle)
    height: Theme.controlHeight
    implicitWidth: Theme.space(220)
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    Text {
        id: hintText

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root.hint
        visible: root.hint.length > 0
    }

    Text {
        id: trailingText

        anchors.right: parent.right
        anchors.rightMargin: Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: root.trailing
        visible: root.trailing.length > 0
    }

    TextInput {
        id: input

        anchors.left: hintText.visible ? hintText.right : parent.left
        anchors.leftMargin: hintText.visible ? Theme.space(6) : Theme.space(10)
        anchors.right: trailingText.visible ? trailingText.left : parent.right
        anchors.rightMargin: trailingText.visible ? Theme.space(6) : Theme.space(10)
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        color: Theme.brightForeground
        cursorDelegate: Rectangle {
            color: Theme.accent
            width: 1
        }
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        selectByMouse: true
        selectionColor: Theme.selection

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: root.placeholder
            visible: input.text.length === 0 && !input.activeFocus
        }

        Keys.onEscapePressed: {
            input.text = "";
            root.cleared();
        }
        Keys.onReturnPressed: root.accepted()
    }
}
