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

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function spyOn(target: var, signalName: string): SignalSpy {
        return createTemporaryObject(spyComponent, testCase, {
            "target": target,
            "signalName": signalName
        }) as SignalSpy;
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

    // DECISIONS C35 W1 (#218): App's add-window route reaches an AddWindow made on the first
    // add and kept; what it hands back reaches the main window.
    function test_the_add_window_route_opens_the_add_window() {
        App.loadDemo(4);
        const window = createTemporaryObject(mainComponent, null, {
            "visible": false
        });
        verify(window !== null);
        compare(window.addWindow, null, "made before anything asked for it");

        const magnet = "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second";
        App.addWindowRequested([magnet]);
        const addWindow = window.addWindow;
        verify(addWindow !== null);
        compare(addWindow.transientParent, null, "a transient child could map with the main window");
        compare(addWindow.addSheet.opened, true);
        compare(addWindow.visible, true);
        compare(window.visible, false, "the main window came up for it");

        // A second hand-off joins the same sheet; the empty list (the menu's add) is the same
        // window again, on a blank sheet once the add is dropped.
        App.addWindowRequested(["magnet:?xt=urn:btih:2222222222222222222222222222222222222222&dn=third"]);
        compare(window.addWindow, addWindow);
        compare(addWindow.addSheet.batchCount, 2);
        addWindow.cancel();
        compare(addWindow.visible, false);
        App.addWindowRequested([]);
        compare(window.addWindow, addWindow);
        compare(addWindow.addSheet.opened, true);

        // `show it` travels as the notice's show; `manage…` opens Settings › Servers here.
        const shown = testCase.spyOn(App, "showTorrentRequested");
        addWindow.showRequested("abc");
        compare(shown.count, 1);
        compare(shown.signalArguments[0][0], "abc");
        addWindow.settingsRequested(App.serverId);
        compare(window.visible, true, "Settings opened in a window nobody sees");
        addWindow.cancel();
    }
}
