import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Strip"

    Component {
        id: stripComponent

        Strip {}
    }

    readonly property var sampleItems: [
        {
            "label": "size",
            "value": "4.2 GB"
        },
        {
            "label": "added",
            "value": "2 days ago"
        },
        {
            "label": "ratio",
            "value": "1.24"
        },
        {
            "label": "eta",
            "value": "12m"
        }
    ]

    function test_holds_the_four_items() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": testCase.sampleItems
                                            });
        compare(strip.items.length, 4);
    }

    function test_label_size_is_one_below_caption() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": testCase.sampleItems
                                            });
        compare(strip._labelSize, Theme.font.caption - 1);
    }

    function test_height_accounts_for_padding() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": testCase.sampleItems
                                            });
        verify(strip.implicitHeight > Theme.space(6) + Theme.space(10));
    }

    function test_empty_items_still_lays_out() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": []
                                            });
        compare(strip.items.length, 0);
    }
}
