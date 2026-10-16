import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "Button"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: buttonComponent

        Button {}
    }

    Component {
        id: clickedSpy

        SignalSpy {
            signalName: "clicked"
        }
    }

    function test_height_is_control_height() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        compare(button.height, Theme.controlHeight);
    }

    // KIT-13: a text glyph can stand in for the name; an SVG one ("icon:gear") cannot.
    function test_the_accessible_name_never_reads_an_icon_path() {
        const plain = createTemporaryObject(buttonComponent, testCase, {
                                                "glyph": "+"
                                            });
        compare(plain.Accessible.name, "+");
        const svg = createTemporaryObject(buttonComponent, testCase, {
                                              "glyph": "icon:gear"
                                          });
        compare(svg.Accessible.name, "");
        svg.text = "settings";
        compare(svg.Accessible.name, "settings");
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

    // Review Q6: focus is the ring outside the edge, so a focused ordinary button no longer
    // wears the primary's accent outline.
    function test_focus_draws_the_ring_and_leaves_the_border_alone() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "test"
                                             });
        const ring = findChild(button, "focusRing");
        verify(ring);
        compare(ring.shown, false);
        button.forceActiveFocus();
        compare(ring.shown, true);
        compare(ring.x, -Theme.focusExtent);
        compare(button.border.width, 0);
    }

    // M8: a click runs the button and leaves the keyboard where it was, so no ring stays
    // behind and whatever had the focus keeps its keys.
    function test_a_click_does_not_take_the_focus() {
        const holder = createTemporaryQmlObject("import QtQuick; Item { width: 10; height: 10 }",
                                                testCase);
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "test",
                                                 "x": 40,
                                                 "y": 40
                                             });
        let clicks = 0;
        button.clicked.connect(() => clicks++);
        holder.forceActiveFocus();
        mouseClick(button);
        compare(clicks, 1);
        verify(holder.activeFocus, "the click took the keyboard");
        compare(button.activeFocus, false);
        compare(findChild(button, "focusRing").shown, false);
    }

    function test_a_focused_primary_keeps_its_outline_inside_the_ring() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "edit",
                                                 "primary": true
                                             });
        button.forceActiveFocus();
        compare(button.border.color, Theme.accent);
        compare(button.border.width, 1);
        compare(findChild(button, "focusRing").shown, true);
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

    // #75: a screen reader's press runs the click, once.
    function test_the_accessible_press_is_the_click() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": button
                                              });
        button.Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // #75: disabled, the button ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_button_ignores_the_accessible_press() {
        const button = createTemporaryObject(buttonComponent, testCase, {
                                                 "text": "Add"
                                             });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": button
                                              });
        button.enabled = false;
        button.Accessible.pressAction();
        compare(clicked.count, 0);
    }
}
