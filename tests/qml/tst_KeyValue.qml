import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "KeyValue"

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
}
