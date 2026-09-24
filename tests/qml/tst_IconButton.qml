import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "IconButton"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: iconButtonComponent

        IconButton {}
    }

    function test_is_a_square_control_height() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        compare(button.height, Theme.controlHeight);
        compare(button.width, Theme.controlHeight);
    }

    function test_hover_or_cursor_fills_at_the_hover_alpha() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "hasCursor": true
                                             });
        fuzzyCompare(button.color.a, Theme.fill(Theme.fillHover).a, 0.01);
    }

    function test_selected_fills_at_the_selected_alpha() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "selected": true
                                             });
        fuzzyCompare(button.color.a, Theme.fill(Theme.fillSelected).a, 0.01);
    }

    function test_primary_border_is_accent() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "primary": true
                                             });
        compare(button.border.color, Theme.accent);
    }

    function test_focus_draws_the_ring_and_leaves_the_border_alone() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        const ring = findChild(button, "focusRing");
        verify(ring);
        button.forceActiveFocus();
        compare(ring.shown, true);
        compare(button.border.width, 0);
    }

    function test_danger_glyph_is_red() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "✕",
                                                 "danger": true
                                             });
        compare(button._glyphColor, Theme.red);
    }

    function test_click_emits_clicked() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        let clicks = 0;
        button.clicked.connect(() => clicks++);
        button.clicked();
        compare(clicks, 1);
    }
}
