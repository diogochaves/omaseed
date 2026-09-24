import QtQuick
import QtTest
import Omaseed.Kit

// Review Q6: keyboard focus is a 1 px accent ring `Theme.focusGap` outside the control's
// edge, never the control's own border, which the primary button keeps for its outline.
TestCase {
    id: testCase

    height: 200
    name: "FocusRing"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: hostComponent

        Rectangle {
            property alias ring: ring

            activeFocusOnTab: true
            height: 30
            width: 80

            FocusRing {
                id: ring
            }
        }
    }

    function test_it_is_hidden_until_the_parent_has_the_focus() {
        const host = createTemporaryObject(hostComponent, testCase);
        compare(host.ring.visible, false);
        host.forceActiveFocus();
        verify(host.activeFocus);
        compare(host.ring.visible, true);
        host.focus = false;
        compare(host.ring.visible, false);
    }

    function test_it_sits_outside_the_edge() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(Theme.focusExtent > 1, "no gap between the edge and the ring");
        compare(host.ring.x, -Theme.focusExtent);
        compare(host.ring.y, -Theme.focusExtent);
        compare(host.ring.width, host.width + 2 * Theme.focusExtent);
        compare(host.ring.height, host.height + 2 * Theme.focusExtent);
    }

    function test_it_is_a_one_pixel_accent_stroke_with_no_fill() {
        const host = createTemporaryObject(hostComponent, testCase);
        compare(host.ring.border.color, Theme.accent);
        compare(host.ring.border.width, 1);
        compare(host.ring.color.a, 0);
    }

    function test_the_caller_may_bind_when_it_shows() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.ring.shown = true;
        compare(host.ring.visible, true);
    }
}
