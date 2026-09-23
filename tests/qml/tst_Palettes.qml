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

    Component {
        id: chosenSpy

        SignalSpy {
            signalName: "chosen"
        }
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
        table.resetColumnOrder();
        table.moveCursorTo(0);
        jumpPalette.close();
        categoryPalette.close();
        tagPalette.close();
        sortPalette.close();
        columnsPalette.close();
        columnsPalette.query = "";
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

    // ---- choice mode: the add sheet's `…` chips (T4.8) ----------------------------------
    function test_category_choice_mode_answers_the_caller_instead_of_the_torrents() {
        const chosen = createTemporaryObject(chosenSpy, testCase, {
                                                 "target": categoryPalette
                                             });
        categoryPalette.openForChoice();
        verify(categoryPalette.choiceMode);
        compare(categoryPalette.hashes.length, 0, "choice mode acts on no torrent");
        compare(categoryPalette.title, "category for the new torrent");

        categoryPalette.query = "movies";
        const row = categoryPalette.items.find(item => item.key === "movies");
        categoryPalette.activated(categoryPalette.items.indexOf(row), row);

        compare(chosen.count, 1);
        compare(chosen.signalArguments[0][0], "movies");
        verify(!categoryPalette.opened, "the palette stayed up");
    }

    function test_category_choice_mode_hands_back_a_created_name() {
        const chosen = createTemporaryObject(chosenSpy, testCase, {
                                                 "target": categoryPalette
                                             });
        categoryPalette.openForChoice();
        categoryPalette.query = "brand-new-category";
        const last = categoryPalette.items[categoryPalette.items.length - 1];
        verify(last.isCreate === true);
        categoryPalette.activated(categoryPalette.items.length - 1, last);

        compare(chosen.count, 1);
        compare(chosen.signalArguments[0][0], "brand-new-category");
    }

    function test_openFor_puts_the_palette_back_into_apply_mode() {
        categoryPalette.openForChoice();
        verify(categoryPalette.choiceMode);
        categoryPalette.close();
        categoryPalette.openFor([App.filtered.hashAt(0)]);
        verify(!categoryPalette.choiceMode);
        compare(categoryPalette.title, "category for 1 torrent");
    }

    function test_tag_choice_mode_starts_from_the_callers_tags_and_answers_with_them() {
        const chosen = createTemporaryObject(chosenSpy, testCase, {
                                                 "target": tagPalette
                                             });
        tagPalette.openForChoice(["keep"]);
        verify(tagPalette.choiceMode);
        compare(tagPalette.title, "tags for the new torrent");
        const keep = tagPalette.items.find(item => item.key === "keep");
        compare(keep.checked, true, "the caller's tag is not checked");

        const other = tagPalette.items.find(item => item.key === "private");
        tagPalette.toggled(tagPalette.items.indexOf(other), other);
        tagPalette.activated(0, tagPalette.items[0]);

        compare(chosen.count, 1);
        const answer = chosen.signalArguments[0][0];
        compare(answer.length, 2);
        verify(answer.indexOf("keep") >= 0);
        verify(answer.indexOf("private") >= 0);
        verify(!tagPalette.opened);
    }

    function test_tag_choice_mode_keeps_a_created_tag_as_a_row() {
        tagPalette.openForChoice(["not-on-the-server"]);
        const row = tagPalette.items.find(item => item.key === "not-on-the-server");
        verify(row, "a tag the server does not list still needs a row");
        compare(row.checked, true);
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

    // ---- columns (`|`) -------------------------------------------------------------------
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

    // ---- column order (N2) ------------------------------------------------------------
    function test_columns_list_in_the_order_the_table_draws_them() {
        table.placeColumn("eta", "size", false);
        columnsPalette.open();
        const keys = columnsPalette.items.map(item => item.key);
        compare(keys[0], "name");
        compare(keys[1], "eta");
        compare(keys[2], "size");
    }

    function test_alt_arrows_move_the_cursor_column_and_the_cursor_follows() {
        columnsPalette.open();
        columnsPalette.cursorToKey("eta");
        const before = table.columnOrder.indexOf("eta");

        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(table.columnOrder.indexOf("eta"), before - 1);
        compare(columnsPalette.currentItem.key, "eta", "the cursor stayed behind");
        const shown = table.activeColumns.map(column => column.key);
        verify(shown.indexOf("eta") < shown.indexOf("upspeed"), "the table did not follow");

        keyClick(Qt.Key_Down, Qt.AltModifier);
        compare(table.columnOrder.indexOf("eta"), before);
        verify(table.columnOrderIsDefault);
    }

    function test_name_stays_first() {
        columnsPalette.open();
        columnsPalette.cursorToKey("size");
        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(table.columnOrder[0], "name", "size moved past the pinned Name");

        columnsPalette.cursorToKey("name");
        keyClick(Qt.Key_Down, Qt.AltModifier);
        compare(table.columnOrder[0], "name", "Name moved");
    }

    function test_a_filtered_list_moves_past_the_row_it_shows() {
        columnsPalette.open();
        columnsPalette.query = "time";
        const keys = columnsPalette.items.map(item => item.key);
        verify(keys.length >= 2, "the fixture needs two rows matching 'time'");
        columnsPalette.cursorToKey(keys[1]);

        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(columnsPalette.items[0].key, keys[1]);
        compare(columnsPalette.items[1].key, keys[0]);
        compare(columnsPalette.currentItem.key, keys[1]);
    }

    function test_ctrl_r_and_the_last_row_put_back_the_default_order() {
        columnsPalette.open();
        const last = () => columnsPalette.items[columnsPalette.items.length - 1];
        verify(last().key !== columnsPalette.resetKey, "a default order offers a reset");

        columnsPalette.cursorToKey("eta");
        keyClick(Qt.Key_Up, Qt.AltModifier);
        verify(!table.columnOrderIsDefault);
        compare(last().key, columnsPalette.resetKey);

        keyClick("r", Qt.ControlModifier);
        verify(table.columnOrderIsDefault);
        compare(columnsPalette.currentItem.key, "eta", "the cursor lost its column");

        keyClick(Qt.Key_Up, Qt.AltModifier);
        columnsPalette.cursorIndex = columnsPalette.items.length - 1;
        keyClick(Qt.Key_Return);
        verify(table.columnOrderIsDefault, "enter on the last row did not reset");
    }

    function test_enter_toggles_a_column_too() {
        columnsPalette.open();
        columnsPalette.cursorToKey("eta");
        keyClick(Qt.Key_Return);
        verify(table.visibleColumns.indexOf("eta") < 0);
    }

    // The palette's own hint row, the footer and `?` read the same Keymap entries.
    function test_the_hints_come_from_the_keymap() {
        const keys = columnsPalette.hints.map(hint => hint.key);
        compare(keys, ["space", "alt+↑↓", "ctrl+r"]);
        compare(columnsPalette.hints[1].label, Keymap.find("columns", "alt+↑").label);
        compare(columnsPalette.hints[2].label, Keymap.find("columns", "ctrl+r").label);
    }
}
