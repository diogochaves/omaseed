import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    name: "Toggle"

    Component {
        id: toggleComponent

        Toggle {}
    }

    function test_track_is_the_spec_proportions() {
        const toggle = createTemporaryObject(toggleComponent, testCase);
        compare(toggle.trackWidth, Theme.space(28));
        compare(toggle.trackHeight, Theme.space(14));
    }

    function test_space_toggles_when_focused() {
        const toggle = createTemporaryObject(toggleComponent, testCase, {
                                                 "checked": false
                                             });
        toggle.forceActiveFocus();
        let toggles = 0;
        toggle.toggled.connect(() => toggles++);
        keyClick(Qt.Key_Space);
        compare(toggle.checked, true);
        compare(toggles, 1);
    }

    // Review Q6: the ring goes round the track, outside it.
    function test_focus_rings_the_track() {
        const toggle = createTemporaryObject(toggleComponent, testCase);
        const ring = findChild(toggle, "focusRing");
        verify(ring);
        compare(ring.shown, false);
        toggle.forceActiveFocus();
        compare(ring.shown, true);
        compare(ring.width, toggle.trackWidth + 2 * Theme.focusExtent);
        compare(ring.height, toggle.trackHeight + 2 * Theme.focusExtent);
    }

    function test_toggled_signal_fires() {
        const toggle = createTemporaryObject(toggleComponent, testCase, {
                                                 "checked": false
                                             });
        let toggles = 0;
        toggle.toggled.connect(() => toggles++);
        toggle.toggled();
        compare(toggles, 1);
    }
}
