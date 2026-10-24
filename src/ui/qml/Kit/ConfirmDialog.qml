pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The shell's own confirm card (spec v2 §3 Fig. 6, `.confirm`): 370 wide, a title-sized
// message, a dim line under it, an optional row of the caller's own (Fig. 6's "also delete
// files from disk" toggle), and two 88-wide caption buttons at the right.
//
// `h` and `l` — and left and right — move the cursor between the two buttons, `enter` runs
// the one under the cursor, `esc` cancels. `tab` and `shift+tab` also reach the caller's own
// row (KIT-3: Fig. 6's toggle, which `space` then flips), cycling rows → cancel → confirm;
// the button cursor stays drawn meanwhile, since `enter` still runs it. A destructive dialog
// opens with the cursor on *cancel* and prints its confirm button in red, with a red outline
// once the cursor reaches it (`.btn.danger.sel`); a harmless one opens on confirm, drawn as
// the kit's accent-outlined primary.
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
    property string cancelText: qsTr("cancel")
    property string confirmText: qsTr("confirm")
    /// Red confirm button, and the cursor starts on cancel.
    property bool destructive: false
    property bool inline: false
    /// While open, keys the dialog has no use for stop here instead of reaching the view
    /// behind the scrim (KIT-6); only `ctrl+q` goes on, since quitting is never modal.
    property bool modal: true
    /// 0 = cancel, 1 = confirm.
    property int cursor: 0
    /// The card's width; Fig. 6's 370 unless a caller's rows need more (docs/26's 520).
    property int panelWidth: Theme.space(370)
    /// Called first with every key the dialog sees; answering true takes the key (docs/26's
    /// `j`/`k` between a caller's choice rows). Null: only the dialog's own keys.
    property var keyHandler: null

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
    /// Everything `bodyFocusables()` has ever recognised.
    property var _known: []

    readonly property int _margin: Theme.space(16)

    /// Show it, remembering the focus and putting the cursor where the kind of question says.
    function open(): void {
        if (root._opened)
            return;
        root._previousFocus = root.Window.activeFocusItem;
        root.cursor = root.destructive ? 0 : 1;
        root._opened = true;
        root.bodyFocusables(); // claim the rows' tab before the first key
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

    /// The caller's rows the keyboard can reach (Fig. 6's toggle), in tab order. As in
    /// `Sheet`, collecting takes `activeFocusOnTab` off what it finds, because Qt would
    /// otherwise move `tab` along the window's chain, out of the dialog, before the dialog
    /// sees it; `_known` keeps recognising those items afterwards.
    function bodyFocusables(): var {
        const out = [];
        root._collect(bodyColumn, out);
        for (const item of out) {
            if (root._known.indexOf(item) < 0)
                root._known.push(item);
            item.activeFocusOnTab = false;
        }
        return out;
    }

    /// Which of `bodyFocusables()` holds the keyboard, or -1 when the buttons do.
    function bodyFocusIndex(): int {
        const list = root.bodyFocusables();
        let item = root.Window.activeFocusItem;
        while (item && item !== root) {
            const at = list.indexOf(item);
            if (at >= 0)
                return at;
            item = item.parent;
        }
        return -1;
    }

    function _collect(item: var, out: var): void {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (!child.visible || !child.enabled)
                continue;
            if (child.activeFocusOnTab === true || root._known.indexOf(child) >= 0) {
                out.push(child);
                continue;
            }
            root._collect(child, out);
        }
    }

    /// `tab` runs over the caller's rows and then the two buttons, and wraps; `shift+tab`
    /// runs back. A dialog with no rows keeps the old clamp between its two buttons.
    function _cycle(delta: int): void {
        const rows = root.bodyFocusables();
        const at = root.bodyFocusIndex();
        // Slots: 0 .. rows-1 are the rows, rows is cancel, rows + 1 is confirm.
        const count = rows.length + 2;
        const here = at >= 0 ? at : rows.length + root.cursor;
        let next = here + delta;
        if (rows.length === 0)
            next = Math.max(0, Math.min(1, next));
        else
            next = (next + count) % count;
        if (next < rows.length) {
            rows[next].forceActiveFocus();
        } else {
            root.cursor = next - rows.length;
            root.forceActiveFocus();
        }
    }

    /// The one key a modal overlay lets through: `ctrl+q`.
    function _passesThrough(event: var): bool {
        return (event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_Q;
    }

    function _onKey(event: var): void {
        if (root.keyHandler && root.keyHandler(event) === true) {
            event.accepted = true;
            return;
        }
        switch (event.key) {
        case Qt.Key_Escape:
            root.cancel();
            break;
        case Qt.Key_H:
        case Qt.Key_Left:
            if (root.bodyFocusIndex() >= 0)
                root.forceActiveFocus();
            root.moveCursor(-1);
            break;
        case Qt.Key_L:
        case Qt.Key_Right:
            if (root.bodyFocusIndex() >= 0)
                root.forceActiveFocus();
            root.moveCursor(1);
            break;
        case Qt.Key_Backtab:
            root._cycle(-1);
            break;
        case Qt.Key_Tab:
            root._cycle((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
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
    implicitWidth: root.panelWidth
    visible: root.inline || root._opened

    Keys.onPressed: event => {
        root._onKey(event);
        if (root.modal && root._opened && !event.accepted)
            event.accepted = !root._passesThrough(event);
    }

    Scrim {
        anchors.fill: parent
        visible: !root.inline

        onDismissed: root.cancel()
    }

    // The panel's ground and edge are the skin's Plate (#171): Omarchy's rounded edge,
    // Console's cut plate.
    Item {
        id: panel

        readonly property int padding: Theme.space(18)

        Accessible.name: root.message
        Accessible.role: Accessible.AlertMessage
        implicitHeight: layout.implicitHeight + 2 * panel.padding
        width: Math.min(root.panelWidth, root.inline ? root.panelWidth
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
                // #79: the line under the question (often a torrent's name). The question
                // is the card's own name already, so its title is not said twice.
                Accessible.name: text
                Accessible.role: Accessible.StaticText
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

                    // 88 wide as drawn, wider when the caption says more ("delete, keep files").
                    Layout.preferredWidth: Math.max(Theme.space(88), confirmButton.implicitWidth)
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

        // The panel's own ground (its padding, the message, the gaps around the buttons)
        // takes the clicks nothing on it wanted. Without it they fell through to the Scrim
        // underneath, and a click inside the dialog cancelled it.
        MouseArea {
            // Last, at z -1: under the rows, and `children[0]` is still the layout.
            acceptedButtons: Qt.AllButtons
            anchors.fill: parent
            z: -1

            onWheel: wheel => wheel.accepted = true
        }

        // Declared last so `children[0]` stays the layout, and drawn first (z -2).
        Plate {
            anchors.fill: parent
            edgeColor: Theme.edgeColor
            edgeWidth: Theme.edgeWidth
            fill: Theme.background
            objectName: "confirmPlate"
            radius: Theme.cornerRadius
            z: -2
        }
    }
}
