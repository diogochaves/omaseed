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

    // #201 F16: a strip that heads a column says whether the column has the keys: lit with
    // them, a dim hairline without; Settings' strips keep the skin's line.
    function test_a_column_strip_lights_while_it_has_the_keys() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel,
                                               "currentIndex": 1,
                                               "keysShown": true,
                                               "keysHere": false
                                           });
        const line = findChild(findChild(tabs, "tab_1"), "tabLine");
        compare(line.height, Theme.hairWidth);
        verify(Qt.colorEqual(line.color, Theme.fill(Theme.borderHover)));
        tabs.keysHere = true;
        compare(line.height, Theme.litWidth);
        compare(line.color, Theme.litColor);
        tabs.keysShown = false;
        compare(line.height, Theme.tabLineWidth);
        compare(line.color, Theme.tabLineColor);
    }

    // #171: the current tab's underline is the skin's: Omarchy's 1 px in the bright
    // foreground, Console's lit line (board 46 `.tab.on`: 2 px of the accent).
    function test_the_current_tab_is_underlined_by_the_skins_line() {
        const tabs = createTemporaryObject(tabsComponent, testCase, {
                                               "model": testCase.sampleModel,
                                               "currentIndex": 1
                                           });
        const line = findChild(findChild(tabs, "tab_1"), "tabLine");
        verify(findChild(tabs, "tab_1").current);
        compare(line.height, Theme.tabLineWidth);
        compare(line.color, Theme.tabLineColor);
        if (Theme.skin.name === "console") {
            compare(line.height, Theme.litWidth);
            compare(line.color, Theme.litColor);
        } else {
            compare(line.height, 1);
            compare(line.color, Theme.brightForeground);
        }
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
