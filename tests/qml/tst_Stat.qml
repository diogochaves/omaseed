import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Stat"

    Component {
        id: statComponent

        Stat {}
    }

    function test_holds_glyph_value_and_tracked_label() {
        const stat = createTemporaryObject(statComponent, testCase, {
                                               "glyph": "↓",
                                               "value": "4.21 MB/s",
                                               "label": "download"
                                           });
        compare(stat.glyph, "↓");
        compare(stat.value, "4.21 MB/s");
        compare(stat.label, "download");
        compare(stat.glyphColor, Theme.foreground);
    }
}
