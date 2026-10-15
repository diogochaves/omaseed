import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// T3.3. Every case runs against the demo library (spec v2 §1 Fig. 1's sixteen rows), which
// the table sorts by `dlspeed` descending the way the figure's header does — so row 0 is the
// figure's first row and its cells can be compared word for word. `init()` puts the sort, the
// cursor, the selection and the column set back, because the proxy model is the one `App`
// singleton the whole runner shares.
TestCase {
    id: testCase

    // Typed probes: `findChild()` hands back a QObject, and going through a typed property is
    // what lets qmllint check the member access.
    property TableHeader headerProbe: null
    property Text cellProbe: null
    // The status cell is a `Glyph` (text or SVG icon), not a `Text`.
    property Glyph glyphProbe: null
    property ListView rowsProbe: null
    property TorrentTable spare: null
    property MouseArea gripProbe: null

    readonly property list<string> allColumns: ["name", "size", "progress", "status", "seedsPeers",
        "dlspeed", "upspeed", "eta"]

    height: 600
    name: "TorrentTable"
    visible: true
    when: windowShown
    width: 1200

    TorrentTable {
        id: table

        anchors.fill: parent
        filtered: App.filtered
    }

    TextMetrics {
        id: widest

        text: "9998.99"
    }

    SignalSpy {
        id: renameSpy

        signalName: "renameRequested"
        target: table
    }

    SignalSpy {
        id: actionsSpy

        signalName: "actionsRequested"
        target: table
    }

    SignalSpy {
        id: escapeSpy

        signalName: "escapePressed"
        target: table
    }

    SignalSpy {
        id: railSpy

        signalName: "focusRail"
        target: table
    }

    SignalSpy {
        id: detailsSpy

        signalName: "focusDetails"
        target: table
    }

    SignalSpy {
        id: activatedSpy

        signalName: "activated"
        target: table
    }

    SignalSpy {
        id: columnsSpy

        signalName: "columnsRequested"
        target: table
    }

    Component {
        id: tableComponent

        TorrentTable {
            height: 600
            width: 1200
        }
    }

    function fixedWidth(widths: var): int {
        let total = 0;
        for (const width of widths)
            total += Theme.space(width);
        return total;
    }

    function cellText(objectName: string): string {
        testCase.cellProbe = findChild(table, objectName);
        verify(testCase.cellProbe, "no cell " + objectName);
        return testCase.cellProbe.text;
    }

    function header(key: string): TableHeader {
        testCase.headerProbe = findChild(table, "header_" + key);
        return testCase.headerProbe;
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function init() {
        table.cancelRename();
        table.clearSelection();
        table.visibleColumns = testCase.allColumns;
        table.resetColumnOrder();
        table.resetColumnWidths();
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        table.moveCursorTo(0);
        renameSpy.clear();
        actionsSpy.clear();
        escapeSpy.clear();
        railSpy.clear();
        detailsSpy.clear();
        // Capped: when the test before left nothing to repaint no frame comes, and the
        // default 5 s wait would run out for nothing.
        waitForRendering(table, 250);
    }

    function test_row_count_follows_the_filter_model() {
        testCase.rowsProbe = findChild(table, "rows");
        verify(testCase.rowsProbe);
        compare(testCase.rowsProbe.count, App.filtered.count);
        compare(testCase.rowsProbe.count, 16);
    }

    // "missing files" does not fit the Status column: the table says "missing", with the
    // errored glyph; every other state word passes through.
    function test_missing_files_is_short_in_the_status_column() {
        compare(table.statusWord("missing files"), "missing");
        compare(table.statusWord("downloading"), "downloading");
        compare(table.statusWord("error"), "error");
        compare(table.statusGlyphs["missing files"], table.statusGlyphs["error"]);
    }

    function test_header_titles_and_widths_match_the_spec() {
        compare(header("name").text, "Name");
        compare(header("size").text, "Size");
        compare(header("progress").text, "Progress");
        compare(header("status").text, "Status");
        compare(header("seedsPeers").text, "S / P");
        compare(header("dlspeed").text, Glyphs.arrowDown);
        compare(header("upspeed").text, Glyphs.arrowUp);
        compare(header("eta").text, "ETA");

        // `grid-template-columns: minmax(0,1fr) 72px 118px 104px 62px 84px 84px 62px`, status widened to 116 (Q17).
        compare(header("size").width, Theme.space(72));
        compare(header("progress").width, Theme.space(118));
        compare(header("status").width, Theme.space(116));
        compare(header("seedsPeers").width, Theme.space(62));
        compare(header("dlspeed").width, Theme.space(84));
        compare(header("upspeed").width, Theme.space(84));
        compare(header("eta").width, Theme.space(62));
        // The Name column takes the rest. Each column is rounded on its own, so the sum is
        // of the rounded widths, not `Theme.space()` of the sum.
        const fixed = testCase.fixedWidth([72, 118, 116, 62, 84, 84, 62]);
        compare(header("name").width, table.width - fixed);
    }

    // docs/10 §4 (Diogo's ruling): a table too narrow for its columns keeps every one of them
    // and scrolls sideways; Name never goes under `minimumNameWidth`.
    function test_a_narrow_table_scrolls_instead_of_squeezing() {
        const fixed = testCase.fixedWidth([72, 118, 116, 62, 84, 84, 62]);
        table.anchors.fill = undefined;
        table.width = 600;
        table.height = 400;
        compare(table.rowWidth, fixed + Theme.space(table.minimumNameWidth));
        compare(header("name").width, Theme.space(table.minimumNameWidth));
        compare(header("eta").width, Theme.space(62));
        const bar = findChild(table, "sideways");
        verify(bar.needed);
        const list = findChild(table, "rows");
        table.scrollSideways(100000);
        compare(list.contentX, table.rowWidth - list.width);
        table.scrollSideways(-100000);
        compare(list.contentX, 0);
        table.anchors.fill = testCase;
    }

    // docs/10 §4: many more columns, off until the palette turns them on.
    function test_the_optional_columns_start_hidden() {
        compare(table.visibleColumns, testCase.allColumns);
        verify(table.columns.length > 20);
        compare(findChild(table, "header_addedOn"), null);
    }

    function test_turning_on_added_shows_its_header_and_cells() {
        const saved = table.visibleColumns.slice();
        table.visibleColumns = saved.concat(["addedOn", "ratio"]);
        compare(header("addedOn").text, "Added");
        compare(header("addedOn").width, Theme.space(112));
        const first = App.filtered.index(0, 0);
        const added = App.filtered.data(first, TorrentModel.AddedOnShortTextRole);
        verify(added.length > 0);
        compare(cellText("addedOn_0"), added);
        compare(cellText("ratio_0"), App.filtered.data(first, TorrentModel.RatioTextRole));
        table.visibleColumns = saved;
    }

    // Diogo's desk: "105.78" ran into the tracks before it. They keep a gap from the number,
    // and the widest ratio shown ("9998.99"; ∞ above it) fits beside them.
    function test_the_ratio_keeps_clear_of_its_tracks() {
        const saved = table.visibleColumns.slice();
        table.visibleColumns = saved.concat(["ratio"]);
        let checked = 0;
        for (let i = 0; i < App.filtered.rowCount(); i++) {
            const tracks = findChild(table, "ratioTracks_" + i);
            if (!tracks || !tracks.visible)
                continue;
            cellText("ratio_" + i);
            const cell = testCase.cellProbe;
            verify(tracks.x + tracks.width + Theme.space(6)
                   <= cell.width - cell.rightPadding - cell.contentWidth, "row " + i);
            checked += 1;
        }
        verify(checked > 0, "no complete row shows its tracks");
        widest.font = testCase.cellProbe.font;
        verify(Theme.space(8) + findChild(table, "ratioTracks_0").implicitWidth + Theme.space(6)
               + widest.advanceWidth + Theme.space(8) <= testCase.cellProbe.width,
               "9998.99 is " + widest.advanceWidth + " wide");
        table.visibleColumns = saved;
    }

    function test_a_wide_table_does_not_scroll() {
        compare(table.rowWidth, table.width);
        verify(!findChild(table, "sideways").needed);
    }

    function test_first_row_shows_the_figure_texts() {
        compare(cellText("name_0"), "archlinux-2026.09.01-x86_64.iso");
        compare(cellText("category_0"), "linux");
        compare(cellText("size_0"), "1.31 GB");
        compare(cellText("progress_0"), "48.9%");
        compare(cellText("status_0"), "downloading");
        compare(cellText("seedsPeers_0"), "21 / 42");
        compare(cellText("dlspeed_0"), "1.84 MB/s");
        compare(cellText("upspeed_0"), "112 KB/s");
        compare(cellText("eta_0"), "2m 40s");
    }

    function test_the_name_elides_and_keeps_the_category_visible() {
        testCase.cellProbe = findChild(table, "name_4");
        verify(testCase.cellProbe);
        compare(testCase.cellProbe.elide, Text.ElideRight);
        testCase.cellProbe = findChild(table, "category_4");
        verify(testCase.cellProbe);
        compare(testCase.cellProbe.color, Theme.dimForeground);
        compare(testCase.cellProbe.font.pixelSize, Theme.font.small);
    }

    // docs/16 §5: "private", in magenta, after the category of a private torrent only (the
    // demo's movies, tv and music rows).
    function test_private_torrents_say_so_after_the_category() {
        let seen = 0;
        for (let i = 0; i < 16; ++i) {
            testCase.cellProbe = findChild(table, "category_" + i);
            if (!testCase.cellProbe)
                continue; // below the fold: no delegate
            const category = testCase.cellProbe.visible ? testCase.cellProbe.text : "";
            testCase.cellProbe = findChild(table, "private_" + i);
            verify(testCase.cellProbe, "no private marker cell on row " + i);
            const expected = ["movies", "tv", "music"].indexOf(category) >= 0;
            compare(testCase.cellProbe.visible, expected, "row " + i + " (" + category + ")");
            if (expected) {
                ++seen;
                compare(testCase.cellProbe.text, "private");
                compare(testCase.cellProbe.color, Theme.magenta);
            }
        }
        verify(seen > 0, "no private row drawn");
    }

    function test_status_cell_uses_the_state_glyph_and_colour() {
        testCase.glyphProbe = findChild(table, "statusGlyph_0");
        verify(testCase.glyphProbe);
        compare(testCase.glyphProbe.text, Glyphs.statusDownloading);
        compare(testCase.glyphProbe.color, Theme.stateDownloading);

        let seeding = -1;
        for (let row = 0; row < App.filtered.count; ++row) {
            if (cellText("status_" + row) === "seeding") {
                seeding = row;
                break;
            }
        }
        verify(seeding >= 0, "no seeding row in the demo library");
        testCase.glyphProbe = findChild(table, "statusGlyph_" + seeding);
        compare(testCase.glyphProbe.text, Glyphs.statusSeeding);
        compare(testCase.glyphProbe.color, Theme.stateSeeding);
    }

    // docs/28 R1: an errored row says the short cause from the server's log, not "error".
    // The demo's errored torrent comes with a "No space left on device" line.
    function test_errored_row_shows_the_cause() {
        let errored = -1;
        for (let row = 0; row < App.filtered.count; ++row) {
            if (App.torrentRow(App.filtered.hashAt(row)).stateText === "error") {
                errored = row;
                break;
            }
        }
        verify(errored >= 0, "no errored row in the demo library");
        compare(cellText("status_" + errored), "disk full");
        testCase.glyphProbe = findChild(table, "statusGlyph_" + errored);
        compare(testCase.glyphProbe.text, Glyphs.statusErrored);
        compare(testCase.glyphProbe.color, Theme.stateError);
    }

    function test_move_cursor_moves_the_cursor_hash() {
        compare(table.cursorIndex, 0);
        const first = table.cursorHash;
        verify(first.length > 0);
        table.moveCursor(1);
        compare(table.cursorIndex, 1);
        verify(table.cursorHash !== first);
        compare(table.cursorHash, App.filtered.hashAt(1));

        // Clamped at both ends.
        table.moveCursor(-5);
        compare(table.cursorIndex, 0);
        table.moveCursor(1000);
        compare(table.cursorIndex, App.filtered.count - 1);
    }

    function test_space_and_range_select_two_rows() {
        compare(table.selectedCount, 0);
        table.toggleCursorSelection();
        compare(table.selectedCount, 1);
        table.moveCursor(1);
        table.selectRangeToCursor();
        compare(table.selectedCount, 2);
        const hashes = table.selectedHashes();
        compare(hashes.length, 2);
        compare(hashes[0], App.filtered.hashAt(0));
        compare(hashes[1], App.filtered.hashAt(1));
    }

    // The `ItemSelectionModel` tracks the proxy's `layoutChanged`, so a selection made
    // before a re-sort still names the same torrents afterwards.
    function test_selection_survives_a_resort() {
        table.toggleCursorSelection();
        table.moveCursor(1);
        table.toggleCursorSelection();
        const before = table.selectedHashes();
        compare(before.length, 2);
        table.sortBy("name");
        const after = table.selectedHashes();
        compare(after.length, 2);
        compare(after.indexOf(before[0]) >= 0, true);
        compare(after.indexOf(before[1]) >= 0, true);
    }

    function test_select_all_and_clear() {
        table.selectAll();
        compare(table.selectedCount, 16);
        table.clearSelection();
        compare(table.selectedCount, 0);
    }

    function test_sort_by_size_reorders_and_reverses() {
        const before = App.filtered.hashAt(0);
        table.sortBy("size");
        compare(App.filtered.sortRoleName, "size");
        compare(table.sortRoleName, "size");
        compare(App.filtered.sortOrder, Qt.DescendingOrder);
        verify(App.filtered.hashAt(0) !== before);
        compare(cellText("size_0"), "81.4 GB");

        table.sortBy("size");
        compare(App.filtered.sortOrder, Qt.AscendingOrder);
        compare(cellText("size_0"), "330 MB");
    }

    function test_sorted_header_shows_the_chevron() {
        compare(header("dlspeed").sorted, true);
        compare(header("dlspeed").descending, true);
        compare(header("size").sorted, false);
        table.sortBy("size");
        compare(header("size").sorted, true);
        compare(header("dlspeed").sorted, false);
    }

    function test_cursor_follows_its_torrent_across_a_resort() {
        table.moveCursorTo(3);
        const hash = table.cursorHash;
        table.sortBy("name");
        compare(table.cursorHash, hash);
        compare(table.cursorIndex, App.filtered.rowOfHash(hash));
    }

    function test_rename_commits_the_edited_text() {
        compare(table.renaming, false);
        table.startRename();
        compare(table.renaming, true);
        compare(table.renameText, "archlinux-2026.09.01-x86_64.iso");
        table.renameText = "archlinux-renamed.iso";
        table.commitRename();
        compare(table.renaming, false);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][0], App.filtered.hashAt(0));
        compare(renameSpy.signalArguments[0][1], "archlinux-renamed.iso");
    }

    // Batch-2 J5: the editor widens over the next columns to show the whole name, opens on
    // its start with all of it selected, and goes away on enter or esc.
    function test_rename_widens_and_opens_on_the_start_of_the_name() {
        table.startRename();
        const loader = findChild(table, "renameEditor");
        verify(loader !== null && loader.item !== null);
        const name = table.renameText;
        const input = loader.item.children[1];
        compare(input.selectedText, name);
        compare(input.cursorPosition, 0);
        verify(loader.width >= input.contentWidth, "the name does not fit the editor");

        // A name longer than the Name cell takes the next columns, never past the table.
        input.text = name + name + name + name;
        verify(loader.width > table.columnLayout.name.w, "the editor stayed inside the Name cell");
        verify(loader.x + loader.width <= table.width, "the editor ran past the table");
        table.cancelRename();
        compare(table.renaming, false);
        compare(loader.item, null);
    }

    // Review VIEW-18: trimmed like the add sheet's Name; an unchanged name is not sent.
    function test_rename_trims_and_skips_an_unchanged_name() {
        table.startRename();
        table.renameText = "  archlinux-trimmed.iso ";
        table.commitRename();
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][1], "archlinux-trimmed.iso");

        table.startRename();
        table.renameText = table.renameText + " ";
        table.commitRename();
        compare(renameSpy.count, 1, "an unchanged name went to the server");
    }

    function test_rename_cancel_emits_nothing() {
        table.startRename();
        table.renameText = "ignored";
        table.cancelRename();
        compare(table.renaming, false);
        compare(renameSpy.count, 0);
    }

    function test_escape_clears_then_reports() {
        table.forceActiveFocus();
        verify(table.activeFocus);
        table.selectAll();
        keyClick(Qt.Key_Escape);
        compare(table.selectedCount, 0);
        compare(escapeSpy.count, 0);
        keyClick(Qt.Key_Escape);
        compare(escapeSpy.count, 1);
    }

    // Key delivery: `keyClick()` reaches a focused Item in the offscreen runner (mouse clicks
    // do not, see T1.3). This is the proof for the whole map, which goes through one handler.
    function test_keys_drive_the_cursor_and_the_selection() {
        table.forceActiveFocus();
        verify(table.activeFocus);
        compare(table.cursorIndex, 0);
        keyClick(Qt.Key_J);
        compare(table.cursorIndex, 1);
        keyClick(Qt.Key_K);
        compare(table.cursorIndex, 0);
        keyClick(Qt.Key_G, Qt.ShiftModifier);
        compare(table.cursorIndex, App.filtered.count - 1);
        keyClick(Qt.Key_G);
        compare(table.cursorIndex, 0);
        keyClick(Qt.Key_Space);
        compare(table.selectedCount, 1);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_V);
        compare(table.selectedCount, 2);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(table.selectedCount, 16);
        keyClick(Qt.Key_R);
        compare(table.renaming, true);
    }

    function test_hiding_a_column_removes_its_header_and_cell() {
        verify(header("eta"));
        table.visibleColumns = ["name", "size", "progress", "status", "seedsPeers", "dlspeed",
                                "upspeed"];
        waitForRendering(table);
        compare(header("eta"), null);
        testCase.cellProbe = findChild(table, "eta_0");
        compare(testCase.cellProbe.visible, false);
        // The Name column grows by the hidden column's width.
        const fixed = testCase.fixedWidth([72, 118, 116, 62, 84, 84]);
        compare(header("name").width, table.width - fixed);
    }

    // ---- column order (N2) ------------------------------------------------------------
    function test_the_column_order_starts_as_declared() {
        compare(table.columnOrder, table.columns.map(column => column.key));
        verify(table.columnOrderIsDefault);
        compare(table.activeColumns.map(column => column.key), testCase.allColumns);
    }

    function test_moving_a_column_moves_its_header_and_its_cells() {
        verify(header("eta").x > header("upspeed").x);
        verify(table.moveColumn("eta", -1, testCase.allColumns));
        waitForRendering(table);
        compare(table.activeColumns.map(column => column.key), ["name", "size", "progress",
                                                                "status", "seedsPeers", "dlspeed",
                                                                "eta", "upspeed"]);
        verify(header("eta").x < header("upspeed").x);
        testCase.cellProbe = findChild(table, "eta_0");
        compare(testCase.cellProbe.x, header("eta").x);
        verify(!table.columnOrderIsDefault);

        table.resetColumnOrder();
        verify(table.columnOrderIsDefault);
        verify(header("eta").x > header("upspeed").x);
    }

    function test_name_is_pinned_first() {
        verify(!table.canMoveColumn("name"));
        verify(!table.moveColumn("name", 1, null));
        verify(!table.moveColumn("size", -1, null), "size moved in front of Name");
        compare(table.columnOrder[0], "name");
        // Before Name is read as right after it.
        verify(table.placeColumn("eta", "name", false));
        compare(table.columnOrder.slice(0, 3), ["name", "eta", "size"]);
    }

    // qui-style: a header cell dragged along the header moves its column, and the drag does
    // not also sort by it.
    function test_dragging_a_header_cell_moves_its_column() {
        const eta = header("eta");
        const size = header("size");
        mouseDrag(eta, eta.width / 2, eta.height / 2, size.x - eta.x - eta.width / 2 + 5, 0,
                  Qt.LeftButton, Qt.NoModifier, 20);
        compare(table.activeColumns.map(column => column.key).slice(0, 3), ["name", "eta",
                                                                            "size"]);
        compare(App.filtered.sortRoleName, "dlspeed", "the drag also sorted");
        compare(findChild(table, "dropMarker").visible, false);
    }

    // M13: mid-drag, a ghost of the cell rides under the pointer where it was grabbed, a
    // 1 px line marks the landing, and every header cell shows the closed hand; all of it
    // goes on release.
    function test_a_header_drag_carries_a_ghost_and_marks_the_landing() {
        const eta = header("eta");
        const size = header("size");
        const grab = eta.width / 2;
        const y = eta.height / 2;
        mousePress(eta, grab, y);
        const target = size.x - eta.x + 5;
        const steps = 10;
        for (let i = 1; i <= steps; ++i)
            mouseMove(eta, grab + (target - grab) * i / steps, y);

        const ghost = findChild(table, "dragGhost");
        verify(ghost.visible, "no ghost mid-drag");
        compare(ghost.width, eta.width);
        verify(Math.abs(ghost.x + grab - (eta.x + target)) <= 1,
               "the ghost is not under the pointer: " + ghost.x);
        const marker = findChild(table, "dropMarker");
        verify(marker.visible, "no landing line");
        compare(marker.width, 1);
        compare(marker.x, size.x, "the line is not on Size's left edge");
        verify(marker.z > ghost.z, "the ghost hides the landing line");
        verify(size.dragging && eta.dragging, "the header cells do not show the drag");

        mouseRelease(eta, target, y);
        verify(!ghost.visible, "the ghost stayed after the drop");
        verify(!size.dragging);
        compare(table.activeColumns.map(column => column.key).slice(0, 3), ["name", "eta",
                                                                            "size"]);
        compare(App.filtered.sortRoleName, "dlspeed", "the drag also sorted");
    }

    function test_a_header_click_still_sorts_and_name_does_not_drag() {
        const size = header("size");
        mouseClick(size, size.width / 2, size.height / 2);
        compare(App.filtered.sortRoleName, "size");

        const name = header("name");
        mouseDrag(name, name.width / 2, name.height / 2, 300, 0, Qt.LeftButton, Qt.NoModifier,
                  20);
        verify(!findChild(table, "dragGhost").visible);
        compare(table.columnOrder[0], "name");
        verify(table.columnOrderIsDefault, "dragging Name moved a column");
    }

    // A saved order is repaired, not trusted: Name first, no strangers or repeats, and a
    // column it does not know yet goes back beside its default neighbour.
    function test_a_saved_order_is_made_whole() {
        const all = table.columns.map(column => column.key);
        const order = table.normalizedColumnOrder(["eta", "bogus", "name", "eta", "size"]);
        compare(order.slice(0, 2), ["name", "eta"]);
        verify(order.indexOf("size") < order.indexOf("progress"));
        compare(order.length, all.length);
        compare(order.slice().sort(), all.slice().sort());

        const withoutHash = all.filter(key => key !== "hash");
        compare(table.normalizedColumnOrder(withoutHash), all);
        compare(table.normalizedColumnOrder([]), all);
        compare(table.normalizedColumnOrder(undefined), all);
    }

    // ---- column widths (#52) -----------------------------------------------------------
    function grip(key: string): MouseArea {
        testCase.gripProbe = findChild(table, "grip_" + key);
        verify(testCase.gripProbe, "no grip for " + key);
        return testCase.gripProbe;
    }

    /// Drag `key`'s grip sideways by `dx` pixels, in steps, the way a hand would. The grip
    /// follows the edge it resizes, so the pointer is placed in the table's coordinates.
    function dragGrip(key: string, dx: real): void {
        const handle = grip(key);
        const at = handle.mapToItem(table, handle.width / 2, handle.height / 2);
        mousePress(table, at.x, at.y);
        const steps = 10;
        for (let i = 1; i <= steps; ++i)
            mouseMove(table, at.x + dx * i / steps, at.y);
        mouseRelease(table, at.x + dx, at.y);
    }

    // Nothing in the header changes until the pointer is on a grip: then the resize cursor
    // and a mark on the boundary.
    function test_a_grip_sits_on_the_right_edge_and_shows_only_under_the_pointer() {
        const size = header("size");
        const handle = grip("size");
        compare(handle.x + handle.width / 2, size.x + size.width);
        compare(handle.cursorShape, Qt.SplitHCursor);
        const mark = findChild(handle, "gripMark");
        mouseMove(header("progress"), header("progress").width / 2, size.height / 2);
        verify(!mark.visible, "the grip shows with the pointer away");
        mouseMove(handle, handle.width / 2, handle.height / 2);
        verify(mark.visible, "the grip does not show under the pointer");
        mouseMove(header("progress"), header("progress").width / 2, size.height / 2);
        verify(!mark.visible);
    }

    // A drag on the grip widens its column by as much as the pointer went; Name gives up the
    // room. It neither sorts nor moves the column, and the width is kept in base-12 units.
    function test_dragging_a_grip_resizes_its_column() {
        const before = header("size").width;
        const name = header("name").width;
        dragGrip("size", 40);
        verify(Math.abs(header("size").width - (before + 40)) <= 1, "Size is "
               + header("size").width);
        verify(Math.abs(header("name").width - (name - 40)) <= 1);
        compare(table.columnWidths.size, Math.round(table.unitsOf(before + 40)));
        compare(header("size").width, Theme.space(table.columnWidths.size));
        testCase.cellProbe = findChild(table, "size_0");
        compare(testCase.cellProbe.width, header("size").width);
        compare(App.filtered.sortRoleName, "dlspeed", "the drag also sorted");
        verify(table.columnOrderIsDefault, "the drag moved the column");
        verify(!table.columnWidthsAreDefault);
        verify(!table.columnLayoutIsDefault);
    }

    // A press that does not travel changes nothing, and a click on the grip does not sort.
    function test_a_click_on_a_grip_changes_nothing() {
        const handle = grip("size");
        mouseClick(handle, handle.width / 2, handle.height / 2);
        verify(table.columnWidthsAreDefault);
        compare(App.filtered.sortRoleName, "dlspeed");
    }

    // Every column but Name stops at `minimumColumnWidth`; Name never goes under its own
    // minimum, and its grip only sets the least it gets: with room it still takes the rest.
    function test_columns_keep_their_minimum_and_name_keeps_its_own() {
        dragGrip("size", -2000);
        compare(header("size").width, Theme.space(table.minimumColumnWidth));
        compare(table.columnWidths.size, table.minimumColumnWidth);

        table.setColumnWidth("eta", 2);
        compare(header("eta").width, Theme.space(table.minimumColumnWidth));
        table.setColumnWidth("eta", 99999);
        compare(table.columnWidths.eta, table.maximumColumnWidth);

        const rest = header("name").width;
        dragGrip("name", -2000);
        compare(header("name").width, rest, "Name gave up room nobody else takes");
        verify(table.columnWidths.name === undefined, "Name kept a width under its minimum");
        table.setColumnWidth("name", 10);
        verify(table.columnWidths.name === undefined);

        // Its least past the room there is: the rows grow and scroll sideways.
        dragGrip("name", 100);
        verify(Math.abs(header("name").width - (rest + 100)) <= 1);
        verify(table.rowWidth > table.width);
        verify(findChild(table, "sideways").needed);
    }

    // Too narrow for its text, a cell elides and the hover label offers the whole of it;
    // the header's caption elides too, instead of running into the next one.
    function test_a_narrow_column_elides_its_cells_and_caption() {
        table.setColumnWidth("size", table.minimumColumnWidth);
        table.setColumnWidth("status", table.minimumColumnWidth);
        waitForRendering(table, 250);
        cellText("size_0");
        verify(testCase.cellProbe.truncated, "Size did not elide at its minimum");
        const caption = findChild(header("status"), "caption");
        verify(caption.truncated, "the Status caption runs past its cell");
        verify(caption.x + caption.width <= header("status").width);
        const row = findChild(table, "row_0");
        const box = table.columnLayout.size;
        compare(row.elidedTextAt(box.x + 2), App.filtered.data(App.filtered.index(0, 0),
                                                               TorrentModel.SizeTextRole));
    }

    // A double-click on a grip fits its column to the widest cell on screen and the caption,
    // so none of them elides; it works from a narrower or a wider width alike.
    function test_a_double_click_on_a_grip_fits_the_widest_visible_cell() {
        for (const key of ["size", "status", "eta", "dlspeed"]) {
            table.setColumnWidth(key, table.minimumColumnWidth);
            waitForRendering(table, 250);
            const handle = grip(key);
            mouseDoubleClickSequence(handle, handle.width / 2, handle.height / 2);
            waitForRendering(table, 250);
            let widest = header(key).fitWidth;
            const rows = findChild(table, "rows");
            for (let i = 0; i < App.filtered.count; ++i) {
                const row = findChild(table, "row_" + i);
                if (row === null || row.y >= rows.contentY + rows.height)
                    continue;
                widest = Math.max(widest, row.fitWidth(key));
                if (key === "status")
                    continue;
                cellText(key + "_" + i);
                verify(!testCase.cellProbe.truncated, key + " " + i + " still elides");
            }
            verify(header(key).width >= widest, key + " is " + header(key).width + ", needs "
                   + widest);
            verify(header(key).width <= widest + Theme.space(2), key + " is wider than it needs");
        }
        // The status word: the widest of the demo's is "downloading" (Q17).
        for (let i = 0; i < 8; ++i)
            verify(!findChild(table, "status_" + i).truncated, "status " + i + " elides");

        // From wider than needed, it narrows to fit.
        table.setColumnWidth("eta", 400);
        mouseDoubleClickSequence(grip("eta"), grip("eta").width / 2, grip("eta").height / 2);
        verify(header("eta").width < Theme.space(400));
    }

    // #24 T4: fitted, a complete row's ratio keeps its tracks and the gap before them.
    function test_a_fitted_ratio_keeps_its_tracks() {
        table.visibleColumns = testCase.allColumns.concat(["ratio"]);
        table.setColumnWidth("ratio", table.minimumColumnWidth);
        waitForRendering(table, 250);
        verify(!findChild(table, "ratioTracks_0").visible, "the tracks crowd a narrow ratio");
        table.fitColumn("ratio");
        waitForRendering(table, 250);
        let shown = 0;
        for (let i = 0; i < App.filtered.count; ++i) {
            const tracks = findChild(table, "ratioTracks_" + i);
            if (!tracks)
                continue;
            const complete = App.filtered.data(App.filtered.index(i, 0), TorrentModel.ProgressRole) >= 1;
            if (!complete)
                continue;
            verify(tracks.visible, "row " + i + " lost its tracks");
            cellText("ratio_" + i);
            verify(tracks.x + tracks.width + Theme.space(6)
                   <= testCase.cellProbe.width - testCase.cellProbe.rightPadding
                   - testCase.cellProbe.contentWidth, "row " + i);
            shown += 1;
        }
        verify(shown > 0, "no complete row to check");
    }

    // The widths are base-12 units: what a cell draws is `Theme.space()` of them, as for the
    // defaults, so a base size change scales a dragged column with the rest.
    function test_widths_are_kept_in_base_units() {
        table.setColumnWidth("progress", 150);
        compare(table.columnWidths.progress, 150);
        compare(header("progress").width, Theme.space(150));
        compare(table.unitsOf(Theme.space(150)), 150);
        // Back at the default, the entry goes, so a later default reaches it.
        table.setColumnWidth("progress", 118);
        verify(table.columnWidthsAreDefault);
    }

    // A saved map is repaired, not trusted.
    function test_saved_widths_are_made_whole() {
        table.setColumnWidths({
                                  "size": 90,
                                  "bogus": 50,
                                  "eta": "wide",
                                  "status": 116,
                                  "dlspeed": 3,
                                  "name": 40
                              });
        compare(JSON.stringify(table.columnWidths), JSON.stringify({
                                                                       "size": 90,
                                                                       "dlspeed": table.minimumColumnWidth
                                                                   }));
        table.setColumnWidths(null);
        verify(table.columnWidthsAreDefault);
    }

    // "default order" puts back both the order and the widths.
    function test_reset_columns_puts_back_order_and_widths() {
        table.setColumnWidth("size", 150);
        table.moveColumn("eta", -1, null);
        verify(!table.columnLayoutIsDefault);
        table.resetColumns();
        verify(table.columnOrderIsDefault);
        verify(table.columnWidthsAreDefault);
        compare(header("size").width, Theme.space(72));
    }

    // docs/10 §4 with a dragged column: the table scrolls, Name keeps its minimum.
    function test_a_widened_column_in_a_narrow_table_scrolls_instead_of_squeezing() {
        table.anchors.fill = undefined;
        table.width = 700;
        table.height = 400;
        table.setColumnWidth("status", 400);
        compare(header("name").width, Theme.space(table.minimumNameWidth));
        compare(header("status").width, Theme.space(400));
        verify(findChild(table, "sideways").needed);
        table.anchors.fill = testCase;
    }

    // The click rules: a plain click selects only its row and is the range anchor, ctrl
    // toggles and moves the anchor, shift makes the selection anchor..row, ctrl+shift adds
    // anchor..row; the cursor follows every click.
    function clickRow(index: int, modifiers: int): void {
        pressRow(index, Qt.LeftButton, modifiers);
    }

    function pressRow(index: int, button: int, modifiers: int): void {
        const row = findChild(table, "row_" + index);
        verify(row, "no row " + index);
        mouseClick(row, Theme.space(40), row.height / 2, button, modifiers);
    }

    function selectedRows(): list<int> {
        const rows = [];
        for (let i = 0; i < App.filtered.count; ++i) {
            if (table.isRowSelected(i))
                rows.push(i);
        }
        return rows;
    }

    function test_a_shift_click_replaces_the_selection_with_the_range() {
        clickRow(2, Qt.NoModifier);
        compare(selectedRows(), [2]);
        clickRow(5, Qt.ControlModifier);
        compare(selectedRows(), [2, 5]);
        clickRow(7, Qt.ShiftModifier);
        compare(selectedRows(), [5, 6, 7], "shift+click added to the selection");
        compare(table.cursorIndex, 7);
        // The anchor stayed on 5: a second shift+click re-draws from it.
        clickRow(3, Qt.ShiftModifier);
        compare(selectedRows(), [3, 4, 5]);
        compare(table.cursorIndex, 3);
    }

    function test_a_ctrl_shift_click_adds_the_range() {
        clickRow(1, Qt.NoModifier);
        clickRow(4, Qt.ControlModifier);
        compare(selectedRows(), [1, 4]);
        clickRow(6, Qt.ControlModifier | Qt.ShiftModifier);
        compare(selectedRows(), [1, 4, 5, 6], "ctrl+shift+click did not add anchor..row");
        compare(table.cursorIndex, 6);
    }

    // The live sort moves rows between two clicks: the anchor stays on its torrent.
    function test_the_anchor_follows_its_torrent_across_a_resort() {
        clickRow(2, Qt.NoModifier);
        const anchor = App.filtered.hashAt(2);
        table.sortBy("name");
        const from = App.filtered.rowOfHash(anchor);
        const to = from > 3 ? from - 2 : from + 2;
        clickRow(to, Qt.ShiftModifier);
        const hashes = table.selectedHashes();
        compare(hashes.length, 3);
        verify(hashes.indexOf(anchor) >= 0, "the range no longer starts at the anchor's torrent");
    }

    // With no click yet the cursor is the anchor, as it is for `v`.
    function test_a_first_shift_click_draws_from_the_cursor() {
        table.moveCursorTo(2);
        clickRow(4, Qt.ShiftModifier);
        compare(selectedRows(), [2, 3, 4]);
    }

    function test_a_right_click_keeps_a_selection_it_lands_in() {
        clickRow(1, Qt.NoModifier);
        clickRow(3, Qt.ShiftModifier);
        pressRow(2, Qt.RightButton, Qt.NoModifier);
        compare(selectedRows(), [1, 2, 3]);
        compare(actionsSpy.count, 1);
        pressRow(6, Qt.RightButton, Qt.NoModifier);
        compare(selectedRows(), [6]);
        compare(actionsSpy.count, 2);
    }

    // A click below the last row (and in the room left for the selection bar) clears the
    // selection, which puts the bar away; the cursor stays.
    function test_a_click_on_the_empty_space_clears_the_selection() {
        table.bottomRoom = Theme.space(80);
        const empty = findChild(table, "emptySpace");
        verify(empty);
        verify(empty.height > 0, "sixteen rows leave no empty space");
        clickRow(1, Qt.NoModifier);
        clickRow(4, Qt.ShiftModifier);
        compare(table.selectedCount, 4);
        mouseClick(empty, empty.width / 2, empty.height - Theme.space(10));
        compare(table.selectedCount, 0);
        compare(table.cursorIndex, 4);
        table.bottomRoom = 0;
    }

    // Like Nautilus: clicking away from the inline editor commits it.
    function test_a_click_away_commits_the_rename() {
        table.forceActiveFocus();
        table.startRename();
        table.renameText = "archlinux-clicked-away.iso";
        const renamed = App.filtered.hashAt(0);
        clickRow(3, Qt.NoModifier);
        compare(table.renaming, false);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][0], renamed);
        compare(renameSpy.signalArguments[0][1], "archlinux-clicked-away.iso");
        compare(table.cursorIndex, 3);
        verify(table.activeFocus);

        table.startRename();
        table.renameText = "also-committed.iso";
        mouseClick(findChild(table, "emptySpace"), Theme.space(40), Theme.space(4));
        compare(table.renaming, false);
        compare(renameSpy.count, 2);
    }

    // The hover label covers every cell that elides, not only the name: an optional column
    // cut short (the 40-digit hash) names itself in full; a cell shown whole asks for none.
    function test_an_elided_cell_offers_its_whole_text() {
        const saved = table.visibleColumns.slice();
        table.visibleColumns = saved.concat(["hash"]);
        waitForRendering(table, 250);
        const row = findChild(table, "row_0");
        const box = table.columnLayout.hash;
        testCase.cellProbe = findChild(table, "hash_0");
        verify(testCase.cellProbe.truncated, "the hash fits its column");
        compare(row.elidedTextAt(box.x + box.w / 2), testCase.cellProbe.text);
        const size = table.columnLayout.size;
        compare(row.elidedTextAt(size.x + size.w / 2), "");
        table.visibleColumns = saved;
    }

    function test_enter_asks_for_the_actions_menu() {
        table.moveCursorTo(2);
        table.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(actionsSpy.count, 1);
        compare(actionsSpy.signalArguments[0][0], App.filtered.hashAt(2));
    }

    function test_rail_and_details_focus_keys() {
        table.forceActiveFocus();
        keyClick(Qt.Key_H);
        keyClick(Qt.Key_L);
        compare(railSpy.count, 1);
        compare(detailsSpy.count, 1);
    }

    // A table built over an already-sorted proxy must not read its own initial sort as a
    // second click on the same header and reverse it.
    function test_a_fresh_table_starts_on_the_first_row_of_its_initial_sort() {
        App.filtered.setSortByRoleName("name");
        App.filtered.sortOrder = Qt.AscendingOrder;
        testCase.spare = createTemporaryObject(tableComponent, testCase, {
                                                   "filtered": App.filtered
                                               });
        verify(testCase.spare);
        compare(App.filtered.sortRoleName, "dlspeed");
        compare(App.filtered.sortOrder, Qt.DescendingOrder);
        compare(testCase.spare.cursorIndex, 0);
        compare(testCase.spare.cursorHash, App.filtered.hashAt(0));
        testCase.spare.destroy();
        testCase.spare = null;
    }

    // #51 D1: a left double-click on a row is `activated(hash)`, after the first click put
    // the cursor and the selection there; a right or modified double-click is not.
    function test_a_double_click_activates_its_row() {
        activatedSpy.clear();
        const row = findChild(table, "row_3");
        mouseDoubleClickSequence(row, Theme.space(40), row.height / 2);
        compare(activatedSpy.count, 1);
        compare(activatedSpy.signalArguments[0][0], App.filtered.hashAt(3));
        compare(table.cursorIndex, 3);
        compare(selectedRows(), [3]);
        mouseDoubleClickSequence(row, Theme.space(40), row.height / 2, Qt.RightButton);
        mouseDoubleClickSequence(row, Theme.space(40), row.height / 2, Qt.LeftButton,
                                 Qt.ControlModifier);
        compare(activatedSpy.count, 1);
    }

    // #51 D8: a middle-click toggles its row, as ctrl+click does, and moves the cursor.
    function test_a_middle_click_toggles_its_row() {
        clickRow(2, Qt.NoModifier);
        pressRow(5, Qt.MiddleButton, Qt.NoModifier);
        compare(selectedRows(), [2, 5]);
        compare(table.cursorIndex, 5);
        pressRow(2, Qt.MiddleButton, Qt.NoModifier);
        compare(selectedRows(), [5]);
        compare(table.cursorIndex, 2);
    }

    // #51 D6: a right-click on a header cell asks for the columns palette and sorts nothing.
    function test_a_header_right_click_asks_for_the_columns() {
        columnsSpy.clear();
        const size = header("size");
        mouseClick(size, size.width / 2, size.height / 2, Qt.RightButton);
        compare(columnsSpy.count, 1);
        compare(App.filtered.sortRoleName, "dlspeed");
    }

    // #51 D9: the pointer over the rows holds their order; over the header it does not. The
    // host can hold it too (the open actions menu).
    function test_the_pointer_over_the_rows_holds_their_order() {
        const row = findChild(table, "row_2");
        mouseMove(row, Theme.space(40), row.height / 2);
        tryVerify(() => table.orderHeld, 1000, "hovering a row did not hold the order");
        tryVerify(() => App.filtered.holdOrder, 1000);
        const size = header("size");
        mouseMove(size, size.width / 2, size.height / 2);
        tryVerify(() => !table.orderHeld, 1000, "leaving the rows did not let go");
        tryVerify(() => !App.filtered.holdOrder, 1000);
        table.holdOrder = true;
        tryVerify(() => App.filtered.holdOrder, 1000);
        // Let go and taken again in one event (D17: the menu closes and opens again on
        // another row): the hold never lapses, so the rows never re-sort in between.
        const spy = createTemporaryObject(holdSpyComponent, testCase);
        table.holdOrder = false;
        table.holdOrder = true;
        wait(50);
        compare(spy.count, 0);
        verify(App.filtered.holdOrder);
        table.holdOrder = false;
        tryVerify(() => !App.filtered.holdOrder, 1000);
    }

    Component {
        id: holdSpyComponent

        SignalSpy {
            signalName: "holdOrderChanged"
            target: App.filtered
        }
    }

    // #51 D17: a right-click handed on from the open menu's scrim, at a point in the table:
    // the row there by the right-click rule, nothing off the rows.
    function test_a_right_click_handed_on_lands_on_the_row_there() {
        actionsSpy.clear();
        clickRow(1, Qt.NoModifier);
        clickRow(3, Qt.ShiftModifier);
        const inside = findChild(table, "row_2").mapToItem(table, Theme.space(40), Theme.space(4));
        verify(table.contextClickAt(inside.x, inside.y));
        compare(selectedRows(), [1, 2, 3], "a row inside the selection keeps it");
        compare(table.cursorIndex, 2);
        const outside = findChild(table, "row_6").mapToItem(table, Theme.space(40), Theme.space(4));
        verify(table.contextClickAt(outside.x, outside.y));
        compare(selectedRows(), [6]);
        compare(actionsSpy.count, 2);
        compare(actionsSpy.signalArguments[1][0], App.filtered.hashAt(6));
        verify(!table.contextClickAt(Theme.space(40), Theme.space(4)), "the header is no row");
        compare(actionsSpy.count, 2);
    }
}
