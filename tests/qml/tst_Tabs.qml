import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Tabs"

    Component {
        id: tabsComponent

        Tabs {}
    }

    readonly property var sampleModel: [
        {
            "text": "General",
            "key": "1"
        },
        {
            "text": "Files",
            "key": "2"
        },
        {
            "text": "Trackers",
            "key": "3",
            "badge": true
        },
        {
            "text": "Peers",
            "key": "4"
        }
    ]

    function test_height_is_2_2_times_body_rounded() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel
                                           });
        compare(tabs.implicitHeight, Math.round(Theme.font.body * 2.2));
    }

    function test_current_index_defaults_to_zero() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel
                                           });
        compare(tabs.currentIndex, 0);
    }

    function test_current_index_is_settable() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel,
                                               "currentIndex": 2
                                           });
        compare(tabs.currentIndex, 2);
    }

    function test_activated_emits_with_the_index() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel
                                           });
        let lastIndex = -1;
        tabs.activated.connect(index => lastIndex = index);
        tabs.activated(2);
        compare(lastIndex, 2);
    }

    function test_holds_a_badge_flag_per_tab() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel
                                           });
        compare(tabs.model[2].badge, true);
        compare(tabs.model[0].badge, undefined);
    }
}
