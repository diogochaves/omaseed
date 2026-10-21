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

    Component {
        id: trailingClickedSpy

        SignalSpy {
            signalName: "trailingClicked"
        }
    }

    Component {
        id: openerClickedSpy

        SignalSpy {
            signalName: "openerClicked"
        }
    }

    // A numeric field in a pane taller than its view: what the wheel scrolls when the field
    // does not take it.
    Component {
        id: scrollerComponent

        Flickable {
            readonly property alias field: numberField

            contentHeight: 1000
            height: 150
            width: 300

            Field {
                id: numberField

                maximum: 5
                minimum: 1
                numeric: true
                placeholder: "∞"
                width: 200
                y: 20
            }
        }
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

    // #51 D16: with the caret in it, a numeric field steps a notch at a time, clamped to its
    // range, and the wheel goes no further.
    function test_a_numeric_field_steps_with_the_wheel_while_it_has_the_caret() {
        const scroller = createTemporaryObject(scrollerComponent, testCase);
        const field = scroller.field;
        field.text = "3";
        field.focusInput();
        verify(field.editing);
        mouseWheel(field, field.width / 2, field.height / 2, 0, 120);
        compare(field.text, "4");
        mouseWheel(field, field.width / 2, field.height / 2, 0, 240);
        compare(field.text, "5", "past the maximum");
        mouseWheel(field, field.width / 2, field.height / 2, 0, -120);
        compare(field.text, "4");
        mouseWheel(field, field.width / 2, field.height / 2, 0, -960);
        compare(field.text, "1", "past the minimum");
        wait(50);
        compare(scroller.contentY, 0, "the pane scrolled under a field that took the wheel");

        // A touchpad's small deltas add up to a notch.
        mouseWheel(field, field.width / 2, field.height / 2, 0, 60);
        compare(field.text, "1");
        mouseWheel(field, field.width / 2, field.height / 2, 0, 60);
        compare(field.text, "2");

        // Empty is ∞ here: up starts at the smallest limit, down leaves it empty.
        field.text = "";
        mouseWheel(field, field.width / 2, field.height / 2, 0, -120);
        compare(field.text, "");
        mouseWheel(field, field.width / 2, field.height / 2, 0, 120);
        compare(field.text, "2");
        // What is not a number stays for the caller to refuse.
        field.text = "lots";
        mouseWheel(field, field.width / 2, field.height / 2, 0, 120);
        compare(field.text, "lots");
    }

    // Without the caret the wheel is the pane's: scrolling past a value never changes it.
    function test_a_numeric_field_without_the_caret_lets_the_wheel_scroll_on() {
        const scroller = createTemporaryObject(scrollerComponent, testCase);
        const field = scroller.field;
        field.text = "3";
        verify(!field.editing);
        mouseWheel(field, field.width / 2, field.height / 2, 0, -120);
        compare(field.text, "3");
        tryVerify(() => scroller.contentY > 0, 1000, "the pane did not scroll");
    }

    // The step's own places and the typed ones are kept (a share ratio steps by 0.1), and an
    // empty field steps from its numeric placeholder or `emptyValue`.
    function test_a_numeric_step_keeps_its_decimals_and_starts_from_the_empty_meaning() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "numeric": true,
                                                "step": 0.1,
                                                "text": "1.25"
                                            });
        field.stepBy(1);
        compare(field.text, "1.35");
        field.text = "2";
        field.stepBy(-3);
        compare(field.text, "1.7");
        field.step = 1;
        field.text = "";
        field.placeholder = "75";
        field.stepBy(-1);
        compare(field.text, "74");
        field.text = "";
        field.placeholder = "follow";
        field.emptyValue = 14;
        field.stepBy(1);
        compare(field.text, "15");
        field.numeric = false;
        field.stepBy(1);
        compare(field.text, "15", "a plain field stepped");
    }

    // #75: a screen reader's press on a clickable trailing is its click, once.
    function test_the_accessible_press_on_the_trailing_is_the_click() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "browse",
                                                "trailingKey": "ctrl+o",
                                                "trailingClickable": true
                                            });
        const clicked = createTemporaryObject(trailingClickedSpy, testCase, {
                                                  "target": field
                                              });
        findChild(field, "fieldTrailingButton").Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // #75: a plain trailing, and a disabled field's, ignore a screen reader's press, as they
    // ignore the pointer.
    function test_what_takes_no_click_ignores_the_accessible_press() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "2 of 5 files"
                                            });
        const clicked = createTemporaryObject(trailingClickedSpy, testCase, {
                                                  "target": field
                                              });
        const button = findChild(field, "fieldTrailingButton");
        button.Accessible.pressAction();
        compare(clicked.count, 0, "a plain trailing ran");
        field.trailingClickable = true;
        field.enabled = false;
        button.Accessible.pressAction();
        compare(clicked.count, 0, "a disabled field's trailing ran");
    }

    // #78: the clickable trailing is named by its action, its key the description; with a
    // `trailingName` the text (a count) goes in the description too. A plain trailing is a
    // label, and no trailing is nothing at all.
    function test_the_trailing_is_named_by_its_action_and_the_key_is_its_description() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "trailing": "browse",
                                                "trailingKey": "ctrl+o",
                                                "trailingClickable": true
                                            });
        const button = findChild(field, "fieldTrailingButton");
        compare(button.Accessible.name, "browse");
        compare(button.Accessible.description, "shortcut ctrl+o");
        compare(button.Accessible.role, Accessible.Button);
        compare(button.Accessible.ignored, false);

        field.trailing = "2 of 5 files";
        field.trailingKey = "esc";
        field.trailingName = "clear the filter";
        compare(button.Accessible.name, "clear the filter");
        compare(button.Accessible.description, "2 of 5 files · shortcut esc");

        field.trailingClickable = false;
        field.trailingName = "";
        field.trailingKey = "";
        compare(button.Accessible.name, "2 of 5 files");
        compare(button.Accessible.role, Accessible.StaticText);

        field.trailing = "";
        compare(button.Accessible.ignored, true, "an empty trailing is in the tree");
    }

    // #78: the field is named by its placeholder, unless `accessibleName` says otherwise (a
    // placeholder that is a glyph or a lesson in syntax); that placeholder is then the
    // description.
    function test_the_field_is_named_by_its_placeholder_or_its_accessible_name() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300,
                                                "placeholder": "filter torrents"
                                            });
        const input = findChild(field, "fieldInput");
        compare(input.Accessible.name, "filter torrents");
        compare(input.Accessible.description, "");
        field.placeholder = "∞";
        field.accessibleName = "download limit";
        compare(input.Accessible.name, "download limit");
        compare(input.Accessible.description, "∞");
    }

    // C18 D5 (board 45): no opener unless asked; with one, "places ▾" before the trailing, in
    // the accent, flipping to ▴ while expanded, and the text stops before it.
    function test_an_opener_sits_before_the_trailing_and_flips_its_arrow() {
        const plain = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 300
                                            });
        verify(!findChild(plain, "fieldOpenerText").visible, "an opener nobody asked for");
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 360,
                                                "opener": "places",
                                                "trailing": "browse",
                                                "trailingClickable": true
                                            });
        const opener = findChild(field, "fieldOpenerText");
        const trailing = findChild(field, "fieldTrailingText");
        verify(opener.visible);
        compare(opener.text, "places " + Glyphs.chevronDown);
        compare(opener.color, Theme.accent);
        verify(opener.x + opener.width <= trailing.x, "the opener is not before browse");
        const input = findChild(field, "fieldInput");
        verify(input.x + input.width <= opener.x, "the text runs under the opener");
        field.openerExpanded = true;
        compare(opener.text, "places " + Glyphs.chevronUp);
    }

    // A click and a screen reader's press both open it; a disabled field's opener takes none.
    function test_the_opener_takes_a_click_and_an_accessible_press() {
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 360,
                                                "opener": "places"
                                            });
        const clicked = createTemporaryObject(openerClickedSpy, testCase, {
                                                  "target": field
                                              });
        const button = findChild(field, "fieldOpenerButton");
        mouseClick(button);
        compare(clicked.count, 1);
        button.Accessible.pressAction();
        compare(clicked.count, 2);
        field.enabled = false;
        button.Accessible.pressAction();
        compare(clicked.count, 2, "a disabled field's opener ran");
    }

    // For a screen reader: with an opener the field is a combobox, collapsed or expanded, and
    // the opener a button with its description; without one, an editable text as before.
    function test_with_an_opener_the_field_reads_as_a_combobox() {
        const plain = createTemporaryObject(fieldComponent, testCase);
        compare(findChild(plain, "fieldInput").Accessible.role, Accessible.EditableText);
        verify(findChild(plain, "fieldOpenerButton").Accessible.ignored);
        const field = createTemporaryObject(fieldComponent, testCase, {
                                                "width": 360,
                                                "opener": "places",
                                                "openerDescription": "9 places"
                                            });
        const input = findChild(field, "fieldInput");
        const button = findChild(field, "fieldOpenerButton");
        compare(input.Accessible.role, Accessible.ComboBox);
        compare(button.Accessible.role, Accessible.Button);
        compare(button.Accessible.name, "places");
        compare(button.Accessible.description, "9 places");
        verify(!button.Accessible.expanded);
        field.openerExpanded = true;
        verify(button.Accessible.expanded);
        verify(input.Accessible.expanded);
    }
}
