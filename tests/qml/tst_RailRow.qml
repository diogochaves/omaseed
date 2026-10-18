import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "RailRow"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: railRowComponent

        RailRow {}
    }

    Component {
        id: clickedSpy

        SignalSpy {
            signalName: "clicked"
        }
    }

    function test_height_is_the_rail_row_height() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading"
                                          });
        compare(row.height, Theme.railRowHeight);
        compare(row.implicitHeight, Theme.railRowHeight);
    }

    function test_idle_is_transparent() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading"
                                          });
        compare(row.color.a, 0);
    }

    function test_selected_fills_at_the_selected_alpha() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading",
                                              "selected": true
                                          });
        fuzzyCompare(row.color.a, Theme.fill(Theme.fillSelected).a, 0.01);
    }

    function test_has_cursor_fills_at_the_hover_alpha() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading",
                                              "hasCursor": true
                                          });
        fuzzyCompare(row.color.a, Theme.fill(Theme.fillHover).a, 0.01);
    }

    function test_count_of_minus_one_hides_the_count() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "all",
                                              "count": -1
                                          });
        compare(row.count, -1);
    }

    function test_long_name_elides_and_keeps_the_count_inside_the_row() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "width": Theme.space(200),
                                              "text": "tracker.example.net-with-a-very-long-hostname-indeed",
                                              "count": 8
                                          });
        compare(row.truncated, true);
        verify(row.countRight <= row.width);
    }

    // docs/33 R2: a server row prints a word or a mark in the count's place, in its state's
    // colour, and its dot takes the state's colour too.
    function test_count_text_replaces_the_count() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "private",
                                              "count": -1,
                                              "countText": "down",
                                              "countColor": Theme.red,
                                              "glyphColor": Theme.red
                                          });
        const count = findChild(row, "railRowCount");
        compare(count.text, "down");
        compare(count.color, Theme.red);
        row.countText = "";
        row.count = 4;
        compare(count.text, "4");
        compare(count.color, Theme.red);
    }

    // Board 42 F1 (DECISIONS C15 M2): a folder row sits in by its depth, a drive's top reads
    // dim, and a folder's ▾ folds it without picking it.
    function test_a_folder_row_indents_dims_and_folds_from_its_glyph() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "games",
                                              "glyph": "▾",
                                              "width": 200
                                          });
        const area = findChild(row, "railRowGlyphArea");
        verify(!area.visible);
        const label = findChild(row, "railRowLabel");
        const flat = label.x;
        row.indent = 2;
        compare(label.x, flat + 2 * Theme.space(12));
        row.dimText = true;
        row.glyphClickable = true;
        verify(area.visible);
        const clicks = createTemporaryObject(clickedSpy, testCase, {
                                                 "target": row
                                             });
        const folds = createTemporaryObject(clickedSpy, testCase, {
                                                "target": row,
                                                "signalName": "glyphClicked"
                                            });
        mouseClick(area);
        compare(folds.count, 1);
        compare(clicks.count, 0);
        mouseClick(row, row.width - 20, row.height / 2);
        compare(clicks.count, 1);
    }

    function test_click_emits_clicked() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading"
                                          });
        let clicks = 0;
        row.clicked.connect(() => clicks++);
        row.clicked();
        compare(clicks, 1);
    }

    // #75: a screen reader's press runs the click, once.
    function test_the_accessible_press_is_the_click() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading"
                                          });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": row
                                              });
        row.Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // #79: the count at the right is the row's description; none, none.
    function test_the_count_is_the_description() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading",
                                              "count": 8
                                          });
        compare(row.Accessible.name, "downloading");
        compare(row.Accessible.description, "8");
        row.countText = "2 servers";
        compare(row.Accessible.description, "2 servers");
        row.countText = "";
        row.count = -1;
        compare(row.Accessible.description, "");
    }

    // #75: disabled, the row ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_row_ignores_the_accessible_press() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading",
                                              "enabled": false
                                          });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": row
                                              });
        row.Accessible.pressAction();
        compare(clicked.count, 0);
    }
}
