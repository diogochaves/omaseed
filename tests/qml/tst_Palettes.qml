import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// T4.4. Every case runs against the demo library (`App.loadDemo(16)`, spec v2 §1 Fig. 1): the
// jump, category, tags, sort and columns palettes over `App`'s real models and a real
// `TorrentTable`. `init()` puts the shared `App`/`table` state back, since both are the one
// App singleton and the one table this whole file reuses.
TestCase {
    id: testCase

    height: 600
    name: "Palettes"
    visible: true
    when: windowShown
    width: 1200

    TorrentTable {
        id: table

        anchors.fill: parent
        filtered: App.filtered
    }

    JumpPalette {
        id: jumpPalette

        anchors.fill: parent
        table: table
    }

    CategoryPalette {
        id: categoryPalette

        anchors.fill: parent
    }

    TagPalette {
        id: tagPalette

        anchors.fill: parent
    }

    SortPalette {
        id: sortPalette

        anchors.fill: parent
        table: table
    }

    ColumnsPalette {
        id: columnsPalette

        anchors.fill: parent
        table: table
    }

    SignalSpy {
        id: actionsSpy

        signalName: "showActionsRequested"
        target: App
    }

    function init() {
        if (App.torrents.count !== 16)
            App.loadDemo(16);
        App.filtered.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        App.filtered.clearCategory();
        App.filtered.clearTag();
        App.filtered.clearTracker();
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        table.visibleColumns = table.columns.map(column => column.key);
        table.moveCursorTo(0);
        jumpPalette.close();
        categoryPalette.close();
        tagPalette.close();
        sortPalette.close();
        columnsPalette.close();
        actionsSpy.clear();
    }

    // ---- jump (ctrl+k, Fig. 12) ---------------------------------------------------------
    function test_jump_matches_by_a_fuzzy_name_hit() {
        jumpPalette.open();
        jumpPalette.query = "bunny";
        compare(jumpPalette.items.length, 1, "only Big Buck Bunny should match");
        verify(jumpPalette.items[0].title.indexOf("Bunny") >= 0);
    }

    function test_jump_matches_by_hash_and_tracker_too() {
        jumpPalette.open();
        const hash = App.filtered.hashAt(0);
        jumpPalette.query = hash.substring(0, 8);
        verify(jumpPalette.items.length >= 1, "a hash prefix should match its own torrent");

        jumpPalette.query = "example.org";
        verify(jumpPalette.items.length > 1, "every movies-category row shares that tracker");
    }

    function test_jump_enter_clears_a_hiding_filter_and_moves_the_cursor() {
        // Hide the Bunny row behind an unrelated text filter first.
        App.filtered.text = "archlinux";
        verify(App.filtered.rowOfHash(hashFor("Bunny")) < 0, "the fixture is not hiding it");

        jumpPalette.open();
        jumpPalette.query = "bunny";
        jumpPalette.activate();

        compare(App.filtered.text, "", "the hiding filter should be cleared");
        compare(table.cursorHash, hashFor("Bunny"));
        verify(!jumpPalette.opened);
    }

    function test_jump_tab_jumps_and_asks_for_the_actions_menu() {
        jumpPalette.open();
        jumpPalette.query = "bunny";
        keyClick(Qt.Key_Tab);

        compare(table.cursorHash, hashFor("Bunny"));
        compare(actionsSpy.count, 1);
        compare(actionsSpy.signalArguments[0][0], hashFor("Bunny"));
        verify(!jumpPalette.opened);
    }

    /// The hash of the one demo row whose name contains `nameContains` (`App.jumpMatches()`
    /// already does the fuzzy lookup; this just takes the first hit's key).
    function hashFor(nameContains) {
        const rows = App.jumpMatches(nameContains, 1);
        return rows.length > 0 ? rows[0].key : "";
    }

    // ---- category (`C`, Fig. 13) ---------------------------------------------------------
    function test_category_rows_are_none_then_every_category() {
        categoryPalette.openFor([App.filtered.hashAt(0)]);
        const titles = categoryPalette.items.map(item => item.title);
        compare(titles[0], "none");
        for (const name of App.categories.names())
            verify(titles.indexOf(name) >= 0, "missing category " + name);
    }

    function test_category_shows_a_create_row_for_an_unknown_name() {
        categoryPalette.openFor([App.filtered.hashAt(0)]);
        categoryPalette.query = "brand-new-category";
        const last = categoryPalette.items[categoryPalette.items.length - 1];
        verify(last.isCreate === true);
        compare(last.key, "brand-new-category");
    }

    function test_category_title_counts_the_torrents() {
        categoryPalette.openFor([App.filtered.hashAt(0), App.filtered.hashAt(1)]);
        compare(categoryPalette.title, "category for 2 torrents");
    }

    // ---- tags --------------------------------------------------------------------------
    function test_tag_toggle_flips_the_checked_row() {
        const hash = App.filtered.hashAt(0);
        tagPalette.openFor([hash]);
        verify(tagPalette.items.length > 0, "the demo library has at least one tag");
        const tag = tagPalette.items[0].key;
        const wasChecked = tagPalette.items[0].checked;

        tagPalette.cursorIndex = 0;
        tagPalette.toggled(0, tagPalette.items[0]);

        const row = tagPalette.items.find(item => item.key === tag);
        compare(row.checked, !wasChecked);
    }

    function test_tag_create_row_appears_for_an_unknown_name() {
        tagPalette.openFor([App.filtered.hashAt(0)]);
        tagPalette.query = "brand-new-tag";
        const last = tagPalette.items[tagPalette.items.length - 1];
        verify(last.isCreate === true);
        compare(last.key, "brand-new-tag");
    }

    // ---- sort (`s`, Fig. 14) --------------------------------------------------------------
    function test_sort_marks_the_current_role_descending() {
        sortPalette.open();
        const row = sortPalette.items.find(item => item.key === "dlspeed");
        verify(row.meta.indexOf("current") >= 0);
        verify(row.meta.indexOf(Glyphs.chevronDown) >= 0);
    }

    function test_sort_enter_again_reverses() {
        // `init()` already sorts by dlspeed descending, so choosing "download speed" here is
        // choosing the already-current role: the very first `enter` should reverse it.
        compare(App.filtered.sortRoleName, "dlspeed");
        compare(App.filtered.sortOrder, Qt.DescendingOrder);

        sortPalette.open();
        sortPalette.query = "download speed";
        sortPalette.activate();
        compare(App.filtered.sortRoleName, "dlspeed");
        compare(App.filtered.sortOrder, Qt.AscendingOrder, "the current role should reverse");

        sortPalette.open();
        sortPalette.query = "download speed";
        sortPalette.activate();
        compare(App.filtered.sortOrder, Qt.DescendingOrder, "a second enter reverses it back");
    }

    // ---- columns (`ctrl+,`) ---------------------------------------------------------------
    function test_columns_toggle_hides_a_column_from_the_table() {
        columnsPalette.open();
        const etaRow = columnsPalette.items.find(item => item.key === "eta");
        verify(etaRow.checked);

        columnsPalette.toggled(columnsPalette.items.indexOf(etaRow), etaRow);

        verify(table.visibleColumns.indexOf("eta") < 0);
        verify(table.activeColumns.find(column => column.key === "eta") === undefined);
    }

    function test_columns_name_cannot_be_hidden() {
        columnsPalette.open();
        const nameRow = columnsPalette.items.find(item => item.key === "name");
        columnsPalette.toggled(columnsPalette.items.indexOf(nameRow), nameRow);
        verify(table.visibleColumns.indexOf("name") >= 0, "Name must stay visible");
    }
}
