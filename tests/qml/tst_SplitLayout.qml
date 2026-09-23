import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "SplitLayout"

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
}
