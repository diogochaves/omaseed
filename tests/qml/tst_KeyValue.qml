import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "KeyValue"
    when: windowShown
    width: 400

    Component {
        id: keyValueComponent

        KeyValue {}
    }

    readonly property var sampleItems: [
        {
            "key": "status",
            "value": "downloading",
            "color": Theme.stateDownloading
        },
        {
            "key": "size",
            "value": "4.2 GB"
        }
    ]

    function test_holds_the_items() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": testCase.sampleItems
                                         });
        compare(kv.items.length, 2);
        compare(kv.items[0].color, Theme.stateDownloading);
    }

    function test_height_grows_with_row_count() {
        const one = createTemporaryObject(keyValueComponent, testCase, {
                                              "items": [testCase.sampleItems[0]]
                                          });
        const two = createTemporaryObject(keyValueComponent, testCase, {
                                              "items": testCase.sampleItems
                                          });
        verify(two.implicitHeight > one.implicitHeight);
    }

    function test_a_value_without_colour_falls_back_to_none() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": testCase.sampleItems
                                         });
        compare(kv.items[1].color, undefined);
    }

    function test_empty_items_has_only_padding_height() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": []
                                         });
        compare(kv.implicitHeight, Theme.space(20));
    }

    // Ruling Q7 (review KIT-17): the spec's `.kv` look.
    function test_the_value_is_bright_and_the_key_one_pixel_under_caption() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": [testCase.sampleItems[1]],
                                             "width": 400
                                         });
        const row = findChild(kv, "keyValueRow");
        verify(row !== null);
        compare(row.valueText.color, Theme.brightForeground);
        const key = findChild(kv, "keyValueKey");
        compare(key.font.pixelSize, Theme.font.caption - 1);
        compare(key.font.capitalization, Font.AllUppercase);
    }

    function test_a_long_value_stays_on_one_line_with_an_ellipsis() {
        const long = "/home/someone/Downloads/a/very/long/save/path/that/cannot/fit/in/the/column";
        const short = createTemporaryObject(keyValueComponent, testCase, {
                                                "items": [
                                                    {
                                                        "key": "path",
                                                        "value": "/tmp"
                                                    }
                                                ],
                                                "width": 240
                                            });
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": [
                                                 {
                                                     "key": "path",
                                                     "value": long
                                                 }
                                             ],
                                             "width": 240
                                         });
        const row = findChild(kv, "keyValueRow");
        compare(row.valueText.lineCount, 1);
        verify(row.valueText.truncated, "a value wider than the column did not elide");
        compare(row.valueText.text, long, "the full text is what the tooltip shows");
        compare(kv.implicitHeight, short.implicitHeight, "a long value made the row taller");
        verify(!findChild(short, "keyValueRow").valueText.truncated);

        // Ruling 8: the full value shows in the kit's one tooltip, not a Controls ToolTip.
        const tip = findChild(testCase.Window.window.contentItem, "keyValueTip");
        verify(tip, "the elided value has no kit Tooltip");
        compare(tip.parent, testCase.Window.window.contentItem);
        compare(tip.delay, 400);
        // Hover does not reach an offscreen window, so show it the way the row's handler does.
        tip.showFor(row.valueText, row.valueText.text);
        tryCompare(tip, "shown", true);
        compare(tip.text, long);
        tip.hide();
    }
}
