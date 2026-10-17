import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "KeyValue"
    visible: true
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

    // An item with a `url` is a link: a click on its value says which, for the caller to
    // open (About's repository); a plain value takes no click.
    function test_a_url_item_is_a_link() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": [
                                                 {
                                                     "key": "version",
                                                     "value": "0.9.0"
                                                 },
                                                 {
                                                     "key": "repository",
                                                     "value": "https://example.invalid/omaseed",
                                                     "url": "https://example.invalid/omaseed"
                                                 }
                                             ],
                                             "width": 400
                                         });
        let opened = [];
        kv.linkActivated.connect(url => opened.push(url));
        const rows = [];
        for (let i = 0; i < kv.children[0].children.length; ++i) {
            const child = kv.children[0].children[i];
            if (child.objectName === "keyValueRow")
                rows.push(child);
        }
        compare(rows.length, 2);
        verify(!rows[0].isLink);
        verify(rows[1].isLink);
        waitForItemPolished(rows[1]);
        // The link ends with its text: the rest of the row is no target.
        verify(rows[1].valueText.width <= rows[1].valueText.implicitWidth + 0.5);
        mouseClick(rows[0].valueText);
        compare(opened.length, 0);
        mouseClick(rows[1].valueText);
        compare(opened, ["https://example.invalid/omaseed"]);
    }

    // #78: a screen reader finds each value named by its key, the value its description, so
    // an empty value is no empty label; a link's press opens it, as its click.
    function test_a_value_is_named_by_its_key() {
        const kv = createTemporaryObject(keyValueComponent, testCase, {
                                             "items": [
                                                 {
                                                     "key": "version",
                                                     "value": ""
                                                 },
                                                 {
                                                     "key": "repository",
                                                     "value": "https://example.invalid/omaseed",
                                                     "url": "https://example.invalid/omaseed"
                                                 }
                                             ],
                                             "width": 400
                                         });
        let opened = [];
        kv.linkActivated.connect(url => opened.push(url));
        const rows = [];
        for (let i = 0; i < kv.children[0].children.length; ++i) {
            const child = kv.children[0].children[i];
            if (child.objectName === "keyValueRow")
                rows.push(child);
        }
        compare(rows[0].valueText.Accessible.name, "version");
        compare(rows[0].valueText.Accessible.description, "");
        compare(rows[0].valueText.Accessible.role, Accessible.StaticText);
        compare(rows[1].valueText.Accessible.name, "repository");
        compare(rows[1].valueText.Accessible.description, "https://example.invalid/omaseed");
        compare(rows[1].valueText.Accessible.role, Accessible.Link);
        rows[0].valueText.Accessible.pressAction();
        compare(opened.length, 0, "a plain value opened something");
        rows[1].valueText.Accessible.pressAction();
        compare(opened, ["https://example.invalid/omaseed"]);
    }
}
