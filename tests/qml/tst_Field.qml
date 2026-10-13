import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Field"
    visible: true
    when: windowShown
    width: 400
    height: 200

    Component {
        id: fieldComponent

        Field {}
    }

    function test_height_is_control_height() {
        const field = createTemporaryObject(fieldComponent, testCase);
        compare(field.height, Theme.controlHeight);
    }

    function test_idle_fill_and_border_match_theme() {
        const field = createTemporaryObject(fieldComponent, testCase);
        fuzzyCompare(field.color.a, Theme.fill(Theme.fillIdle).a, 0.01);
    }

    function test_escape_clears_and_emits_cleared() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "text": "fedora 45"
                                            });
        let cleared = 0;
        field.cleared.connect(() => cleared++);
        field.text = "";
        field.cleared();
        compare(field.text, "");
        compare(cleared, 1);
    }

    // Review RUN-5: a form field opened with a value passes esc on instead of clearing it.
    function test_a_field_that_does_not_clear_on_esc_keeps_its_text() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "text": "/data/torrents",
                                                "escClears": false
                                            });
        let cleared = 0;
        field.cleared.connect(() => cleared++);
        field.focusInput();
        keyClick(Qt.Key_Escape);
        compare(field.text, "/data/torrents");
        compare(cleared, 0);

        field.escClears = true;
        keyClick(Qt.Key_Escape);
        compare(field.text, "");
        compare(cleared, 1);
    }

    function test_accepted_signal_fires() {
        const field = createTemporaryObject(fieldComponent, testCase);
        let accepted = 0;
        field.accepted.connect(() => accepted++);
        field.accepted();
        compare(accepted, 1);
    }

    // KIT-7: Return and keypad Enter both accept; shift+enter is left for the sheet.
    function test_return_and_keypad_enter_both_accept() {
        const field = createTemporaryObject(fieldComponent, testCase);
        let accepted = 0;
        field.accepted.connect(() => accepted++);
        field.focusInput();
        keyClick(Qt.Key_Return);
        compare(accepted, 1);
        keyClick(Qt.Key_Enter, Qt.KeypadModifier);
        compare(accepted, 2, "keypad enter did not accept");
        keyClick(Qt.Key_Enter, Qt.KeypadModifier | Qt.ShiftModifier);
        compare(accepted, 2, "shift+keypad enter was swallowed by the field");
    }

    // T4.5: a field that holds a secret (spec §3 Fig. 9's `Key`) draws bullets instead of the
    // characters, and `text` still carries the real value for the caller.
    function test_echo_mode_hides_a_secret() {
        const field = createTemporaryObject(fieldComponent, testCase);
        compare(field.echoMode, TextInput.Normal);
        field.echoMode = TextInput.Password;
        field.text = "qbt_secret";
        compare(field.text, "qbt_secret");
        compare(field.echoMode, TextInput.Password);
    }

    // Batch-2 P2: Speed limits opened on the alt-speed field focused Download first, then
    // Alt-speed download, and Download kept drawing its caret over the `∞`. Only the field
    // with the keyboard may show one.
    function test_only_the_focused_field_draws_a_caret() {
        const first = createTemporaryObject(fieldComponent, testCase);
        const second = createTemporaryObject(fieldComponent, testCase, {
                                                 "y": Theme.controlHeight * 2
                                             });
        first.focusInput();
        second.focusInput();
        wait(50);
        const stray = findChild(first, "fieldCaret");
        verify(stray === null || !stray.visible || stray.opacity === 0,
               "the unfocused field still draws a caret");
        const live = findChild(second, "fieldCaret");
        verify(live !== null);
        compare(live.opacity, 1);
    }

    // Batch-2 J6: focusing an empty field keeps its placeholder, dimmed a step, until the
    // first character is typed.
    function test_the_placeholder_stays_until_something_is_typed() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "placeholder": "∞"
                                            });
        const placeholder = findChild(field, "fieldPlaceholder");
        verify(placeholder !== null);
        compare(placeholder.visible, true);
        const idle = placeholder.opacity;

        field.focusInput();
        compare(field.editing, true);
        compare(placeholder.visible, true, "the placeholder went when the field took focus");
        verify(placeholder.opacity < idle, "a focused placeholder should be dimmer");

        keyClick("5");
        compare(field.text, "5");
        compare(placeholder.visible, false);
    }

    // docs/12 F1: the Files filter ends in an `esc` cap, after its count.
    function test_trailing_key_draws_a_cap_after_the_count() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "2 of 5 files"
                                            });
        const cap = findChild(field, "fieldTrailingKey");
        verify(cap !== null);
        verify(!cap.visible);
        field.trailingKey = "esc";
        verify(cap.visible);
        compare(cap.text, "esc");
        verify(cap.x + cap.width <= field.width);
    }

    // Manual box M6: the add sheet's `browse` is a button, not only a key to know.
    function test_a_clickable_trailing_emits_trailing_clicked() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "browse",
                                                "trailingKey": "ctrl+o",
                                                "trailingClickable": true
                                            });
        let clicks = 0;
        field.trailingClicked.connect(() => clicks++);
        const text = findChild(field, "fieldTrailingText");
        const cap = findChild(field, "fieldTrailingKey");
        const button = findChild(field, "fieldTrailingButton");
        compare(button.cursorShape, Qt.PointingHandCursor);
        mouseClick(text);
        mouseClick(cap);
        compare(clicks, 2);

        // The caret's side of the field is still the text input's.
        mouseClick(field, Theme.space(12), field.height / 2);
        compare(clicks, 2);
        verify(field.editing, "a click on the text side did not put the caret in");
    }

    function test_the_trailing_is_plain_text_unless_clickable() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "2 of 5 files"
                                            });
        let clicks = 0;
        field.trailingClicked.connect(() => clicks++);
        const text = findChild(field, "fieldTrailingText");
        mouseClick(text);
        compare(clicks, 0);
        compare(findChild(field, "fieldTrailingButton").enabled, false);
        compare(text.color, Theme.dimForeground);
    }

    function test_a_clickable_trailing_lights_up_under_the_mouse() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "browse",
                                                "trailingClickable": true
                                            });
        const text = findChild(field, "fieldTrailingText");
        compare(text.color, Theme.dimForeground);
        mouseMove(text, text.width / 2, text.height / 2);
        tryCompare(text, "color", Theme.accent);
    }

    // #11: a key in `shortcuts` is handed on as shortcutPressed and typed nothing; others type.
    function test_a_shortcut_key_is_handed_on_not_typed() {
        const field = createTemporaryObject(fieldComponent, testCase);
        field.shortcuts = [{
                "key": Qt.Key_E,
                "modifiers": Qt.ControlModifier
            }];
        let pressed = 0;
        field.shortcutPressed.connect(() => pressed++);
        field.focusInput();
        keyClick("e");
        compare(field.text, "e");
        keyClick(Qt.Key_E, Qt.ControlModifier);
        compare(pressed, 1);
        compare(field.text, "e");
        keyClick(Qt.Key_E, Qt.ControlModifier | Qt.ShiftModifier);
        compare(pressed, 1, "ctrl+shift+e is not ctrl+e");
    }

    // The input is one line tall and centred: a press on the band above it, the padding or
    // the hint glyph still puts the caret in, nearest the press.
    function test_a_press_anywhere_in_the_box_focuses_the_input() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "hint": "/",
                                                "text": "/data/torrents",
                                                "width": 300
                                            });
        verify(!field.editing);
        mouseClick(field, 3, 2); // the hint's padding, top band
        verify(field.editing, "a press on the padding did not focus the field");
        compare(field.cursorPosition, 0);
        mouseClick(field, field.width - 3, field.height - 2);
        compare(field.cursorPosition, field.text.length);
    }

    // A mouse-only user pastes a path through the right-click menu.
    function test_a_right_click_opens_cut_copy_paste() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "text": "/data",
                                                "width": 300
                                            });
        const mouse = findChild(field, "fieldMouse");
        mouseClick(field, field.width / 2, field.height / 2, Qt.RightButton);
        verify(mouse.menu !== null, "no menu");
        compare(mouse.menu.items[2].action, "paste");
        keyClick(Qt.Key_Escape);
        verify(mouse.menu === null);
        verify(field.editing, "the keyboard did not come back");
    }
}
