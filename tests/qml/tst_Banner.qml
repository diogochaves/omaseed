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
}
