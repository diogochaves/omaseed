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

    // #79: a label named by what it counts, the number (which ticks) its description; the
    // glyph in neither. Without a label it keeps the name it is given.
    function test_a_screen_reader_hears_the_label_then_the_value() {
        const stat = createTemporaryObject(statComponent, testCase, {
                                               "glyph": "↓",
                                               "value": "4.21 MB/s",
                                               "label": "download"
                                           });
        compare(stat.Accessible.role, Accessible.StaticText);
        compare(stat.Accessible.name, "download");
        compare(stat.Accessible.description, "4.21 MB/s");
        stat.value = "5.00 MB/s";
        compare(stat.Accessible.name, "download");
        compare(stat.Accessible.description, "5.00 MB/s");

        const bare = createTemporaryObject(statComponent, testCase, {
                                               "glyph": "↓",
                                               "value": "4.2M",
                                               "label": "",
                                               "accessibleName": "download"
                                           });
        compare(bare.Accessible.name, "download");
    }
}
