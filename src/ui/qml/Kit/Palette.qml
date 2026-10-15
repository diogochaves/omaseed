pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The one centred palette of spec v2 §4 (`.palette`, Fig. 12, 13, 14): a query row, rows with
// a title, a right-aligned dim meta and a dim second line, an outlined cursor, and the hint
// line inside the panel. Jump, category, tags, sort and columns are all this component with a
// different list behind it.
//
// Filtering is the caller's job — the palette draws exactly the `items` it is given and
// reports every keystroke through `query`/`queryChanged`, so a picker can rank, group or
// create-on-miss however it likes. `Fuzzy.match(text, query)` is the shared default test.
//
// Keys: `up`/`down` and `ctrl+p`/`ctrl+n` move (never `j`/`k` — in a palette those are typed
// text), `enter` activates, `tab` asks for the secondary action (Fig. 12's "tab actions"),
// `space` toggles when `multi` is set, `esc` clears a typed query and closes when there is
// none (the escape ladder: clear, then close). `inline: true` drops the scrim and the
// centring, for the gallery. A `reorderable` list (the columns palette, N2) also asks to move
// the cursor's row with `alt+up`/`alt+down` (`alt+k`/`alt+j`) and to put the default order back
// with `ctrl+r`; the caller owns the order, as it owns `checked`.
//
// `draggable` (opt-in, M13) lets the mouse order the rows too: press a row and drag it past
// the start-drag distance, a 1 px accent line shows where it will land, and the release asks
// the caller with `dropRequested(index, item, slot)`. A press that never travels that far is
// still a click. An item with `pinned: true` cannot be dragged, and the pinned rows at either
// end of the list keep their ends: nothing lands above a leading one or below a trailing one.
// Near the list's top or bottom edge the list scrolls on its own; `esc` drops the drag.
//
// The mouse: the pointer on a row moves the cursor there (as the kit Menu does), a click is
// `enter` on it (`space` in a multi-select), and a right-click is `tab` on it where the palette
// has a secondary action (`hasSecondary`). A footer hint flagged `clickable` runs too: the
// palette sends `hintClicked`, and the caller does what that key does.
//
// A row with `disabled: true` can't run now (docs/30 §3, D4): its title is dim, its meta and
// second line faint, the cursor skips it (and is -1 when no row can run), and `enter`, a click
// or the pointer do nothing on it, as the kit Menu has it. A row's `via` ("enter s") prints in
// the meta's place after a faint "via": the path to an action that has no key of its own.
// `emptyText` is a dim line over the list, for "nothing matches" (docs/30 P4).
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect MainView.qml and TorrentTable.qml note).
// It is hand-formatted to qmlformat's own style instead.
Item {
    id: root

    /// The dim glyph at the left of the query row (`.q .g`).
    property string glyph: ""
    /// What the row shows instead of the query while nothing is typed — Fig. 13's
    /// "category for 2 torrents". Empty means the row is only ever the query.
    property string title: ""
    /// The dim caption at the right of the query row (`.q .c`), e.g. "2 of 142".
    property string counter: ""
    property alias query: input.text
    /// `[{title, meta, via, detail, checked, key, pinned, disabled}]`.
    property var items: []
    /// A dim line between the query row and the list; empty draws nothing.
    property string emptyText: ""
    /// Show the check column and let `space` toggle rows (the tags picker).
    property bool multi: false
    /// The caller answers `secondary` (`tab`): a right-click on a row asks for it too.
    property bool hasSecondary: false
    /// Let `alt+up`/`alt+down` ask to move the cursor's row and `ctrl+r` ask for the default
    /// order (`moveRequested`, `resetRequested`).
    property bool reorderable: false
    /// Let the mouse drag rows to a new place (`dropRequested`); see the header.
    property bool draggable: false
    property var hints: []
    property string footerRight: ""
    property bool inline: false
    /// While open, keys the palette has no use for stop here instead of reaching the view
    /// behind the scrim (KIT-6); only `ctrl+q` goes on, since quitting is never modal.
    property bool modal: true
    /// The panel's maximum width; the spec's `.palette` is 480.
    property int paletteWidth: Theme.space(480)
    /// The row the keyboard is on.
    property int cursorIndex: 0
    /// How the list says it has more rows above or below (N3): "chevrons" (counted),
    /// "fade" or "scrollbar", as `Kit/ScrollHint` draws them. The host binds it to the
    /// app's scroll-hint setting.
    property string scrollHint: "chevrons"

    readonly property bool opened: root._opened
    readonly property alias panel: panel
    readonly property alias listView: list
    /// The row under the cursor, or null. A row that can't run is never the current one.
    readonly property var currentItem: root.canLand(root.cursorIndex) ? root.items[root.cursorIndex]
            : null

    /// `enter` on a row.
    signal activated(int index, var item)
    /// `tab` on a row (Fig. 12's "tab actions").
    signal secondary(int index, var item)
    /// `space` on a row while `multi` is set. The caller owns the checked state.
    signal toggled(int index, var item)
    /// `esc`, or a click on the scrim.
    signal rejected
    /// `alt+up` (-1) or `alt+down` (+1) on a row while `reorderable` is set. The caller
    /// moves it and, if it wants the cursor to follow, calls `cursorToKey()`.
    signal moveRequested(int index, var item, int delta)
    /// `ctrl+r` while `reorderable` is set.
    signal resetRequested
    /// A dragged row was let go (`draggable`): put row `index` before row `slot`, or after
    /// the last row when `slot` is `items.length`. Never sent for a drop where it started.
    signal dropRequested(int index, var item, int slot)
    /// A click on a footer hint flagged `clickable` (`{key, label, clickable: true}`).
    signal hintClicked(int index, var hint)

    /// True while a row is being dragged.
    readonly property bool dragging: root._dragFrom >= 0
    /// Where the dragged row would land (the `slot` of `dropRequested`), or -1 while nothing
    /// is dragged or the row would stay where it is.
    readonly property int dropSlot: {
        if (root._dragFrom < 0)
            return -1;
        const slot = root._slotAt(list.contentY + root._dragY, root._layoutRevision);
        return slot === root._dragFrom || slot === root._dragFrom + 1 ? -1 : slot;
    }

    property bool _opened: false
    /// The row being dragged, or -1; and the pointer's y over the list while it is.
    property int _dragFrom: -1
    property real _dragY: 0
    property Item _previousFocus: null
    /// Where the pointer last put the cursor, in this item's coordinates.
    property point _pointer: Qt.point(-1, -1)

    readonly property int _margin: Theme.space(16)
    /// The rule between rows: the spec's `.palette .it` uses the foreground at 5 %, lighter
    /// than the 12 % hairline everywhere else.
    readonly property color _rowRule: Theme.fill(Theme.rowRuleAlpha)

    function open(): void {
        if (root._opened)
            return;
        root._previousFocus = root.Window.activeFocusItem;
        root._pointer = Qt.point(-1, -1);
        root._opened = true;
        root.focusQuery();
    }

    function close(): void {
        if (!root._opened)
            return;
        root._opened = false;
        root._cancelDrag();
        const previous = root._previousFocus;
        root._previousFocus = null;
        if (previous)
            previous.forceActiveFocus();
    }

    /// Open with the cursor on the row whose `key` is `key` (the current value) instead of
    /// on row 0 — spec §4 Fig. 14 draws the cursor on "download speed · ▾ current". Falls
    /// back to row 0 when no row carries that key.
    function openOn(key: var): void {
        root.open();
        root.cursorIndex = 0; // the fallback, not the row the last session left it on
        root._settleCursor();
        root.cursorToKey(key);
    }

    /// Move the cursor onto the row carrying `key`. A no-op when there is none.
    function cursorToKey(key: var): void {
        for (let i = 0; i < root.items.length; ++i) {
            if (root.items[i].key === key && root.canLand(i)) {
                root.cursorIndex = i;
                root._showRow(i);
                return;
            }
        }
    }

    /// Put the caret in the query row.
    function focusQuery(): void {
        input.forceActiveFocus();
    }

    /// Move the cursor by `delta` rows that can run, stopping at the last one each way,
    /// keeping it in view.
    function moveCursor(delta: int): void {
        if (root.items.length === 0 || delta === 0)
            return;
        const step = delta > 0 ? 1 : -1;
        let target = root.cursorIndex >= 0 ? root.cursorIndex : (step > 0 ? -1 : root.items.length);
        for (let n = Math.abs(delta); n > 0; --n) {
            const next = root._landing(target + step, step);
            if (next < 0)
                break;
            target = next;
        }
        if (!root.canLand(target))
            return;
        root.cursorIndex = target;
        root._showRow(root.cursorIndex);
    }

    /// Whether the cursor may rest on row `index`: a row that exists and can run.
    function canLand(index: int): bool {
        return index >= 0 && index < root.items.length && root.items[index].disabled !== true;
    }

    /// The first row from `index` on, going by `step`, that the cursor may rest on; -1 if none.
    function _landing(index: int, step: int): int {
        for (let i = index; i >= 0 && i < root.items.length; i += step) {
            if (root.canLand(i))
                return i;
        }
        return -1;
    }

    /// Keep the cursor on a row that can run: where it is, else the next one down, else the
    /// next one up; -1 when no row can run.
    function _settleCursor(): void {
        if (root.items.length === 0) {
            root.cursorIndex = 0;
            return;
        }
        const at = Math.max(0, Math.min(root.cursorIndex, root.items.length - 1));
        const down = root._landing(at, 1);
        root.cursorIndex = down >= 0 ? down : root._landing(at, -1);
    }

    /// Scroll row `index` into view and clear of the hint's bands or fades.
    function _showRow(index: int): void {
        list.positionViewAtIndex(index, ListView.Contain);
        const row = list.itemAtIndex(index);
        if (row === null)
            return;
        const first = list.originY;
        const last = list.originY + Math.max(0, list.contentHeight - list.height);
        if (row.y < list.contentY + hint.clearTop)
            list.contentY = Math.max(first, row.y - hint.clearTop);
        else if (row.y + row.height > list.contentY + list.height - hint.clearBottom)
            list.contentY = Math.min(last, row.y + row.height - list.height + hint.clearBottom);
    }

    /// How many rows are wholly out of view each way. A row under a band counts as out of
    /// view, since the band hides it. `ListView.indexAt` notifies nothing and only sees rows
    /// once the view has laid them out, so each delegate reports its own box to `_rowBoxes`
    /// (index -> [y, height, row]) as it comes, moves, resizes or goes, and bumps
    /// `_layoutRevision`, which every binding below passes in.
    property int _layoutRevision: 0
    property var _rowBoxes: ({})
    function _setRowBox(index: int, row: Item, gone: bool): void {
        const held = root._rowBoxes[index];
        if (!gone)
            root._rowBoxes[index] = [row.y, row.height, row];
        else if (held !== undefined && held[2] === row) // not a newer row at the same index
            delete root._rowBoxes[index];
        root._layoutRevision++;
    }
    /// The row whose box holds content `y`, or -1.
    function _rowAt(y: real, revision: int): int {
        if (revision < 0)
            return -1;
        for (const key in root._rowBoxes) {
            const box = root._rowBoxes[key];
            if (y >= box[0] && y < box[0] + box[1])
                return Number(key);
        }
        return -1;
    }
    readonly property int _rawAbove: Math.max(0,
            root._rowAt(list.contentY + 1, root._layoutRevision))
    readonly property int _rawBelow: {
        const last = root._rowAt(list.contentY + list.height - 1, root._layoutRevision);
        return last < 0 ? 0 : list.count - 1 - last;
    }
    readonly property int _rowsAbove: {
        if (root._rawAbove === 0)
            return 0;
        return Math.max(root._rawAbove,
                root._rowAt(list.contentY + hint.bandHeight + 1, root._layoutRevision));
    }
    readonly property int _rowsBelow: {
        if (root._rawBelow === 0)
            return 0;
        const last = root._rowAt(list.contentY + list.height - hint.bandHeight - 1,
                root._layoutRevision);
        return last < 0 ? root._rawBelow : list.count - 1 - last;
    }

    /// How much of a row counted above or below still shows past a band's inner edge (#8):
    /// the hint stretches the band over it. 0 when no row straddles that edge.
    function _straddle(edge: real, revision: int, below: bool): real {
        const index = root._rowAt(edge, revision);
        if (index < 0)
            return 0;
        const box = root._rowBoxes[index];
        if (box[0] >= edge - 0.5 || box[0] + box[1] <= edge + 0.5)
            return 0;
        return below ? edge - box[0] : box[0] + box[1] - edge;
    }
    readonly property real _topCover: root._rowsAbove === 0 ? 0
            : root._straddle(list.contentY + hint.bandHeight, root._layoutRevision, false)
    readonly property real _bottomCover: root._rowsBelow === 0 ? 0
            : root._straddle(list.contentY + list.height - hint.bandHeight, root._layoutRevision,
                             true)

    // ---- dragging a row (`draggable`) ----------------------------------------------------
    /// Whether row `index` may be picked up.
    function canDrag(index: int): bool {
        return root.draggable && index >= 0 && index < root.items.length
                && root.items[index].pinned !== true;
    }

    /// The slots a drop may use, `[first, last]`: the pinned rows at the list's two ends keep
    /// their places, so the slots stop short of them.
    function _slotRange(): var {
        let first = 0;
        while (first < root.items.length && root.items[first].pinned === true)
            ++first;
        let last = root.items.length;
        while (last > first && root.items[last - 1].pinned === true)
            --last;
        return [first, last];
    }

    /// The slot for content `y`: before the row under it when `y` is in its top half, after
    /// it in the bottom half, clamped to `_slotRange()`.
    function _slotAt(y: real, revision: int): int {
        if (revision < 0)
            return -1;
        let slot = y < 0 ? 0 : root.items.length;
        for (const key in root._rowBoxes) {
            const box = root._rowBoxes[key];
            if (y >= box[0] && y < box[0] + box[1]) {
                slot = Number(key) + (y < box[0] + box[1] / 2 ? 0 : 1);
                break;
            }
        }
        const range = root._slotRange();
        return Math.max(range[0], Math.min(range[1], slot));
    }

    /// The content y of the line drawn for `slot`: the top of the row it goes before, or the
    /// bottom of the last row. -1 while that row is not laid out.
    function _slotY(slot: int, revision: int): real {
        if (slot < 0 || revision < 0)
            return -1;
        if (slot < root.items.length) {
            const box = root._rowBoxes[slot];
            return box === undefined ? -1 : box[0];
        }
        const last = root._rowBoxes[root.items.length - 1];
        return last === undefined ? -1 : last[0] + last[1];
    }

    function _startDrag(index: int, y: real): void {
        root.cursorIndex = index;
        root._dragY = y;
        root._dragFrom = index;
    }

    /// Let go: ask the caller to move the row, unless it would land where it started.
    function _finishDrag(): void {
        const from = root._dragFrom;
        const slot = root.dropSlot;
        root._dragFrom = -1;
        if (from >= 0 && slot >= 0)
            root.dropRequested(from, root.items[from], slot);
    }

    function _cancelDrag(): void {
        root._dragFrom = -1;
    }

    /// The pointer is on row `index` (through its MouseArea `area`): the cursor follows it,
    /// unless only the rows moved under a still pointer. Qt re-delivers hover when the list
    /// scrolls, and the keyboard cursor scrolling into view would hand the cursor straight
    /// back to whatever row came to rest under the pointer.
    /// The first hover after opening only notes where the pointer rests: a palette that opens
    /// under a still pointer gets that hover when its rows first move under it, and it is no
    /// move of the pointer.
    function _pointerOn(index: int, area: MouseArea): void {
        const at = area.mapToItem(root, area.mouseX, area.mouseY);
        const known = root._pointer.x >= 0;
        if (at.x === root._pointer.x && at.y === root._pointer.y)
            return;
        root._pointer = at;
        if (known && !root.dragging && root.canLand(index))
            root.cursorIndex = index;
    }

    /// A right-click on row `index`: the cursor goes there and, where the palette has one,
    /// its secondary action runs, as `tab` would.
    function _secondaryClick(index: int): void {
        if (!root.canLand(index))
            return;
        root.cursorIndex = index;
        if (root.hasSecondary)
            root.secondary(index, root.items[index]);
    }

    /// `enter`: hand the cursor's row to the caller. The caller closes the palette.
    function activate(): void {
        if (root.currentItem)
            root.activated(root.cursorIndex, root.currentItem);
    }

    /// The shared matcher, so a caller filtering for this palette need not import anything.
    function match(text: string, query: string): bool {
        return Fuzzy.match(text, query);
    }

    /// The one key a modal overlay lets through: `ctrl+q`.
    function _passesThrough(event: var): bool {
        return (event.modifiers & Qt.ControlModifier) !== 0 && event.key === Qt.Key_Q;
    }

    function _onKey(event: var): void {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
        const alt = (event.modifiers & Qt.AltModifier) !== 0;
        // `esc` mid-drag drops the drag, and only that: the query and the palette stay.
        if (root.dragging && event.key === Qt.Key_Escape) {
            root._cancelDrag();
            event.accepted = true;
            return;
        }
        if (root.reorderable && alt && !ctrl) {
            let delta = 0;
            if (event.key === Qt.Key_Up || event.key === Qt.Key_K)
                delta = -1;
            else if (event.key === Qt.Key_Down || event.key === Qt.Key_J)
                delta = 1;
            if (delta !== 0) {
                if (root.currentItem)
                    root.moveRequested(root.cursorIndex, root.currentItem, delta);
                event.accepted = true;
                return;
            }
        }
        if (root.reorderable && ctrl && event.key === Qt.Key_R) {
            root.resetRequested();
            event.accepted = true;
            return;
        }
        // Not ctrl+shift+p: that is the command palette's key (docs/30 §2), not "up".
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
        if (ctrl && (event.key === Qt.Key_N || (event.key === Qt.Key_P && !shift))) {
            root.moveCursor(event.key === Qt.Key_N ? 1 : -1);
            event.accepted = true;
            return;
        }
        if (ctrl)
            return;
        switch (event.key) {
        case Qt.Key_Escape:
            if (root.query.length > 0) {
                root.query = "";
                break;
            }
            root.close();
            root.rejected();
            break;
        case Qt.Key_Down:
            root.moveCursor(1);
            break;
        case Qt.Key_Up:
            root.moveCursor(-1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.activate();
            break;
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            if (root.currentItem)
                root.secondary(root.cursorIndex, root.currentItem);
            break;
        case Qt.Key_Space:
            if (!root.multi)
                return;
            if (root.currentItem)
                root.toggled(root.cursorIndex, root.currentItem);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    implicitHeight: panel.implicitHeight
    implicitWidth: root.paletteWidth
    visible: root.inline || root._opened

    Keys.onPressed: event => {
        root._onKey(event);
        if (root.modal && root._opened && !event.accepted)
            event.accepted = !root._passesThrough(event);
    }
    onItemsChanged: root._settleCursor()

    onQueryChanged: {
        root.cursorIndex = 0;
        root._settleCursor();
    }

    // A drag held near the list's top or bottom edge scrolls it, faster the closer it gets;
    // `dropSlot` follows the scroll on its own.
    Timer {
        id: edgeScroll

        readonly property real zone: Theme.space(28)
        readonly property real speed: {
            if (!root.dragging)
                return 0;
            const zone = edgeScroll.zone;
            const top = zone - root._dragY;
            if (top > 0)
                return -Math.ceil(Math.min(top, zone) / zone * Theme.space(10));
            const bottom = root._dragY - (list.height - zone);
            if (bottom > 0)
                return Math.ceil(Math.min(bottom, zone) / zone * Theme.space(10));
            return 0;
        }

        interval: 16
        repeat: true
        running: edgeScroll.speed !== 0

        onTriggered: {
            const first = list.originY;
            const last = list.originY + Math.max(0, list.contentHeight - list.height);
            list.contentY = Math.max(first, Math.min(last, list.contentY + edgeScroll.speed));
        }
    }

    Scrim {
        anchors.fill: parent
        visible: !root.inline

        onDismissed: {
            root.close();
            root.rejected();
        }
    }

    Rectangle {
        id: panel

        border.color: Theme.accent
        border.width: 1
        color: Theme.background
        implicitHeight: layout.implicitHeight
        radius: Theme.cornerRadius
        width: Math.min(root.paletteWidth, root.inline ? root.paletteWidth
                : Math.max(0, root.width - 2 * root._margin))
        x: root.inline ? 0 : Math.round((root.width - width) / 2)
        y: root.inline ? 0 : Math.round((root.height - height) / 2)

        ColumnLayout {
            id: layout

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0

            // the query row (`.palette .q`) -----------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight + Theme.space(8)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space(14)
                    anchors.rightMargin: Theme.space(14)
                    spacing: Theme.space(10)

                    Glyph {
                        color: Theme.dimForeground
                        text: root.glyph
                        visible: root.glyph.length > 0
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        TextInput {
                            id: input

                            Accessible.name: root.title
                            Accessible.role: Accessible.EditableText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            clip: true
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.body
                            selectByMouse: true
                            selectionColor: Theme.selection

                            cursorDelegate: Rectangle {
                                color: Theme.accent
                                width: 1
                            }

                            Keys.onPressed: event => root._onKey(event)
                        }

                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            dim: true
                            elide: Text.ElideRight
                            text: root.title
                            visible: root.title.length > 0 && input.text.length === 0
                        }
                    }

                    Label {
                        dim: true
                        size: "caption"
                        text: root.counter
                        visible: root.counter.length > 0
                    }
                }

                Hairline {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                }

                // A press anywhere on the row puts the caret in the query; a right-click
                // opens cut / copy / paste / select all, as in a Field. Last, at z -1: under
                // the input, and `children[0]` is still the row's layout.
                InputMouseArea {
                    anchors.fill: parent
                    input: input
                    objectName: "paletteQueryMouse"
                    z: -1
                }
            }

            // docs/30 P4: why the list is empty (`.pal .empty`) ----------------------------
            Label {
                Layout.bottomMargin: Theme.space(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space(16)
                Layout.rightMargin: Theme.space(16)
                Layout.topMargin: Theme.space(14)
                dim: true
                elide: Text.ElideRight
                objectName: "paletteEmptyText"
                size: "small"
                text: root.emptyText
                visible: root.emptyText.length > 0
            }

            // the rows (`.palette .it`) ---------------------------------------------
            ListView {
                id: list

                Accessible.role: Accessible.List
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Theme.space(360))
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                model: root.items

                delegate: Rectangle {
                    id: row

                    required property int index
                    required property var modelData

                    readonly property bool current: row.index === root.cursorIndex
                    /// docs/30 D4: this row can't run now.
                    readonly property bool disabled: row.modelData.disabled === true
                    /// The faint step (the board's 40 %), for a dim row's meta and second line
                    /// and the "via" before a path.
                    readonly property color faint: Theme.fill(Theme.borderIdle)
                    /// This row is the one being dragged: it keeps the selected fill until
                    /// it lands.
                    readonly property bool lifted: row.index === root._dragFrom

                    Accessible.name: row.modelData.title ?? ""
                    Accessible.role: Accessible.ListItem
                    Accessible.selected: row.current
                    color: row.lifted ? Theme.fill(Theme.fillSelected) : row.current
                            ? Theme.fill(Theme.fillHover) : "transparent"
                    height: rowColumn.implicitHeight + 2 * Theme.space(6)
                    width: ListView.view.width

                    // `indexAt` notifies nothing, so a row that comes, goes, moves or
                    // resizes asks the scroll-hint counts to look again.
                    Component.onCompleted: root._setRowBox(row.index, row, false)
                    Component.onDestruction: root._setRowBox(row.index, row, true)
                    onHeightChanged: root._setRowBox(row.index, row, false)
                    onYChanged: root._setRowBox(row.index, row, false)

                    Rectangle {
                        anchors.fill: parent
                        border.color: Theme.fill(Theme.borderHover)
                        border.width: 1
                        color: "transparent"
                        visible: row.current
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        color: root._rowRule
                        height: 1
                    }

                    RowLayout {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(14)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(14)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space(8)

                        Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: Theme.space(14)
                            bright: row.modelData.checked === true
                            dim: row.modelData.checked !== true
                            horizontalAlignment: Text.AlignHCenter
                            text: row.modelData.checked === true ? Glyphs.check : Glyphs.middot
                            visible: root.multi
                        }

                        ColumnLayout {
                            id: rowColumn

                            Layout.fillWidth: true
                            spacing: Theme.space(1)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space(8)

                                Label {
                                    Layout.fillWidth: true
                                    bright: !row.disabled
                                    dim: row.disabled
                                    elide: Text.ElideRight
                                    font.weight: row.disabled ? Font.Normal : Font.Medium
                                    objectName: "paletteRowTitle"
                                    text: row.modelData.title ?? ""
                                }

                                Label {
                                    color: row.faint
                                    objectName: "paletteRowVia"
                                    size: "small"
                                    text: qsTr("via")
                                    visible: (row.modelData.via ?? "").length > 0
                                }

                                Label {
                                    color: row.disabled ? row.faint : Theme.dimForeground
                                    objectName: "paletteRowMeta"
                                    size: "small"
                                    text: (row.modelData.via ?? "").length > 0 ? row.modelData.via
                                            : (row.modelData.meta ?? "")
                                    visible: text.length > 0
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                color: row.disabled ? row.faint : Theme.dimForeground
                                elide: Text.ElideRight
                                objectName: "paletteRowDetail"
                                size: "small"
                                text: row.modelData.detail !== undefined ? row.modelData.detail : ""
                                visible: text.length > 0
                            }
                        }
                    }

                    MouseArea {
                        id: rowArea

                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        anchors.fill: parent
                        cursorShape: row.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                        hoverEnabled: true
                        objectName: "paletteRow_" + row.index

                        onEntered: root._pointerOn(row.index, rowArea)
                        onPositionChanged: root._pointerOn(row.index, rowArea)
                        // In a multi-select a click is `space` on that row, not `enter`:
                        // `enter` applies the whole selection (the tags picker), so a
                        // click that activated could never pick a tag (KIT-4).
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                root._secondaryClick(row.index);
                                return;
                            }
                            if (row.disabled)
                                return;
                            root.cursorIndex = row.index;
                            if (root.multi)
                                root.toggled(row.index, row.modelData);
                            else
                                root.activate();
                        }
                    }
                }
            }

            // the hint line (`.palette .ft`) ----------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: footerHints.implicitHeight + 2 * Theme.space(6)
                visible: root.hints.length > 0 || root.footerRight.length > 0

                KeyHints {
                    id: footerHints

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(14)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space(14)
                    anchors.verticalCenter: parent.verticalCenter
                    model: root.hints
                    objectName: "paletteHints"
                    pinned: root.footerRight.length > 0 ? {
                        "key": "",
                        "label": root.footerRight
                    } : null
                    size: "caption"

                    onHintClicked: (index, hint) => root.hintClicked(index, hint)
                }
            }
        }

        // M13: a `draggable` list takes its presses here, over the rows (under the scroll
        // hint's bands, which page on a click), so the drag keeps its grab even when the row
        // it started on scrolls out and its delegate goes. A press that never travels the
        // start-drag distance is the row's click, as the delegate's own MouseArea has it.
        // Wheel events are not taken, so they still reach the list.
        MouseArea {
            id: dragArea

            property real pressY: 0
            property int pressRow: -1
            property bool dragged: false

            cursorShape: root.dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
            enabled: root.draggable
            height: list.height
            objectName: "paletteDragArea"
            preventStealing: true
            width: list.width
            x: list.x
            y: layout.y + list.y

            onCanceled: root._cancelDrag()
            onClicked: {
                const index = dragArea.pressRow;
                if (dragArea.dragged || !root.canLand(index))
                    return;
                root.cursorIndex = index;
                if (root.multi)
                    root.toggled(index, root.items[index]);
                else
                    root.activate();
            }
            onPositionChanged: mouse => {
                if (dragArea.dragged) {
                    root._dragY = mouse.y;
                    return;
                }
                if (!root.canDrag(dragArea.pressRow)
                        || Math.abs(mouse.y - dragArea.pressY)
                        < Application.styleHints.startDragDistance)
                    return;
                dragArea.dragged = true;
                root._startDrag(dragArea.pressRow, mouse.y);
            }
            onPressed: mouse => {
                dragArea.pressY = mouse.y;
                dragArea.pressRow = root._rowAt(list.contentY + mouse.y, root._layoutRevision);
                dragArea.dragged = false;
            }
            onReleased: {
                if (root.dragging)
                    root._finishDrag();
            }
        }

        // Where the dragged row lands: a 1 px accent line on the rule between two rows.
        Rectangle {
            id: dropIndicator

            readonly property real at: root._slotY(root.dropSlot, root._layoutRevision)
                    - list.contentY

            color: Theme.accent
            height: 1
            objectName: "dropIndicator"
            visible: root.dropSlot >= 0 && dropIndicator.at >= 0
                     && dropIndicator.at <= list.height
            width: list.width - 2 * panel.border.width
            x: list.x + panel.border.width
            y: layout.y + list.y + Math.min(list.height - 1,
                    Math.max(0, Math.round(dropIndicator.at) - 1))
        }

        // N3: more rows above or below, in the look the scroll-hint setting picks. Inset
        // by the panel's 1 px edge, so a band never covers the accent border.
        ScrollHint {
            id: hint

            bottomCover: root._bottomCover
            countAbove: root._rowsAbove
            countBelow: root._rowsBelow
            flickable: list
            height: list.height
            objectName: "scrollHint"
            revealed: root._opened
            style: root.scrollHint
            topCover: root._topCover
            width: list.width - 2 * panel.border.width
            x: list.x + panel.border.width
            y: layout.y + list.y
        }

        // The panel's own ground (its padding, the gaps around the field and the list) takes
        // the clicks nothing on it wanted. Without it they fell through to the Scrim
        // underneath, and a click inside the palette closed it.
        MouseArea {
            // Last, at z -1: under the rows, and `children[0]` is still the layout.
            acceptedButtons: Qt.AllButtons
            anchors.fill: parent
            z: -1

            onWheel: wheel => wheel.accepted = true
        }
    }
}
