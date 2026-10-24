import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 100
    name: "KeyWatch"
    visible: true
    when: windowShown
    width: 200

    Component {
        id: watchComponent

        KeyWatch {}
    }

    SignalSpy {
        id: spy

        signalName: "keyPressed"
    }


    // docs/31 F1: every key press in the window is heard, a modifier alone is not, and the
    // press goes on to whoever has the focus.
    function test_hears_key_presses_and_takes_none() {
        const watch = createTemporaryObject(watchComponent, testCase);
        const target = createTemporaryQmlObject(
                         "import QtQuick; Item { property int presses: 0; focus: true; Keys.onPressed: presses++ }",
                         testCase);
        target.forceActiveFocus();
        spy.clear();
        spy.target = watch;
        keyClick("x");
        compare(spy.count, 1);
        compare(target.presses, 1);
        keyClick(Qt.Key_Shift);
        compare(spy.count, 1);
        watch.enabled = false;
        keyClick("y");
        compare(spy.count, 1);
        compare(target.presses, 3);
    }

    // DECISIONS C24 H10: alt alone sets `altHeld`; its release, the next key (alt+j), another
    // modifier or disabling the watch clears it (the window losing the focus: tst_keywatch).
    // Alt is no key press of its own: the note after `A` stays.
    function test_alt_alone_is_held_until_anything_else() {
        const watch = createTemporaryObject(watchComponent, testCase);
        spy.clear();
        spy.target = watch;
        compare(watch.altHeld, false);
        keyPress(Qt.Key_Alt);
        compare(watch.altHeld, true);
        compare(spy.count, 0);
        keyRelease(Qt.Key_Alt);
        compare(watch.altHeld, false);

        // alt+j: held for the press of alt, gone with j's, and j's release brings nothing back.
        keyPress(Qt.Key_Alt);
        compare(watch.altHeld, true);
        keyClick(Qt.Key_J, Qt.AltModifier);
        compare(watch.altHeld, false);
        compare(spy.count, 1);
        keyRelease(Qt.Key_Alt);
        compare(watch.altHeld, false);

        // alt with ctrl or shift already down is a chord, not alt alone.
        keyPress(Qt.Key_Alt, Qt.ControlModifier);
        compare(watch.altHeld, false);
        keyRelease(Qt.Key_Alt, Qt.ControlModifier);
        keyPress(Qt.Key_Alt);
        keyPress(Qt.Key_Shift, Qt.AltModifier);
        compare(watch.altHeld, false);
        keyRelease(Qt.Key_Shift, Qt.AltModifier);
        keyRelease(Qt.Key_Alt);

        keyPress(Qt.Key_Alt);
        compare(watch.altHeld, true);
        watch.enabled = false;
        compare(watch.altHeld, false);
        keyRelease(Qt.Key_Alt);
        keyPress(Qt.Key_Alt);
        compare(watch.altHeld, false, "a disabled watch heard the press");
        keyRelease(Qt.Key_Alt);
        watch.enabled = true;
    }
}
