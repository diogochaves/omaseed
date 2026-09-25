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
}
