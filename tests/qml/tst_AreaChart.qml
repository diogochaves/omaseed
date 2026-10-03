import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "AreaChart"
    visible: true
    when: windowShown
    width: 800

    Component {
        id: chartComponent

        AreaChart {
            height: 190
            width: 600 + Theme.space(54)
        }
    }

    function pt(up, down, gap) {
        return {
            "up": up,
            "down": down,
            "gap": gap === true
        };
    }

    function test_x_and_index_are_inverse_across_the_plot() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 0), pt(2, 0), pt(3, 0), pt(4, 0), pt(5, 0)]
                                            });
        compare(chart._right, 600);
        compare(chart.xAt(0), 0);
        compare(chart.xAt(4), 600);
        compare(chart.xAt(2), 300);
        for (let i = 0; i < 5; i++)
            compare(chart.indexAt(chart.xAt(i)), i);
        compare(chart.indexAt(70), 0);
        compare(chart.indexAt(80), 1);
        compare(chart.indexAt(-50), 0);
        compare(chart.indexAt(5000), 4);
    }

    function test_no_points() {
        const chart = createTemporaryObject(chartComponent, testCase);
        compare(chart.indexAt(10), -1);
        compare(chart._upLine.length, 0);
        compare(chart._upArea.length, 0);
        compare(chart.hoveredIndex, -1);
        verify(!chart._haloRunning);
    }

    // Upload gets upShare of the height above the baseline, scaled to its own peak; download
    // the rest below, scaled to max(maxDown, maxUp / 2).
    function test_scale_splits_the_height_at_the_up_share() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(100, 10), pt(400, 50), pt(200, 0)]
                                            });
        const g = chart._geo;
        const mid = chart._top + (chart._bottom - chart._top) * 0.66;
        fuzzyCompare(g.mid, mid, 1e-6);
        compare(g.maxUp, 400);
        compare(g.maxDown, 50);
        fuzzyCompare(chart._upY(400), chart._top, 1e-6);
        fuzzyCompare(chart._upLine[1].y, chart._top, 1e-6);
        // Download is small, so it is scaled to half the upload peak (200), not to its own 50.
        fuzzyCompare(chart._downY(200), chart._bottom, 1e-6);
        fuzzyCompare(chart._downLine[1].y, mid + 50 * (chart._bottom - mid) / 200, 1e-6);
    }

    function test_big_download_scales_to_itself() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(100, 900), pt(100, 300)]
                                            });
        fuzzyCompare(chart._downLine[0].y, chart._bottom, 1e-6);
    }

    function test_up_share_moves_the_baseline() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 1), pt(2, 1)],
                                                "upShare": 0.5
                                            });
        fuzzyCompare(chart._geo.mid, (chart._top + chart._bottom) / 2, 1e-6);
    }

    function test_areas_close_on_the_baseline() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 1), pt(2, 3), pt(3, 2)]
                                            });
        const area = chart._upArea;
        compare(area.length, 5);
        fuzzyCompare(area[3].x, 600, 1e-6);
        fuzzyCompare(area[3].y, chart._geo.mid, 1e-6);
        fuzzyCompare(area[4].x, 0, 1e-6);
        fuzzyCompare(area[4].y, chart._geo.mid, 1e-6);
        fuzzyCompare(chart._downArea[3].y, chart._geo.mid, 1e-6);
    }

    function test_points_before_the_history_are_left_out() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(null, null), pt(null, null), pt(5, 1), pt(7, 0)]
                                            });
        compare(chart._geo.firstKnown, 2);
        compare(chart._geo.known, 2);
        compare(chart._upLine.length, 2);
        fuzzyCompare(chart._upLine[0].x, chart.xAt(2), 1e-6);
        fuzzyCompare(chart._upArea[3].x, chart.xAt(2), 1e-6);
    }

    function test_gap_runs_are_grouped_and_captioned() {
        const calls = [];
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 0), pt(1, 0, true), pt(1, 0, true), pt(1, 0), pt(1, 0, true), pt(1, 0)],
                                                "gapCaption": function (first, last) {
                                                    calls.push([first, last]);
                                                    return "closed";
                                                }
                                            });
        const gaps = chart._geo.gaps;
        compare(gaps.length, 2);
        compare(gaps[0].first, 1);
        compare(gaps[0].last, 2);
        compare(gaps[1].first, 4);
        compare(gaps[1].last, 4);
        verify(calls.some(c => c[0] === 1 && c[1] === 2));
        verify(calls.some(c => c[0] === 4 && c[1] === 4));
    }

    function test_peak_is_the_largest_upload() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(null, null), pt(3, 0), pt(9, 0), pt(4, 0)],
                                                "formatValue": function (v) {
                                                    return v + " B/s";
                                                }
                                            });
        compare(chart._geo.peakIndex, 2);
        compare(chart.peakText.arg(chart.formatValue(9)), "peak 9 B/s");
    }

    function test_no_peak_when_nothing_was_given() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(0, 0), pt(0, 2)]
                                            });
        compare(chart._geo.peakIndex, -1);
    }

    function test_hover_maps_to_an_index_and_away_is_minus_one() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 0), pt(2, 0), pt(3, 0)]
                                            });
        compare(chart.hoveredIndex, -1);
        chart._hoverX = 290;
        compare(chart.hoveredIndex, 1);
        chart._hoverX = 600;
        compare(chart.hoveredIndex, 2);
        // In the value gutter: not over the plot.
        chart._hoverX = 620;
        compare(chart.hoveredIndex, -1);
        chart._hoverX = -1;
        compare(chart.hoveredIndex, -1);
    }

    // The halo animates only while the chart is visible and the last point is known.
    function test_halo_runs_only_while_visible() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(1, 0), pt(2, 0)]
                                            });
        verify(chart._haloRunning);
        chart.visible = false;
        verify(!chart._haloRunning);
        chart.visible = true;
        verify(chart._haloRunning);
        chart.animate = false;
        verify(!chart._haloRunning);
    }

    function test_no_halo_when_the_last_point_is_unknown() {
        const chart = createTemporaryObject(chartComponent, testCase, {
                                                "points": [pt(null, null), pt(null, null)]
                                            });
        verify(!chart._lastKnown);
        verify(!chart._haloRunning);
    }

    function test_defaults() {
        const chart = createTemporaryObject(chartComponent, testCase);
        compare(chart.upColor, Theme.stateSeeding);
        compare(chart.downColor, Theme.stateDownloading);
        fuzzyCompare(chart.upShare, 0.66, 1e-6);
        compare(chart.beforeText, "before the ledger");
        compare(chart.formatValue(12), "12");
        compare(chart.gapCaption(0, 1), "");
    }
}
