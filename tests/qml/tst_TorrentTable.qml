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
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        table.moveCursorTo(0);
        renameSpy.clear();
        actionsSpy.clear();
        escapeSpy.clear();
        railSpy.clear();
        detailsSpy.clear();
        waitForRendering(table);
    }

    function test_row_count_follows_the_filter_model() {
        testCase.rowsProbe = findChild(table, "rows");
        verify(testCase.rowsProbe);
        compare(testCase.rowsProbe.count, App.filtered.count);
        compare(testCase.rowsProbe.count, 16);
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

    function test_status_cell_uses_the_state_glyph_and_colour() {
        compare(cellText("statusGlyph_0"), Glyphs.statusDownloading);
        testCase.cellProbe = findChild(table, "statusGlyph_0");
        compare(testCase.cellProbe.color, Theme.stateDownloading);

        let seeding = -1;
        for (let row = 0; row < App.filtered.count; ++row) {
            if (cellText("status_" + row) === "seeding") {
                seeding = row;
                break;
            }
        }
        verify(seeding >= 0, "no seeding row in the demo library");
        compare(cellText("statusGlyph_" + seeding), Glyphs.statusSeeding);
        testCase.cellProbe = findChild(table, "statusGlyph_" + seeding);
        compare(testCase.cellProbe.color, Theme.stateSeeding);
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
