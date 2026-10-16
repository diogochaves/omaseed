import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "TableRow"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: rowComponent

        TableRow {}
    }

    function test_height_is_the_row_height() {
        const row = createTemporaryObject(rowComponent, testCase);
        compare(row.height, Theme.rowHeight);
        compare(row.implicitHeight, Theme.rowHeight);
    }

    function test_idle_is_transparent_with_no_border() {
        const row = createTemporaryObject(rowComponent, testCase);
        compare(row.color.a, 0);
        compare(row.border.width, 0);
        compare(row.textColor, Theme.foreground);
    }

    function test_selected_fills_and_brightens_text() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "selected": true
                                          });
        fuzzyCompare(row.color.a, Theme.fill(Theme.fillSelected).a, 0.01);
        compare(row.textColor, Theme.brightForeground);
    }

    function test_cursor_fills_and_draws_an_inset_border() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "hasCursor": true
                                          });
        fuzzyCompare(row.color.a, Theme.fill(Theme.fillHover).a, 0.01);
        compare(row.border.width, 1);
    }

    function test_selected_and_cursor_together_use_the_24_percent_fill() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "selected": true,
                                              "hasCursor": true
                                          });
        fuzzyCompare(row.color.a, Theme.fill(0.24).a, 0.01);
    }

    function test_content_alias_accepts_cells() {
        const row = createTemporaryObject(rowComponent, testCase);
        compare(row.content.length, 0);
    }

    function test_click_emits_clicked_with_its_modifiers() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "width": 200
                                          });
        const seen = [];
        row.clicked.connect(modifiers => seen.push(modifiers));
        mouseClick(row);
        mouseClick(row, 10, 10, Qt.LeftButton, Qt.ShiftModifier);
        compare(seen.length, 2);
        compare(seen[0], Qt.NoModifier);
        compare(seen[1], Qt.ShiftModifier);
    }

    function test_right_click_emits_right_clicked() {
        const row = createTemporaryObject(rowComponent, testCase);
        let rightClicks = 0;
        row.rightClicked.connect(() => rightClicks++);
        row.rightClicked();
        compare(rightClicks, 1);
    }

    function test_double_click_emits_double_clicked() {
        const row = createTemporaryObject(rowComponent, testCase);
        let doubleClicks = 0;
        row.doubleClicked.connect(() => doubleClicks++);
        row.doubleClicked();
        compare(doubleClicks, 1);
    }

    // #77: a screen reader finds a list item, selected as the row is, named by the caller.
    function test_it_publishes_a_selectable_list_item() {
        const row = createTemporaryObject(rowComponent, testCase);
        row.Accessible.name = "debian.iso";
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, "debian.iso");
        verify(row.Accessible.selectable);
        verify(!row.Accessible.selected);
        row.selected = true;
        verify(row.Accessible.selected);
    }

    // #77: its press is a plain click, once; disabled, it does nothing, as the pointer.
    function test_the_accessible_press_is_a_plain_click() {
        const row = createTemporaryObject(rowComponent, testCase);
        const seen = [];
        row.clicked.connect(modifiers => seen.push(modifiers));
        row.Accessible.pressAction();
        compare(seen.length, 1);
        compare(seen[0], Qt.NoModifier);
        row.enabled = false;
        row.Accessible.pressAction();
        compare(seen.length, 1, "a disabled row took the press");
    }
}
