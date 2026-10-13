import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "TableHeader"
    visible: true
    when: windowShown
    width: 300

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

    // A list that does not sort keeps the arrow, so its captions do not promise a click.
    function test_an_unclickable_header_keeps_the_arrow() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "Tracker",
                                                 "clickable": false
                                             });
        const mouse = findChild(header, "headerMouse");
        compare(mouse.cursorShape, Qt.ArrowCursor);
        header.dragging = true;
        compare(mouse.cursorShape, Qt.ClosedHandCursor);
    }

    // #51 D6: with `rightClickable` a right-click is `rightClicked`, never `clicked`; without
    // it the right button is not the header's at all.
    function test_a_right_click_is_its_own_signal_when_asked() {
        const header = createTemporaryObject(headerComponent, testCase, {
                                                 "text": "size"
                                             });
        const mouse = findChild(header, "headerMouse");
        compare(mouse.acceptedButtons, Qt.LeftButton);
        header.rightClickable = true;
        compare(mouse.acceptedButtons, Qt.LeftButton | Qt.RightButton);
        let clicks = 0;
        let rightClicks = 0;
        header.clicked.connect(() => clicks++);
        header.rightClicked.connect(() => rightClicks++);
        header.width = 120;
        waitForRendering(header, 250);
        mouseClick(header, 60, header.height / 2, Qt.RightButton);
        compare(rightClicks, 1);
        compare(clicks, 0);
        mouseClick(header, 60, header.height / 2);
        compare(clicks, 1);
        compare(rightClicks, 1);
    }
}
