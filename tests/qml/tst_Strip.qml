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

    function test_rule_is_on_by_default_and_can_be_turned_off() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": testCase.sampleItems
                                            });
        compare(strip.rule, true);
        strip.rule = false;
        compare(strip.rule, false);
    }

    // #79: each cell is a label named by its caption, the value its description.
    function test_a_screen_reader_hears_each_cell() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": testCase.sampleItems
                                            });
        const cell = findChild(strip, "stripCell");
        verify(cell !== null);
        compare(cell.Accessible.role, Accessible.StaticText);
        compare(cell.Accessible.name, "size");
        compare(cell.Accessible.description, "4.2 GB");
    }

    function test_empty_items_still_lays_out() {
        const strip = createTemporaryObject(stripComponent, testCase, {
                                                "items": []
                                            });
        compare(strip.items.length, 0);
    }
}
