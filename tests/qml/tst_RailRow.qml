import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "RailRow"

    Component {
        id: railRowComponent

        RailRow {}
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

    function test_click_emits_clicked() {
        const row = createTemporaryObject(railRowComponent, testCase, {
                                              "text": "downloading"
                                          });
        let clicks = 0;
        row.clicked.connect(() => clicks++);
        row.clicked();
        compare(clicks, 1);
    }
}
