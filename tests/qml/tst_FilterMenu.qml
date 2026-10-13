pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit

// docs/15 Q1 A: the hero's filters on its last step, in one menu: the name filter on top,
// one row per filter with its ✕, a rule and "clear all filters".
TestCase {
    id: testCase

    readonly property var chips: [
        {
            "key": "status",
            "glyph": Glyphs.arrowDown,
            "text": "downloading"
        },
        {
            "key": "category",
            "glyph": Glyphs.category,
            "text": "linux"
        },
        {
            "key": "tag",
            "glyph": Glyphs.tag,
            "text": "keep"
        }
    ]

    height: 600
    name: "FilterMenu"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: menuComponent

        FilterMenu {
            id: menu

            property var cleared: []
            property int clearedAll: 0
            property string edited: ""

            anchors.fill: parent

            onAllCleared: menu.clearedAll += 1
            onFilterCleared: key => {
                menu.cleared = menu.cleared.concat([key]);
                menu.filters = menu.filters.filter(f => f.key !== key);
            }
            onTextEdited: text => menu.edited = text
        }
    }

    function makeMenu() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "filters": testCase.chips,
                                               "text": "iso"
                                           });
        menu.openAt(100, 40);
        return menu;
    }

    function test_opens_with_the_caret_in_the_name_filter() {
        const menu = makeMenu();
        verify(menu.opened);
        compare(menu.cursorIndex, -1);
        compare(menu.field.text, "iso");
        verify(menu.field.editing);
        verify(findChild(menu, "filterMenuRow_status").visible);
        verify(findChild(menu, "filterMenuRow_tag").visible);
        verify(findChild(menu, "filterMenuClearAll").visible);
        compare(menu.panel.border.color, Theme.accent);
    }

    function test_typing_goes_to_the_field() {
        const menu = makeMenu();
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_K);
        compare(menu.field.text, "isojk");
        compare(menu.edited, "isojk");
        compare(menu.cursorIndex, -1);
    }

    function test_rows_move_and_clear_with_the_keys() {
        const menu = makeMenu();
        keyClick(Qt.Key_Down);
        compare(menu.cursorIndex, 0);
        verify(!menu.field.editing);
        keyClick(Qt.Key_J);
        compare(menu.cursorIndex, 1);
        keyClick(Qt.Key_X);
        compare(menu.cleared, ["category"]);
        verify(menu.opened);
        compare(menu.cursorIndex, 1); // now on "keep"
        keyClick(Qt.Key_Return);
        compare(menu.cleared, ["category", "tag"]);
        // The cursor never lands past the last filter by itself.
        compare(menu.cursorIndex, 0);
        keyClick(Qt.Key_K);
        compare(menu.cursorIndex, -1);
        verify(menu.field.editing);
    }

    function test_a_letter_on_the_rows_goes_back_to_the_field() {
        const menu = makeMenu();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_A);
        compare(menu.cursorIndex, -1);
        verify(menu.field.editing);
        compare(menu.field.text, "isoa");
    }

    function test_clear_all_closes() {
        const menu = makeMenu();
        for (let i = 0; i < 5; ++i)
            keyClick(Qt.Key_Down);
        compare(menu.cursorIndex, 3); // past the three filters
        keyClick(Qt.Key_Return);
        compare(menu.clearedAll, 1);
        verify(!menu.opened);
    }

    function test_the_last_filter_cleared_closes_the_menu() {
        const menu = makeMenu();
        menu.filters = [testCase.chips[0]];
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_X);
        verify(!menu.opened);
    }

    function test_esc_closes_and_keeps_the_filter() {
        const menu = makeMenu();
        let rejected = 0;
        menu.rejected.connect(() => rejected += 1);
        keyClick(Qt.Key_Escape);
        verify(!menu.opened);
        compare(rejected, 1);
        compare(menu.field.text, "iso");
        compare(menu.clearedAll, 0);
    }

    function test_enter_in_the_field_accepts() {
        const menu = makeMenu();
        let accepted = 0;
        menu.accepted.connect(() => accepted += 1);
        keyClick(Qt.Key_Return);
        compare(accepted, 1);
        verify(!menu.opened);
        compare(menu.cleared, []);
    }

    function test_a_click_on_a_row_clears_it() {
        const menu = makeMenu();
        mouseClick(findChild(menu, "filterMenuRow_status"));
        compare(menu.cleared, ["status"]);
    }

    // A short window: the panel stops at its edges, and the rows scroll into view.
    function test_a_long_menu_is_cut_to_the_window_and_scrolls() {
        const many = [];
        for (let i = 0; i < 30; ++i)
            many.push({
                          "key": "k" + i,
                          "glyph": Glyphs.tag,
                          "text": "filter " + i
                      });
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "filters": many
                                           });
        menu.openAt(100, 40);
        const edge = Theme.space(8);
        verify(menu.panel.implicitHeight > testCase.height);
        compare(menu.panel.height, testCase.height - 2 * edge);
        compare(menu.panel.y, edge);
        compare(menu.scroller.contentY, 0);
        verify(findChild(menu, "filterMenuScrollBar").visible);

        // Down to "clear all filters", the last row: it comes into view.
        for (let i = 0; i <= 30; ++i)
            keyClick(Qt.Key_Down);
        compare(menu.cursorIndex, 30);
        const clearAll = findChild(menu, "filterMenuClearAll");
        verify(clearAll.y + clearAll.height <= menu.scroller.contentY + menu.scroller.height + 0.5);
        mouseClick(clearAll);
        compare(menu.clearedAll, 1);
    }
}
