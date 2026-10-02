import QtQuick
import QtTest
import "../../plugin/Format.js" as Format

// #16 (T6.5). The bar widget's number rules (docs/07 §2, docs/20 §2): the bar's short form
// and its fixed widths, the popup's full units, ETA, relative time, ratios.
TestCase {
    name: "PluginFormat"

    readonly property real k: 1024
    readonly property real m: 1024 * 1024
    readonly property real g: 1024 * 1024 * 1024

    function test_bar_value_boundaries() {
        compare(Format.barValue(0), "0");
        compare(Format.barValue(999), "0");
        compare(Format.barValue(1000), "0");
        compare(Format.barValue(1023), "0");
        compare(Format.barValue(1024), "1.0K");
        compare(Format.barValue(10 * m), "10M");
        compare(Format.barValue(999 * m), "999M");
        compare(Format.barValue(g), "1.0G");
    }

    function test_bar_value_decimals_under_ten_only() {
        compare(Format.barValue(4.21 * m), "4.2M");
        compare(Format.barValue(842 * k), "842K");
        compare(Format.barValue(112 * m), "112M");
        compare(Format.barValue(1.1 * g), "1.1G");
        // 9.96 rounds to ten: no decimal from there.
        compare(Format.barValue(9.96 * m), "10M");
    }

    function test_bar_value_never_reads_a_thousand_of_a_unit() {
        compare(Format.barValue(999.4 * k), "999K");
        compare(Format.barValue(999.6 * k), "1.0M");
        compare(Format.barValue(1023 * k), "1.0M");
        compare(Format.barValue(1023.9 * m), "1.0G");
    }

    function test_bar_value_unknown_is_empty() {
        compare(Format.barValue(-1), "");
        compare(Format.barValue(undefined), "");
        compare(Format.barValue(null), "");
        compare(Format.barValue(NaN), "");
    }

    function sweep() {
        var values = [0, 1, 999, 1000, 1023, 1024, 1025, 10239, 10240, 1023 * 1024,
                      1048575, 1048576, 10 * m - 1, 10 * m, 99.95 * m, 999 * m, 999.9 * m,
                      1023.99 * m, g, 9.99 * g, 512 * g];
        for (var e = 0; e < 40; e += 0.37)
            values.push(Math.floor(Math.pow(2, e)));
        return values;
    }

    function test_horizontal_field_is_always_five_characters() {
        var values = sweep();
        for (var i = 0; i < values.length; i++) {
            var down = Format.barField(values[i], "↓");
            var up = Format.barField(values[i], "↑");
            compare(down.length, 5, "↓ " + values[i] + " → '" + down + "'");
            compare(up.length, 5, "↑ " + values[i] + " → '" + up + "'");
            compare(down.charAt(0), "↓");
        }
        compare(Format.barField(4.21 * m, "↓"), "↓4.2M");
        compare(Format.barField(842 * k, "↑"), "↑842K");
        compare(Format.barField(0, "↓"), "↓   0");
        compare(Format.barField(-1, "↓"), "");
    }

    function test_vertical_value_fits_four_characters() {
        var values = sweep();
        for (var i = 0; i < values.length; i++) {
            var text = Format.verticalValue(values[i]);
            verify(text.length >= 1 && text.length <= 4, values[i] + " → '" + text + "'");
        }
        compare(Format.verticalValue(1.2 * m), "1.2M");
        compare(Format.verticalValue(0), "0");
        compare(Format.verticalValue(-1), "");
    }

    function test_popup_speeds_in_full_units() {
        compare(Format.speed(0), "0 B/s");
        compare(Format.speed(812), "812 B/s");
        compare(Format.speed(812 * k), "812 KB/s");
        compare(Format.speed(1.18 * m), "1.18 MB/s");
        compare(Format.speed(2.31 * m), "2.31 MB/s");
        compare(Format.speed(14.2 * m), "14.2 MB/s");
        compare(Format.speed(-1), "—");
        // 1023.7 KB/s would round to "1024 KB/s": carried into MB.
        compare(Format.speed(1023.7 * k), "1.00 MB/s");
    }

    function test_limits_shortest_form() {
        compare(Format.limit(0), "∞");
        compare(Format.limit(m), "1 MB/s");
        compare(Format.limit(200 * k), "200 KB/s");
        compare(Format.limit(1.5 * m), "1.5 MB/s");
        compare(Format.limit(12 * m), "12 MB/s");
    }

    function test_byte_sizes_as_the_app_prints_them() {
        compare(Format.size(0), "0 B");
        compare(Format.size(999), "999 B");
        compare(Format.size(1024), "1 KB");
        compare(Format.size(6.11 * g), "6.11 GB");
        compare(Format.size(14.2 * g), "14.2 GB");
        compare(Format.size(96.4 * g), "96.4 GB");
        compare(Format.size(2.1 * 1024 * g), "2.10 TB");
        compare(Format.size(undefined), "—");
    }

    function test_percent_is_truncated() {
        compare(Format.percent(0.489), "48.9 %");
        compare(Format.percent(0.872), "87.2 %");
        compare(Format.percent(0.99996), "99.9 %");
        compare(Format.percent(1), "100 %");
        compare(Format.percent(0), "0.0 %");
    }

    function test_eta() {
        compare(Format.eta(45), "45s");
        compare(Format.eta(160), "2m 40s");
        compare(Format.eta(425), "7m 05s");
        compare(Format.eta(180), "3m");
        compare(Format.eta(3900), "1h 05m");
        compare(Format.eta(2 * 86400 + 4 * 3600 + 5), "2d 4h");
        compare(Format.eta(-1), "∞");
        compare(Format.eta(8640000), "∞");
    }

    function test_relative_time() {
        var now = 1789346626;
        compare(Format.ago(now - 5, now), "just now");
        compare(Format.ago(now - 12 * 60, now), "12 min ago");
        compare(Format.ago(now - 3600, now), "1 h ago");
        compare(Format.ago(now - 23 * 3600, now), "23 h ago");
        compare(Format.ago(now - 30 * 3600, now), "yesterday");
        compare(Format.ago(now - 4 * 86400, now), "4 days ago");
        // A clock a little ahead of the app's is "just now", not the future.
        compare(Format.ago(now + 3, now), "just now");
        compare(Format.ago(0, now), "");
    }

    function test_ratios_and_milestones() {
        compare(Format.ratio(4.58), "4.58");
        compare(Format.ratio(-1), "—");
        compare(Format.ratio(10000), "∞");
        compare(Format.milestone(1), "paid back");
        compare(Format.milestone(2), "ratio 2.0");
        compare(Format.milestone(5), "ratio 5.0");
    }

    function test_weekday() {
        compare(Format.weekday("2026-10-02"), "Fri");
        compare(Format.weekday("2026-09-26"), "Sat");
        compare(Format.weekday("nope"), "");
    }
}
