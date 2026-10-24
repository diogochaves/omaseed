pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The list rows of Settings' qBittorrent group (docs/16 §4: trackers to add, banned
// addresses, patterns to skip, watch folders, the Web UI's networks and domains): one entry
// per line in a plain text box. `ctrl+enter` or "save" hands the lines back through `saved`;
// enter is a new line. Blank lines and stray spaces are dropped. A click anywhere in the box
// off the text puts the caret at the end; a scroll bar shows once the lines outgrow the box.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect Menu.qml notes). It is hand-formatted.
Sheet {
    id: root

    /// What the lines are ("tracker URLs"), for the field's label.
    property string what: ""
    readonly property alias editor: editor

    /// The lines, trimmed, without blanks.
    signal saved(var lines)

    function openWith(title: string, what: string, lines: var): void {
        root.title = title;
        root.what = what;
        editor.text = lines.join("\n");
        root.open();
        editor.cursorPosition = editor.length;
    }

    /// Put the caret at the end of the text, the editor's keyboard on.
    function focusEnd(): void {
        editor.forceActiveFocus();
        editor.cursorPosition = editor.length;
    }

    function lines(): var {
        return editor.text.split("\n").map(line => line.trim()).filter(line => line.length > 0);
    }

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("save"),
            "primary": true
        }
    ]
    glyph: Glyphs.settings
    sheetWidth: Theme.space(520)
    subtitle: qsTr("one per line · %1").arg(root.what)

    onAccepted: {
        root.saved(root.lines());
        root.close();
    }

    Rectangle {
        /// The sheet's focus trap gives the keyboard to anything with a `focusInput()`.
        function focusInput(): void {
            editor.forceActiveFocus();
        }

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.space(220)
        border.color: editor.activeFocus ? Theme.accent : Theme.fill(Theme.borderIdle)
        border.width: 1
        color: Theme.on.idle.fill
        radius: Theme.cornerRadius

        // The box's padding: a click there puts the caret at the end, as below the text does.
        // a11y-ignore A2: the pointer's way into the editor, which a screen reader finds (#77)
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            objectName: "listBoxMouse"

            onPressed: root.focusEnd()
        }

        Flickable {
            id: scroller

            anchors.fill: parent
            anchors.margins: Theme.space(8)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            // At least the box: the room under the last line is the content's, so a click
            // there reaches the MouseArea below rather than the Flickable.
            contentHeight: Math.max(editor.implicitHeight, scroller.height)
            contentWidth: width

            // The editor is only as tall as its lines: the rest of the box focuses it too.
            // a11y-ignore A2: the pointer's way into the editor, as above (#77)
            MouseArea {
                cursorShape: Qt.IBeamCursor
                height: scroller.contentHeight
                objectName: "listEmptyMouse"
                width: scroller.width
                z: -1

                onPressed: root.focusEnd()
            }

            ScrollBar {
                id: listBar

                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.top: parent.top
                flickable: scroller
                objectName: "listScrollBar"
                parent: scroller
                vertical: true
                width: Theme.space(6)
            }

            TextEdit {
                id: editor

                // #91: the box by the lines it holds ("tracker URLs").
                Accessible.name: root.what.length > 0 ? root.what : root.title
                Accessible.role: Accessible.EditableText
                // The lines on the box's 4 % fill, a selection on the selected fill
                // (docs/DECISIONS.md C30 F1).
                color: Theme.on.idle.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                objectName: "listEditor"
                selectByMouse: true
                selectedTextColor: Theme.on.selected.brightForeground
                selectionColor: Theme.on.selected.fill
                width: scroller.width - (listBar.needed ? listBar.width + Theme.space(4) : 0)
                wrapMode: TextEdit.WrapAnywhere

                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && (event.modifiers & Qt.ControlModifier)) {
                        root.triggerPrimary();
                        event.accepted = true;
                    }
                }
                // Keep the caret in view while typing past the bottom.
                onCursorRectangleChanged: {
                    const rect = editor.cursorRectangle;
                    if (rect.y < scroller.contentY)
                        scroller.contentY = rect.y;
                    else if (rect.y + rect.height > scroller.contentY + scroller.height)
                        scroller.contentY = rect.y + rect.height - scroller.height;
                }
            }
        }
    }
}
