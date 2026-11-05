pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// #217 (board 54 W1, W3, DECISIONS C35): `Views/AddWindow.qml`, the add sheet in a window of
// its own, against the demo library (`App.loadDemo(16)`). Its first title, a size fixed to the
// sheet's (minimum = maximum, up to 80 % of the screen's height, following the sheet as it
// grows), never a transient child, its own palettes, and gone when the sheet is.
TestCase {
    id: testCase

    /// Magnets whose hashes are nobody's in the demo library.
    readonly property string freshMagnet:
        "magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071&dn=Fedora-Workstation-Live-44-1.6.x86_64.iso&xl=2512345678"
    readonly property string secondMagnet: "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=second"
    readonly property string thirdMagnet:
        "magnet:?xt=urn:btih:2222222222222222222222222222222222222222&dn=third&xl=1048576"

    height: 300
    name: "AddWindow"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: windowComponent

        AddWindow {}
    }

    Component {
        id: settingsSpy

        SignalSpy {
            signalName: "settingsRequested"
        }
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    /// A new add window, declared under the test's own window as Main.qml's child would be.
    function makeWindow() {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window !== null);
        return window;
    }

    /// The height the window should have: the sheet's own, up to the cap.
    function wantedHeight(window) {
        return Math.ceil(Math.min(window.heightCap, window.addSheet.naturalHeight));
    }

    /// Until the window has the sheet's height and kept it over two polls: the sheet's layout
    /// settles a polish pass after a change, so a first reading can be the height it had.
    function waitSettled(window) {
        let last = -1;
        tryVerify(() => {
            const height = window.height;
            const same = height === last && height === testCase.wantedHeight(window);
            last = height;
            return same;
        }, 5000, "the window never settled on the sheet's height");
        return window.height;
    }

    /// W3: minimum = maximum, the window at that size, never past the cap.
    function verifyFixed(window) {
        compare(window.minimumWidth, window.maximumWidth);
        compare(window.minimumHeight, window.maximumHeight);
        compare(window.width, window.minimumWidth);
        compare(window.height, window.minimumHeight);
        verify(window.height <= window.heightCap, window.height + " > " + window.heightCap);
    }

    function test_it_opens_under_its_first_title_and_alone() {
        const window = testCase.makeWindow();
        compare(window.visible, false, "nothing opens it until an add does");
        compare(window.title, "omaseed · add");
        compare(window.transientParent, null);

        window.openBlank();
        compare(window.visible, true);
        compare(window.addSheet.opened, true);
        compare(window.activeOverlay, window.addSheet);
        testCase.waitSettled(window);
        testCase.verifyFixed(window);
        compare(window.width, window.addSheet.sheetWidth);
        // The panel is the whole window, its edge the window's.
        compare(window.addSheet.framed, false);
        tryCompare(window.addSheet.panel, "width", window.width);
        tryCompare(window.addSheet.panel, "height", window.height);
    }

    function test_the_cap_is_80_percent_of_the_screen() {
        const window = testCase.makeWindow();
        verify(window.screen !== null);
        compare(window.heightCap, Math.floor(0.8 * window.screen.height));
    }

    function test_the_window_grows_with_the_batch_list() {
        const window = testCase.makeWindow();
        window.heightCap = 10000; // the sheet's own height, whatever the test screen's
        window.openList([testCase.freshMagnet, testCase.secondMagnet], "handoff");
        compare(window.addSheet.batchMode, true);
        const list = window.addSheet.batchList;
        tryCompare(list, "height", 2 * Theme.railRowHeight + 2);
        const two = testCase.waitSettled(window);

        // W7: a hand-off while it is open joins the batch, a row more.
        window.openList([testCase.thirdMagnet], "handoff");
        compare(window.addSheet.batchCount, 3);
        tryCompare(list, "height", 3 * Theme.railRowHeight + 2);
        verify(testCase.waitSettled(window) > two, "the window did not grow with the list");
        testCase.verifyFixed(window);
        tryCompare(window.addSheet.panel, "height", window.height);
    }

    function test_the_tree_takes_its_rows_up_to_the_cap() {
        const window = testCase.makeWindow();
        const sheet = window.addSheet;
        const tree = sheet.filesTree;
        window.heightCap = 10000;
        window.openList([App.demoTorrentPath()], "file");
        tryCompare(sheet, "showFiles", true, 5000);

        // Room for every row: the tree shows them all and nothing scrolls.
        testCase.waitSettled(window);
        tryCompare(tree, "height", tree.contentHeight);
        verify(!tree.scrollBar.needed);
        testCase.verifyFixed(window);

        // A short screen: the window stops at the cap and only the tree scrolls.
        const full = window.height;
        window.heightCap = full - 4 * Theme.railRowHeight;
        tryCompare(window, "height", window.heightCap);
        testCase.verifyFixed(window);
        compare(Math.ceil(sheet.naturalHeight), full, "the sheet's own height does not follow the window");
        tryVerify(() => tree.height < tree.contentHeight);
        compare(tree.scrollBar.needed, true);
        tryVerify(() => sheet.panel.implicitHeight <= window.height + 1, 5000, "only the tree scrolls");
        tryCompare(sheet.panel, "height", window.height);
    }

    function test_it_closes_with_the_sheet() {
        const window = testCase.makeWindow();
        window.openBlank();
        compare(window.visible, true);
        window.addSheet.dismiss();
        compare(window.addSheet.opened, false);
        compare(window.visible, false);
        compare(window.activeOverlay, null);
    }

    function test_the_chips_open_its_own_palettes() {
        const window = testCase.makeWindow();
        window.openBlank();
        window.addSheet.categoryPaletteRequested();
        compare(window.categoryPalette.opened, true);
        compare(window.categoryPalette.choiceMode, true);
        compare(window.activeOverlay, window.categoryPalette);
        window.categoryPalette.chosen("linux");
        compare(window.addSheet.chosenCategory(), "linux");
        window.categoryPalette.close();
        compare(window.activeOverlay, window.addSheet);

        window.addSheet.tagPaletteRequested();
        compare(window.tagPalette.opened, true);
        compare(window.tagPalette.choiceMode, true);
        compare(window.activeOverlay, window.tagPalette);
        window.tagPalette.close();
        compare(window.activeOverlay, window.addSheet);
    }

    // DECISIONS C35, settled 2026-11-05: with one server the note is the sheet's own.
    function test_one_server_says_so_at_the_sheet_foot() {
        compare(App.serverCount <= 1, true);
        const window = testCase.makeWindow();
        window.openBlank();
        window.flow.serverPaletteRequested();
        compare(window.serverPalette.opened, false);
        compare(window.addSheet.note, "one server · add another in Settings › Servers");
        window.addSheet.dismiss();
        compare(window.addSheet.note, "");
    }

    // DECISIONS C35, settled 2026-11-05: `manage…` is Settings in the main window.
    function test_manage_asks_the_host_for_settings() {
        const window = testCase.makeWindow();
        const spy = createTemporaryObject(settingsSpy, testCase, {
            "target": window
        });
        window.openBlank();
        window.serverPalette.manageRequested("seedbox");
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "seedbox");
    }
}
