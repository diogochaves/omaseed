pragma ComponentBehavior: Bound

import QtQml.Models
import QtQuick
import Omaseed
import Omaseed.Kit

// The torrent table (spec v2 §1 Fig. 1, `.tbl`): a header row of `TableHeader` cells over a
// virtualised `ListView` of `TableRow` bands.
//
// Columns. `columns` is the ordered record of what the table can show — key, title, the
// Torrent role it sorts by, the base-12 width in pixels (0 = the flexible Name column), the
// alignment and the order a first click on it uses. `visibleColumns` is the live set T4.4's
// columns palette and `state.json` drive; `columnLayout` turns the two into one
// `{key: {x, w}}` map, so a delegate does two cheap lookups per cell instead of filtering the
// column list once per row. When the columns chosen don't fit beside a Name of at least
// `minimumNameWidth`, the rows grow wider than the view and it scrolls sideways under a
// `ScrollBar` (docs/10 §4, Diogo's ruling: columns never step aside on their own). The widths
// are the spec's `grid-template-columns: minmax(0,1fr)
// 72px 118px 104px 62px 84px 84px 62px` put through `Theme.space()`, so base 14 scales them
// the way the spec's `[data-base="14"]` rule does.
//
// Cursor and selection are two different things, as in the figure. `cursorIndex` is the
// outlined band (`.tbl .r.cur`), the row every key acts on; the `ItemSelectionModel` is the
// filled band (`.tbl .r.sel`); a row that is both gets `TableRow`'s single 24 % fill
// (`.tbl .r.sel.cur`). The cursor is kept on its torrent by hash across a re-sort or a
// removal, and clamps when that hash is gone.
//
// This view never talks to the network. `r` opens the inline editor and emits
// `renameRequested`; `enter` and right-click emit `actionsRequested`; phase 4 wires both to
// `QbtClient`.
Item {
    id: root

    // ---- data -------------------------------------------------------------------------
    required property TorrentFilterModel filtered

    // ---- columns ----------------------------------------------------------------------
    /// The ordered column record. `width` is in spec pixels at base 12 (0 = flexible) and
    /// goes through `Theme.space()` at use; `role` is the Torrent role name the header sorts
    /// by; `order` is the order a first click on that header applies.
    ///
    /// `name`, where given, is what the columns palette lists instead of a terse `title`.
    ///
    /// The first eight are Fig. 1's and drawn by cells of their own. The rest are optional
    /// (docs/10 §4, "like qui"): off until the columns palette turns them on (`default:
    /// false`), each one plain text read from its `text` role.
    property var columns: [
        {
            "key": "name",
            "title": qsTr("Name"),
            "role": "name",
            "width": 0,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true
        },
        {
            "key": "size",
            "title": qsTr("Size"),
            "role": "size",
            "width": 72,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        {
            "key": "progress",
            "title": qsTr("Progress"),
            "role": "progress",
            "width": 118,
            "align": Qt.AlignLeft,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        {
            "key": "status",
            "title": qsTr("Status"),
            "role": "stateText",
            // The spec's grid says 104, which cannot hold "downloading" next to its glyph in
            // the mono font at any base size; the orchestrator widened it to 116 (taken from
            // the flexible Name column) rather than elide the most-read word in the table.
            // Diogo's call to revert: docs/QUESTIONS.md Q17.
            "width": 116,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true
        },
        {
            "key": "seedsPeers",
            "name": qsTr("S / P  seeds / peers"),
            "title": qsTr("S / P"),
            "role": "numSeeds",
            "width": 62,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        {
            "key": "dlspeed",
            "name": qsTr("↓  download speed"),
            "title": Glyphs.arrowDown,
            "role": "dlspeed",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        {
            "key": "upspeed",
            "name": qsTr("↑  upload speed"),
            "title": Glyphs.arrowUp,
            "role": "upspeed",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        {
            "key": "eta",
            "title": qsTr("ETA"),
            "role": "eta",
            "width": 62,
            "align": Qt.AlignRight,
            "order": Qt.AscendingOrder,
            "visible": true
        },
        {
            "key": "addedOn",
            "title": qsTr("Added"),
            "role": "addedOn",
            "text": "addedOnShortText",
            "width": 112,
            "align": Qt.AlignLeft,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "completionOn",
            "title": qsTr("Completed"),
            "role": "completionOn",
            "text": "completionOnText",
            "width": 112,
            "align": Qt.AlignLeft,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "lastActivity",
            "title": qsTr("Last active"),
            "role": "lastActivity",
            "text": "lastActivityText",
            "width": 112,
            "align": Qt.AlignLeft,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "ratio",
            "title": qsTr("Ratio"),
            "role": "ratio",
            "text": "ratioText",
            "width": 60,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "category",
            "title": qsTr("Category"),
            "role": "category",
            "text": "category",
            "width": 96,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "tags",
            "title": qsTr("Tags"),
            "role": "tagsText",
            "text": "tagsText",
            "width": 120,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "tracker",
            "title": qsTr("Tracker"),
            "role": "trackerHost",
            "text": "trackerHost",
            "width": 150,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "seeds",
            "title": qsTr("Seeds"),
            "role": "numSeeds",
            "text": "seedsText",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "peers",
            "title": qsTr("Peers"),
            "role": "numLeechs",
            "text": "peersText",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "totalSize",
            "title": qsTr("Total size"),
            "role": "totalSize",
            "text": "totalSizeText",
            "width": 86,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "downloaded",
            "title": qsTr("Downloaded"),
            "role": "downloaded",
            "text": "downloadedText",
            "width": 90,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "uploaded",
            "title": qsTr("Uploaded"),
            "role": "uploaded",
            "text": "uploadedText",
            "width": 90,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "remaining",
            "title": qsTr("Remaining"),
            "role": "amountLeft",
            "text": "amountLeftText",
            "width": 90,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "availability",
            "title": qsTr("Availability"),
            "role": "availability",
            "text": "availabilityText",
            "width": 100,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "dlLimit",
            "name": qsTr("↓ limit  download limit"),
            "title": Glyphs.arrowDown + " " + qsTr("limit"),
            "role": "dlLimit",
            "text": "dlLimitText",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "upLimit",
            "name": qsTr("↑ limit  upload limit"),
            "title": Glyphs.arrowUp + " " + qsTr("limit"),
            "role": "upLimit",
            "text": "upLimitText",
            "width": 84,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "queue",
            "title": qsTr("Queue"),
            "role": "priority",
            "text": "priorityText",
            "width": 60,
            "align": Qt.AlignRight,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "timeActive",
            "title": qsTr("Time active"),
            "role": "timeActive",
            "text": "timeActiveText",
            "width": 104,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "seedingTime",
            "title": qsTr("Seeding time"),
            "role": "seedingTime",
            "text": "seedingTimeText",
            "width": 110,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "savePath",
            "title": qsTr("Save path"),
            "role": "savePath",
            "text": "savePath",
            "width": 220,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        },
        {
            "key": "hash",
            "title": qsTr("Hash"),
            "role": "infohashV1",
            "text": "infohashV1",
            "width": 130,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true,
            "default": false
        }
    ]

    /// The keys that are on screen. T4.4 writes this from the columns palette and from
    /// `state.json`. It says which, not where: `columnOrder` places them.
    property list<string> visibleColumns: root.columns.filter(column => column.default !== false).map(
                                              column => column.key)

    /// Every column key, shown or not, in the order the table draws them (N2). The columns
    /// palette moves keys in it (`alt+↑`/`alt+↓`), a header cell can be dragged along the
    /// header, and `state.json` keeps it per server. Name is pinned first: it is the one
    /// flexible column, and with the table scrolled back to its left edge it keeps every
    /// row's identity on screen however many columns are chosen. Write it through
    /// `setColumnOrder()` (or `moveColumn()`/`placeColumn()`), which repair any list.
    property list<string> columnOrder: root.defaultColumnOrder
    /// The order `columns` declares: Fig. 1's eight, then the optional ones.
    readonly property list<string> defaultColumnOrder: root.columns.map(column => column.key)
    /// False once `columnOrder` differs from `defaultColumnOrder`.
    readonly property bool columnOrderIsDefault: root.columnOrder.join(",")
                                                 === root.defaultColumnOrder.join(",")

    /// The smallest the flexible Name column is allowed to get, in spec pixels.
    property int minimumNameWidth: 160

    // ---- cursor, selection, rename ------------------------------------------------------
    property int cursorIndex: 0
    readonly property string cursorHash: internal.cursorHash
    readonly property alias selectedCount: internal.selectedCount
    readonly property bool renaming: internal.renameIndex >= 0
    /// The row being renamed, or -1. Main compares it with the cursor to decide whether the
    /// footer should teach `enter apply · esc cancel` instead of the table's usual line.
    readonly property alias renameIndex: internal.renameIndex

    /// The live text of the inline editor, so a caller (and the test) can read or replace
    /// what `commitRename()` will emit.
    property string renameText: ""

    readonly property string sortRoleName: root.filtered.sortRoleName

    /// The sort the table starts on: the spec's Fig. 1 header marks `↓` as the sorted
    /// column, descending. Write these before the table completes to restore a saved sort.
    property string initialSortRoleName: "dlspeed"
    property int initialSortOrder: Qt.DescendingOrder

    /// The filled band of `.tbl .r.sel`, exposed so the details pane can follow it.
    readonly property ItemSelectionModel selection: ItemSelectionModel {
        model: root.filtered
    }

    signal actionsRequested(string hash, real x, real y)
    signal renameRequested(string hash, string name)
    signal activated(string hash)
    signal focusRail
    signal focusDetails
    signal escapePressed

    // ---- derived -------------------------------------------------------------------------
    /// `columns` in `columnOrder`.
    readonly property var orderedColumns: {
        const byKey = {};
        for (const column of root.columns)
            byKey[column.key] = column;
        return root.columnOrder.map(key => byKey[key]).filter(column => column !== undefined);
    }
    readonly property var activeColumns: root.orderedColumns.filter(column => {
        return column.visible && root.visibleColumns.indexOf(column.key) >= 0;
    })
    /// The active columns that are plain text, drawn by one generic cell each.
    readonly property var textColumns: root.activeColumns.filter(column => column.text !== undefined)

    /// How wide the rows are: the view's width, or more when the chosen columns need it.
    readonly property int rowWidth: {
        let fixed = 0;
        for (const column of root.activeColumns) {
            if (column.width > 0)
                fixed += Theme.space(column.width);
        }
        return Math.max(root.width, fixed + Theme.space(root.minimumNameWidth));
    }

    /// `{key: {"x": px, "w": px}}` for every key in `columns`; a hidden column has `w` 0.
    readonly property var columnLayout: {
        const layout = {};
        for (const column of root.columns)
            layout[column.key] = {
                "x": 0,
                "w": 0
            };
        const shown = root.activeColumns;
        let fixed = 0;
        for (const column of shown) {
            if (column.width > 0)
                fixed += Theme.space(column.width);
        }
        const flexible = root.rowWidth - fixed;
        let x = 0;
        for (const column of shown) {
            const w = column.width > 0 ? Theme.space(column.width) : flexible;
            layout[column.key] = {
                "x": x,
                "w": w
            };
            x += w;
        }
        return layout;
    }

    /// The status glyph per `stateText`, built once instead of per row (spec §1 `ST`).
    readonly property var statusGlyphs: {
        return {
            "downloading": Glyphs.statusDownloading,
            "metadata": Glyphs.statusDownloading,
            "seeding": Glyphs.statusSeeding,
            "completed": Glyphs.statusCompleted,
            "stopped": Glyphs.statusStopped,
            "stalled": Glyphs.statusStalled,
            "checking": Glyphs.statusChecking,
            "moving": Glyphs.statusChecking,
            "queued": Glyphs.statusQueued,
            "error": Glyphs.statusErrored
        };
    }

    implicitHeight: Theme.rowHeight * 8
    implicitWidth: Theme.space(900)

    // ---- public functions ----------------------------------------------------------------
    /// Sort by a Torrent role name; the same role twice reverses the order.
    function sortBy(roleName: string): void {
        if (roleName.length === 0)
            return;
        if (root.filtered.sortRoleName === roleName) {
            const up = root.filtered.sortOrder === Qt.AscendingOrder;
            root.filtered.sortOrder = up ? Qt.DescendingOrder : Qt.AscendingOrder;
            return;
        }
        let order = Qt.AscendingOrder;
        for (const column of root.columns) {
            if (column.role === roleName)
                order = column.order;
        }
        root.filtered.setSortByRoleName(roleName);
        root.filtered.sortOrder = order;
    }

    function selectedHashes(): var {
        const rows = root.selection.selectedIndexes;
        const hashes = [];
        for (const index of rows)
            hashes.push(root.filtered.hashAt(index.row));
        return hashes;
    }

    // ---- column order (N2) -----------------------------------------------------------------
    /// `keys` made into a whole order: unknown and repeated keys dropped, a key it lacks (a
    /// column added in a later version, say) put back after the column it follows by default,
    /// and Name first.
    function normalizedColumnOrder(keys: var): var {
        const known = root.defaultColumnOrder;
        const order = [];
        for (const key of (keys === undefined || keys === null ? [] : keys)) {
            const text = String(key);
            if (known.indexOf(text) >= 0 && order.indexOf(text) < 0 && text !== "name")
                order.push(text);
        }
        order.unshift("name");
        for (let i = 0; i < known.length; ++i) {
            if (order.indexOf(known[i]) >= 0)
                continue;
            const after = i > 0 ? order.indexOf(known[i - 1]) : -1;
            order.splice(after + 1, 0, known[i]);
        }
        return order;
    }

    function setColumnOrder(keys: var): void {
        const order = root.normalizedColumnOrder(keys);
        if (order.join(",") !== root.columnOrder.join(","))
            root.columnOrder = order;
    }

    function resetColumnOrder(): void {
        root.setColumnOrder(root.defaultColumnOrder);
    }

    /// Whether `key` may move at all: every column but the pinned Name.
    function canMoveColumn(key: string): bool {
        return key !== "name" && root.columnOrder.indexOf(key) > 0;
    }

    /// Put `key` right before (`after` false) or right after `target`. Returns whether the
    /// order changed. Name stays first whatever is asked.
    function placeColumn(key: string, target: string, after: bool): bool {
        if (!root.canMoveColumn(key) || key === target || root.columnOrder.indexOf(target) < 0)
            return false;
        const order = root.columnOrder.filter(other => other !== key);
        const at = order.indexOf(target) + (after ? 1 : 0);
        order.splice(Math.max(1, at), 0, key);
        const before = root.columnOrder.join(",");
        root.setColumnOrder(order);
        return root.columnOrder.join(",") !== before;
    }

    /// Swap `key` with its neighbour `delta` (−1 up/left, +1 down/right) places away in
    /// `columnOrder`, or, given `among`, with its neighbour in that list instead (the rows a
    /// filtered palette shows, or the columns on screen). Returns whether it moved.
    function moveColumn(key: string, delta: int, among: var): bool {
        const list = among !== undefined && among !== null ? among : root.columnOrder;
        const at = list.indexOf(key);
        const to = at + delta;
        if (at < 0 || delta === 0 || to < 0 || to >= list.length)
            return false;
        return root.placeColumn(key, list[to], delta > 0);
    }

    /// `ctrl+h` / `ctrl+l` and shift+wheel: scroll the rows sideways by `step` pixels.
    function scrollSideways(step: real): void {
        const far = Math.max(0, list.contentWidth - list.width);
        list.contentX = Math.max(0, Math.min(far, list.contentX + step));
    }

    function moveCursor(delta: int): void {
        root.moveCursorTo(root.cursorIndex + delta);
    }

    function moveCursorTo(index: int): void {
        const last = root.filtered.count - 1;
        const wanted = last < 0 ? 0 : Math.max(0, Math.min(index, last));
        root.cursorIndex = wanted;
        list.positionViewAtIndex(wanted, ListView.Contain);
    }

    function selectAll(): void {
        const count = root.filtered.count;
        for (let row = 0; row < count; ++row)
            internal.selectRow(row, ItemSelectionModel.Select);
        internal.refreshSelection();
    }

    /// The hover label for an elided name, in this item's coordinates. Rows call these
    /// rather than reaching into the panel, so the table owns when it is up.
    function showNameTip(x: int, y: int, text: string): void {
        nameTip.showAt(x, y, text);
    }

    function hideNameTip(): void {
        nameTip.hide();
    }

    function clearSelection(): void {
        root.selection.clearSelection();
        internal.anchorIndex = -1;
        internal.refreshSelection();
    }

    function toggleCursorSelection(): void {
        root.toggleSelection(root.cursorIndex);
    }

    /// `v`: select everything between the last toggled row and the cursor.
    function selectRangeToCursor(): void {
        const anchor = internal.anchorIndex < 0 ? root.cursorIndex : internal.anchorIndex;
        const from = Math.min(anchor, root.cursorIndex);
        const to = Math.max(anchor, root.cursorIndex);
        for (let row = from; row <= to; ++row)
            internal.selectRow(row, ItemSelectionModel.Select);
        internal.refreshSelection();
    }

    /// Select only this row: the plain left click.
    function selectOnly(index: int): void {
        internal.selectRow(index, ItemSelectionModel.ClearAndSelect);
        internal.anchorIndex = index;
        internal.refreshSelection();
    }

    function toggleSelection(index: int): void {
        internal.selectRow(index, ItemSelectionModel.Toggle);
        internal.anchorIndex = index;
        internal.refreshSelection();
    }

    function selectRangeTo(index: int): void {
        root.cursorIndex = index;
        root.selectRangeToCursor();
    }

    function isRowSelected(index: int): bool {
        // `internal.revision` is what makes this a live binding: the selection model's own
        // signal fires once per selected row, which would otherwise re-run every delegate
        // binding thousands of times during `ctrl+a`.
        if (internal.revision < 0)
            return false;
        return root.selection.isRowSelected(index);
    }

    /// `r`: open the inline editor on the cursor row with the whole name selected.
    function startRename(): void {
        if (root.filtered.count <= 0)
            return;
        // The text first: writing `renameIndex` is what loads the editor, and the editor
        // selects whatever `renameText` holds at that moment.
        const at = root.filtered.index(root.cursorIndex, 0);
        root.renameText = root.filtered.data(at, TorrentModel.NameRole);
        internal.renameHash = root.filtered.hashAt(root.cursorIndex);
        internal.renameIndex = root.cursorIndex;
    }

    function commitRename(): void {
        if (!root.renaming)
            return;
        const hash = internal.renameHash;
        const name = root.renameText;
        internal.closeRename();
        if (name.length > 0)
            root.renameRequested(hash, name);
    }

    function cancelRename(): void {
        internal.closeRename();
    }

    /// `enter` and right-click: ask for the actions menu, anchored on the cursor row.
    function requestActions(): void {
        if (root.cursorHash.length === 0)
            return;
        const top = header.height + (root.cursorIndex * Theme.rowHeight);
        const y = top - list.contentY + Theme.rowHeight;
        root.actionsRequested(root.cursorHash, Theme.space(24), y);
    }

    onCursorIndexChanged: internal.cursorHash = root.filtered.hashAt(root.cursorIndex)

    // `setSortByRoleName` rather than `sortBy()`: the role may already be the initial one (a
    // restored `state.json`, or a second table over the same proxy), and `sortBy()` would
    // read that as "the same header again" and reverse it. The cursor then goes to the first
    // row of the sort that results, not to wherever the two re-sorts dragged it.
    Component.onCompleted: {
        root.filtered.setSortByRoleName(root.initialSortRoleName);
        root.filtered.sortOrder = root.initialSortOrder;
        root.cursorIndex = 0;
        internal.cursorHash = root.filtered.hashAt(0);
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_J:
            root.moveCursor(1);
            break;
        case Qt.Key_K:
            // `ctrl+k` is the jump palette (T4.4); plain `k` moves the cursor up.
            if (event.modifiers & Qt.ControlModifier)
                return;
            root.moveCursor(-1);
            break;
        case Qt.Key_G:
            if (event.modifiers & Qt.ShiftModifier)
                root.moveCursorTo(root.filtered.count - 1);
            else
                root.moveCursorTo(0);
            break;
        case Qt.Key_Space:
            root.toggleCursorSelection();
            break;
        case Qt.Key_V:
            root.selectRangeToCursor();
            break;
        case Qt.Key_A:
            if (!(event.modifiers & Qt.ControlModifier))
                return;
            root.selectAll();
            break;
        case Qt.Key_Escape:
            if (root.selectedCount > 0)
                root.clearSelection();
            else
                root.escapePressed();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.requestActions();
            break;
        case Qt.Key_R:
            if (event.modifiers & Qt.ShiftModifier)
                return;
            root.startRename();
            break;
        case Qt.Key_H:
            if (event.modifiers & Qt.ControlModifier)
                root.scrollSideways(-Theme.space(120));
            else
                root.focusRail();
            break;
        case Qt.Key_L:
            if (event.modifiers & Qt.ControlModifier) {
                root.scrollSideways(Theme.space(120));
                break;
            }
            // Plain `l` hands the focus right; `L` is the limits action and belongs to Main.
            if (event.modifiers & Qt.ShiftModifier)
                return;
            root.focusDetails();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // ---- internals ---------------------------------------------------------------------
    QtObject {
        id: internal

        property int selectedCount: 0
        property int revision: 0
        property int anchorIndex: -1
        property int renameIndex: -1
        property string renameHash: ""
        property string cursorHash: ""

        // A header cell being dragged (N2): its key, and the pointer's x in the header.
        property string dragKey: ""
        property real dragX: 0
        /// `{key, after, x}`: the column the drag would land beside, or null while nothing
        /// is dragged or the drop would change nothing.
        readonly property var dropTarget: {
            if (internal.dragKey.length === 0)
                return null;
            const shown = root.activeColumns;
            for (let i = 0; i < shown.length; ++i) {
                const box = root.columnLayout[shown[i].key];
                if (internal.dragX >= box.x + box.w && i < shown.length - 1)
                    continue;
                const key = shown[i].key;
                const after = key === "name" || internal.dragX >= box.x + box.w / 2;
                // Onto itself, or onto the side of a neighbour it already sits on.
                const neighbour = shown[i + (after ? 1 : -1)];
                if (key === internal.dragKey || (neighbour !== undefined && neighbour.key
                                                 === internal.dragKey))
                    return null;
                return {
                    "key": key,
                    "after": after,
                    "x": after ? box.x + box.w : box.x
                };
            }
            return null;
        }

        function dropColumn(): void {
            const target = internal.dropTarget;
            if (target !== null)
                root.placeColumn(internal.dragKey, target.key, target.after);
            internal.dragKey = "";
        }

        function selectRow(row: int, command: int): void {
            root.selection.select(root.filtered.index(row, 0), command);
        }

        function refreshSelection(): void {
            internal.selectedCount = root.selection.selectedIndexes.length;
            internal.revision = internal.revision + 1;
        }

        function closeRename(): void {
            internal.renameIndex = -1;
            internal.renameHash = "";
            root.renameText = "";
            root.forceActiveFocus();
        }

        /// A re-sort, a filter change or a removal moves rows under the cursor: follow the
        /// hash while it is still in the view, clamp when it is gone.
        function restoreCursor(): void {
            if (internal.cursorHash.length === 0) {
                // Nothing to follow yet: the first rows of a sync, or a table built before
                // its model was filled.
                internal.cursorHash = root.filtered.hashAt(root.cursorIndex);
                internal.refreshSelection();
                return;
            }
            const row = root.filtered.rowOfHash(internal.cursorHash);
            if (row >= 0) {
                root.cursorIndex = row;
            } else {
                const last = root.filtered.count - 1;
                if (last < 0)
                    root.cursorIndex = 0;
                else
                    root.cursorIndex = Math.max(0, Math.min(root.cursorIndex, last));
                internal.cursorHash = root.filtered.hashAt(root.cursorIndex);
            }
            if (internal.renameIndex >= 0) {
                const renamed = root.filtered.rowOfHash(internal.renameHash);
                if (renamed < 0)
                    internal.closeRename();
                else
                    internal.renameIndex = renamed;
            }
            internal.refreshSelection();
        }
    }

    Connections {
        function onLayoutChanged(): void {
            internal.restoreCursor();
        }

        function onModelReset(): void {
            internal.restoreCursor();
        }

        function onRowsInserted(): void {
            internal.restoreCursor();
        }

        function onRowsRemoved(): void {
            internal.restoreCursor();
        }

        target: root.filtered
    }

    // ---- header ----------------------------------------------------------------------------
    // The header scrolls sideways with the rows: it is the rows' width, shifted by their
    // `contentX` inside a clipped strip.
    Item {
        id: headerStrip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clip: true
        height: header.height
    }

    Item {
        id: header

        height: Theme.font.body * 2
        parent: headerStrip
        width: root.rowWidth
        x: -list.contentX

        Repeater {
            model: root.activeColumns

            delegate: TableHeader {
                id: headerCell

                required property var modelData

                readonly property var box: root.columnLayout[headerCell.modelData.key]

                alignRight: headerCell.modelData.align === Qt.AlignRight
                descending: root.filtered.sortOrder === Qt.DescendingOrder
                objectName: "header_" + headerCell.modelData.key
                sorted: root.sortRoleName === headerCell.modelData.role
                text: headerCell.modelData.title
                width: headerCell.box.w
                x: headerCell.box.x

                onClicked: root.sortBy(headerCell.modelData.role)

                // N2, qui-style: drag a header cell along the header to move its column.
                // Past the drag threshold the handler takes the press from the cell's own
                // MouseArea, so a drag never sorts.
                DragHandler {
                    id: headerDrag

                    cursorShape: Qt.ClosedHandCursor
                    enabled: root.canMoveColumn(headerCell.modelData.key)
                    target: null
                    yAxis.enabled: false

                    onActiveChanged: {
                        if (headerDrag.active) {
                            internal.dragX = headerCell.x + headerDrag.centroid.position.x;
                            internal.dragKey = headerCell.modelData.key;
                        } else {
                            internal.dropColumn();
                        }
                    }
                    onCentroidChanged: {
                        if (headerDrag.active)
                            internal.dragX = headerCell.x + headerDrag.centroid.position.x;
                    }
                }

                // The column being dragged.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.fill(Theme.fillSelected)
                    visible: internal.dragKey === headerCell.modelData.key
                }
            }
        }

        // Where the dragged column would land.
        Rectangle {
            color: Theme.accent
            height: parent.height
            objectName: "dropMarker"
            visible: internal.dropTarget !== null
            width: 2
            x: internal.dropTarget !== null ? Math.max(0, Math.min(parent.width - width,
                                                                   internal.dropTarget.x - 1)) : 0
        }
    }

    // ---- rows ------------------------------------------------------------------------------
    ListView {
        id: list

        anchors.bottom: sideways.visible ? sideways.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerStrip.bottom
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: root.rowWidth
        flickableDirection: Flickable.AutoFlickDirection
        cacheBuffer: Theme.rowHeight * 6
        clip: true
        keyNavigationEnabled: false
        model: root.filtered
        objectName: "rows"
        reuseItems: true

        delegate: Item {
            id: row

            required property string category
            required property string dlspeedText
            /// The row's whole model entry, for the optional columns' `text` roles.
            required property var model
            required property string etaText
            required property string hash
            required property int index
            required property string name
            required property real progress
            required property string progressText
            required property string seedsPeersText
            required property string sizeText
            required property string stateRole
            required property string stateText
            required property string upspeedText

            readonly property bool editing: root.renaming && row.index === internal.renameIndex
            readonly property bool isCursor: row.index === root.cursorIndex
            readonly property bool isSelected: root.isRowSelected(row.index)
            readonly property color stateColor: Theme[row.stateRole]
            readonly property string statusGlyph: {
                const glyph = root.statusGlyphs[row.stateText];
                return glyph !== undefined ? glyph : Glyphs.dot;
            }

            height: Theme.rowHeight
            objectName: "row_" + row.index
            width: root.rowWidth

            TableRow {
                id: band

                anchors.fill: parent
                hasCursor: row.isCursor
                selected: row.isSelected

                // The hover fill of `.tbl .r:hover`. `TableRow` carries the cursor and
                // selection bands only, and its own MouseArea never sees these events (the
                // delegate's one is on top), so the hover state lives here.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.fill(Theme.fillHover)
                    visible: pointer.containsMouse && !row.isSelected && !row.isCursor
                }

                // Name + dimmed category (`.tbl > div.nm`): the name elides, the category
                // keeps its width and sits right after it.
                Item {
                    id: nameCell

                    readonly property int inner: nameCell.width - 2 * Theme.space(8)
                    readonly property int tail: {
                        if (!categoryText.visible)
                            return 0;
                        return categoryText.implicitWidth + Theme.space(8);
                    }

                    clip: true
                    height: parent.height
                    visible: width > 0
                    width: root.columnLayout.name.w
                    x: root.columnLayout.name.x

                    Text {
                        id: nameText

                        readonly property int room: Math.max(0, nameCell.inner - nameCell.tail)

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: band.textColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.body
                        objectName: "name_" + row.index
                        text: row.name
                        visible: !row.editing
                        width: Math.min(implicitWidth, nameText.room)
                    }

                    Text {
                        id: categoryText

                        anchors.left: nameText.right
                        anchors.leftMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        objectName: "category_" + row.index
                        text: row.category
                        visible: !row.editing && row.category.length > 0
                    }

                    Loader {
                        active: row.editing
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(8)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.rowHeight - Theme.space(8)
                        sourceComponent: renameEditor
                    }
                }

                Text {
                    color: band.textColor
                    font.family: Theme.fontFamily
                    font.features: ({
                            "tnum": 1
                        })
                    font.pixelSize: Theme.font.body
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    leftPadding: Theme.space(8)
                    objectName: "size_" + row.index
                    rightPadding: Theme.space(8)
                    text: row.sizeText
                    verticalAlignment: Text.AlignVCenter
                    visible: width > 0
                    width: root.columnLayout.size.w
                    x: root.columnLayout.size.x
                }

                // Progress (`.tbl > div.pg`): the line in the state colour, then the percent
                // from the `progressText` role in a fixed 40 u column.
                Item {
                    id: progressCell

                    height: parent.height
                    visible: width > 0
                    width: root.columnLayout.progress.w
                    x: root.columnLayout.progress.x

                    ProgressLine {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(8)
                        anchors.right: progressText.left
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.stateColor
                        height: Theme.rowHeight
                        objectName: "progressLine_" + row.index
                        showText: false
                        value: row.progress
                    }

                    Text {
                        id: progressText

                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.features: ({
                                "tnum": 1
                            })
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignRight
                        objectName: "progress_" + row.index
                        text: row.progressText
                        width: Theme.space(40)
                    }
                }

                // Status (`.tbl .st`): the glyph in the state colour, the word in the row's
                // own text colour.
                Item {
                    id: statusCell

                    clip: true
                    height: parent.height
                    visible: width > 0
                    width: root.columnLayout.status.w
                    x: root.columnLayout.status.x

                    Glyph {
                        id: statusGlyphText

                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.stateColor
                        height: Theme.font.body
                        objectName: "statusGlyph_" + row.index
                        pixelSize: Theme.font.body
                        text: row.statusGlyph
                        width: Theme.space(12)
                    }

                    Text {
                        anchors.left: statusGlyphText.right
                        anchors.leftMargin: Theme.space(6)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: band.textColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.body
                        objectName: "status_" + row.index
                        text: row.stateText
                    }
                }

                Text {
                    color: band.textColor
                    font.family: Theme.fontFamily
                    font.features: ({
                            "tnum": 1
                        })
                    font.pixelSize: Theme.font.body
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    leftPadding: Theme.space(8)
                    objectName: "seedsPeers_" + row.index
                    rightPadding: Theme.space(8)
                    text: row.seedsPeersText
                    verticalAlignment: Text.AlignVCenter
                    visible: width > 0
                    width: root.columnLayout.seedsPeers.w
                    x: root.columnLayout.seedsPeers.x
                }

                Text {
                    color: band.textColor
                    font.family: Theme.fontFamily
                    font.features: ({
                            "tnum": 1
                        })
                    font.pixelSize: Theme.font.body
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    leftPadding: Theme.space(8)
                    objectName: "dlspeed_" + row.index
                    rightPadding: Theme.space(8)
                    text: row.dlspeedText
                    verticalAlignment: Text.AlignVCenter
                    visible: width > 0
                    width: root.columnLayout.dlspeed.w
                    x: root.columnLayout.dlspeed.x
                }

                Text {
                    color: band.textColor
                    font.family: Theme.fontFamily
                    font.features: ({
                            "tnum": 1
                        })
                    font.pixelSize: Theme.font.body
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    leftPadding: Theme.space(8)
                    objectName: "upspeed_" + row.index
                    rightPadding: Theme.space(8)
                    text: row.upspeedText
                    verticalAlignment: Text.AlignVCenter
                    visible: width > 0
                    width: root.columnLayout.upspeed.w
                    x: root.columnLayout.upspeed.x
                }

                Text {
                    color: band.textColor
                    font.family: Theme.fontFamily
                    font.features: ({
                            "tnum": 1
                        })
                    font.pixelSize: Theme.font.body
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    leftPadding: Theme.space(8)
                    objectName: "eta_" + row.index
                    rightPadding: Theme.space(8)
                    text: row.etaText
                    verticalAlignment: Text.AlignVCenter
                    visible: width > 0
                    width: root.columnLayout.eta.w
                    x: root.columnLayout.eta.x
                }

                // The optional columns: one plain cell each, the number columns tabular.
                Repeater {
                    model: root.textColumns

                    delegate: Text {
                        id: textCell

                        required property var modelData

                        readonly property var box: root.columnLayout[textCell.modelData.key]
                        readonly property var value: row.model[textCell.modelData.text]

                        color: band.textColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.features: ({
                                "tnum": 1
                            })
                        font.pixelSize: Theme.font.body
                        height: band.height
                        horizontalAlignment: textCell.modelData.align === Qt.AlignRight
                                             ? Text.AlignRight : Text.AlignLeft
                        leftPadding: Theme.space(8)
                        objectName: textCell.modelData.key + "_" + row.index
                        rightPadding: Theme.space(8)
                        text: textCell.value === undefined || textCell.value === null
                              ? "" : String(textCell.value)
                        verticalAlignment: Text.AlignVCenter
                        width: textCell.box.w
                        x: textCell.box.x
                    }
                }
            }

            MouseArea {
                id: pointer

                acceptedButtons: Qt.LeftButton | Qt.RightButton
                anchors.fill: parent
                enabled: !row.editing
                hoverEnabled: true

                onClicked: mouse => {
                    root.forceActiveFocus();
                    if (mouse.button === Qt.RightButton) {
                        root.cursorIndex = row.index;
                        if (!row.isSelected)
                            root.selectOnly(row.index);
                        const point = pointer.mapToItem(root, mouse.x, mouse.y);
                        root.actionsRequested(row.hash, point.x, point.y);
                        return;
                    }
                    if (mouse.modifiers & Qt.ControlModifier) {
                        root.cursorIndex = row.index;
                        root.toggleSelection(row.index);
                        return;
                    }
                    if (mouse.modifiers & Qt.ShiftModifier) {
                        root.selectRangeTo(row.index);
                        return;
                    }
                    root.cursorIndex = row.index;
                    root.selectOnly(row.index);
                }
                onDoubleClicked: root.activated(row.hash)
                onExited: root.hideNameTip()
                onPositionChanged: mouse => {
                    const overName = mouse.x >= nameCell.x
                                   && mouse.x < nameCell.x + nameCell.width;
                    if (!overName || !nameText.truncated) {
                        root.hideNameTip();
                        return;
                    }
                    const at = pointer.mapToItem(root, mouse.x, mouse.y);
                    root.showNameTip(at.x, at.y, row.name);
                }
            }
        }
    }

    ScrollBar {
        id: sideways

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        flickable: list
        objectName: "sideways"
    }

    // A vertical wheel with shift held scrolls sideways, as it does in a browser; a touchpad's
    // own sideways swipe already reaches the list, which flicks both ways.
    WheelHandler {
        acceptedModifiers: Qt.ShiftModifier
        target: null

        onWheel: event => {
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x;
            root.scrollSideways(-delta / 120 * Theme.space(60));
        }
    }

    // A name too long for its column is elided (`Text.ElideRight`). Selecting the row already
    // shows it whole in the details pane, so this covers the other case: reading a row you do
    // not want to select. It only ever appears over a name that really was cut.
    Tooltip {
        id: nameTip
    }

    // The inline rename editor (`.tbl .rn .ren`): a 1 px accent outline, the row height less
    // 8 u, the whole name selected at a 35 % accent highlight.
    Component {
        id: renameEditor

        Rectangle {
            border.color: Theme.accent
            border.width: 1
            color: Theme.fill(Theme.fillIdle)

            TextInput {
                id: editor

                readonly property color accent: Theme.accent
                readonly property color highlight: Qt.rgba(editor.accent.r, editor.accent.g, editor.accent.b, 0.35)

                anchors.fill: parent
                anchors.leftMargin: Theme.space(6)
                anchors.rightMargin: Theme.space(6)
                clip: true
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                selectByMouse: true
                selectedTextColor: Theme.brightForeground
                selectionColor: editor.highlight
                text: root.renameText
                verticalAlignment: TextInput.AlignVCenter

                Keys.onEscapePressed: root.cancelRename()
                onAccepted: root.commitRename()
                onTextChanged: root.renameText = editor.text

                Component.onCompleted: {
                    editor.selectAll();
                    editor.forceActiveFocus();
                }
            }
        }
    }
}
