pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The shell's own confirm card (spec v2 §3 Fig. 6, `.confirm`): 370 wide, a title-sized
// message, a dim line under it, an optional row of the caller's own (Fig. 6's "also delete
// files from disk" toggle), and two 88-wide caption buttons at the right.
//
// `h` and `l` — and `tab`, `shift+tab`, left and right — move the cursor between the two
// buttons, `enter` runs the one under the cursor, `esc` cancels. A destructive dialog opens
// with the cursor on *cancel* and prints its confirm button in red, with a red outline once
// the cursor reaches it (`.btn.danger.sel`); a harmless one opens on confirm, drawn as the
// kit's accent-outlined primary.
//
// `inline: true` drops the scrim and the centring, for the gallery.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect MainView.qml and TorrentTable.qml note).
// It is hand-formatted to qmlformat's own style instead.
Item {
    id: root

    /// The question (`.confirm .msg`) and the dim line under it (`.confirm .sub`).
    property string message: ""
    property string subtitle: ""
    /// The two button captions.
    property string cancelText: qsTr("Cancel")
    property string confirmText: qsTr("Confirm")
    /// Red confirm button, and the cursor starts on cancel.
    property bool destructive: false
    property bool inline: false
    /// 0 = cancel, 1 = confirm.
    property int cursor: 0

    /// The optional row between the message and the buttons (Fig. 6's toggle).
    default property alias content: bodyColumn.data

    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias body: bodyColumn
    readonly property alias cancelButton: cancelButton
    readonly property alias confirmButton: confirmButton

    signal accepted
    signal rejected

    property bool _opened: false
    property Item _previousFocus: null

    readonly property int _margin: Theme.space(16)

    /// Show it, remembering the focus and putting the cursor where the kind of question says.
    function open(): void {
        if (root._opened)
            return;
        root._previousFocus = root.Window.activeFocusItem;
        root.cursor = root.destructive ? 0 : 1;
        root._opened = true;
        root.forceActiveFocus();
    }

    function close(): void {
        if (!root._opened)
            return;
        root._opened = false;
        const previous = root._previousFocus;
        root._previousFocus = null;
        if (previous)
            previous.forceActiveFocus();
    }

    /// Move the cursor between the two buttons; it does not wrap (there are only two).
    function moveCursor(delta: int): void {
        root.cursor = Math.max(0, Math.min(1, root.cursor + delta));
    }

    /// Run the button under the cursor.
    function activate(): void {
        if (root.cursor === 1)
            root.confirm();
        else
            root.cancel();
    }

    function confirm(): void {
        root.close();
        root.accepted();
    }

    function cancel(): void {
        root.close();
        root.rejected();
    }

    function _onKey(event: var): void {
        switch (event.key) {
        case Qt.Key_Escape:
            root.cancel();
            break;
        case Qt.Key_H:
        case Qt.Key_Left:
        case Qt.Key_Backtab:
            root.moveCursor(-1);
            break;
        case Qt.Key_L:
        case Qt.Key_Right:
            root.moveCursor(1);
            break;
        case Qt.Key_Tab:
            root.moveCursor((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.activate();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    implicitHeight: panel.implicitHeight
    implicitWidth: Theme.space(370)
    visible: root.inline || root._opened

    Keys.onPressed: event => root._onKey(event)

    Scrim {
        anchors.fill: parent
        visible: !root.inline

        onDismissed: root.cancel()
    }

    Rectangle {
        id: panel

        readonly property int padding: Theme.space(18)

        border.color: Theme.accent
        border.width: 1
        color: Theme.background
        implicitHeight: layout.implicitHeight + 2 * panel.padding
        radius: Theme.cornerRadius
        width: Math.min(Theme.space(370), root.inline ? Theme.space(370)
                : Math.max(0, root.width - 2 * root._margin))
        x: root.inline ? 0 : Math.round((root.width - width) / 2)
        y: root.inline ? 0 : Math.round((root.height - height) / 2)

        ColumnLayout {
            id: layout

            anchors.left: parent.left
            anchors.margins: panel.padding
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Theme.space(16)

            Label {
                Layout.fillWidth: true
                bright: true
                lineHeight: 1.35
                size: "title"
                text: root.message
                wrapMode: Text.WordWrap
            }

            // A torrent name is often one long word, so it breaks at the edge rather than
            // running past it.
            Label {
                Layout.fillWidth: true
                dim: true
                size: "small"
                text: root.subtitle
                visible: root.subtitle.length > 0
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            ColumnLayout {
                id: bodyColumn

                Layout.fillWidth: true
                spacing: Theme.space(8)
            }

            RowLayout {
                id: buttons

                Layout.alignment: Qt.AlignRight
                Layout.fillWidth: true
                spacing: Theme.space(8)

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    id: cancelButton

                    Layout.preferredWidth: Theme.space(88)
                    // Fig. 6 draws both buttons as `.btn b` — a hairline outline at idle.
                    bordered: true
                    fontSize: Theme.font.caption
                    hasCursor: root.cursor === 0
                    text: root.cancelText

                    onClicked: root.cancel()
                }

                Button {
                    id: confirmButton

                    Layout.preferredWidth: Theme.space(88)
                    bordered: true
                    danger: root.destructive
                    fontSize: Theme.font.caption
                    hasCursor: root.cursor === 1
                    primary: !root.destructive && root.cursor === 1
                    selected: root.destructive && root.cursor === 1
                    text: root.confirmText

                    onClicked: root.confirm()
                }
            }
        }
    }
}
