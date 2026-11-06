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

    // #171 step 6: the line is the skin's Meter, drawn to the fraction as it always was.
    function test_line_is_the_skins_meter() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 0.5,
                                               "showText": false
                                           });
        const meter = findChild(line, "progressLineMeter");
        verify(meter !== null);
        compare(meter.height, Theme.progressLineThickness);
        compare(meter.value, 0.5);
        verify(!meter.snap);
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

    // REG-3: truncated like Format::percentCompact, so only a complete value reads "100%"
    // (and never "100.0%", KIT-15).
    function test_just_under_one_never_reads_one_hundred() {
        const line = createTemporaryObject(progressComponent, testCase, {
                                               "value": 0.9996
                                           });
        compare(line._percentText, "99.9%");
        line.value = 0.99999;
        compare(line._percentText, "99.9%");
        line.value = 0.9994;
        compare(line._percentText, "99.9%");
        line.value = 0.4895;
        compare(line._percentText, "48.9%");
        line.value = 1;
        compare(line._percentText, "100%");
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
