import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    // The fold tests press and drag the divider: the runner delivers mouse events only to a
    // shown window with room for the split.
    height: 800
    name: "SplitLayout"
    visible: true
    when: windowShown
    width: 1600

    Component {
        id: splitComponent

        SplitLayout {}
    }

    Component {
        id: itemComponent

        Item {}
    }

    function test_docked_above_the_breakpoint() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": split_breakpoint(),
                                                "height": 600
                                            });
        compare(split.docked, true);
    }

    function test_undocked_below_the_breakpoint() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": split_breakpoint() - 1,
                                                "height": 600
                                            });
        compare(split.docked, false);
    }

    function test_breakpoint_reads_the_reference_width() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": split_breakpoint() - 200,
                                                "height": 600,
                                                "referenceWidth": split_breakpoint()
                                            });
        compare(split.docked, true);
        split.referenceWidth = split_breakpoint() - 1;
        compare(split.docked, false);
    }

    function test_auto_docks_only_from_16_by_10() {
        const wide = split_breakpoint();
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": wide,
                                                "height": Math.floor(wide / 1.6)
                                            });
        compare(split.docked, true);
        // As wide as the breakpoint but taller than 16:10: the details go to the bottom.
        split.height = Math.floor(wide / 1.6) + 2;
        compare(split.docked, false);
        // Half of a 3440 x 1440 ultrawide, at any scale: bottom.
        split.width = Math.max(wide, 1720);
        split.height = Math.round(split.width * 1440 / 1720);
        compare(split.docked, false);
        // A pinned side ignores the shape.
        split.placement = "right";
        compare(split.docked, true);
    }

    function test_shape_reads_the_reference_height() {
        const wide = split_breakpoint();
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": wide,
                                                "height": 100,
                                                "referenceHeight": wide
                                            });
        compare(split.docked, false);
        split.referenceHeight = Math.floor(wide / 1.6);
        compare(split.docked, true);
    }

    function test_breakpoint_is_scaled_space() {
        const split = createTemporaryObject(splitComponent, testCase);
        compare(split.breakpoint, Theme.space(1100));
    }

    function test_default_side_sizes() {
        const split = createTemporaryObject(splitComponent, testCase);
        compare(split.sideWidth, Theme.space(330));
        compare(split.sideHeight, Theme.space(260));
    }

    function test_docked_geometry_puts_side_at_full_height() {
        const main = createTemporaryObject(itemComponent, testCase);
        const side = createTemporaryObject(itemComponent, testCase);
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 1400,
                                                "height": 600,
                                                "main": main,
                                                "side": side
                                            });
        compare(split.docked, true);
        compare(side.width, split.sideWidth);
        compare(side.height, 600);
        compare(main.width, 1400 - split.sideWidth - 1);
        compare(main.height, 600);
    }

    function test_undocked_geometry_puts_side_under_main() {
        const main = createTemporaryObject(itemComponent, testCase);
        const side = createTemporaryObject(itemComponent, testCase);
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 800,
                                                "height": 600,
                                                "main": main,
                                                "side": side
                                            });
        compare(split.docked, false);
        compare(side.width, 800);
        compare(side.height, split.sideHeight);
        compare(main.width, 800);
        compare(main.height, 600 - split.sideHeight - 1);
    }

    function test_side_visible_false_collapses_the_side() {
        const main = createTemporaryObject(itemComponent, testCase);
        const side = createTemporaryObject(itemComponent, testCase);
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 1400,
                                                "height": 600,
                                                "main": main,
                                                "side": side,
                                                "sideVisible": false
                                            });
        compare(main.width, 1400);
        compare(main.height, 600);
    }

    function split_breakpoint() {
        return Theme.space(1100);
    }

    // docs/10 §5: a pinned placement ignores the breakpoint.
    function test_placement_pins_the_side() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 800,
                                                "height": 600
                                            });
        compare(split.docked, false);
        split.placement = "right";
        compare(split.docked, true);
        split.width = 3000;
        split.placement = "bottom";
        compare(split.docked, false);
        split.placement = "auto";
        compare(split.docked, true);
    }

    // docs/10 §5: `<` `>` resize along the docked axis, clamped, and say so.
    function test_resize_by_is_clamped_and_signals() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 1600,
                                                "height": 600
                                            });
        let resized = 0;
        split.resized.connect(() => resized += 1);
        const before = split.shownSideWidth;
        split.resizeBy(40);
        compare(split.shownSideWidth, before + 40);
        compare(resized, 1);
        split.resizeBy(100000);
        compare(split.shownSideWidth, split.maximumSideWidth);
        split.resizeBy(-100000);
        compare(split.shownSideWidth, split.minimumSideWidth);

        split.placement = "bottom";
        split.resizeBy(-100000);
        compare(split.shownSideHeight, split.minimumSideHeight);
        split.resizeBy(100000);
        compare(split.shownSideHeight, Math.round(600 * 0.6));
    }

    // ---- docs/17 M10: folding ---------------------------------------------------------------

    Component {
        id: barComponent

        Item {
            implicitHeight: 31
        }
    }

    /// A bottom split (800 × 600) with a main, a side and a one-line folded bar.
    function bottomSplit() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 800,
                                                "height": 600,
                                                "placement": "bottom",
                                                "main": createTemporaryObject(itemComponent, testCase),
                                                "side": createTemporaryObject(itemComponent, testCase),
                                                "foldedBar": createTemporaryObject(barComponent,
                                                                                   testCase)
                                            });
        waitForRendering(split);
        return split;
    }

    /// Press the divider's grip, move so the bottom side would be `size` tall (without
    /// releasing), and return the grip for the caller's `mouseRelease`.
    function dragBottomTo(split, size) {
        const grip = findChild(split, "grip");
        mousePress(grip, 40, grip.height / 2);
        // First past the drag distance, as a hand would, then onto the size asked for.
        mouseMove(grip, 40, grip.height / 2 - 30);
        const y = split.height - 1 - size;
        mouseMove(grip, 40, y - grip.y);
        return grip;
    }

    // Diogo's rule: at the default size, a double-click folds; again, it unfolds to it.
    function test_a_double_click_folds_and_unfolds_at_the_default() {
        const split = bottomSplit();
        let toggles = 0;
        split.foldToggled.connect(() => toggles += 1);

        mouseDoubleClickSequence(findChild(split, "grip"), 40, 3);
        compare(split.folded, true);
        compare(split.main.height, 600 - 31 - 1, "the table does not get the pane's room");
        compare(split.foldedBar.height, 31);
        verify(split.foldedBar.parent.visible);
        verify(!split.side.parent.visible);
        compare(toggles, 1);

        mouseDoubleClickSequence(findChild(split, "grip"), 40, 3);
        compare(split.folded, false);
        compare(split.sideHeight, split.defaultSideHeight);
        compare(split.side.height, split.defaultSideHeight);
        compare(toggles, 2);
    }

    // …and at a size the user dragged, unfolding brings that size back, not the default.
    function test_folding_keeps_a_custom_size() {
        const split = bottomSplit();
        split.sideHeight = Theme.space(200);
        split.toggleFold();
        compare(split.folded, true);
        split.toggleFold();
        compare(split.folded, false);
        compare(split.sideHeight, Theme.space(200));
        compare(split.side.height, Theme.space(200));
    }

    // docs/17 M10.2: within 16 u of the default a drag lands on it and the tip says so.
    function test_a_drag_near_the_default_snaps_to_it() {
        const split = bottomSplit();
        split.sideHeight = Theme.space(160);
        const tip = findChild(split, "sizeTip");

        const grip = dragBottomTo(split, split.defaultSideHeight + Theme.space(10));
        compare(split.dragging, true);
        compare(split.snapped, true);
        compare(split.sideHeight, split.defaultSideHeight);
        verify(tip.visible);
        compare(tip.border.color, Theme.accent);

        // Further than the pull: the size follows the pointer, and the tip shows it.
        mouseMove(grip, 40, split.height - 1 - (split.defaultSideHeight + Theme.space(40)) - grip.y);
        compare(split.snapped, false);
        compare(split.sideHeight, split.defaultSideHeight + Theme.space(40));

        let resized = 0;
        split.resized.connect(() => resized += 1);
        mouseRelease(grip, 40, grip.height / 2);
        compare(resized, 1);
        verify(!tip.visible);
    }

    // A click on the divider is not a drag: nothing moves.
    function test_a_click_on_the_divider_changes_nothing() {
        const split = bottomSplit();
        mouseClick(findChild(split, "grip"), 40, 3);
        compare(split.folded, false);
        compare(split.sideHeight, split.defaultSideHeight);
    }

    // Dragging a folded pane unfolds it to where it is let go.
    function test_dragging_a_folded_pane_unfolds_it_there() {
        const split = bottomSplit();
        split.toggleFold();
        let toggles = 0;
        split.foldToggled.connect(() => toggles += 1);

        const grip = dragBottomTo(split, Theme.space(200));
        compare(split.folded, false);
        compare(split.sideHeight, Theme.space(200));
        mouseRelease(grip, 40, grip.height / 2);
        compare(toggles, 1);
    }

    // docs/17 M10.4: on the right the same rules; folded is a 12 u column, and a click on it
    // unfolds.
    function test_the_right_dock_folds_to_a_column() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 1400,
                                                "height": 600,
                                                "placement": "bottom",
                                                "main": createTemporaryObject(itemComponent, testCase),
                                                "side": createTemporaryObject(itemComponent, testCase),
                                                "foldedBar": createTemporaryObject(barComponent,
                                                                                   testCase)
                                            });
        // Moved from the bottom, as `I` does: the column's chevron must follow.
        split.placement = "right";
        waitForRendering(split);
        mouseDoubleClickSequence(findChild(split, "grip"), 3, 300);
        compare(split.folded, true);
        const column = findChild(split, "foldedColumn");
        verify(column.visible);
        compare(column.width, Theme.space(12));
        compare(column.height, 600);
        compare(split.main.width, 1400 - Theme.space(12) - 1);
        verify(!split.foldedBar.parent.visible, "the bottom strip showed on the right");
        const chevron = findChild(split, "foldedChevron");
        const at = chevron.mapToItem(column, 0, 0);
        verify(at.x >= 0 && at.x + chevron.width <= column.width,
               "the ‹ is drawn outside its column, at " + at.x);

        mouseClick(column, column.width / 2, 300);
        compare(split.folded, false);
        compare(split.side.width, split.shownSideWidth);
    }

    // A size chosen in a wide window is kept, but drawn clamped in a narrow one.
    function test_a_kept_size_is_clamped_to_the_room() {
        const split = createTemporaryObject(splitComponent, testCase, {
                                                "width": 1600,
                                                "height": 600,
                                                "placement": "right",
                                                "sideWidth": Theme.space(540)
                                            });
        compare(split.shownSideWidth, Theme.space(540));
        split.width = Theme.space(600);
        compare(split.sideWidth, Theme.space(540));
        compare(split.shownSideWidth, split.maximumSideWidth);
    }
}
