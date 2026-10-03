import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "StackStrip"

    Component {
        id: stripComponent

        StackStrip {
            width: 200
        }
    }

    function test_height_is_twelve_spec_pixels() {
        const strip = createTemporaryObject(stripComponent, testCase);
        compare(strip.implicitHeight, Theme.space(12));
    }

    function test_blocks_are_proportional_with_gaps_and_zeros_skipped() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "segments": [{
                                                        "value": 1,
                                                        "color": Theme.yellow
                                                    }, {
                                                        "value": 0,
                                                        "color": Theme.green
                                                    }, {
                                                        "value": 3,
                                                        "color": Theme.green
                                                    }]
                                            });
        const gap = Theme.space(2);
        const available = 200 - gap;
        const b = strip._blocks;
        compare(b.length, 3);
        fuzzyCompare(b[0].x, 0, 1e-6);
        fuzzyCompare(b[0].width, available / 4, 1e-6);
        compare(b[1].width, 0);
        fuzzyCompare(b[2].x, available / 4 + gap, 1e-6);
        fuzzyCompare(b[2].width, available * 3 / 4, 1e-6);
        fuzzyCompare(b[2].x + b[2].width, 200, 1e-6);
    }

    // A tiny share still shows, at the minimum width; the others give the room back, so the
    // row still ends at the right edge.
    function test_small_segment_gets_the_minimum_and_the_row_still_fits() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "segments": [{
                                                        "value": 1
                                                    }, {
                                                        "value": 1000
                                                    }, {
                                                        "value": 1000
                                                    }]
                                            });
        const b = strip._blocks;
        fuzzyCompare(b[0].width, Theme.space(3), 1e-6);
        fuzzyCompare(b[1].width, b[2].width, 1e-6);
        fuzzyCompare(b[2].x + b[2].width, 200, 1e-6);
    }

    function test_nothing_to_show() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "segments": [{
                                                        "value": 0
                                                    }]
                                            });
        compare(strip._blocks.length, 1);
        compare(strip._blocks[0].width, 0);
    }

    function test_one_segment_fills_the_row() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "segments": [{
                                                        "value": 7,
                                                        "hatched": true
                                                    }]
                                            });
        fuzzyCompare(strip._blocks[0].width, 200, 1e-6);
    }
}
