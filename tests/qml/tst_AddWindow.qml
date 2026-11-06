pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// #217 (board 54 W1, W3, W4, W7, W8, DECISIONS C35): `Views/AddWindow.qml`, the add sheet in a
// window of its own, against the demo library (`App.loadDemo(16)`). Its first title, a size
// fixed to the sheet's (minimum = maximum, up to 80 % of the screen's height, following the
// sheet as it grows, and settled before it maps), never a transient child, its own palettes,
// gone when the sheet is; Fig. 10 in it, a hand-off mapping it again where the user is, its
// asks for the keyboard until it has had it once (#230), and esc, cancel and the window's
// close as the sheet's.
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
        id: spyComponent

        SignalSpy {}
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

    /// A magnet for a torrent the demo library holds, with `extra` (`&tr=…`) after its name.
    function knownMagnet(extra) {
        return "magnet:?xt=urn:btih:" + App.filtered.hashAt(0) + "&dn=already-here" + (extra || "");
    }

    function spyOn(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
                                         "target": target,
                                         "signalName": signalName
                                     });
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

    // ---- W4: already in the library -----------------------------------------------------

    function test_fig_10_alone_is_the_window() {
        const window = testCase.makeWindow();
        const figure = window.duplicateSheet;
        window.openList([testCase.knownMagnet()], "handoff");
        compare(figure.opened, true);
        compare(window.addSheet.opened, false, "the form never shows for it");
        compare(window.visible, true);
        compare(window.activeOverlay, figure);
        // Fig. 10 at its own size, its edge the window's, mapped at that size.
        compare(figure.framed, false);
        compare(window.width, figure.sheetWidth);
        compare(window.height, Math.ceil(Math.min(window.heightCap, figure.panel.implicitHeight)));
        testCase.verifyFixed(window);
        tryCompare(figure.panel, "width", window.width);
        tryCompare(figure.panel, "height", window.height);

        // `close` closes the window.
        mouseClick(figure.cancelButton());
        compare(figure.opened, false);
        compare(window.visible, false);
    }

    function test_show_it_closes_the_window_and_asks_for_the_main_one() {
        const window = testCase.makeWindow();
        const shown = testCase.spyOn(window, "showRequested");
        window.openList([testCase.knownMagnet()], "handoff");
        compare(window.duplicateSheet.opened, true);
        window.duplicateSheet.actionTriggered(window.duplicateSheet._showText);
        compare(window.visible, false);
        compare(window.activeOverlay, null);
        compare(shown.count, 1);
        compare(shown.signalArguments[0][0], App.filtered.hashAt(0));
    }

    function test_a_merge_closes_the_window_and_says_so() {
        const window = testCase.makeWindow();
        const merged = testCase.spyOn(window, "merged");
        const sent = testCase.spyOn(App, "trackersMerged");
        window.openList([testCase.knownMagnet("&tr=udp%3A%2F%2Ft.example%3A1%2Fannounce")], "handoff");
        const figure = window.duplicateSheet;
        compare(figure.opened, true);
        tryCompare(figure, "canMerge", true); // the demo answers at once
        figure.actionTriggered(figure._mergeText);
        compare(window.visible, false);
        compare(sent.count, 1, "the merge never reached App");
        compare(merged.count, 1, "the host was not told, for W5's notice");
        compare(merged.signalArguments[0][0], App.filtered.hashAt(0));
    }

    // Board 54 W5 (#219): the window's adds ask App for "Torrent added", a batch's too; App
    // sends one per torrent (tst_app_notifications).
    function test_its_adds_ask_for_the_added_notice() {
        const window = testCase.makeWindow();
        compare(window.flow.notifiesAdds, true);
        const request = window.flow.requestFor({
            "kind": "magnet",
            "value": testCase.freshMagnet
        });
        compare(request.notifyAdded, true);
        compare(request.kind, "magnet");
        compare(request.value, testCase.freshMagnet);
        const batch = window.flow.requestFor({
            "items": [{
                    "kind": "magnet"
                }, {
                    "kind": "magnet"
                }]
        });
        compare(batch.notifyAdded, true);
        compare(batch.items.length, 2);
    }

    function test_fig_10_over_the_next_source_floats_over_the_sheet() {
        const window = testCase.makeWindow();
        window.openList([testCase.knownMagnet(), testCase.freshMagnet], "drop");
        window.addSheet.oneByOne();
        compare(window.duplicateSheet.opened, true);
        compare(window.addSheet.opened, true, "the next source waits behind it");
        compare(window.duplicateSheet.framed, true);
        compare(window.activeOverlay, window.duplicateSheet);
        testCase.waitSettled(window);
        compare(window.width, window.addSheet.sheetWidth);
        testCase.verifyFixed(window);

        // Its `close` leaves the next one up, and the window with it.
        mouseClick(window.duplicateSheet.cancelButton());
        compare(window.visible, true);
        compare(window.activeOverlay, window.addSheet);
        compare(window.addSheet.sourceField.text, testCase.freshMagnet);
    }

    // ---- W7: hand-offs in a row, and the size on every map ------------------------------

    function test_a_hand_off_maps_the_window_again_where_the_user_is() {
        const window = testCase.makeWindow();
        window.openBlank();
        tryCompare(window, "active", true);
        // The user is elsewhere now: the hand-off comes from another app.
        testCase.Window.window.requestActivate();
        tryCompare(window, "active", false);

        const maps = testCase.spyOn(window, "visibleChanged");
        window.openList([testCase.freshMagnet], "handoff");
        compare(maps.count, 2, "the window was raised where it was, not mapped again");
        compare(window.visible, true);
        compare(window.addSheet.sourceField.text, testCase.freshMagnet, "the hand-off did not join");
        tryCompare(window, "active", true);

        // Already in front of the user: it joins, and the window stays as it is.
        maps.clear();
        window.openList([testCase.secondMagnet], "handoff");
        compare(window.addSheet.batchCount, 2);
        compare(maps.count, 0);
        compare(window.visible, true);
    }

    // #218, #230: mapped while a layer (a bar popup, Omarchy's menu) holds the keyboard, the
    // window asks again until it has had it once. Offscreen focuses a window as it shows, so
    // the test's own window takes the keyboard straight back, before the first retry, as
    // Hyprland gives it to the main window when the menu closes; and a window flagged
    // WindowDoesNotAcceptFocus stands in for one the compositor won't focus yet: Qt drops its
    // asks (with a warning each) until the flag goes.
    function mapUnfocused(window) {
        window.flags |= Qt.WindowDoesNotAcceptFocus;
        window.openBlank();
        testCase.Window.window.requestActivate();
        verify(window.askingForKeyboard);
        tryCompare(window, "active", false);
    }

    function test_it_asks_for_the_keyboard_until_it_has_had_it() {
        const window = testCase.makeWindow();
        testCase.mapUnfocused(window);
        wait(2500);
        compare(window.visible, true);
        compare(window.active, false);
        verify(window.askingForKeyboard, "it stopped asking at #218's 2 s, under a menu held 2.5 s");

        // The menu closes: the next ask has it, and the asking stops.
        window.flags &= ~Qt.WindowDoesNotAcceptFocus;
        tryCompare(window, "active", true);
        tryCompare(window, "askingForKeyboard", false);

        // Once it has had the keyboard, a move elsewhere is the user's, never undone.
        testCase.Window.window.requestActivate();
        tryCompare(window, "active", false);
        wait(400);
        compare(window.active, false, "it took the keyboard back from where the user went");
    }

    function test_it_stops_asking_at_its_limit() {
        const window = testCase.makeWindow();
        window.keyboardAskLimit = 400;
        testCase.mapUnfocused(window);
        tryCompare(window, "askingForKeyboard", false, 2000, "it asked past its limit");
        compare(window.active, false);

        // A compositor that would focus it now gets no more asks.
        window.flags &= ~Qt.WindowDoesNotAcceptFocus;
        wait(400);
        compare(window.active, false, "it asked past its limit");
    }

    function test_hidden_it_stops_asking() {
        const window = testCase.makeWindow();
        testCase.mapUnfocused(window);
        window.cancel();
        compare(window.visible, false);
        tryCompare(window, "askingForKeyboard", false, 1000);
    }

    function test_it_maps_at_the_sheet_height() {
        const window = testCase.makeWindow();
        window.heightCap = 10000;
        window.openList([testCase.freshMagnet, testCase.secondMagnet], "handoff");
        compare(window.height, testCase.waitSettled(window), "mapped at a height it then left");
        const two = window.height;
        window.addSheet.dismiss();
        compare(window.visible, false);

        // Hidden, the window polished nothing since: the map itself must.
        window.openList([testCase.freshMagnet, testCase.secondMagnet, testCase.thirdMagnet], "handoff");
        const mapped = window.height;
        verify(mapped > two, "mapped at the last height, " + mapped);
        compare(mapped, testCase.waitSettled(window), "mapped at a height it then left");
        testCase.verifyFixed(window);
    }

    function test_nothing_to_show_maps_nothing() {
        const window = testCase.makeWindow();
        window.openList([], "handoff");
        compare(window.visible, false);
    }

    // ---- W8: esc, cancel and the window's close -----------------------------------------

    function test_escape_goes_to_cancel_then_drops_everything() {
        const window = testCase.makeWindow();
        const sheet = window.addSheet;
        window.openList([testCase.freshMagnet, testCase.secondMagnet], "drop");
        sheet.oneByOne();
        compare(sheet.queue.length, 1);
        tryCompare(window, "active", true);

        const cancel = sheet.cancelButton();
        for (let i = 0; i < 3 && !cancel.activeFocus; ++i) {
            keyClick(Qt.Key_Escape);
            compare(sheet.opened, true, "esc threw the add away before cancel");
        }
        verify(cancel.activeFocus, "esc never moved to cancel");
        compare(window.visible, true);

        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
        compare(sheet.queue.length, 0, "what was waiting stayed");
        compare(window.visible, false);
    }

    /// #229: a window that has had an add already, given up with esc. The hidden window keeps
    /// what last had the keyboard (the sheet's `cancel`), and the next add sheet remembers it,
    /// as after the magnet walk's earlier adds.
    function usedWindow() {
        const window = testCase.makeWindow();
        window.openList([testCase.freshMagnet], "handoff");
        tryCompare(window, "active", true);
        for (let i = 0; i < 4 && window.visible; ++i)
            keyClick(Qt.Key_Escape);
        compare(window.visible, false);
        return window;
    }

    // #229: Fig. 10 alone holds the keyboard (the add sheet that found it, closing behind it,
    // must not take it back), and esc on it closes it and the window (W8).
    function test_escape_closes_fig_10_alone() {
        const window = testCase.usedWindow();
        const figure = window.duplicateSheet;
        window.openList([testCase.knownMagnet()], "handoff");
        compare(window.addSheet.opened, false);
        compare(figure.opened, true);
        tryCompare(window, "active", true);
        tryVerify(() => figure.primaryButton().activeFocus, 2000, "show it never had the keyboard");

        keyClick(Qt.Key_Escape);
        compare(figure.opened, false, "esc left Fig. 10 up");
        compare(window.visible, false);
        compare(window.activeOverlay, null);
    }

    // #229: tab moves between Fig. 10's buttons, and enter on `show it` acts.
    function test_fig_10_alone_answers_tab_and_enter() {
        const window = testCase.usedWindow();
        const figure = window.duplicateSheet;
        const shown = testCase.spyOn(window, "showRequested");
        window.openList([testCase.knownMagnet()], "handoff");
        tryCompare(window, "active", true);
        const show = figure.primaryButton();
        tryVerify(() => show.activeFocus, 2000, "show it never had the keyboard");

        keyClick(Qt.Key_Tab);
        verify(figure.cancelButton().activeFocus, "tab did not move to close");
        keyClick(Qt.Key_Tab);
        verify(show.activeFocus, "tab did not come back to show it");

        keyClick(Qt.Key_Return);
        compare(shown.count, 1, "enter on show it did nothing");
        compare(figure.opened, false);
        compare(window.visible, false);
    }

    function test_cancel_drops_everything_and_closes() {
        const window = testCase.makeWindow();
        const sheet = window.addSheet;
        window.openList([testCase.freshMagnet, testCase.secondMagnet], "drop");
        sheet.oneByOne();
        compare(sheet.queue.length, 1);
        mouseClick(sheet.cancelButton());
        compare(sheet.opened, false);
        compare(sheet.queue.length, 0);
        compare(window.visible, false);
    }

    function test_closing_the_window_is_a_cancel() {
        const window = testCase.makeWindow();
        const sheet = window.addSheet;
        window.openList([testCase.freshMagnet, testCase.secondMagnet], "drop");
        sheet.oneByOne();
        sheet.tagPaletteRequested();
        compare(window.tagPalette.opened, true);

        window.close(); // super+w
        compare(window.visible, false);
        compare(sheet.opened, false);
        compare(sheet.queue.length, 0);
        compare(window.tagPalette.opened, false);
        compare(window.activeOverlay, null);

        // Fig. 10 goes the same way.
        window.openList([testCase.knownMagnet()], "handoff");
        compare(window.duplicateSheet.opened, true);
        window.close();
        compare(window.duplicateSheet.opened, false);
        compare(window.visible, false);
    }

    // C20: qBittorrent's OK closes the window with the sheet; an add already out when the
    // window closes goes on, and its answer brings nothing back.
    function test_an_add_sent_goes_on() {
        const window = testCase.makeWindow();
        const sheet = window.addSheet;
        const finished = testCase.spyOn(App, "addFinished");
        window.openList([testCase.freshMagnet], "handoff");
        sheet.submit(false);
        compare(finished.count, 1);
        compare(finished.signalArguments[0][0], true);
        compare(window.visible, false, "the OK did not close the window");

        window.openList([testCase.secondMagnet], "handoff");
        sheet._adding = true; // as if qBittorrent had not answered yet
        window.close();
        compare(window.visible, false);
        App.addFinished(true, "", "");
        compare(sheet.opened, false, "the answer brought the sheet back");
        compare(window.visible, false);
        compare(sheet._adding, false);
    }
}
