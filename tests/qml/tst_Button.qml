import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Button"

    Component {
        id: buttonComponent

        Button {}
    }

    function test_height_is_control_height() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        compare(button.height, Theme.controlHeight);
    }

    function test_idle_has_no_chrome() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        compare(button.border.width, 0);
        compare(button.color.a, 0);
    }

    function test_bordered_idle_uses_the_idle_fill_and_border() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add",
                                                 "bordered": true
                                             });
        fuzzyCompare(button.color.a, Theme.fill(Theme.fillIdle).a, 0.01);
        compare(button.border.width, 1);
    }

    function test_hover_or_cursor_fills_at_the_hover_alpha() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add",
                                                 "hasCursor": true
                                             });
        const expected = Theme.fill(Theme.fillHover);
        fuzzyCompare(button.color.a, expected.a, 0.01);
        compare(button.border.width, 1);
    }

    function test_selected_fills_at_the_selected_alpha() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add",
                                                 "selected": true
                                             });
        fuzzyCompare(button.color.a, Theme.fill(Theme.fillSelected).a, 0.01);
    }

    function test_pressed_fills_at_the_pressed_alpha() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add",
                                                 "pressed": true
                                             });
        fuzzyCompare(button.color.a, Theme.fill(Theme.fillPressed).a, 0.01);
    }

    function test_primary_border_is_accent() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add",
                                                 "primary": true
                                             });
        compare(button.border.color, Theme.accent);
    }

    function test_danger_text_is_red() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Delete",
                                                 "danger": true
                                             });
        compare(button._textColor, Theme.red);
    }

    function test_danger_selected_border_is_red() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Delete",
                                                 "danger": true,
                                                 "selected": true
                                             });
        compare(button.border.color, Theme.red);
    }

    function test_click_emits_clicked() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        let clicks = 0;
        button.clicked.connect(() => clicks++);
        button.clicked();
        compare(clicks, 1);
    }
}
