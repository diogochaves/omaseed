import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "ProgressLine"

    Component {
        id: progressComponent

        ProgressLine {}
    }

    function test_track_is_two_pixels_tall() {
        const line = createTemporaryObject(progressComponent, testCase);
        compare(line.implicitHeight, Theme.space(2));
    }

    function test_default_colour_is_accent() {
        const line = createTemporaryObject(progressComponent, testCase);
        compare(line.color, Theme.accent);
    }

    function test_percent_text_at_zero() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 0
                                           });
        compare(line._percentText, "0.0%");
    }

    function test_percent_text_rounds_to_one_decimal() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 0.489
                                           });
        compare(line._percentText, "48.9%");
    }

    function test_percent_text_is_100_percent_at_full() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 1
                                           });
        compare(line._percentText, "100%");
    }

    function test_value_is_clamped_above_one() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 1.4
                                           });
        compare(line._clamped, 1);
        compare(line._percentText, "100%");
    }

    function test_value_is_clamped_below_zero() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": -0.2
                                           });
        compare(line._clamped, 0);
    }

    function test_show_text_defaults_to_true() {
        const line = createTemporaryObject(progressComponent, testCase);
        compare(line.showText, true);
    }
}
