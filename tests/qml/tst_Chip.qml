import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "Chip"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: chipComponent

        Chip {}
    }

    Component {
        id: clickedSpy

        SignalSpy {
            signalName: "clicked"
        }
    }

    function test_height_matches_the_spec_chip_height() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux"
                                           });
        compare(chip.height, Theme.space(22));
    }

    function test_idle_is_bordered() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux"
                                           });
        compare(chip.border.width, 1);
        fuzzyCompare(chip.color.a, Theme.fill(Theme.fillIdle).a, 0.01);
    }

    function test_selected_has_no_border_and_selected_fill() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux",
                                               "selected": true
                                           });
        compare(chip.border.width, 0);
        fuzzyCompare(chip.color.a, Theme.on.selected.fill.a, 0.01);
    }

    function test_hover_or_cursor_fills_at_the_hover_alpha() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux",
                                               "hasCursor": true
                                           });
        fuzzyCompare(chip.color.a, Theme.on.hover.fill.a, 0.01);
    }

    function test_click_emits_clicked() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux"
                                           });
        let clicks = 0;
        chip.clicked.connect(() => clicks++);
        chip.clicked();
        compare(clicks, 1);
    }

    // T4.5: `focusable` opts a chip into the keyboard (spec §3 Fig. 9's auth row, where the
    // three chips are the only way to choose). Off by default, so no other chip's tab order
    // moves.
    function test_focusable_joins_the_tab_order_and_space_picks() {
        const plain = createTemporaryObject(chipComponent, testCase, {
                                                "text": "linux"
                                            });
        compare(plain.activeFocusOnTab, false);

        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "API key",
                                               "focusable": true
                                           });
        compare(chip.activeFocusOnTab, true);
        let clicks = 0;
        chip.clicked.connect(() => clicks++);
        chip.forceActiveFocus();
        verify(chip.activeFocus);
        // Focused, the chip carries the focus ring outside its edge (review Q6), selected
        // or not, and its own border stays what it was.
        compare(findChild(chip, "focusRing").shown, true);
        verify(chip.border.color !== Theme.accent);
        chip.selected = true;
        compare(chip.border.width, 0);
        compare(findChild(chip, "focusRing").shown, true);
        keyClick(Qt.Key_Space);
        compare(clicks, 1);
        keyClick(Qt.Key_Return);
        compare(clicks, 2);
    }

    // M8: a focusable chip (the hero's alt-speed) runs on a click and leaves the keyboard
    // where it was, as Button does.
    function test_a_click_does_not_take_the_focus() {
        const holder = createTemporaryQmlObject("import QtQuick; Item { width: 10; height: 10 }",
                                                testCase);
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "alt-speed",
                                               "focusable": true,
                                               "x": 40,
                                               "y": 40
                                           });
        let clicks = 0;
        chip.clicked.connect(() => clicks++);
        holder.forceActiveFocus();
        mouseClick(chip);
        compare(clicks, 1);
        verify(holder.activeFocus, "the click took the keyboard");
        compare(chip.activeFocus, false);
    }

    // Manual box M14: a chip can carry the kit's Tooltip (the hero's alt-speed, glyph only
    // while off), shown on keyboard focus as on hover.
    function test_a_chip_tooltip_shows_on_focus() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "glyph": "⇅",
                                               "focusable": true,
                                               "tooltip": "alt-speed · A"
                                           });
        const tip = chip.tooltipItem;
        compare(tip.parent, chip.Window.window.contentItem);
        compare(tip.shown, false);
        chip.forceActiveFocus();
        tryCompare(tip, "shown", true);
        compare(tip.text, "alt-speed · A");
        chip.focus = false;
        compare(tip.shown, false);
    }

    function test_a_chip_without_tooltip_never_shows_one() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux",
                                               "focusable": true
                                           });
        chip.forceActiveFocus();
        wait(450);
        compare(chip.tooltipItem.shown, false);
    }

    // docs/32 L1: the log face's level chips, a key before the word and a count after it.
    function test_a_level_chip_draws_its_key_and_count() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "warning",
                                               "keyText": "3",
                                               "count": "10",
                                               "toggle": true,
                                               "selected": true,
                                               "tone": Theme.yellow
                                           });
        const key = findChild(chip, "chipKey");
        const count = findChild(chip, "chipCount");
        verify(key.visible);
        compare(key.text, "3");
        verify(count.visible);
        compare(count.text, "10");
        // DECISIONS C30 F1, F3: on the selected fill, its roles lifted against that fill.
        compare(chip.ground, Theme.on.selected);
        tryCompare(count, "color", Theme.on.selected.dimForeground);
        // The key is the dim role at full strength: fainter, it read under the floor (#188).
        tryCompare(key, "color", Theme.on.selected.dimForeground);
        compare(key.opacity, 1);
        // On, the word takes the level's colour, as it reads on the fill.
        tryCompare(findChild(chip, "chipLabel"), "color", Theme.on.selected.yellow);
        verify(!Qt.colorEqual(Theme.on.selected.dimForeground, Theme.dimForeground));
        // Wider than the word alone.
        const plain = createTemporaryObject(chipComponent, testCase, {
                                                "text": "warning"
                                            });
        verify(chip.implicitWidth > plain.implicitWidth);
        verify(!findChild(plain, "chipKey").visible);
        verify(!findChild(plain, "chipCount").visible);
    }

    function test_a_toggle_off_reads_dim_and_a_selected_one_without_tone_bright() {
        const off = createTemporaryObject(chipComponent, testCase, {
                                              "text": "normal",
                                              "toggle": true
                                          });
        // Off, the chip's idle fill is its ground (DECISIONS C30 F1).
        compare(off.ground, Theme.on.idle);
        compare(findChild(off, "chipLabel").color, Theme.on.idle.dimForeground);
        const on = createTemporaryObject(chipComponent, testCase, {
                                             "text": "normal",
                                             "toggle": true,
                                             "selected": true
                                         });
        compare(findChild(on, "chipLabel").color, Theme.on.selected.brightForeground);
        // A label chip, not a switch, keeps the foreground when off.
        const label = createTemporaryObject(chipComponent, testCase, {
                                                "text": "linux"
                                            });
        compare(findChild(label, "chipLabel").color, Theme.on.idle.foreground);
    }

    // docs/32 L2: the torrent chip, an accent edge, selected or not.
    function test_an_accent_chip_keeps_an_accent_edge() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "torrent: log55-move.bin",
                                               "accent": true
                                           });
        compare(chip.border.width, 1);
        compare(chip.border.color, Theme.accent);
        chip.selected = true;
        compare(chip.border.width, 1);
    }

    // docs/31 D3, D5: the hero's burst warns in green, and "until" follows the limits in the
    // same colour, at full strength: fainter, it read under C30's floor (#188).
    function test_a_warning_chip_takes_its_colour_and_a_suffix() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "burst ↓∞ ↑∞",
                                               "suffix": "· until 07:00",
                                               "warn": true,
                                               "warnColor": Theme.green
                                           });
        compare(chip.border.color, Theme.green);
        const label = findChild(chip, "chipLabel");
        const suffix = findChild(chip, "chipSuffix");
        compare(label.color, Theme.green);
        compare(suffix.color, Theme.green);
        verify(suffix.visible);
        compare(suffix.opacity, 1);
        compare(chip.Accessible.name, "burst ↓∞ ↑∞ · until 07:00");
        // Without a suffix nothing is drawn after the text, and warn is yellow by default.
        const plain = createTemporaryObject(chipComponent, testCase, {
                                                "text": "↓10 KB/s ↑∞",
                                                "warn": true
                                            });
        verify(!findChild(plain, "chipSuffix").visible);
        compare(plain.border.color, Theme.yellow);
    }

    // #75: a screen reader's press runs the click, once.
    function test_the_accessible_press_is_the_click() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux"
                                           });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": chip
                                              });
        chip.Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // #75: disabled, the chip ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_chip_ignores_the_accessible_press() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux"
                                           });
        const clicked = createTemporaryObject(clickedSpy, testCase, {
                                                  "target": chip
                                              });
        chip.enabled = false;
        chip.Accessible.pressAction();
        compare(clicked.count, 0);
    }

    // #79: the count a chip shows and its key are its description.
    function test_the_count_and_the_key_are_the_description() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "normal"
                                           });
        compare(chip.Accessible.description, "");
        chip.count = "19";
        compare(chip.Accessible.description, "19");
        chip.keyText = "1";
        compare(chip.Accessible.name, "normal");
        compare(chip.Accessible.description, "19 · shortcut 1");
    }

    // #91: a chip that is a choice says whether it is on; one that only acts says nothing.
    function test_a_choice_chip_publishes_its_state() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "API key"
                                           });
        verify(!chip.Accessible.checkable);
        chip.selected = true;
        verify(!chip.Accessible.checked, "a chip that is no choice reads as checked");
        chip.checkable = true;
        verify(chip.Accessible.checkable);
        verify(chip.Accessible.checked);
        chip.selected = false;
        verify(!chip.Accessible.checked);
    }
}
