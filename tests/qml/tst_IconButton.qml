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

    Component {
        id: clickedSpy

        SignalSpy {
            signalName: "clicked"
        }
    }

    function test_is_a_square_control_height() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        compare(button.height, Theme.controlHeight);
        compare(button.width, Theme.controlHeight);
    }

    // #78: named by its tooltip, then by nothing; never by its glyph, which a screen reader
    // reads as noise or nothing.
    function test_the_accessible_name_is_the_tooltip_never_the_glyph() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        compare(button.Accessible.name, "");
        button.tooltip = "add";
        compare(button.Accessible.name, "add");
        const svg = createTemporaryObject(iconButtonComponent, testCase, {
                                              "glyph": "icon:gear"
                                          });
        compare(svg.Accessible.name, "");
    }

    function test_hover_or_cursor_fills_at_the_hover_alpha() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "hasCursor": true
                                             });
        fuzzyCompare(button.color.a, Theme.on.hover.fill.a, 0.01);
    }

    function test_selected_fills_at_the_selected_alpha() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "selected": true
                                             });
        fuzzyCompare(button.color.a, Theme.on.selected.fill.a, 0.01);
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

    // Review Q8: the button's name is the kit's one Tooltip, with its accent edge and its
    // 400 ms rest, shown on keyboard focus as on hover, under the button.
    function test_keyboard_focus_shows_the_kit_tooltip_under_the_button() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "tooltip": "add torrent",
                                                 "x": 40,
                                                 "y": 20
                                             });
        const tip = button.tooltipItem;
        compare(tip.delay, 400);
        compare(tip.parent, button.Window.window.contentItem);
        compare(tip.shown, false);
        button.forceActiveFocus();
        tryCompare(tip, "shown", true);
        compare(tip.text, "add torrent");
        const panel = tip.panelItem;
        compare(findChild(tip, "tooltipPlate").edgeColor, Theme.accent);
        const under = button.mapToItem(tip, 0, button.height);
        verify(panel.y >= under.y, "the tip is not under the button");
        button.focus = false;
        compare(tip.shown, false);
    }

    // M8: a click leaves the keyboard where it was, so no ring stays behind and the tip is
    // the hover's alone (tst_Main checks it with the title bar's + add; this runner never
    // delivers the hover leaving a lone button).
    function test_a_click_does_not_take_the_focus() {
        const holder = createTemporaryQmlObject("import QtQuick; Item { width: 10; height: 10 }",
                                                testCase);
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+",
                                                 "tooltip": "settings",
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

    function test_no_tooltip_text_never_shows() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        button.forceActiveFocus();
        wait(450);
        compare(button.tooltipItem.shown, false);
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

    // #75: a screen reader's press runs the click, once.
    function test_the_accessible_press_is_the_click() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": button
                                              });
        button.Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // #75: disabled, the button ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_button_ignores_the_accessible_press() {
        const button = createTemporaryObject(iconButtonComponent, testCase, {
                                                 "glyph": "+"
                                             });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": button
                                              });
        button.enabled = false;
        button.Accessible.pressAction();
        compare(clicked.count, 0);
    }
}
