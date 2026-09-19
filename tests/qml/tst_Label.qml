import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Label"

    Component {
        id: labelComponent

        Label {}
    }

    function test_default_colour_is_foreground() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "plain"
                                            });
        compare(label.color, Theme.foreground);
        compare(label.font.pixelSize, Theme.font.body);
    }

    function test_tracked_is_uppercase_caption_sized_and_dim() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "seeding",
                                                "tracked": true
                                            });
        compare(label.font.capitalization, Font.AllUppercase);
        compare(label.font.pixelSize, Theme.font.caption);
        compare(label.color, Theme.dimForeground);
        verify(label.font.letterSpacing > 0);
    }

    function test_bright_overrides_dim() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "title",
                                                "bright": true
                                            });
        compare(label.color, Theme.brightForeground);
    }

    function test_size_selects_a_theme_font_token() {
        const label = createTemporaryObject(labelComponent, testCase, {
                                                "text": "big",
                                                "size": "heading"
                                            });
        compare(label.font.pixelSize, Theme.font.heading);
    }
}
