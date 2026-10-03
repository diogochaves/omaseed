import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "RatioLadder"
    visible: true
    when: windowShown
    width: 800

    readonly property real gib: 1024 * 1024 * 1024

    Component {
        id: ladderComponent

        RatioLadder {
            height: 150
            width: 600
        }
    }

    function test_log_axis_ends_and_clamps() {
        const ladder = createTemporaryObject(ladderComponent, testCase);
        fuzzyCompare(ladder._x(0.1), ladder._padLeft, 1e-6);
        fuzzyCompare(ladder._x(30), 600 - ladder._padRight, 1e-6);
        fuzzyCompare(ladder._x(0.01), ladder._x(0.1), 1e-6);
        fuzzyCompare(ladder._x(0), ladder._x(0.1), 1e-6);
        fuzzyCompare(ladder._x(500), ladder._x(30), 1e-6);
        const span = 600 - ladder._padLeft - ladder._padRight;
        fuzzyCompare(ladder._x(1), ladder._padLeft + span * 1 / Math.log10(300), 1e-6);
        // Equal ratios are equal distances on a log axis.
        fuzzyCompare(ladder._x(2) - ladder._x(1), ladder._x(10) - ladder._x(5), 1e-6);
    }

    function test_radius_from_size() {
        const ladder = createTemporaryObject(ladderComponent, testCase);
        const s = ladder._scale;
        fuzzyCompare(ladder._radius(0), 2.5 * s, 1e-6);
        fuzzyCompare(ladder._radius(1 * testCase.gib), 2.5 * s, 1e-6);
        fuzzyCompare(ladder._radius(4 * testCase.gib), 3.2 * s, 1e-6);
        fuzzyCompare(ladder._radius(25 * testCase.gib), 8 * s, 1e-6);
        fuzzyCompare(ladder._radius(400 * testCase.gib), 11 * s, 1e-6);
    }

    // The biggest is placed first, on the middle line; equal dots after it step away from it,
    // alternating up and down, until none overlaps.
    function test_beeswarm_keeps_dots_apart() {
        const pts = [];
        for (let i = 0; i < 6; i++)
            pts.push({
                         "ratio": 1.5,
                         "size": (i === 3 ? 30 : 1) * testCase.gib,
                         "name": "t" + i
                     });
        const ladder = createTemporaryObject(ladderComponent, testCase, {
                                                 "points": pts
                                             });
        const dots = ladder._dots;
        compare(dots.length, 6);
        compare(dots[0].index, 3);
        fuzzyCompare(dots[0].y, ladder._midY, 1e-6);
        for (let a = 0; a < dots.length; a++) {
            for (let b = a + 1; b < dots.length; b++)
                verify(Math.hypot(dots[a].x - dots[b].x, dots[a].y - dots[b].y) > dots[a].r + dots[b].r + 1);
        }
    }

    function test_two_small_equal_dots() {
        const ladder = createTemporaryObject(ladderComponent, testCase, {
                                                 "points": [{
                                                         "ratio": 2,
                                                         "size": testCase.gib
                                                     }, {
                                                         "ratio": 2,
                                                         "size": testCase.gib
                                                     }]
                                             });
        const s = ladder._scale;
        // Radius 2.5: -4 and +4 overlap the first dot, -8 is the first free place.
        fuzzyCompare(ladder._dots[1].y, ladder._midY - 8 * s, 1e-6);
    }

    function test_hover_finds_the_dot() {
        const ladder = createTemporaryObject(ladderComponent, testCase, {
                                                 "points": [{
                                                         "ratio": 0.3,
                                                         "size": 9 * testCase.gib
                                                     }, {
                                                         "ratio": 5,
                                                         "size": 2 * testCase.gib
                                                     }]
                                             });
        compare(ladder.hoveredIndex, -1);
        ladder._hoverY = ladder._midY;
        ladder._hoverX = ladder._x(5);
        compare(ladder.hoveredIndex, 1);
        ladder._hoverX = ladder._x(0.3) + 1;
        compare(ladder.hoveredIndex, 0);
        ladder._hoverX = ladder._x(1);
        compare(ladder.hoveredIndex, -1);
        compare(ladder.indexAt(ladder._x(5), ladder._midY), 1);
    }

    function test_defaults() {
        const ladder = createTemporaryObject(ladderComponent, testCase);
        compare(ladder.paidText, "paid back");
        compare(ladder._dots.length, 0);
        compare(ladder._ticks.length, 8);
    }
}
