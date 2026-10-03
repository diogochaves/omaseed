import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Sparkline"

    Component {
        id: sparkComponent

        Sparkline {
            height: 30
            width: 100
        }
    }

    function test_defaults() {
        const spark = createTemporaryObject(sparkComponent, testCase);
        compare(spark.color, Theme.stateSeeding);
        fuzzyCompare(spark.lineAlpha, 0.55, 1e-6);
        fuzzyCompare(spark.fillAlpha, 0.08, 1e-6);
        compare(spark.maxValue, 0);
    }

    function test_no_values_draws_nothing() {
        const spark = createTemporaryObject(sparkComponent, testCase);
        verify(!spark._drawn);
        compare(spark._points.length, 0);
        compare(spark._areaPoints.length, 0);
    }

    function test_one_value_draws_nothing() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [5]
                                            });
        verify(!spark._drawn);
        compare(spark._points.length, 0);
    }

    // Spread evenly across the width; the largest value half a pixel under the top, zero half
    // a pixel above the bottom, so the 1 px stroke stays whole.
    function test_points_span_the_box() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [0, 5, 10]
                                            });
        verify(spark._drawn);
        compare(spark._max, 10);
        compare(spark._points.length, 3);
        fuzzyCompare(spark._points[0].x, 0, 1e-6);
        fuzzyCompare(spark._points[0].y, 29.5, 1e-6);
        fuzzyCompare(spark._points[1].x, 50, 1e-6);
        fuzzyCompare(spark._points[1].y, 15, 1e-6);
        fuzzyCompare(spark._points[2].x, 100, 1e-6);
        fuzzyCompare(spark._points[2].y, 0.5, 1e-6);
    }

    function test_area_closes_down_to_the_bottom_edge() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [1, 2]
                                            });
        const area = spark._areaPoints;
        compare(area.length, 4);
        compare(area[2].x, 100);
        compare(area[2].y, 30);
        compare(area[3].x, 0);
        compare(area[3].y, 30);
    }

    function test_fixed_max_scales_and_clamps() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [10, 40],
                                                "maxValue": 20
                                            });
        compare(spark._max, 20);
        fuzzyCompare(spark._points[0].y, 15, 1e-6);
        fuzzyCompare(spark._points[1].y, 0.5, 1e-6);
    }

    function test_non_numbers_count_as_zero() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [null, 4, NaN]
                                            });
        compare(spark._max, 4);
        fuzzyCompare(spark._points[0].y, 29.5, 1e-6);
        fuzzyCompare(spark._points[2].y, 29.5, 1e-6);
    }

    function test_all_zero_lies_on_the_bottom() {
        const spark = createTemporaryObject(sparkComponent, testCase, {
                                                "values": [0, 0, 0]
                                            });
        compare(spark._max, 0);
        for (const p of spark._points)
            fuzzyCompare(p.y, 29.5, 1e-6);
    }
}
