import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Chip"

    Component {
        id: chipComponent

        Chip {}
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
        fuzzyCompare(chip.color.a, Theme.fill(Theme.fillSelected).a, 0.01);
    }

    function test_hover_or_cursor_fills_at_the_hover_alpha() {
        const chip = createTemporaryObject(chipComponent, testCase, {
                                               "text": "linux",
                                               "hasCursor": true
                                           });
        fuzzyCompare(chip.color.a, Theme.fill(Theme.fillHover).a, 0.01);
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
        // Focused, the chip carries the accent ring even while selected.
        compare(chip.border.color, Theme.accent);
        keyClick(Qt.Key_Space);
        compare(clicks, 1);
        keyClick(Qt.Key_Return);
        compare(clicks, 2);
    }
}
