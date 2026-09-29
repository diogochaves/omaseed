import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "TableHeader"

    Component {
        id: headerComponent

        TableHeader {}
    }

    function test_height_is_twice_body() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "name"
                                             });
        compare(header.implicitHeight, Theme.font.body * 2);
    }

    function test_idle_is_dim() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "name"
                                             });
        compare(header.sorted, false);
    }

    function test_sorted_shows_descending_glyph() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "size",
                                                 "sorted": true,
                                                 "descending": true
                                             });
        compare(header.sorted, true);
        compare(header.descending, true);
    }

    function test_align_right_is_settable() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "size",
                                                 "alignRight": true
                                             });
        compare(header.alignRight, true);
    }

    function test_click_emits_clicked() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "name"
                                             });
        let clicks = 0;
        header.clicked.connect(() => clicks++);
        header.clicked();
        compare(clicks, 1);
    }

    function test_a_drag_shows_the_closed_hand() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "size"
                                             });
        const mouse = findChild(header, "headerMouse");
        compare(mouse.cursorShape, Qt.PointingHandCursor);
        header.dragging = true;
        compare(mouse.cursorShape, Qt.ClosedHandCursor);
    }
}
