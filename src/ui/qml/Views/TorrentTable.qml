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
// #52: a grip on each header cell's right edge resizes its column (a double-click fits the
// widest cell on screen). `columnWidths` keeps what was changed, in the same base-12 units, so
// a base size change scales a dragged width as it does a default one. Name stays the flexible
// column: its grip sets the least it may get (never under `minimumNameWidth`), and it still
// takes whatever room the others leave (docs/DECISIONS.md, "The table's column widths").
//
// Cursor and selection are two different things, as in the figure. `cursorIndex` is the
// outlined band (`.tbl .r.cur`), the row every key acts on; the `ItemSelectionModel` is the
// filled band (`.tbl .r.sel`); a row that is both gets `TableRow`'s single 24 % fill
// (`.tbl .r.sel.cur`). The cursor is kept on its torrent by hash across a re-sort or a
// removal, and clamps when that hash is gone.
//
// DECISIONS C24 H3 (#168): the rows display and the overview are faces of this table. While
// `face` is set, it draws the rows and the table hides its header and list but keeps
// everything else: the cursor, the selection, the anchor, the keys, the rename, and every
// function the window and the outcome strip call (revealHash, selectHashes, hashInView,
// scrollToRow…), which ask the face where its rows are. One brain, three faces.
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
    /// `spoken`, where given, is what a screen reader calls the header instead of a `title`
    /// that is or holds a glyph or an abbreviation (`↓`, `S / P`; #78).
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
            // Kept that way: docs/DECISIONS.md Q17.
            "width": 116,
            "align": Qt.AlignLeft,
            "order": Qt.AscendingOrder,
            "visible": true
        },
        {
            "key": "seedsPeers",
            "name": qsTr("S / P  seeds / peers"),
            "title": qsTr("S / P"),
            "spoken": qsTr("Seeds and peers"),
            "role": "numSeeds",
            "width": 62,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
        },
        // #24 T3: seeds in the swarm (the tracker's count, not the connected peers), yellow
        // at or under the rare threshold, "only you" when you are the last seed.
        {
            "key": "swarm",
            "name": qsTr("Swarm  seeds in the swarm"),
            "title": qsTr("Swarm"),
            "role": "numComplete",
            "text": "numComplete",
            "width": 76,
            "align": Qt.AlignRight,
            "order": Qt.AscendingOrder,
            "visible": true
        },
        {
            "key": "dlspeed",
            "name": qsTr("↓  download speed"),
            "title": Glyphs.arrowDown,
            "spoken": qsTr("Download speed"),
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
            "spoken": qsTr("Upload speed"),
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
            // #24 T4: two short tracks before the number (to 1.0, then on to the limit). Wide
            // enough to keep a gap between them and "9998.99", the widest ratio (∞ above it).
            "width": 112,
            "align": Qt.AlignRight,
            "order": Qt.DescendingOrder,
            "visible": true
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
            "spoken": qsTr("Download limit"),
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
            "spoken": qsTr("Upload limit"),
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

    /// #52: the widths changed from the default, `{key: width}` in spec pixels at base 12
    /// like `columns[].width` (a base size change scales them too). A column at its default
    /// has no entry, so a later default reaches it. Name's entry is the least it may get: it
    /// still takes the room the others leave. Write it through `setColumnWidth()`,
    /// `setColumnWidths()` or `resetColumnWidths()`, which keep it whole.
    property var columnWidths: ({})
    /// The narrowest a column other than Name can be dragged, in spec pixels: room for its
    /// padding and a glyph or two.
    property int minimumColumnWidth: 32
    /// The widest any column can be dragged or fitted, in spec pixels.
    property int maximumColumnWidth: 1200
    /// Where the skin speaks the status word in the caption voice (board 46 `.verb`: Console's
    /// upper case, tracked), the Status column's default holds "downloading" whole beside its
    /// glyph, in spec pixels; 0 under the body voice, whose 116 holds it (Q17).
    readonly property int statusVerbUnits: Theme.verbCaption ? Math.ceil(root.unitsOf(
                                                                             Theme.space(8) + Theme.space(12)
                                                                             + Theme.space(6)
                                                                             + statusVerbMetrics.advanceWidth
                                                                             + Theme.space(8))) : 0
    /// False once a column has a width of its own.
    readonly property bool columnWidthsAreDefault: Object.keys(root.columnWidths).length === 0
    /// The columns palette's "default order" row puts back both (#52).
    readonly property bool columnLayoutIsDefault: root.columnOrderIsDefault
                                                  && root.columnWidthsAreDefault
    /// Room past the last row, so something floating over the table's bottom (the selection
    /// bar, docs/17 M9.6) never hides it: the list can always scroll it clear.
    property real bottomRoom: 0

    // ---- cursor, selection, rename ------------------------------------------------------
    property int cursorIndex: 0
    /// #201 F16: the list has the keys. While the rail or the details pane has them, the
    /// cursor row keeps its fill (the pane still follows it) and drops its edge: the hairline,
    /// or the skin's mark. The same in every face: table, rows and shelves.
    property bool keysHere: true
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

    /// The host holds the row order too, beside the pointer over the rows (#51 D9): while the
    /// actions menu is open over them, so a right-click on another row (D17) lands on the
    /// torrent it was aimed at.
    property bool holdOrder: false
    /// Whether the rows' order is held now (TorrentFilterModel::holdOrder).
    readonly property bool orderHeld: (root.face !== null ? root.face.hovered : rowsHover.hovered)
                                      || root.holdOrder

    /// Moves whenever the selection does: a binding that asks isRowSelected() or
    /// hashSelected() reads it, as the rows do.
    readonly property alias selectionRevision: internal.revision
    /// How far the rows are scrolled, for a binding that asks hashInView().
    readonly property real scrollY: root.face !== null ? root.face.scrollY : list.contentY

    /// DECISIONS C24 H3: the display drawing the rows instead of the table's own header and
    /// list (LibraryRows, LibraryShelves), or null. A face is an Item with `scrollY`,
    /// `hovered` (the pointer is over its rows), `inlineRename` (it draws the rename editor
    /// itself) and four functions: `positionAt(row)` scrolls just enough for the row to be
    /// wholly in view, `rowInView(row)` says whether it is, `rowAnchor(row)` is the point
    /// under the row a menu opens at and `rowAt(x, y)` the row there or -1 (both in the face's
    /// own coordinates).
    property Item face: null

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
    /// DECISIONS C18 D6: a plain left click put the selection on one row; `hadSelection`
    /// says whether one was on before it (the host's hint on the selection bar).
    signal plainClicked(bool hadSelection)
    /// A right-click on a header cell (#51 D6): the host opens the columns palette.
    signal columnsRequested
    /// `r` while a face with no inline editor draws the rows (#168 assumed 9): the host opens
    /// the rename sheet on `hash`.
    signal renameSheetRequested(string hash)

    // ---- derived -------------------------------------------------------------------------
    /// `columns` in `columnOrder`.
    readonly property var orderedColumns: {
        const byKey = {};
        for (const column of root.columns)
            byKey[column.key] = column;
        return root.columnOrder.map(key => byKey[key]).filter(column => column !== undefined);
    }
    readonly property var activeColumns: {
        const shown = root.orderedColumns.filter(column => {
            return column.visible && root.visibleColumns.indexOf(column.key) >= 0;
        });
        // J1: the Move column right after Name, where a wide table can't scroll it away.
        if (root.showMoveColumn)
            shown.splice(1, 0, root._moveColumnSpec);
        return shown;
    }

    /// Board 42 J1 (DECISIONS C15 M10): a move's "show" adds the Move column, the job's word
    /// for each torrent (moved, failed, moving now, queued, waiting…), from `moveStepFor`. It
    /// is not one of the table's columns: nothing saves it, and it goes with the "show".
    property bool showMoveColumn: false
    /// `hash => {text, glyph, kind}`; called again when `moveRevision` moves.
    property var moveStepFor: null
    property int moveRevision: 0
    /// DECISIONS C18 D1: a quick action's mark in the row's status cell, `hash => {text,
    /// kind ("done" or "failed")}` ({} for none); called again when `rowMarkRevision` moves.
    property var rowMarkFor: null
    property int rowMarkRevision: 0
    /// C18 D3: `esc` goes to the host first (the outcome strip's), even with a selection on.
    property bool escapeTaken: false
    readonly property var _moveColumnSpec: ({
                                                "key": "move",
                                                "title": qsTr("Move"),
                                                "role": "name",
                                                "text": "move",
                                                "width": 120,
                                                "align": Qt.AlignLeft,
                                                "order": Qt.AscendingOrder,
                                                "visible": true
                                            })
    /// The active columns that are plain text, drawn by one generic cell each.
    readonly property var textColumns: root.activeColumns.filter(column => column.text !== undefined)

    /// DECISIONS C18 D6 (board 45 M1): the row's start, before the first column: a □ under the
    /// pointer (a click adds the row, shift+click the range), ■ on the selected rows.
    readonly property int tickWidth: Theme.space(16)

    /// How wide the rows are: the view's width, or more when the chosen columns need it.
    readonly property int rowWidth: {
        let fixed = 0;
        let least = Theme.space(root.minimumNameWidth);
        for (const column of root.activeColumns) {
            if (column.width > 0)
                fixed += Theme.space(root.widthOf(column.key));
            else
                least = Theme.space(root.widthOf(column.key));
        }
        return Math.max(root.width, root.tickWidth + fixed + least);
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
                fixed += Theme.space(root.widthOf(column.key));
        }
        const flexible = root.rowWidth - root.tickWidth - fixed;
        let x = root.tickWidth;
        for (const column of shown) {
            const w = column.width > 0 ? Theme.space(root.widthOf(column.key)) : flexible;
            layout[column.key] = {
                "x": x,
                "w": w
            };
            x += w;
        }
        return layout;
    }

    /// The Ratio cell's two tracks (#24 T4) side by side, as its row draws them.
    readonly property int ratioTracksWidth: Theme.space(20) + Theme.space(2) + Theme.space(16)

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
            "error": Glyphs.statusErrored,
            "missing files": Glyphs.statusErrored
        };
    }

    implicitHeight: Theme.rowHeight * 8
    implicitWidth: Theme.space(900)

    // ---- #24 T5: rows that are sending pulse (docs/22 §6, docs/DECISIONS.md C7) -----------
    // A 2 px tick at the left edge of a row that uploads, breathing faster the more it sends.
    // Every frame of an animation repaints the whole window, so the rows share one clock that
    // steps `pulseStepMs` (not one animation per row at the display's rate), it runs only
    // while someone can see it, and a row's tick reads it only while the row is on screen.

    /// Ticks show on rows that upload (Settings › Appearance, off by default).
    readonly property bool pulseShown: App.pulseRows
    /// The window is in use: not behind another one, not hidden to the tray (#49's rule).
    property bool pulseWindowActive: root.Window.active
    /// The clock runs: ticks shown, the table on show, the window in use, something uploading,
    /// and the desktop not asking for less motion. Otherwise a tick is shown still.
    readonly property bool pulseRunning: root.pulseShown && !App.reducedMotion && root.visible
                                         && root.pulseWindowActive && App.globalState.upInfoSpeed > 0
    /// Seconds on the rows' clock.
    property real pulseClock: 0
    /// About 12 steps a second: smooth enough for a 2 px line, a fifth of the repaints of
    /// an animation at 60 Hz.
    readonly property int pulseStepMs: 80

    /// Seconds per breath for an upload speed, in four steps (the board's 2.1 MB/s row
    /// breathes in 1.1 s, its 96 KB/s row in 2.6 s). Steps, so a speed that drifts every poll
    /// does not keep changing the beat.
    function pulsePeriod(speed: real): real {
        if (speed >= 1.5 * 1024 * 1024)
            return 1.1;
        if (speed >= 512 * 1024)
            return 1.6;
        if (speed >= 128 * 1024)
            return 2.1;
        return 2.6;
    }

    // The status word's widest in the verb voice, for `statusVerbUnits`.
    TextMetrics {
        id: statusVerbMetrics

        font.capitalization: Theme.captionUpper ? Font.AllUppercase : Font.MixedCase
        font.family: Theme.labelFamily
        font.letterSpacing: Theme.font.caption * Theme.captionTrack(0)
        font.pixelSize: Theme.font.caption
        font.weight: Theme.captionWeight(Font.Normal)
        text: root.statusWord("downloading")
    }

    Timer {
        interval: root.pulseStepMs
        repeat: true
        running: root.pulseRunning

        onTriggered: root.pulseClock += root.pulseStepMs / 1000
    }

    // ---- public functions ----------------------------------------------------------------
    /// The Status column's word for a `stateText`. "missing files" does not fit the column,
    /// so the table says "missing" next to the red glyph; the details pane keeps both words.
    function statusWord(stateText: string): string {
        return stateText === "missing files" ? qsTr("missing") : stateText;
    }

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

    // ---- column widths (#52) ---------------------------------------------------------------
    /// `key`'s width in spec pixels: the one being dragged, else its own, else the default.
    /// Name's is the least it may get (the flexible column takes more when there is room).
    function widthOf(key: string): real {
        if (internal.resizeKey === key)
            return internal.resizeWidth;
        const own = root.columnWidths[key];
        if (own !== undefined)
            return own;
        return root.defaultWidthOf(key);
    }

    /// `key`'s width as `columns` declares it; Name's is `minimumNameWidth`.
    function defaultWidthOf(key: string): real {
        if (key === "name")
            return root.minimumNameWidth;
        if (key === "move")
            return root._moveColumnSpec.width;
        const column = root.columns.find(other => other.key === key);
        if (key === "status" && column !== undefined)
            return Math.max(column.width, root.statusVerbUnits);
        return column !== undefined ? column.width : 0;
    }

    /// `units` held between `key`'s least and the widest a column may be.
    function clampWidth(key: string, units: real): real {
        const least = key === "name" ? root.minimumNameWidth : root.minimumColumnWidth;
        return Math.max(least, Math.min(root.maximumColumnWidth, units));
    }

    /// Spec pixels (base 12) for `px` screen pixels at the current base size.
    function unitsOf(px: real): real {
        return px * 1200 / Theme.space(1200);
    }

    /// Give `key` a width of `units` spec pixels, rounded and held to its limits; back at the
    /// default it loses its entry. An unknown key changes nothing.
    function setColumnWidth(key: string, units: real): void {
        if (root.defaultColumnOrder.indexOf(key) < 0 || !isFinite(units))
            return;
        const width = Math.round(root.clampWidth(key, units));
        const next = Object.assign({}, root.columnWidths);
        if (width === root.defaultWidthOf(key))
            delete next[key];
        else
            next[key] = width;
        if (next[key] !== root.columnWidths[key])
            root.columnWidths = next;
    }

    /// A saved `{key: width}` made whole: unknown keys and values that are not numbers
    /// dropped, the rest held to their limits, defaults left out.
    function setColumnWidths(widths: var): void {
        const next = {};
        if (widths !== undefined && widths !== null && typeof widths === "object") {
            for (const key of Object.keys(widths)) {
                const units = Number(widths[key]);
                if (root.defaultColumnOrder.indexOf(key) < 0 || !isFinite(units) || units <= 0)
                    continue;
                const width = Math.round(root.clampWidth(key, units));
                if (width !== root.defaultWidthOf(key))
                    next[key] = width;
            }
        }
        if (JSON.stringify(next) !== JSON.stringify(root.columnWidths))
            root.columnWidths = next;
    }

    function resetColumnWidths(): void {
        root.setColumnWidths({});
    }

    /// The columns palette's "default order": the order and the widths they came in.
    function resetColumns(): void {
        root.resetColumnOrder();
        root.resetColumnWidths();
    }

    /// A double-click on `key`'s grip: as wide as its header and the widest of its cells on
    /// screen need, so none of them elides. Rows scrolled out of view are not measured.
    function fitColumn(key: string): void {
        const at = root.activeColumns.findIndex(column => column.key === key);
        if (at < 0)
            return;
        const cell = headerCells.itemAt(at) as TableHeader;
        let need = cell !== null ? cell.fitWidth : 0;
        const top = list.contentY;
        const bottom = top + list.height;
        const first = Math.max(0, list.indexAt(list.contentX + 1, top + 1));
        for (let index = first; index < root.filtered.count; ++index) {
            const row = list.itemAtIndex(index);
            if (row === null)
                continue;
            if (row.y >= bottom)
                break;
            if (row.y + row.height <= top)
                continue;
            // The delegate is no named type, so its function is looked up by name.
            const measure = "fitWidth";
            const fit = row[measure];
            if (typeof fit === "function")
                need = Math.max(need, fit(key));
        }
        root.setColumnWidth(key, Math.ceil(root.unitsOf(Math.ceil(need))));
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
        if (root.face !== null)
            root.face.positionAt(wanted);
        else
            list.positionViewAtIndex(wanted, ListView.Contain);
    }

    /// Put the cursor on `hash`, first clearing every filter that hides it (text, status,
    /// category, tag, tracker), so a notice's "show", the duplicate sheet's `show it` and the
    /// jump palette always land. False when the library does not hold `hash`.
    function revealHash(hash: string): bool {
        if (hash.length === 0)
            return false;
        if (root.filtered.rowOfHash(hash) < 0) {
            root.filtered.text = "";
            root.filtered.clearCategory();
            root.filtered.clearTag();
            root.filtered.clearTracker();
            root.filtered.statusFilter = TorrentFilterModel.All;
        }
        const row = root.filtered.rowOfHash(hash);
        if (row < 0)
            return false;
        root.moveCursorTo(row);
        return true;
    }

    function selectAll(): void {
        root.filtered.selectRows(root.selection, 0, root.filtered.count - 1);
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
        internal.anchorHash = "";
        internal.refreshSelection();
    }

    function toggleCursorSelection(): void {
        root.toggleSelection(root.cursorIndex);
    }

    /// `v`: select everything between the last toggled row and the cursor.
    function selectRangeToCursor(): void {
        const anchor = internal.anchorRow();
        const from = Math.min(anchor, root.cursorIndex);
        const to = Math.max(anchor, root.cursorIndex);
        root.filtered.selectRows(root.selection, from, to);
        internal.refreshSelection();
    }

    /// Select only this row: the plain left click.
    function selectOnly(index: int): void {
        internal.selectRow(index, ItemSelectionModel.ClearAndSelect);
        internal.anchorHash = root.filtered.hashAt(index);
        internal.refreshSelection();
    }

    /// C18 (board 45 O3): the outcome strip's "show" selects the torrents it is about, those
    /// the filter shows.
    function selectHashes(hashes: var): void {
        root.selection.clearSelection();
        for (const hash of hashes) {
            const row = root.filtered.rowOfHash(hash);
            if (row >= 0)
                internal.selectRow(row, ItemSelectionModel.Select);
        }
        internal.anchorHash = hashes.length > 0 ? hashes[0] : "";
        internal.refreshSelection();
    }

    function toggleSelection(index: int): void {
        internal.selectRow(index, ItemSelectionModel.Toggle);
        internal.anchorHash = root.filtered.hashAt(index);
        internal.refreshSelection();
    }

    /// shift+click (`add` false): the selection becomes the anchor row through `index`;
    /// ctrl+shift+click (`add` true): that range joins the selection. Either way the cursor
    /// goes to `index` and the anchor stays, so a second shift+click re-draws from it.
    function selectRangeTo(index: int, add: bool): void {
        const anchor = internal.anchorRow();
        root.cursorIndex = index;
        internal.anchorHash = root.filtered.hashAt(anchor);
        root.filtered.selectRows(root.selection, anchor, index, !add);
        internal.refreshSelection();
    }

    /// DECISIONS C30 F8: `hash`'s row is wholly inside the visible part of the list. A
    /// binding reads `scrollY` beside it; rows all have the one height.
    function hashInView(hash: string): bool {
        const row = root.filtered.rowOfHash(hash);
        if (row < 0)
            return false;
        if (root.face !== null)
            return root.face.rowInView(row);
        const top = list.originY + row * Theme.rowHeight;
        return top >= list.contentY - 1 && top + Theme.rowHeight <= list.contentY + list.height + 1;
    }

    /// Scroll just enough for row `row` to be wholly in view; the cursor stays.
    function scrollToRow(row: int): void {
        if (row < 0 || row >= root.filtered.count)
            return;
        if (root.face !== null)
            root.face.positionAt(row);
        else
            list.positionViewAtIndex(row, ListView.Contain);
    }

    /// `hash` is shown and selected.
    function hashSelected(hash: string): bool {
        const row = root.filtered.rowOfHash(hash);
        return row >= 0 && root.isRowSelected(row);
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
        if (root.face !== null && !root.face.inlineRename) {
            root.renameSheetRequested(root.filtered.hashAt(root.cursorIndex));
            return;
        }
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
        // Trimmed like the add sheet's Name, and not sent at all when nothing changed.
        const name = root.renameText.trim();
        internal.closeRename();
        const row = root.filtered.rowOfHash(hash);
        const current = row >= 0 ? root.filtered.data(root.filtered.index(row, 0),
                                                      TorrentModel.NameRole) : "";
        if (name.length > 0 && name !== current)
            root.renameRequested(hash, name);
    }

    function cancelRename(): void {
        internal.closeRename();
    }

    /// A right-click on row `index` at `x`, `y` (this item's coordinates): the cursor goes
    /// there, a row outside the selection becomes the selection alone, and the actions menu
    /// opens at the point.
    function contextClick(index: int, x: real, y: real): void {
        const hash = root.filtered.hashAt(index);
        if (hash.length === 0)
            return;
        root.cursorIndex = index;
        if (!root.isRowSelected(index))
            root.selectOnly(index);
        root.actionsRequested(hash, x, y);
    }

    /// The row under `x`, `y` (this item's coordinates), or -1 off the rows.
    function rowAt(x: real, y: real): int {
        if (root.face !== null) {
            const onFace = root.mapToItem(root.face, x, y);
            return root.face.rowAt(onFace.x, onFace.y);
        }
        const at = root.mapToItem(list.contentItem, x, y);
        if (x < list.x || x >= list.x + list.width || y < list.y || y >= list.y + list.height)
            return -1;
        return list.indexAt(at.x, at.y);
    }

    /// #51 D17: a right-click that lands on the scrim of the open actions menu, at `x`, `y`
    /// (this item's coordinates), right-clicks the row there. False off the rows.
    function contextClickAt(x: real, y: real): bool {
        const index = root.rowAt(x, y);
        if (index < 0)
            return false;
        root.forceActiveFocus();
        root.contextClick(index, x, y);
        return true;
    }

    /// `enter` and right-click: ask for the actions menu, anchored on the cursor row.
    function requestActions(): void {
        if (root.cursorHash.length === 0)
            return;
        if (root.face !== null) {
            const under = root.face.rowAnchor(root.cursorIndex);
            const point = root.face.mapToItem(root, under.x, under.y);
            root.actionsRequested(root.cursorHash, point.x, point.y);
            return;
        }
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
            if (root.selectedCount > 0 && !root.escapeTaken)
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
        /// The range anchor (the last row clicked or toggled), by hash: the live sort moves
        /// rows between two clicks, and a shift+click must still draw from the same torrent.
        property string anchorHash: ""
        property int renameIndex: -1
        property string renameHash: ""
        property string cursorHash: ""

        // A header cell being dragged (N2): its key, the pointer's x in the header, and where
        // in the cell it was grabbed (so the ghost stays under the pointer as it was picked up).
        property string dragKey: ""
        property real dragX: 0
        property real dragGrab: 0
        // A grip being dragged (#52): its column, and the width it has now in spec pixels.
        // `columnWidths` takes it on release, so a drag writes `state.json` once.
        property string resizeKey: ""
        property real resizeWidth: 0
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

        /// The anchor's row now, or the cursor's when there is none (or it left the view).
        function anchorRow(): int {
            const row = internal.anchorHash.length > 0 ? root.filtered.rowOfHash(internal.anchorHash)
                                                       : -1;
            return row < 0 ? root.cursorIndex : row;
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
        visible: root.face === null
    }

    Item {
        id: header

        height: Theme.font.body * 2
        parent: headerStrip
        width: root.rowWidth
        x: -list.contentX

        Repeater {
            id: headerCells

            model: root.activeColumns

            delegate: TableHeader {
                id: headerCell

                required property var modelData

                readonly property var box: root.columnLayout[headerCell.modelData.key]

                // #78: "Download speed", never the arrow the header prints.
                Accessible.name: headerCell.modelData.spoken ?? headerCell.modelData.title
                alignRight: headerCell.modelData.align === Qt.AlignRight
                elides: true
                descending: root.filtered.sortOrder === Qt.DescendingOrder
                dragging: internal.dragKey.length > 0
                // The column being dragged: its caption on the selected fill below.
                ink: Ink {
                    animated: false
                    ground: internal.dragKey === headerCell.modelData.key ? Theme.on.selected
                                                                          : Theme.on.window
                }
                objectName: "header_" + headerCell.modelData.key
                sorted: root.sortRoleName === headerCell.modelData.role
                text: headerCell.modelData.title
                width: headerCell.box.w
                x: headerCell.box.x

                rightClickable: true

                onClicked: root.sortBy(headerCell.modelData.role)
                onRightClicked: root.columnsRequested()

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
                            internal.dragGrab = headerDrag.centroid.pressPosition.x;
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
                    color: Theme.on.selected.fill
                    visible: internal.dragKey === headerCell.modelData.key
                }
            }
        }

        // #52: a grip across each cell's right edge, over the cells so a press there neither
        // sorts nor moves the column. Dragging it resizes the column, a double-click fits it;
        // nothing shows until the pointer is on it. Hidden while a column is being moved.
        Repeater {
            model: root.activeColumns

            // a11y-ignore A2: a drag handle; the columns palette puts widths back (#77)
            delegate: MouseArea {
                id: grip

                required property var modelData

                readonly property string key: grip.modelData.key
                readonly property var box: root.columnLayout[grip.key]
                /// Where the press landed (the table's x, steady while the header scrolls)
                /// and the column's width then, in pixels. Until the pointer goes further than
                /// a click would, a press is a click, maybe the first of a double-click.
                property real pressX: 0
                property real startWidth: 0
                property bool moved: false

                function finish(keep: bool): void {
                    if (keep && grip.moved)
                        root.setColumnWidth(grip.key, internal.resizeWidth);
                    if (internal.resizeKey === grip.key)
                        internal.resizeKey = "";
                    grip.moved = false;
                }

                cursorShape: Qt.SplitHCursor
                height: parent.height
                hoverEnabled: true
                objectName: "grip_" + grip.key
                // The cell under it hears the press too: past the drag distance its
                // DragHandler would take the drag over and move the column instead.
                preventStealing: true
                visible: internal.dragKey.length === 0
                width: Theme.space(6)
                x: grip.box.x + grip.box.w - Math.round(grip.width / 2)
                z: 4

                onCanceled: grip.finish(false)
                onDoubleClicked: root.fitColumn(grip.key)
                onPositionChanged: mouse => {
                    if (!grip.pressed)
                        return;
                    const along = grip.mapToItem(root, mouse.x, mouse.y).x - grip.pressX;
                    if (!grip.moved) {
                        if (Math.abs(along) < Application.styleHints.startDragDistance)
                            return;
                        grip.moved = true;
                    }
                    internal.resizeWidth = root.clampWidth(grip.key, root.unitsOf(
                                                               grip.startWidth + along));
                    internal.resizeKey = grip.key;
                }
                onPressed: mouse => {
                    grip.pressX = grip.mapToItem(root, mouse.x, mouse.y).x;
                    grip.startWidth = grip.box.w;
                    grip.moved = false;
                }
                onReleased: grip.finish(true)

                // On the boundary while the pointer is on it, as the split's grip shows.
                Rectangle {
                    anchors.centerIn: parent
                    color: Theme.accent
                    height: Theme.font.body
                    objectName: "gripMark"
                    radius: Theme.space(1)
                    visible: grip.containsMouse || grip.pressed
                    width: Theme.space(2)
                }
            }
        }

        // Where the dragged column would land: a 1 px accent line on the boundary (M13),
        // drawn over the ghost, which usually sits right on it.
        Rectangle {
            color: Theme.accent
            height: parent.height
            objectName: "dropMarker"
            visible: internal.dropTarget !== null
            width: 1
            z: 3
            x: internal.dropTarget !== null ? Math.max(0, Math.min(parent.width - width,
                                                                   internal.dropTarget.x)) : 0
        }

        // M13: a ghost of the dragged cell rides under the pointer, held where it was
        // grabbed, so the drag reads as carrying the column. Opaque over the cells it passes,
        // outlined in the accent; `enabled: false`, so it never takes the pointer.
        Rectangle {
            id: dragGhost

            readonly property var column: internal.dragKey.length === 0 ? null
                                          : root.activeColumns.find(column => column.key
                                                                    === internal.dragKey) ?? null
            readonly property real cellWidth: dragGhost.column !== null
                                              ? root.columnLayout[internal.dragKey].w : 0

            border.color: Theme.accent
            border.width: Theme.edgeWidth
            color: Theme.background
            enabled: false
            height: parent.height
            objectName: "dragGhost"
            radius: Theme.cornerRadius
            visible: dragGhost.column !== null
            width: dragGhost.cellWidth
            x: Math.max(0, Math.min(parent.width - dragGhost.width,
                                    internal.dragX - internal.dragGrab))
            z: 2

            TableHeader {
                alignRight: dragGhost.column !== null && dragGhost.column.align === Qt.AlignRight
                anchors.fill: parent
                // A picture of the header being dragged; the header itself is in the tree.
                Accessible.ignored: true
                descending: root.filtered.sortOrder === Qt.DescendingOrder
                enabled: false
                sorted: dragGhost.column !== null && root.sortRoleName === dragGhost.column.role
                text: dragGhost.column !== null ? dragGhost.column.title : ""
            }
        }
    }

    // ---- rows ------------------------------------------------------------------------------
    ListView {
        id: list

        anchors.bottom: sideways.visible ? sideways.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerStrip.bottom
        bottomMargin: root.bottomRoom
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: root.rowWidth
        flickableDirection: Flickable.AutoFlickDirection
        cacheBuffer: Theme.rowHeight * 6
        clip: true
        keyNavigationEnabled: false
        model: root.filtered
        objectName: "rows"
        reuseItems: true
        visible: root.face === null

        // #77: the rows' list, for a screen reader; each row is the `TableRow` band in it.
        Accessible.name: qsTr("torrents")
        Accessible.role: Accessible.List

        // #51 D9 (see `orderHeld`): an ancestor of the rows, it hears the hover they take.
        HoverHandler {
            id: rowsHover
        }

        delegate: Item {
            id: row

            required property string category
            required property string dlspeedText
            /// The row's whole model entry, for the optional columns' `text` roles.
            required property var model
            required property string etaText
            required property string hash
            required property int index
            /// docs/16 §5: carries the private flag; "private" follows the category.
            required property bool isPrivate
            required property string name
            required property real progress
            required property string progressText
            required property string seedsPeersText
            required property string sizeText
            required property string stateRole
            required property string stateText
            /// docs/28 R1: the short cause of an error from the server's log, or "".
            required property string statusCause
            /// DECISIONS C20 (#142): in error and stopped; its word ("stopped") takes the
            /// error colour, beside the ! glyph.
            required property bool errorStopped
            /// docs/44 D13: finished, but its files never left the incomplete folder: "move
            /// failed" and its glyph in the error colour, on a row that still seeds.
            required property bool stuckMove
            required property string upspeedText
            /// #24 T5: bytes per second, for the pulse.
            required property real upspeed

            readonly property bool editing: root.renaming && row.index === internal.renameIndex
            readonly property bool isCursor: row.index === root.cursorIndex
            readonly property bool isSelected: root.isRowSelected(row.index)
            readonly property color stateColor: band.ink[row.stateRole]
            readonly property string statusGlyph: {
                const glyph = root.statusGlyphs[row.stateText];
                return glyph !== undefined ? glyph : Glyphs.dot;
            }
            /// DECISIONS C18 D1 (board 45 O1, O2): a quick action's mark, `{text, kind}`, for
            /// its 6 s or while its failure stands; else a move the log says didn't happen,
            /// for as long as its error block does. null for none.
            readonly property var mark: {
                const own = root.rowMarkFor !== null && root.rowMarkRevision >= 0
                          ? root.rowMarkFor(row.hash) : ({});
                if (own.text !== undefined)
                    return own;
                const failure = row.model.failure;
                return failure !== undefined && failure !== null && failure.kind === "move"
                       ? {
                             "text": qsTr("didn't move"),
                             "kind": "failed"
                         } : null;
            }
            readonly property color markColor: row.mark !== null && row.mark.kind === "failed" ? band.ink.red
                                                                                              : band.ink.green

            /// A left click on the row with `modifiers`: the pointer's, and a screen reader's
            /// press (#77), which is a plain one.
            function click(modifiers: int): void {
                root.forceActiveFocus();
                if (modifiers & Qt.ShiftModifier) {
                    root.selectRangeTo(row.index, (modifiers & Qt.ControlModifier) !== 0);
                    return;
                }
                if (modifiers & Qt.ControlModifier) {
                    root.cursorIndex = row.index;
                    root.toggleSelection(row.index);
                    return;
                }
                const had = root.selectedCount > 0;
                root.cursorIndex = row.index;
                root.selectOnly(row.index);
                root.plainClicked(had);
            }

            /// C18 D6: a click on the row's □ adds or removes it and leaves the others;
            /// shift+click adds the range from the anchor.
            function tickClick(modifiers: int): void {
                root.forceActiveFocus();
                if (modifiers & Qt.ShiftModifier) {
                    root.selectRangeTo(row.index, true);
                    return;
                }
                root.cursorIndex = row.index;
                root.toggleSelection(row.index);
            }

            /// The whole text of the cell at `x` when the cell had to elide it, else "": what
            /// the hover label shows (the name, the status word, an optional column).
            function elidedTextAt(x: real): string {
                const shown = root.activeColumns;
                for (const column of shown) {
                    const box = root.columnLayout[column.key];
                    if (x < box.x || x >= box.x + box.w)
                        continue;
                    if (column.key === "name")
                        return nameText.truncated ? row.name : "";
                    if (column.key === "status")
                        return statusWord.truncated ? statusWord.text : "";
                    const cell = row.textCellOf(column.key);
                    return cell !== null && cell.truncated ? cell.text : "";
                }
                return "";
            }

            /// The cell of `key` when it is one plain text (Size, the speeds, an optional
            /// column...), else null.
            function textCellOf(key: string): Text {
                switch (key) {
                case "size":
                    return sizeCell;
                case "seedsPeers":
                    return seedsPeersCell;
                case "dlspeed":
                    return dlspeedCell;
                case "upspeed":
                    return upspeedCell;
                case "eta":
                    return etaCell;
                default:
                    break;
                }
                const at = root.textColumns.findIndex(column => column.key === key);
                return at >= 0 ? optionalCells.itemAt(at) as Text : null;
            }

            /// #52: how wide `key`'s cell must be to show all of this row's content, in pixels.
            // `if`s, not a `switch`: Qt 6.11's compiled QML returned garbage from the path
            // after a `switch` whose cases return and whose default breaks.
            function fitWidth(key: string): real {
                const pad = Theme.space(8);
                if (key === "name")
                    return pad + nameText.implicitWidth + nameCell.tail + pad;
                // The line has no width of its own: 40 u of it, then the percent.
                if (key === "progress")
                    return pad + Theme.space(40) + pad + progressText.width + pad;
                if (key === "status")
                    return pad + statusGlyphText.width + Theme.space(6) + statusWord.implicitWidth
                            + pad;
                const cell = row.textCellOf(key);
                if (cell === null)
                    return 0;
                // #24 T4: a complete row's ratio keeps its tracks and the gap before the number.
                const tracks = key === "ratio" && row.progress >= 1 ? root.ratioTracksWidth
                                                                      + Theme.space(6) : 0;
                return cell.implicitWidth + tracks;
            }

            // #77: a screen reader finds the row by the torrent's name; the speeds, ETA and
            // progress change too often to be in it. Its press is a plain click.
            // #79: what the row says of the torrent (its status, progress, size, category) is
            // the description, which is not read out again as it changes.
            Accessible.description: [statusWord.text, row.progressText, row.sizeText, row.category].filter(
                                        part => part.length > 0).join(" · ")
            Accessible.name: row.name
            Accessible.role: Accessible.ListItem
            Accessible.selectable: true
            Accessible.selected: row.isSelected
            Accessible.onPressAction: if (!row.editing)
                                          row.click(Qt.NoModifier)
            height: Theme.rowHeight
            objectName: "row_" + row.index
            width: root.rowWidth

            TableRow {
                id: band

                // #77: the row a screen reader finds is the delegate (a list view publishes its
                // delegates whatever they are), so the band, a row of its own, stays out of it.
                Accessible.ignored: true
                anchors.fill: parent
                hasCursor: row.isCursor
                keysHere: root.keysHere
                // The hover fill of `.tbl .r:hover`. `TableRow`'s own MouseArea never sees these
                // events (the delegate's one is on top), so the hover state comes from here; it
                // is a ground like the others, and every cell's words take `band.ink`
                // (docs/DECISIONS.md C30 F1, F4).
                hovered: pointer.containsMouse
                selected: row.isSelected

                // #24 T5 (docs/22 §6 `.pulse`): the row is sending. Opacity .25 → 1 → .25 over
                // a breath, on the table's clock; still (and whole) when the clock is off or
                // the row is out of view (the list keeps a few rows built past its edges).
                Rectangle {
                    id: pulseTick

                    readonly property bool sending: root.pulseShown && row.upspeed > 0
                    readonly property bool onScreen: row.y + row.height > list.contentY
                                                     && row.y < list.contentY + list.height

                    color: Theme.stateSeeding
                    height: parent.height - 2 * pulseTick.y
                    objectName: "pulse_" + row.index
                    opacity: pulseTick.sending && root.pulseRunning && pulseTick.onScreen
                             ? 0.625 - 0.375 * Math.cos(2 * Math.PI * root.pulseClock
                                                        / root.pulsePeriod(row.upspeed))
                             : 1
                    visible: pulseTick.sending
                    width: Math.max(1, Math.round(Theme.space(2)))
                    x: 0
                    y: Math.round(Theme.space(5))
                }

                // C18 D6 (board 45 M1): □ under the pointer, ■ on a selected row, in the row's
                // start; the pointer's own MouseArea below takes its click.
                // a11y-ignore A2: the row is selectable already; this only draws its state
                Text {
                    id: tick

                    readonly property bool onTick: pointer.containsMouse && pointer.mouseX < root.tickWidth

                    color: row.isSelected || tick.onTick ? band.ink.accent : band.ink.foreground
                    font.family: Theme.valueFamily
                    font.pixelSize: Theme.font.small
                    height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    objectName: "tick_" + row.index
                    text: row.isSelected ? Glyphs.picked : Glyphs.unpicked
                    verticalAlignment: Text.AlignVCenter
                    visible: (row.isSelected || pointer.containsMouse) && !row.editing
                    width: root.tickWidth
                    x: 0
                }

                // Name + dimmed category (`.tbl > div.nm`): the name elides, the category
                // keeps its width and sits right after it.
                Item {
                    id: nameCell

                    readonly property int inner: nameCell.width - 2 * Theme.space(8)
                    readonly property int tail: (categoryText.visible ? categoryText.implicitWidth
                                                                        + Theme.space(8) : 0)
                                                + (privateText.visible ? privateText.implicitWidth
                                                                         + Theme.space(8) : 0)
                    // #201 F13: the name keeps its floor before the words after it; the
                    // category gives way first, then "private".
                    readonly property int floor: Math.min(nameText.implicitWidth, Theme.tableNameFloor)
                    readonly property int categoryRoom: row.category.length > 0 ? categoryText.implicitWidth
                                                                                  + Theme.space(8) : 0
                    readonly property int privateRoom: row.isPrivate ? privateText.implicitWidth + Theme.space(8)
                                                                     : 0
                    readonly property bool privateFits: nameCell.inner - nameCell.privateRoom >= nameCell.floor
                    readonly property bool categoryFits: nameCell.inner - nameCell.privateRoom
                                                         - nameCell.categoryRoom >= nameCell.floor

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
                        // Board 46 `.tbl td.nm2`: the skin's name voice.
                        font.family: Theme.labelFamily
                        font.letterSpacing: Theme.font.body * Theme.nameTrack
                        font.pixelSize: Theme.font.body
                        font.weight: Theme.nameWeight
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
                        color: band.ink.dimForeground
                        font.family: Theme.valueFamily
                        font.pixelSize: Theme.font.small
                        objectName: "category_" + row.index
                        text: row.category
                        visible: !row.editing && row.category.length > 0 && nameCell.categoryFits
                    }

                    // docs/16 §5: the word "private", in magenta, after the category (or
                    // after the name when there is none).
                    Text {
                        id: privateText

                        anchors.left: categoryText.visible ? categoryText.right : nameText.right
                        anchors.leftMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: band.ink.magenta
                        font.family: Theme.valueFamily
                        font.pixelSize: Theme.font.small
                        objectName: "private_" + row.index
                        text: qsTr("private")
                        visible: !row.editing && row.isPrivate && nameCell.privateFits
                    }
                }

                Text {
                    id: sizeCell

                    color: band.textColor
                    elide: Text.ElideRight
                    font.family: Theme.valueFamily
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

                    // Dragged narrower than its percent (#52), the percent is cut at the edge.
                    clip: true
                    height: parent.height
                    visible: width > 0
                    width: root.columnLayout.progress.w
                    x: root.columnLayout.progress.x

                    // The line, the pulse and the ratio tracks are marks, not words: they keep
                    // the window's colours (DECISIONS C30 lifts text and glyphs only).
                    ProgressLine {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(8)
                        anchors.right: progressText.left
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme[row.stateRole]
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
                        color: band.ink.dimForeground
                        font.family: Theme.valueFamily
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
                // #74: its glyph and word are placed by spacing alone. A row built before the
                // theme had read the shell's base size kept the default's 8 / 12 / 6 px while
                // Theme.space() was a C++ call no binding could follow; it reads
                // Theme.spaceScale now (#156), so they follow the theme whenever it settles.
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
                        color: row.mark !== null ? row.markColor : row.stuckMove ? band.ink.stateError
                                                                                 : row.stateColor
                        height: Theme.font.body
                        objectName: "statusGlyph_" + row.index
                        pixelSize: Theme.font.body
                        text: row.mark !== null ? (row.mark.kind === "failed" ? "✕" : "✓")
                                                : row.stuckMove ? Glyphs.close : row.statusGlyph
                        width: Theme.space(12)
                    }

                    Text {
                        id: statusWord

                        anchors.left: statusGlyphText.right
                        anchors.leftMargin: Theme.space(6)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space(8)
                        anchors.verticalCenter: parent.verticalCenter
                        // Board 46 `.verb`, where the skin says: the caption voice in the
                        // state's colour (the cell's `color: col(t)`).
                        color: row.mark !== null ? row.markColor : row.stuckMove ? band.ink.stateError
                                                                                 : row.errorStopped
                                                                                   || Theme.verbCaption ? row.stateColor
                                                                                                        : band.textColor
                        elide: Text.ElideRight
                        font.capitalization: Theme.verbCaption && Theme.captionUpper ? Font.AllUppercase
                                                                                     : Font.MixedCase
                        font.family: Theme.verbCaption ? Theme.labelFamily : Theme.valueFamily
                        font.letterSpacing: Theme.verbCaption ? Theme.font.caption * Theme.captionTrack(0) : 0
                        font.pixelSize: Theme.verbCaption ? Theme.font.caption : Theme.font.body
                        font.weight: Theme.verbCaption ? Theme.captionWeight(Font.Normal) : Font.Normal
                        objectName: "status_" + row.index
                        text: row.mark !== null ? row.mark.text
                                                : root.statusWord(row.statusCause.length > 0 ? row.statusCause : row.stateText)
                    }
                }

                Text {
                    id: seedsPeersCell

                    color: band.textColor
                    elide: Text.ElideRight
                    font.family: Theme.valueFamily
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
                    id: dlspeedCell

                    color: band.textColor
                    elide: Text.ElideRight
                    font.family: Theme.valueFamily
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
                    id: upspeedCell

                    color: band.textColor
                    elide: Text.ElideRight
                    font.family: Theme.valueFamily
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
                    id: etaCell

                    color: band.textColor
                    elide: Text.ElideRight
                    font.family: Theme.valueFamily
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
                    id: optionalCells

                    model: root.textColumns

                    delegate: Text {
                        id: textCell

                        required property var modelData

                        readonly property var box: root.columnLayout[textCell.modelData.key]
                        readonly property var value: row.model[textCell.modelData.text]
                        readonly property bool isMove: textCell.modelData.key === "move"
                        readonly property var step: textCell.isMove && root.moveStepFor !== null
                                                    && root.moveRevision >= 0 ? root.moveStepFor(row.hash) : null
                        // #24 T3 and T4: the swarm and ratio columns draw a little more.
                        readonly property bool isSwarm: textCell.modelData.key === "swarm"
                        readonly property bool isRatio: textCell.modelData.key === "ratio"
                        readonly property bool complete: row.progress >= 1
                        readonly property int swarmSeeds: textCell.isSwarm && !row.model.hasTrackerError
                                                          ? Number(row.model.numComplete) : -1
                        readonly property bool rare: textCell.complete && textCell.swarmSeeds > 0
                                                     && textCell.swarmSeeds <= App.console.rareSeeds
                        readonly property real ratio: textCell.isRatio ? Math.max(0, Number(row.model.ratio)) : 0
                        readonly property real ratioGoal: {
                            const own = Number(row.model.ratioLimit);
                            return own > 1 ? own : 5;
                        }

                        color: textCell.step !== null && textCell.step !== undefined
                               ? (textCell.step.kind === "ok" ? band.ink.green : textCell.step.kind === "bad"
                                                                ? band.ink.red : textCell.step.kind === "busy"
                                                                  ? band.ink.accent : band.ink.dimForeground)
                               : textCell.rare ? band.ink.yellow
                                               : textCell.isRatio && textCell.ratio >= textCell.ratioGoal
                                                 ? band.ink.stateSeeding : band.textColor
                        // Board 42 M3: a save path is cut from the left, so the folder that
                        // differs stays in sight.
                        elide: textCell.modelData.key === "savePath" ? Text.ElideLeft : Text.ElideRight
                        font.family: Theme.valueFamily
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
                        text: textCell.isMove ? (textCell.step ? textCell.step.glyph + " " + textCell.step.text : "")
                                              : textCell.isSwarm ? (textCell.complete || textCell.swarmSeeds > 1
                                                  ? App.console.swarmText(textCell.swarmSeeds) : "")
                                               : textCell.value === undefined || textCell.value === null
                                                 ? "" : String(textCell.value)
                        verticalAlignment: Text.AlignVCenter
                        width: textCell.box.w
                        x: textCell.box.x

                        // T3: "only you" boxed in the rare colour.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            border.color: Qt.alpha(Theme.yellow, 0.4)
                            border.width: Theme.hairWidth
                            color: "transparent"
                            height: Theme.font.body + Theme.space(4)
                            visible: textCell.isSwarm && textCell.complete && textCell.swarmSeeds === 1
                            width: textCell.contentWidth + Theme.space(10)
                            x: textCell.width - textCell.rightPadding - textCell.contentWidth - Theme.space(5)
                        }

                        // T4: to 1.0 ("paid back"), then from 1.0 to the share limit (or 5).
                        // Left out of a row whose number would reach them.
                        Row {
                            id: ratioTracks

                            anchors.verticalCenter: parent.verticalCenter
                            objectName: textCell.isRatio ? "ratioTracks_" + row.index : ""
                            spacing: Theme.space(2)
                            visible: textCell.isRatio && textCell.complete
                                     && textCell.leftPadding + ratioTracks.implicitWidth + Theme.space(6)
                                        + textCell.contentWidth + textCell.rightPadding <= textCell.width
                            x: textCell.leftPadding

                            Rectangle {
                                color: Theme.fill(Theme.fillHover)
                                height: Theme.space(4)
                                width: Theme.space(20)

                                Rectangle {
                                    color: Theme.foreground
                                    height: parent.height
                                    opacity: 0.45
                                    width: parent.width * Math.min(1, textCell.ratio)
                                }
                            }

                            Rectangle {
                                color: Theme.fill(Theme.fillHover)
                                height: Theme.space(4)
                                width: Theme.space(16)

                                Rectangle {
                                    color: Theme.stateSeeding
                                    height: parent.height
                                    width: parent.width * Math.max(0, Math.min(1, (textCell.ratio - 1)
                                                                               / (textCell.ratioGoal - 1)))
                                }
                            }
                        }
                    }
                }
            }

            MouseArea {
                id: pointer

                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                anchors.fill: parent
                enabled: !row.editing
                hoverEnabled: true

                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        root.forceActiveFocus();
                        const point = pointer.mapToItem(root, mouse.x, mouse.y);
                        root.contextClick(row.index, point.x, point.y);
                        return;
                    }
                    // #51 D8: a middle-click toggles the row, as ctrl+click does.
                    if (mouse.button === Qt.MiddleButton) {
                        root.forceActiveFocus();
                        root.cursorIndex = row.index;
                        root.toggleSelection(row.index);
                        return;
                    }
                    if (mouse.button === Qt.LeftButton && mouse.x < root.tickWidth)
                        row.tickClick(mouse.modifiers);
                    else
                        row.click(mouse.modifiers);
                }
                // The press before it already put the cursor and the selection on the row.
                onDoubleClicked: mouse => {
                    // Two quick ticks are two ticks, not an open.
                    if (mouse.button === Qt.LeftButton && mouse.x < root.tickWidth) {
                        row.tickClick(mouse.modifiers);
                        return;
                    }
                    if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ShiftModifier
                                                                              | Qt.ControlModifier)))
                        root.activated(row.hash);
                }
                onExited: root.hideNameTip()
                onPositionChanged: mouse => {
                    const text = row.elidedTextAt(mouse.x);
                    if (text.length === 0) {
                        root.hideNameTip();
                        return;
                    }
                    const at = pointer.mapToItem(root, mouse.x, mouse.y);
                    root.showNameTip(at.x, at.y, text);
                }
            }

            // The inline rename editor, over the row rather than inside the clipped Name cell
            // (batch-2 J5): it starts where the name does and widens across the next columns
            // to fit the whole name, up to the right edge of what the table shows.
            Loader {
                id: renameLoader

                /// The right edge of the table's visible part, in the row's coordinates.
                readonly property real room: list.contentX + list.width - Theme.space(8) - x

                // A face draws its own editor, or none (#168).
                active: row.editing && root.face === null
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.rowHeight - Theme.space(8)
                objectName: "renameEditor"
                sourceComponent: renameEditor
                width: Math.max(root.columnLayout.name.w - 2 * Theme.space(8), Math.min(
                                    renameLoader.implicitWidth, renameLoader.room))
                x: root.columnLayout.name.x + Theme.space(8)
            }
        }
    }

    // The list's empty space, below its last row and in the `bottomRoom` under the
    // selection bar: a click there clears the selection, as it does in a file manager, which
    // also puts the selection bar away. The cursor stays where it is.
    // a11y-ignore A2: empty space, not a control; esc clears the selection too (#77)
    MouseArea {
        id: emptySpace

        /// Where the rows end, in the list's coordinates.
        readonly property real rowsEnd: list.originY + list.contentHeight - list.contentY

        height: Math.max(0, list.y + list.height - emptySpace.y)
        objectName: "emptySpace"
        width: list.width
        visible: root.face === null
        x: list.x
        y: list.y + Math.max(0, Math.min(list.height, emptySpace.rowsEnd))
        z: 1

        onClicked: {
            // Taking the focus also commits an open rename (the editor's focus loss).
            root.forceActiveFocus();
            root.clearSelection();
        }
    }

    ScrollBar {
        id: sideways

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        flickable: list
        objectName: "sideways"
        visible: root.face === null && sideways.needed
    }

    // A wheel notch moves three rows, and quick notches add up (Kit/WheelScroll).
    WheelScroll {
        anchors.fill: list
        enabled: root.face === null
        flickable: list
        objectName: "tableWheel"
        z: 2
    }

    // #51 D9: while the pointer is over the rows, they keep their order, so a row sorted by a
    // live value (speed) is still the one under the pointer when the click comes. Values go on
    // changing; torrents still come and go; a new sort or filter applies at once
    // (TorrentFilterModel::holdOrder). Leaving the rows lets go: one re-sort. The keyboard
    // holds nothing. The HoverHandler (`rowsHover`) is the list's own, not on an item over the
    // rows: there it left the rows' MouseAreas hovered for good after a click. Delayed to the
    // end of the event: D17's right-click on the menu's scrim closes the menu (the host's hold
    // goes) and opens it again on the row under the point, which must not move in between.
    Binding {
        delayed: true
        property: "holdOrder"
        target: root.filtered
        value: root.orderHeld
    }

    // Where the rows are in the list: the rail's "scrollbar" look (Kit/ScrollHint), whatever
    // the Scroll hint setting says, since a band or a fade would cover rows the cursor moves
    // through. It shows while the list moves and while the pointer is on it, and drags.
    ScrollHint {
        anchors.bottom: list.bottom
        anchors.left: list.left
        anchors.right: list.right
        anchors.top: list.top
        flickable: list
        objectName: "tableScrollHint"
        revealOnHover: false
        style: "scrollbar"
        visible: root.face === null
        z: 3
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
    // 8 u, the whole name selected at a 35 % accent highlight. Opaque, because it lies over the
    // next columns while it is wider than the Name cell; it opens on the start of the name
    // (batch-2 J5: it used to open scrolled to the end, `…6.09.01-x86_64.iso`).
    Component {
        id: renameEditor

        // The field's fill over the window, edged in the accent: the skin's Plate (#171), with
        // its small cut.
        Item {
            implicitWidth: editor.contentWidth + 2 * Theme.space(6) + Theme.space(12)

            Plate {
                anchors.fill: parent
                base: Theme.background
                edgeColor: Theme.accent
                edgeWidth: Theme.hairWidth
                fill: Theme.on.idle.fill
                key: true
                objectName: "renamePlate"
                radius: 0
            }

            // The name on the field's idle fill, its selected part on the 35 % accent
            // highlight over that (docs/DECISIONS.md C30 F1, F5).
            TextInput {
                id: editor

                readonly property color highlight: Theme.on.highlight.fill

                // #91: the inline rename's field, by what it edits.
                Accessible.name: qsTr("torrent name")
                Accessible.role: Accessible.EditableText

                anchors.fill: parent
                anchors.leftMargin: Theme.space(6)
                anchors.rightMargin: Theme.space(6)
                clip: true
                color: Theme.on.idle.brightForeground
                font.family: Theme.valueFamily
                font.pixelSize: Theme.font.body
                selectByMouse: true
                selectedTextColor: Theme.on.highlight.brightForeground
                selectionColor: editor.highlight
                text: root.renameText
                verticalAlignment: TextInput.AlignVCenter

                Keys.onEscapePressed: root.cancelRename()
                onAccepted: root.commitRename()
                // A click away (another row, the empty space, the rail) commits, as Nautilus
                // does; esc and enter close the editor before its focus goes, so they do not
                // pass here. Losing the focus to another window leaves the rename open.
                onActiveFocusChanged: {
                    if (!editor.activeFocus && root.renaming && editor.Window.active)
                        root.commitRename();
                }
                onTextChanged: root.renameText = editor.text

                Component.onCompleted: {
                    // Everything selected, with the cursor at the start so the field shows the
                    // beginning of the name rather than scrolling to its end.
                    editor.select(editor.length, 0);
                    editor.forceActiveFocus();
                }
            }
        }
    }
}
