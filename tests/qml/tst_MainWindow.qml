import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit

// #156: Main.qml's own sizes once Theme.space() follows the base size live. What is inside the
// window follows it; the window keeps the size it opened at, so a tiled one never asks for more
// than its tile.
TestCase {
    id: testCase

    name: "MainWindow"
    when: windowShown

    Component {
        id: mainComponent

        Main {}
    }

    function cleanup() {
        TestTheme.setBaseSize(0); // qmllint disable unqualified
    }

    function test_a_base_size_change_keeps_the_window_size() {
        App.loadDemo(4); // past setup: first run's window is sized by setupSize instead
        const window = createTemporaryObject(mainComponent, null, {
            "visible": false
        });
        verify(window !== null);
        compare(window.setup, false);
        compare(window.width, Theme.space(1360));
        compare(window.height, Theme.space(800));
        compare(window.minimumWidth, Theme.space(480));
        const width = window.width;
        const height = window.height;

        TestTheme.setBaseSize(18); // qmllint disable unqualified
        compare(Theme.font.baseSize, 18);
        compare(window.width, width);
        compare(window.height, height);
        compare(window.minimumWidth, Theme.space(480)); // the floor still scales
    }
}
