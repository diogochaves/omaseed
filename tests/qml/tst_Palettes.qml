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
        id: rejectedSpy

        SignalSpy {
            signalName: "rejected"
        }
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
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.clearCategory();
        App.filtered.clearTag();
        App.filtered.clearTracker();
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        table.visibleColumns = table.columns.map(column => column.key);
        table.resetColumnOrder();
        table.resetColumnWidths();
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

    // A right-click on a row is that row's `tab`: jump there and ask for its actions menu.
    function test_jump_right_click_is_tab_on_that_row() {
        jumpPalette.open();
        jumpPalette.query = "bunny";
        waitForRendering(jumpPalette.listView, 500);
        const row = jumpPalette.listView.itemAtIndex(0);
        verify(row);
        mouseClick(row, 30, 10, Qt.RightButton);

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
    /// The tag palette's row `key`, laid out.
    function tagRow(key) {
        const index = tagPalette.items.findIndex(item => item.key === key);
        verify(index >= 0, "no row " + key);
        tagPalette.listView.positionViewAtIndex(index, ListView.Contain);
        waitForRendering(tagPalette.panel, 500); // laid out where the mouse will look
        const row = tagPalette.listView.itemAtIndex(index);
        verify(row, "row " + key + " is not laid out");
        return row;
    }

    // The selection bar's `tags` opens this palette: a mouse user ticks a row and clicks
    // `enter apply`, which applies as `enter` does.
    function test_tag_enter_apply_is_a_button() {
        const hash = App.filtered.hashAt(0);
        tagPalette.openFor([hash]);
        const index = tagPalette.items.findIndex(item => item.checked !== true);
        verify(index >= 0, "every demo tag is already on the torrent");
        const tag = tagPalette.items[index].key;
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": tagPalette
                                               });
        mouseClick(tagRow(tag), 30, 10);
        verify(tagPalette.opened, "a click on a row closed the palette");
        compare(tagPalette.items.find(item => item.key === tag).checked, true);

        mouseClick(findChild(tagPalette, "keyHint_enter"));
        verify(!tagPalette.opened, "enter apply did not close the palette");
        compare(rejected.count, 0, "enter apply threw the ticks away");
        // What `_applyDiff()` sent: the tag ticked that was not.
        compare(tagPalette._state[tag], true);
        verify(tagPalette._original[tag] !== true);
        compare(findChild(findChild(tagPalette, "keyHint_enter"), "keyHintArea").cursorShape,
                Qt.PointingHandCursor);
    }

    // The add sheet's `…` chip: the same button answers with `chosen`.
    function test_tag_enter_apply_answers_in_choice_mode() {
        const chosen = createTemporaryObject(chosenSpy, testCase, {
                                                 "target": tagPalette
                                             });
        tagPalette.openForChoice([]);
        mouseClick(tagRow("private"), 30, 10);
        mouseClick(findChild(tagPalette, "keyHint_enter"));
        compare(chosen.count, 1);
        compare(chosen.signalArguments[0][0], ["private"]);
        verify(!tagPalette.opened);
    }

    // A click on `create "x"` creates the tag and ticks it, and a later `enter` on it keeps
    // it ticked: it toggled it off again, and nothing was applied.
    function test_tag_a_click_on_the_create_row_creates_and_ticks_it() {
        tagPalette.openFor([App.filtered.hashAt(0)]);
        tagPalette.query = "fresh-tag";
        const create = tagPalette.items[tagPalette.items.length - 1];
        verify(create.isCreate === true);

        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": tagPalette
                                               });
        mouseClick(tagRow("fresh-tag"), 30, 10);
        const ticked = tagPalette.items.find(item => item.key === "fresh-tag");
        compare(ticked.checked, true, "the click did not tick the new tag");
        verify(tagPalette.checkedTags().indexOf("fresh-tag") >= 0);
        compare(tagPalette._created, ["fresh-tag"]);

        // `enter` on the row it still is: created once, still ticked, applied.
        compare(tagPalette.currentItem.key, "fresh-tag");
        keyClick(Qt.Key_Return);
        verify(!tagPalette.opened);
        compare(rejected.count, 0);
        compare(tagPalette._created, ["fresh-tag"], "the tag was created twice");
        compare(tagPalette._state["fresh-tag"], true, "enter ticked the new tag off again");
    }

    // Choice mode: the created tag comes back to the caller, ticked once.
    function test_tag_create_row_click_in_choice_mode() {
        const chosen = createTemporaryObject(chosenSpy, testCase, {
                                                 "target": tagPalette
                                             });
        tagPalette.openForChoice([]);
        tagPalette.query = "fresh-choice";
        mouseClick(tagRow("fresh-choice"), 30, 10);
        keyClick(Qt.Key_Return);
        compare(chosen.count, 1);
        compare(chosen.signalArguments[0][0], ["fresh-choice"]);
    }

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

    // Review RUN-3: the Web API sends tags comma-separated, so "alpha,beta" would become two.
    function test_a_name_with_a_comma_offers_no_create_row_and_says_why() {
        tagPalette.openFor([App.filtered.hashAt(0)]);
        tagPalette.query = "alpha,beta";
        verify(!tagPalette.items.some(item => item.isCreate === true),
               "a tag with a comma can be created");
        verify(tagPalette.footerRight.indexOf(",") >= 0, "nothing says why");
        tagPalette.query = "alpha";
        verify(tagPalette.items.some(item => item.isCreate === true));
        compare(tagPalette.footerRight, "new name creates it");
    }

    function test_the_create_row_trims_the_name() {
        tagPalette.openFor([App.filtered.hashAt(0)]);
        tagPalette.query = "  spaced  ";
        const last = tagPalette.items[tagPalette.items.length - 1];
        verify(last.isCreate === true);
        compare(last.key, "spaced");
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

    // A palette that opens under a pointer left resting there: Qt re-sends hover when the
    // rows move (alt+↑ here), and that first hover is no move of the pointer, so the cursor
    // must stay on the row the keys moved (CI caught it; locally the pointer sat elsewhere).
    function test_rows_moving_under_a_resting_pointer_keep_the_cursor() {
        columnsPalette.open();
        columnsPalette.cursorToKey("eta");
        // The pointer rests on eta's row; alt+↑ slides the row above under it.
        const area = findChild(columnsPalette, "paletteRow_"
                               + columnsPalette.items.indexOf(columnsPalette.currentItem));
        verify(area, "eta's row");
        const at = area.mapToItem(testCase, area.width / 2, area.height / 2);
        columnsPalette.close();
        mouseMove(testCase, at.x, at.y);

        columnsPalette.open();
        columnsPalette.cursorToKey("eta");
        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(columnsPalette.currentItem.key, "eta", "the resting pointer took the cursor");
        keyClick(Qt.Key_Down, Qt.AltModifier);
        mouseMove(testCase, 1, 1);
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

    // #52: a dragged width alone offers the reset too, and the reset puts it back.
    function test_default_order_puts_back_the_widths_too() {
        table.setColumnWidth("size", 150);
        columnsPalette.open();
        const last = () => columnsPalette.items[columnsPalette.items.length - 1];
        verify(table.columnOrderIsDefault);
        compare(last().key, columnsPalette.resetKey, "a dragged width offers no reset");

        keyClick("r", Qt.ControlModifier);
        verify(table.columnWidthsAreDefault, "ctrl+r kept the width");
        verify(last().key !== columnsPalette.resetKey);

        table.setColumnWidth("eta", 120);
        columnsPalette.cursorIndex = columnsPalette.items.length - 1;
        keyClick(Qt.Key_Return);
        verify(table.columnWidthsAreDefault, "enter on the last row kept the width");
    }

    function test_enter_toggles_a_column_too() {
        columnsPalette.open();
        columnsPalette.cursorToKey("eta");
        keyClick(Qt.Key_Return);
        verify(table.visibleColumns.indexOf("eta") < 0);
    }

    // ---- dragging a column (M13) -------------------------------------------------------
    /// The palette's drag surface and the centre of row `index` in its coordinates.
    function dragArea() {
        return findChild(columnsPalette, "paletteDragArea");
    }
    function rowCentre(index) {
        const row = columnsPalette.listView.itemAtIndex(index);
        verify(row !== null, "row " + index + " is not laid out");
        return row.y - columnsPalette.listView.contentY + row.height / 2;
    }
    function rowTop(index) {
        const row = columnsPalette.listView.itemAtIndex(index);
        return row.y - columnsPalette.listView.contentY;
    }
    /// Press on row `from`, travel in steps to `y`, and hand back before releasing so a
    /// caller can look mid-drag.
    function dragTo(from, y) {
        const area = dragArea();
        const x = area.width / 2;
        const start = rowCentre(from);
        mousePress(area, x, start);
        const steps = 6;
        for (let i = 1; i <= steps; ++i)
            mouseMove(area, x, start + (y - start) * i / steps);
        return area;
    }

    function test_a_dragged_row_lands_where_the_line_shows() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const keys = columnsPalette.items.map(item => item.key);
        const area = dragTo(3, rowTop(2) + 3);
        verify(columnsPalette.dragging, "the drag did not start");
        compare(columnsPalette.dropSlot, 2);
        verify(findChild(columnsPalette, "dropIndicator").visible, "no drop line");
        mouseRelease(area, area.width / 2, rowTop(2) + 3);

        verify(!columnsPalette.dragging);
        compare(table.columnOrder.indexOf(keys[3]), 2, "row 3 did not move above row 2");
        compare(table.columnOrder.indexOf(keys[2]), 3);
        compare(columnsPalette.currentItem.key, keys[3], "the cursor did not follow");
        verify(table.visibleColumns.indexOf(keys[3]) >= 0, "the drag toggled the column");
    }

    function test_a_row_dropped_on_name_lands_after_it() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const moved = columnsPalette.items[3].key;
        const area = dragTo(3, rowTop(0) + 2);
        compare(columnsPalette.dropSlot, 1, "the slot went above the pinned Name");
        mouseRelease(area, area.width / 2, rowTop(0) + 2);
        compare(table.columnOrder[0], "name");
        compare(table.columnOrder[1], moved);
    }

    function test_name_cannot_be_dragged() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const area = dragTo(0, rowCentre(3));
        verify(!columnsPalette.dragging, "Name was picked up");
        mouseRelease(area, area.width / 2, rowCentre(3));
        verify(table.columnOrderIsDefault);
    }

    function test_a_press_that_does_not_travel_toggles() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const index = 2;
        const key = columnsPalette.items[index].key;
        mouseClick(dragArea(), dragArea().width / 2, rowCentre(index));
        verify(table.visibleColumns.indexOf(key) < 0, "the click did not toggle the column");
        verify(table.columnOrderIsDefault, "the click moved the column");
        compare(columnsPalette.currentItem.key, key);
    }

    function test_a_drop_where_it_started_changes_nothing() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const area = dragTo(2, rowCentre(2) + 8);
        compare(columnsPalette.dropSlot, -1, "a drop in place offered a slot");
        verify(!findChild(columnsPalette, "dropIndicator").visible);
        mouseRelease(area, area.width / 2, rowCentre(2) + 8);
        verify(table.columnOrderIsDefault);
    }

    function test_esc_drops_the_drag_and_keeps_the_palette() {
        columnsPalette.open();
        waitForRendering(columnsPalette);
        const area = dragTo(3, rowTop(1) + 2);
        verify(columnsPalette.dragging);
        keyClick(Qt.Key_Escape);
        verify(!columnsPalette.dragging);
        verify(columnsPalette.opened, "esc closed the palette mid-drag");
        mouseRelease(area, area.width / 2, rowTop(1) + 2);
        verify(table.columnOrderIsDefault, "the dropped drag still moved the column");
    }

    function test_a_filtered_list_is_not_draggable() {
        columnsPalette.open();
        verify(columnsPalette.draggable);
        columnsPalette.query = "time";
        verify(!columnsPalette.draggable);
        verify(!tagPalette.draggable, "only the columns palette opts in");
    }

    // The palette's own hint row, the footer and `?` read the same Keymap entries.
    function test_the_hints_come_from_the_keymap() {
        const keys = columnsPalette.hints.map(hint => hint.key);
        compare(keys, ["space", "drag · alt+↑↓", "ctrl+r"]);
        compare(columnsPalette.hints[1].label, Keymap.find("columns", "alt+↑").label);
        compare(columnsPalette.hints[2].label, Keymap.find("columns", "ctrl+r").label);
    }
}
