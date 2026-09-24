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
}
