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
}
