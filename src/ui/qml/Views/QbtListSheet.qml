pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The list rows of Settings' qBittorrent group (docs/16 §4: trackers to add, banned
// addresses, patterns to skip, watch folders, the Web UI's networks and domains): one entry
// per line in a plain text box. `ctrl+enter` or "save" hands the lines back through `saved`;
// enter is a new line. Blank lines and stray spaces are dropped.
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
    hints: [
        {
            "key": "ctrl+enter",
            "label": qsTr("save")
        },
        {
            "key": "enter",
            "label": qsTr("new line")
        }
    ]
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
        color: Theme.fill(Theme.fillIdle)
        radius: Theme.cornerRadius

        Flickable {
            id: scroller

            anchors.fill: parent
            anchors.margins: Theme.space(8)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            contentHeight: editor.implicitHeight
            contentWidth: width

            TextEdit {
                id: editor

                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                objectName: "listEditor"
                selectByMouse: true
                selectedTextColor: Theme.brightForeground
                selectionColor: Theme.fill(Theme.fillSelected)
                width: scroller.width
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
