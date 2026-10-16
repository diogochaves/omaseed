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

    Component {
        id: activatedSpy

        SignalSpy {
            signalName: "activated"
        }
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

    // `wheelSteps`: a notch is a tab, down or right the next, smaller pieces adding up, and
    // the ends stop it. `mouseWheel` does not deliver offscreen (tst_WheelScroll.qml), so the
    // angles go through `_wheel`, the one function the wheel calls.
    function test_the_wheel_steps_a_tab_a_notch() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel,
                                               "wheelSteps": true
                                           });
        let activated = [];
        tabs.activated.connect(index => activated.push(index));
        tabs._wheel(0, -120);
        compare(tabs.currentIndex, 1);
        tabs._wheel(0, -60);
        compare(tabs.currentIndex, 1);
        tabs._wheel(0, -60);
        compare(tabs.currentIndex, 2);
        tabs._wheel(-120, 0); // sideways: right is the next one too
        compare(tabs.currentIndex, 3);
        tabs._wheel(0, -360);
        compare(tabs.currentIndex, 3);
        tabs._wheel(0, 240);
        compare(tabs.currentIndex, 1);
        compare(activated, [1, 2, 3, 2, 1]);
        tabs._wheel(0, 600);
        compare(tabs.currentIndex, 0);
    }

    // #75: a screen reader's press on a tab is its click: that tab is current, said once.
    function test_the_accessible_press_on_a_tab_is_the_click() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel
                                           });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": tabs
                                                });
        const tab = findChild(tabs, "tab_2");
        verify(tab);
        tab.Accessible.pressAction();
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], 2);
        compare(tabs.currentIndex, 2);
    }

    // #75: disabled, the strip ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_strip_ignores_the_accessible_press() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel,
                                               "enabled": false
                                           });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": tabs
                                                });
        findChild(tabs, "tab_2").Accessible.pressAction();
        compare(activated.count, 0);
        compare(tabs.currentIndex, 0);
    }
}
