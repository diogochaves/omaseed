import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Banner"

    Component {
        id: bannerComponent

        Banner {}
    }

    Component {
        id: triggeredSpy

        SignalSpy {
            signalName: "actionTriggered"
        }
    }

    readonly property var retryAction: [
        {
            "label": "retry now",
            "key": "retry"
        }
    ]

    function test_error_kind_is_red() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "error",
                                                 "text": "connection refused"
                                             });
        compare(banner._kindColor, Theme.red);
    }

    function test_warning_kind_is_yellow() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "warning",
                                                 "text": "home is unreachable"
                                             });
        compare(banner._kindColor, Theme.yellow);
    }

    function test_info_kind_is_accent() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "info",
                                                 "text": "theme switched"
                                             });
        compare(banner._kindColor, Theme.accent);
    }

    function test_action_triggered_emits_the_key() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "error",
                                                 "text": "connection refused",
                                                 "actions": [
                                                     {
                                                         "label": "retry now",
                                                         "key": "retry"
                                                     }
                                                 ]
                                             });
        let lastKey = "";
        banner.actionTriggered.connect(key => lastKey = key);
        banner.actionTriggered("retry");
        compare(lastKey, "retry");
    }

    // #75: a screen reader's press on an action is its click: the action's key, once.
    function test_the_accessible_press_on_an_action_is_the_click() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "error",
                                                 "text": "connection refused",
                                                 "actions": testCase.retryAction
                                             });
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": banner
                                                });
        findChild(banner, "bannerAction-retry").Accessible.pressAction();
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "retry");
    }

    // #75: disabled, the banner ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_banner_ignores_the_accessible_press() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "kind": "error",
                                                 "text": "connection refused",
                                                 "actions": testCase.retryAction,
                                                 "enabled": false
                                             });
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": banner
                                                });
        findChild(banner, "bannerAction-retry").Accessible.pressAction();
        compare(triggered.count, 0);
    }
}
