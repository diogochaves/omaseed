pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The anchored actions list of spec v2 §3 Fig. 11 (`.menu`, `.actions`): verb at the left,
// key at the right, separators between the groups. Opened at the cursor row on `enter` or by
// a right-click, so the caller sets `anchorX`/`anchorY` in this item's own coordinates; the
// panel clamps itself inside the item rather than hanging off the window edge. Taller than the
// item (the actions menu on a short window), the panel is cut to the item's height less the
// edges and its rows scroll: the wheel moves them, and the keyboard cursor keeps itself in
// view. The clamp already slides a panel that does not fit below its anchor up over it, so a
// menu never needs to flip to open upwards.
//
// `items` is `[{text, glyph, key, action, disabled, danger}]`, or `{separator: true}` for a
// rule. `activated` carries `action` when the row has one and the text otherwise. `heading`,
// when set, is a dim line above the rows, never a row itself (Q24: "2 selected"); `footer` a
// dim caption under them, wrapped, under a hairline. A row may also carry `glyphColor` (its
// glyph's colour: the server list's state dots, docs/34 D3 a), `note` (a dim word at the
// right, where a key would be, that no key press matches), `noteColor` (that word's colour, a
// failing server's red or yellow), `dim` (its text dim, though it can still be picked) and
// `checked` (a check or radio row: a ✓ before its key while true, and a screen reader hears
// it checkable). `{caption: true, text}` is a dim small line between the rows that is never
// a row (the app menu's "servers", "this window"). `headingTitle`, when set, leads the
// heading in bright demi-bold ("omaseed" before "0.5.0 · public", board 46).
//
// Keys: `j`/`k` and up/down move over the rows (separators and disabled rows are skipped),
// `enter` runs the row under the cursor, a row's own key letter runs it straight away
// (Fig. 11: `c` rechecks without moving the cursor first), `esc` closes.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect MainView.qml and TorrentTable.qml note).
// It is hand-formatted to qmlformat's own style instead.
Item {
    id: root

    /// `[{text, glyph, key, action, disabled, danger}]` / `{separator: true}`.
    property var items: []
    /// Where the panel's top-left corner wants to be, in this item's coordinates.
    property int anchorX: 0
    property int anchorY: 0
    /// The spec's `.menu` is 280 wide at most.
    property int menuWidth: Theme.space(280)
    /// A dim line above the rows saying what they act on; empty draws none.
    property string heading: ""
    /// A bright word before the heading (board 46's app menu: "omaseed"); empty draws none.
    property string headingTitle: ""
    /// A dim caption under the rows, wrapped (docs/34 D5 a); empty draws none.
    property string footer: ""
    property int cursorIndex: 0
    /// Draw the panel alone, at 0,0, with no scrim (the gallery).
    property bool inline: false
    /// Whether the scrim behind the panel paints the spec's 70 % ground (Fig. 11) or stays
    /// invisible while still eating the clicks underneath.
    property bool dimmed: true
    /// While open, keys the menu has no use for stop here instead of reaching the view
    /// behind the scrim (KIT-6); only `ctrl+q` goes on, since quitting is never modal.
    property bool modal: true
    /// A right-click outside the panel closes the menu as any click does, and is then
    /// `rightClickedOutside`, for the owner to right-click what lies there (#51 D17).
    property bool passRightClick: false

    readonly property bool opened: root._opened
    readonly property alias panel: panel
    /// The rows' column and the Flickable that scrolls it, for the tests.
    readonly property alias rowsLayout: rows
    readonly property alias rowsScroller: rowScroller

    /// The row's `action`, or its `text` when it has none.
    signal activated(string action)
    /// `esc`, or a click outside the panel.
    signal rejected
    /// With `passRightClick`: a right-click outside the panel at `x`, `y` (this item's
    /// coordinates), after the menu closed and `rejected` went out.
    signal rightClickedOutside(real x, real y)

    property bool _opened: false
    property Item _previousFocus: null
    /// `openAbove()`: `anchorY` is where the panel's bottom edge wants to be.
    property bool _up: false

    readonly property int _edge: Theme.space(8)
    /// Where the pointer last put the cursor, in this item's coordinates.
    property point _pointer: Qt.point(-1, -1)

    /// The pointer is on row `index` (through its MouseArea `area`): the cursor follows it,
    /// unless only the rows moved under a still pointer. Qt re-delivers hover when content
    /// scrolls, and the keyboard cursor scrolling into view would hand the cursor straight
    /// back to whatever row came to rest under the pointer.
    /// The first hover after opening only notes where the pointer rests, as in Kit Palette: a
    /// menu that opens under a still pointer (a right-click opens it right there) gets that
    /// hover without the pointer moving.
    function _pointerOn(index: int, area: MouseArea): void {
        const at = area.mapToItem(root, area.mouseX, area.mouseY);
        const known = root._pointer.x >= 0;
        if (at.x === root._pointer.x && at.y === root._pointer.y)
            return;
        root._pointer = at;
        if (known)
            root.cursorIndex = index;
    }

    function open(): void {
        if (root._opened)
            return;
        root._previousFocus = root.Window.activeFocusItem;
        root.cursorIndex = root._firstEnabled();
        root._pointer = Qt.point(-1, -1);
        root._opened = true;
        rowScroller.contentY = 0;
        root._showCursor();
        root.forceActiveFocus();
    }

    /// Open the menu with its top-left corner at `x`, `y` (clamped inside this item).
    function openAt(x: int, y: int): void {
        root._up = false;
        root.anchorX = x;
        root.anchorY = y;
        root.open();
    }

    /// Open the menu with its bottom-left corner at `x`, `y` (clamped inside this item): the
    /// panel grows up from a control at the bottom of the view (the selection bar's ⋯).
    function openAbove(x: int, y: int): void {
        root._up = true;
        root.anchorX = x;
        root.anchorY = y;
        root.open();
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

    /// Move over the rows, skipping separators and disabled rows; it does not wrap.
    function moveCursor(delta: int): void {
        let at = root.cursorIndex + delta;
        while (at >= 0 && at < root.items.length) {
            if (root.selectable(at)) {
                root.cursorIndex = at;
                root._showCursor();
                return;
            }
            at += delta;
        }
    }

    /// Scroll the rows so the cursor's row is in view. Only the keyboard asks: a row the
    /// pointer moves onto is already in view, and scrolling to it would fight the wheel.
    function _showCursor(): void {
        const row = rowRepeater.itemAt(root.cursorIndex);
        if (row === null)
            return;
        if (row.y < rowScroller.contentY)
            rowScroller.contentY = row.y;
        else if (row.y + row.height > rowScroller.contentY + rowScroller.height)
            rowScroller.contentY = row.y + row.height - rowScroller.height;
    }

    /// True when row `index` is a real, enabled row.
    function selectable(index: int): bool {
        if (index < 0 || index >= root.items.length)
            return false;
        const item = root.items[index];
        return item.separator !== true && item.caption !== true && item.disabled !== true;
    }

    /// Run row `index`, if it can be run.
    function activate(index: int): void {
        if (!root.selectable(index))
            return;
        const item = root.items[index];
        root.close();
        root.activated(item.action !== undefined ? item.action : item.text);
    }

    /// The row whose own key chip is exactly `text`, or -1.
    function indexOfKey(text: string): int {
        if (text.length === 0) // a modifier alone (alt, held for the badges) is no row's key
            return -1;
        for (let i = 0; i < root.items.length; ++i) {
            if (root.selectable(i) && root.items[i].key === text)
                return i;
        }
        return -1;
    }

    function _firstEnabled(): int {
        for (let i = 0; i < root.items.length; ++i) {
            if (root.selectable(i))
                return i;
        }
        return -1;
    }

    /// The one key a modal overlay lets through: `ctrl+q`.
    function _passesThrough(event: var): bool {
        return (event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_Q;
    }

    function _onKey(event: var): void {
        if ((event.modifiers & Qt.ControlModifier) !== 0)
            return;
        switch (event.key) {
        case Qt.Key_Escape:
            root.close();
            root.rejected();
            break;
        case Qt.Key_Down:
        case Qt.Key_J:
            root.moveCursor(1);
            break;
        case Qt.Key_Up:
        case Qt.Key_K:
            root.moveCursor(-1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.activate(root.cursorIndex);
            break;
        // KIT-2: unaccepted, `tab` goes on to the window, which moves the focus to whatever
        // is next behind the scrim; the menu stays drawn but deaf. It moves the cursor.
        case Qt.Key_Tab:
            root.moveCursor((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
            break;
        case Qt.Key_Backtab:
            root.moveCursor(-1);
            break;
        default:
            {
                const at = root.indexOfKey(event.text);
                if (at < 0)
                    return;
                root.activate(at);
            }
            break;
        }
        event.accepted = true;
    }

    implicitHeight: panel.implicitHeight
    implicitWidth: root.menuWidth
    visible: root.inline || root._opened

    Keys.onPressed: event => {
        root._onKey(event);
        if (root.modal && root._opened && !event.accepted)
            event.accepted = !root._passesThrough(event);
    }

    Scrim {
        id: scrim

        anchors.fill: parent
        opacity: root.dimmed ? 1 : 0
        passRightClick: root.passRightClick
        visible: !root.inline

        onDismissed: {
            root.close();
            root.rejected();
        }
        onRightClicked: (x, y) => {
            const at = scrim.mapToItem(root, x, y);
            root.close();
            root.rejected();
            root.rightClickedOutside(at.x, at.y);
        }
    }

    Item {
        id: panel

        readonly property int padding: Theme.space(6)

        // #79: a menu with a heading ("switch omaseed to") is named by it, so a screen reader
        // says it first; since #91 the heading also comes before the rows in the tree.
        Accessible.name: [root.headingTitle, root.heading].filter(part => part.length > 0).join(" · ")
        Accessible.role: Accessible.PopupMenu
        implicitHeight: heading.height + rows.implicitHeight + footerItem.height
                        + 2 * panel.padding
        // Never taller than the item it clamps itself in: the rows scroll instead.
        height: root.inline ? implicitHeight
                : Math.min(implicitHeight, Math.max(0, root.height - 2 * root._edge))
        width: Math.min(root.menuWidth, root.inline ? root.menuWidth
                : Math.max(0, root.width - 2 * root._edge))
        x: root.inline ? 0
                : Math.max(root._edge, Math.min(root.anchorX, root.width - width - root._edge))
        y: root.inline ? 0
                : Math.max(root._edge, Math.min(root._up ? root.anchorY - height : root.anchorY,
                                                root.height - height - root._edge))

        // The menu's ground and edge (board 46 `.menu`), drawn by the skin's Plate face.
        Plate {
            anchors.fill: parent
            edgeColor: Theme.menuEdgeColor
            edgeWidth: Theme.menuEdgeWidth
            fill: Theme.background
            objectName: "menuPlate"
        }

        // Before the rows in the tree (#91: a screen reader reads children in declaration
        // order, and the heading is said first), drawn above them all the same.
        Item {
            id: heading

            readonly property bool shown: root.heading.length > 0 || root.headingTitle.length > 0
            /// Board 46 `.menu .mh` (a grid): the bright title on its line, the dim words under
            /// it; the dim words alone (Q24's "2 selected") take the title's line.
            readonly property bool twoLines: root.headingTitle.length > 0 && root.heading.length > 0
            readonly property real contentHeight: heading.twoLines
                                                  ? Theme.railRowHeight + Theme.menuHeadLineGap + headingLine.implicitHeight
                                                  : Theme.railRowHeight

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: panel.padding
            height: heading.shown ? heading.contentHeight + 2 * Theme.menuHeadPadding + 1 : 0
            objectName: "menuHeading"
            visible: heading.shown
            z: 1

            Label {
                id: headingLine

                anchors.left: parent.left
                anchors.leftMargin: Theme.menuHeadInset
                anchors.right: parent.right
                anchors.rightMargin: Theme.menuHeadInset
                dim: true
                elide: Text.ElideRight
                height: heading.twoLines ? implicitHeight : Theme.railRowHeight
                objectName: "menuHeadingLine"
                size: heading.twoLines ? "small" : "body"
                text: root.heading
                verticalAlignment: Text.AlignVCenter
                y: heading.twoLines ? Theme.railRowHeight + Theme.menuHeadLineGap : 0
            }

            Label {
                id: headingTitle

                anchors.left: parent.left
                anchors.leftMargin: Theme.menuHeadInset
                anchors.right: parent.right
                anchors.rightMargin: Theme.menuHeadInset
                bright: true
                elide: Text.ElideRight
                font.weight: Theme.menuHeadTitleWeight
                height: Theme.railRowHeight
                objectName: "menuHeadingTitle"
                text: root.headingTitle
                // Board 46 `.menu .mh b`: the skin's heading voice.
                voice: "title"
                verticalAlignment: Text.AlignVCenter
                visible: root.headingTitle.length > 0
            }

            Hairline {
                anchors.left: parent.left
                anchors.leftMargin: Theme.menuHeadInset
                anchors.right: parent.right
                anchors.rightMargin: Theme.menuHeadInset
                y: heading.contentHeight + Theme.menuHeadPadding
            }
        }

        Flickable {
            id: rowScroller

            anchors.bottom: footerItem.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: heading.bottom
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            contentHeight: rows.implicitHeight
            contentWidth: width
            interactive: contentHeight > height
            objectName: "menuRowScroller"

            ColumnLayout {
                id: rows

                objectName: "menuRows"
                spacing: 0
                width: rowScroller.width

                Repeater {
                    id: rowRepeater

                    model: root.items

                    Item {
                        id: menuRow

                        required property int index
                        required property var modelData

                        readonly property bool current: menuRow.index === root.cursorIndex
                        readonly property bool disabled: menuRow.modelData.disabled === true
                        readonly property bool separator: menuRow.modelData.separator === true
                        readonly property bool caption: menuRow.modelData.caption === true
                        readonly property bool checkable: menuRow.modelData.checked !== undefined
                        readonly property bool filled: menuRow.current && !menuRow.separator
                                                       && !menuRow.caption
                        /// docs/DECISIONS.md C30 F1: the row's words on its ground, the cursor's
                        /// fill on the current row. The fill comes and goes at once, so do they.
                        /// A row's `glyphColor` and `noteColor` are window roles: the ground's of
                        /// the same (C30 F3).
                        readonly property Ink ink: Ink {
                            animated: false
                            ground: menuRow.filled ? Theme.on.cursor : Theme.on.window
                        }

                        Accessible.checkable: menuRow.checkable
                        Accessible.checked: menuRow.modelData.checked === true
                        Accessible.name: menuRow.modelData.text ?? ""
                        Accessible.role: menuRow.separator ? Accessible.Separator
                                       : menuRow.caption ? Accessible.StaticText
                                       : Accessible.MenuItem
                        // A screen reader's press is the click (#75): not on a separator, a
                        // disabled row or a disabled menu, which take no click either. A
                        // MouseArea's own `enabled` is its flag alone, so the row's says
                        // whether something above it is disabled.
                        Accessible.onPressAction: if (menuRow.enabled && rowArea.enabled)
                                                      root.activate(menuRow.index)
                        Layout.fillWidth: true
                        Layout.preferredHeight: menuRow.separator ? 2 * Theme.space(4) + 1
                                : menuRow.caption ? captionText.implicitHeight + Theme.space(6)
                                : Theme.railRowHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.on.cursor.fill
                            visible: menuRow.filled
                        }

                        // The cursor's mark: none in Omarchy's face (the fill says it),
                        // Console's brackets (board 46 `.mi.cur::after`).
                        SelectionMark {
                            anchors.fill: parent
                            objectName: "menuRowMark"
                            visible: menuRow.filled
                        }

                        Label {
                            id: captionText

                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(14)
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(14)
                            dim: true
                            elide: Text.ElideRight
                            objectName: "menuRowCaption"
                            size: "caption"
                            text: menuRow.caption ? (menuRow.modelData.text ?? "") : ""
                            visible: menuRow.caption
                        }

                        Hairline {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(14)
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(14)
                            anchors.verticalCenter: parent.verticalCenter
                            visible: menuRow.separator
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space(14)
                            anchors.rightMargin: Theme.space(14)
                            spacing: Theme.space(8)
                            visible: !menuRow.separator && !menuRow.caption

                            Glyph {
                                Layout.preferredHeight: Theme.font.body
                                Layout.preferredWidth: Theme.space(14)
                                color: menuRow.ink.of(menuRow.modelData.glyphColor ?? Theme.dimForeground)
                                objectName: "menuRowGlyph"
                                text: menuRow.modelData.glyph ?? ""
                                visible: text.length > 0
                            }

                            Label {
                                Layout.fillWidth: true
                                color: menuRow.disabled ? menuRow.ink.dimForeground
                                        : menuRow.modelData.danger === true ? menuRow.ink.red
                                        : menuRow.current ? menuRow.ink.accent
                                        : menuRow.modelData.dim === true ? menuRow.ink.dimForeground
                                        : menuRow.ink.foreground
                                elide: Text.ElideRight
                                objectName: "menuRowText"
                                text: menuRow.modelData.text !== undefined
                                      ? menuRow.modelData.text : ""
                            }

                            Glyph {
                                Layout.preferredHeight: Theme.font.caption
                                Layout.preferredWidth: Theme.font.caption
                                color: menuRow.ink.accent
                                objectName: "menuRowCheck"
                                pixelSize: Theme.font.caption
                                text: Glyphs.check
                                visible: menuRow.modelData.checked === true
                            }

                            Label {
                                color: menuRow.ink.of(menuRow.modelData.noteColor ?? Theme.dimForeground)
                                objectName: "menuRowNote"
                                size: "caption"
                                text: menuRow.modelData.key !== undefined
                                      ? menuRow.modelData.key : (menuRow.modelData.note ?? "")
                                visible: text.length > 0
                            }
                        }

                        MouseArea {
                            id: rowArea

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: !menuRow.separator && !menuRow.caption && !menuRow.disabled
                            hoverEnabled: true

                            onClicked: root.activate(menuRow.index)
                            onEntered: root._pointerOn(menuRow.index, rowArea)
                            onPositionChanged: root._pointerOn(menuRow.index, rowArea)
                        }
                    }
                }
            }
        }

        // A wheel notch moves three rows, and adds to a glide still running.
        WheelScroll {
            anchors.fill: rowScroller
            flickable: rowScroller
            rowHeight: Theme.railRowHeight
        }

        // There is more above or below: a thin bar at the right edge, inside the border,
        // shown only while the rows overflow.
        ScrollBar {
            flickable: rowScroller
            height: rowScroller.height
            objectName: "menuScrollBar"
            vertical: true
            width: Theme.space(4)
            x: panel.width - width - Theme.menuEdgeWidth - Theme.space(2)
            y: rowScroller.y
        }

        // Under the rows: a hairline, then the caption, wrapped to the panel's width.
        Item {
            id: footerItem

            anchors.bottom: parent.bottom
            anchors.bottomMargin: panel.padding
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.footer.length > 0 ? footerText.implicitHeight + 2 * Theme.space(6) + 1 : 0
            objectName: "menuFooter"
            visible: root.footer.length > 0

            Hairline {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(14)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(14)
                y: Theme.space(4)
            }

            Label {
                id: footerText

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(14)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(14)
                dim: true
                size: "small"
                text: root.footer
                wrapMode: Text.WordWrap
                y: Theme.space(6) + 1
            }
        }

        // The panel's own ground (its padding, a separator, a disabled row) takes the clicks
        // nothing on it wanted. Without it they fell through to the Scrim underneath, and a
        // click inside the menu closed it.
        MouseArea {
            // Last, at z -1: under the rows, and `children[0]` is still the layout.
            acceptedButtons: Qt.AllButtons
            anchors.fill: parent
            z: -1

            onWheel: wheel => wheel.accepted = true
        }
    }
}
