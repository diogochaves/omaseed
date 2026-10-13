import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 200
    name: "Toggle"
    visible: true
    when: windowShown
    width: 300

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

    // Q25: on fills the track with accent at the pressed step; off stays the idle foreground fill.
    function test_on_fills_the_track_with_accent() {
        const toggle = createTemporaryObject(toggleComponent, testCase, {
                                                 "checked": true
                                             });
        const track = findChild(toggle, "toggleTrack");
        verify(track);
        const on = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, Theme.fillPressed);
        tryVerify(() => Qt.colorEqual(track.color, on));
        toggle.checked = false;
        tryVerify(() => Qt.colorEqual(track.color, Theme.fill(Theme.fillIdle)));
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

    // M8: a click flips the switch and leaves the keyboard where it was, so no ring stays
    // round the track once the pointer has gone.
    function test_a_click_does_not_take_the_focus() {
        const holder = createTemporaryQmlObject("import QtQuick; Item { width: 10; height: 10 }",
                                                testCase);
        const toggle = createTemporaryObject(toggleComponent, testCase, {
                                                 "x": 40,
                                                 "y": 40,
                                                 "text": "label"
                                             });
        let toggles = 0;
        toggle.toggled.connect(() => toggles++);
        holder.forceActiveFocus();
        mouseClick(toggle);
        compare(toggle.checked, true);
        compare(toggles, 1);
        verify(holder.activeFocus, "the click took the keyboard");
        compare(toggle.activeFocus, false);
        compare(findChild(toggle, "focusRing").shown, false);
    }
}
