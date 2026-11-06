pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// Views/AltSpeedMenu (board 51 D8 a `altPicker()`, DECISIONS C32 D8 a): off, turtle and burst
// with their limits, the one in force checked; how long a burst lasts; and the way to edit them.
TestCase {
    id: testCase

    height: 700
    name: "AltSpeedMenu"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: menuComponent

        AltSpeedMenu {
            anchors.fill: parent
            burstLimits: "↓∞ ↑2.00M"
            serverName: "public"
            turtleLimits: "↓300K ↑50K"
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function _kinds(menu: var): var {
        return menu.items.map(item => item.separator ? "|" : item.caption ? "~" + item.text : item.action);
    }

    function test_the_presets_then_the_length_then_edit() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(testCase._kinds(menu), ["off", "turtle", "burst", "|", "~burst lasts", "minutes:30", "minutes:60",
                                        "minutes:0", "|", "edit"]);
        compare(menu.headingTitle, "Alternative speeds");
        compare(menu.heading, "public · one set at a time");
        compare(menu.items[1].note, "↓300K ↑50K");
        compare(menu.items[2].note, "↓∞ ↑2.00M");
        compare(menu.items[1].glyphColor, Theme.yellow);
        compare(menu.items[2].glyphColor, Theme.green);
        // The one in force, and the burst's length, checked.
        verify(menu.items[0].checked);
        verify(!menu.items[2].checked);
        verify(menu.items[6].checked);
        menu.current = "burst";
        menu.burstMinutes = 0;
        verify(!menu.items[0].checked);
        verify(menu.items[2].checked);
        verify(menu.items[7].checked);
        verify(!menu.items[6].checked);
    }

    function test_a_pick_says_which() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const picked = createTemporaryObject(spyComponent, testCase, {
                                                 "target": menu,
                                                 "signalName": "activated"
                                             });
        menu.openAbove(100, 600);
        verify(menu.opened);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        compare(picked.count, 1);
        compare(picked.signalArguments[0][0], "burst");
        verify(!menu.opened);
    }
}
