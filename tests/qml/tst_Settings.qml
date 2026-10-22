import QtCore
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// T4.5. `Views/SettingsModal.qml` and `Views/ServerSheet.qml` — spec v2 §5 Fig. 16 and §3
// Fig. 9: the six nav rows, the digits that jump between panes, the Servers pane's rows, the
// toggles round-tripping through App, the escape ladder (server sheet first, then the modal)
// and the server sheet's view/edit switch with its masked key.
//
// `keyClick` reaches the focused item in the offscreen runner; `mouseClick` does not (the
// session-2 finding), so anything a click would do is driven through the component's own
// functions and signals instead.
TestCase {
    id: testCase

    name: "Settings"

    height: 720
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: modalComponent

        SettingsModal {
            anchors.fill: parent
        }
    }

    // A window too short for the tallest pane (J3).
    Component {
        id: shortHostComponent

        Item {
            height: 420
            width: 1100
        }
    }

    // docs/15 Q3 A: a window at the 560 px floor (base 14).
    Component {
        id: narrowHostComponent

        Item {
            height: 720
            width: Theme.space(480)
        }
    }

    Component {
        id: rowComponent

        SettingsRow {
            description: "Off keeps the bundled Tokyo Night whatever the desktop does."
            glyph: Glyphs.appearance
            title: "Follow the Omarchy theme"

            Toggle {}
        }
    }

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    /// Depth-first search by objectName; QML's TestCase has no findChild of its own.
    function childNamed(item, name) {
        if (item.objectName === name)
            return item;
        for (let i = 0; i < item.children.length; ++i) {
            const found = testCase.childNamed(item.children[i], name);
            if (found)
                return found;
        }
        return null;
    }

    /// A server that exists only for one test. Added through the model, not through
    /// `App.addServer`, so nothing tries to move a live engine onto it.
    function addTestServer(name, url) {
        return App.serverList.add({
            "name": name,
            "url": url,
            "authMode": "apiKey",
            "pathMapRemote": "/downloads",
            "pathMapLocal": "/data/torrents"
        }, "");
    }

    function init() {
        App.loadDemo(16);
    }

    function cleanup() {
        // Leave no server behind: App is a singleton shared with every other test file.
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
    }

    /// Every `Toggle` under `item`: the kit's switch, known by its track.
    function togglesUnder(item, found) {
        if (item.trackWidth !== undefined && typeof item.toggle === "function")
            found.push(item);
        for (let i = 0; i < item.children.length; ++i)
            testCase.togglesUnder(item.children[i], found);
        return found;
    }

    // #78: every switch in the modal has a name, its row's title: the title is a sibling
    // text, so without one a screen reader landed on a switch named nothing.
    function test_every_switch_is_named_by_its_row() {
        const modal = createTemporaryObject(modalComponent, testCase);
        const toggles = testCase.togglesUnder(modal, []);
        verify(toggles.length >= 10, "found " + toggles.length + " switches");
        const names = toggles.map(toggle => toggle.Accessible.name);
        for (let i = 0; i < names.length; ++i)
            verify(names[i].length > 0, "switch " + i + " has no name");
        verify(names.indexOf("Show the Servers group in the rail") >= 0);
        verify(names.indexOf("Follow the Omarchy theme") >= 0);
        verify(names.indexOf("Pulse sending rows") >= 0);
        verify(names.indexOf("Weekly recap") >= 0);
    }

    function test_the_nav_has_the_seven_rows() {
        // The figure's six, and Backup after Appearance (#31, docs/24 §3).
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(modal.navRows.length, 7);
        const texts = modal.navRows.map(row => row.text);
        compare(texts, ["General", "Servers", "Notifications", "Keybindings", "Appearance", "Backup", "About"]);
        for (let i = 0; i < 7; ++i)
            verify(testCase.childNamed(modal, "navRow" + i) !== null);
        // The first pane is General, and the nav has the keyboard.
        compare(modal.paneIndex, 0);
        compare(modal.area, "nav");
    }

    // #79: the dialog is "settings", the pane's title its heading, and the line beside it
    // (which server, how it saves) text.
    function test_a_screen_reader_hears_the_dialog_and_the_pane_title() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const title = testCase.childNamed(modal, "paneTitle");
        compare(title.Accessible.role, Accessible.Heading);
        compare(title.Accessible.name, "General");
        let dialog = title.parent;
        while (dialog !== null && dialog.Accessible.role !== Accessible.Dialog)
            dialog = dialog.parent;
        verify(dialog !== null, "no dialog above the pane");
        compare(dialog.Accessible.name, "settings");
        const head = testCase.childNamed(modal, "qbtNavHead");
        compare(head.Accessible.role, Accessible.Heading);
        compare(head.Accessible.name, "qBittorrent");
        modal.close();
    }

    function test_digits_switch_panes_and_tab_hands_over_the_keyboard() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        verify(modal.opened);

        keyClick(Qt.Key_2);
        compare(modal.paneIndex, 1);
        compare(testCase.childNamed(modal, "navRow1").selected, true);
        compare(testCase.childNamed(modal, "navRow0").selected, false);

        keyClick(Qt.Key_Tab);
        compare(modal.area, "pane");
        keyClick(Qt.Key_Tab);
        compare(modal.area, "nav");

        // j and k walk the nav, through both groups (docs/16 §2), and wrap.
        keyClick(Qt.Key_J);
        compare(modal.paneIndex, 2);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        compare(modal.paneIndex, modal.paneCount - 1);
        keyClick(Qt.Key_J);
        compare(modal.paneIndex, 0);

        keyClick(Qt.Key_7);
        compare(modal.paneIndex, 6);
        keyClick(Qt.Key_1);
        compare(modal.paneIndex, 0);
    }

    // #31, docs/24 §3: back up, restore, automatic copies.
    function test_the_backup_pane() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_6);
        compare(modal.paneIndex, 5);
        compare(modal.paneRowCount, 3);
        verify(testCase.childNamed(modal, "backupRow").visible);
        verify(App.backupSummary.indexOf("Passwords and API keys stay in the keyring") >= 0, App.backupSummary);

        // The third row steps the automatic copies: weekly (the default), daily, off, weekly.
        compare(App.autoBackupMode, "weekly");
        modal.area = "pane";
        modal.paneCursor = 2;
        modal.activateRow();
        compare(App.autoBackupMode, "daily");
        modal.activateRow();
        compare(App.autoBackupMode, "off");
        modal.activateRow();
        compare(App.autoBackupMode, "weekly");
    }

    function test_a_backup_reads_back_into_the_restore_sheet() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_6);
        // The tests' own data home (tests/CMakeLists.txt), never the real one.
        const path = String(StandardPaths.writableLocation(StandardPaths.GenericDataLocation)).replace(/^file:\/\//, "")
                     + "/omaseed-tst-settings.omaseed";
        compare(App.backUpTo(path), "");

        const info = App.readBackup(path);
        verify(info.ok, info.error);
        verify(info.title.indexOf("omaseed-tst-settings.omaseed") >= 0, info.title);
        verify(info.safety.indexOf("before-restore-") >= 0, info.safety);
        const sheet = modal.restoreSheet;
        sheet.openFor(info);
        verify(sheet.opened);
        const settings = info.parts.find(part => part.key === "settings");
        verify(settings !== undefined && settings.checked);
        const before = sheet.count;
        verify(before >= 1);
        sheet.toggle("settings");
        compare(sheet.count, before - 1);
        sheet.toggle("settings");

        // The tests' App is a demo, and a demo never writes the real settings: restore says no.
        // (OmaseedBackup's own tests restore for real.)
        verify(App.demoMode);
        const result = App.restoreBackup({
            "settings": true,
            "servers": false,
            "history": []
        });
        verify(!result.ok);
        verify(result.text.indexOf("demo") >= 0, result.text);
        sheet.close();

        // Anything that is not an omaseed backup is refused before the sheet.
        const notBackup = App.readBackup(String(Qt.resolvedUrl("tst_Settings.qml")).replace(/^file:\/\//, ""));
        verify(!notBackup.ok);
        verify(notBackup.error.indexOf("not an omaseed backup") >= 0, notBackup.error);
    }

    function test_esc_closes_the_modal_and_gives_the_focus_back() {
        const probe = createTemporaryObject(probeComponent, testCase);
        const modal = createTemporaryObject(modalComponent, testCase);
        probe.forceActiveFocus();
        verify(probe.activeFocus);

        modal.open();
        verify(modal.opened);
        verify(!probe.activeFocus);

        closedSpy.clear();
        closedSpy.target = modal;
        keyClick(Qt.Key_Escape);
        verify(!modal.opened);
        compare(closedSpy.count, 1);
        verify(probe.activeFocus);
    }

    SignalSpy {
        id: closedSpy

        signalName: "closed"
    }

    SignalSpy {
        id: helpSpy

        signalName: "helpRequested"
    }

    SignalSpy {
        id: serverChangedSpy

        signalName: "serverChanged"
        target: App
    }

    // The pane head's `esc` looks like a key, so a click does what the key does.
    function test_a_click_on_the_esc_caption_closes_the_modal() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        closedSpy.clear();
        closedSpy.target = modal;
        const esc = testCase.childNamed(modal, "settingsEsc");
        verify(esc !== null && esc.visible);
        mouseClick(esc);
        verify(!modal.opened);
        compare(closedSpy.count, 1);
    }

    // The Keybindings pane's `?` cap opens the help sheet, as the key does (MainView closes
    // the modal for it).
    function test_a_click_on_the_question_cap_asks_for_help() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_4);
        helpSpy.clear();
        helpSpy.target = modal;
        const cap = testCase.childNamed(modal, "keysHelpKey");
        verify(cap !== null && cap.visible);
        mouseClick(cap);
        compare(helpSpy.count, 1);
    }

    Component {
        id: settingsRowComponent

        SettingsRow {
            description: "Off keeps the bundled Tokyo Night."
            title: "Follow the Omarchy theme"
            width: 400
        }
    }

    Component {
        id: restoreComponent

        Item {
            property alias sheet: restore

            anchors.fill: parent

            RestoreSheet {
                id: restore

                anchors.fill: parent
            }
        }
    }

    // #77: a restore part is a check box named by the part; its toggle and press tick it,
    // and a part the file lacks takes neither.
    function test_a_restore_part_is_a_check_box() {
        const host = createTemporaryObject(restoreComponent, testCase);
        const sheet = host.sheet;
        sheet.openFor({
                          "title": "Restore",
                          "parts": [{
                                  "key": "servers",
                                  "label": "servers",
                                  "detail": "two servers",
                                  "enabled": true,
                                  "checked": true
                              }, {
                                  "key": "torrents",
                                  "label": "torrents",
                                  "enabled": false,
                                  "checked": false
                              }]
                      });
        const part = testCase.childNamed(sheet, "restorePart-servers");
        compare(part.Accessible.role, Accessible.CheckBox);
        compare(part.Accessible.name, "servers");
        compare(part.Accessible.description, "two servers");
        verify(part.Accessible.checked);
        part.Accessible.toggleAction();
        verify(!part.Accessible.checked);
        part.Accessible.pressAction();
        verify(part.Accessible.checked);
        const missing = testCase.childNamed(sheet, "restorePart-torrents");
        missing.Accessible.toggleAction();
        verify(!missing.Accessible.checked, "a part the file lacks was ticked");
        sheet.close();
    }

    // #77: the pane head's `esc` and the Keybindings pane's `?` are buttons whose press is
    // their click; a behaviour row is a group named by its title around its control.
    function test_the_heads_buttons_and_the_rows_for_a_screen_reader() {
        const row = createTemporaryObject(settingsRowComponent, testCase);
        compare(row.Accessible.role, Accessible.Grouping);
        compare(row.Accessible.name, "Follow the Omarchy theme");
        compare(row.Accessible.description, "Off keeps the bundled Tokyo Night.");

        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_4);
        helpSpy.clear();
        helpSpy.target = modal;
        const cap = testCase.childNamed(modal, "keysHelpKey");
        compare(cap.Accessible.role, Accessible.Button);
        compare(cap.Accessible.name, "every shortcut");
        cap.Accessible.pressAction();
        compare(helpSpy.count, 1);

        closedSpy.clear();
        closedSpy.target = modal;
        const esc = testCase.childNamed(modal, "settingsEsc");
        compare(esc.Accessible.role, Accessible.Button);
        compare(esc.Accessible.name, "close");
        esc.Accessible.pressAction();
        verify(!modal.opened);
        compare(closedSpy.count, 1);
    }

    // docs/34 D3 a, D5 a: the qBittorrent group's server is a select once there are two
    // servers; a click opens its list on the next one, enter moves the app there, and the
    // footer says the library went too. With one, a label that does nothing.
    function test_a_click_on_the_qbittorrent_server_select_opens_its_list() {
        const first = testCase.addTestServer("first", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        const select = testCase.childNamed(modal, "qbtNavServer");
        verify(select !== null && select.visible);
        verify(!select.switches);
        verify(testCase.childNamed(select, "serverSelectPlain").visible);
        waitForItemPolished(select.parent);
        verify(select.width > 0);
        serverChangedSpy.clear();
        mouseClick(select);
        compare(serverChangedSpy.count, 0, "one server: nothing to move to");
        verify(!select.listOpen);
        verify(modal.opened);

        const second = testCase.addTestServer("second", "http://127.0.0.1:8082");
        compare(App.serverList.defaultId, first);
        verify(select.switches);
        waitForItemPolished(select.parent);
        serverChangedSpy.clear();
        mouseClick(select);
        verify(select.listOpen);
        compare(serverChangedSpy.count, 0, "the click only opens the list");
        compare(select.list.heading, "switch omaseed to");
        compare(select.list.footer, "The library and the tray follow, and it becomes the default.");
        compare(select.list.items.map(item => item.text), ["first", "second"]);
        compare(select.list.items[0].note, "on it ✓");
        compare(select.list.cursorIndex, 1);
        keyClick(Qt.Key_Return);
        verify(!select.listOpen);
        verify(serverChangedSpy.count > 0);
        compare(App.serverList.defaultId, second);
        compare(modal.sheet.footerRight, "omaseed is on second now · the library too");
        verify(modal.opened);
        verify(modal.inQbt);
    }

    // Review RUN-14: lowercase buttons, and a server without a credential has no secret to lose.
    function test_removing_a_server_without_auth_does_not_mention_a_secret() {
        const id = App.serverList.add({
                                          "name": "open",
                                          "url": "http://127.0.0.1:8081",
                                          "authMode": "none"
                                      }, "");
        verify(id.length > 0);
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        modal.paneCursor = 0;
        modal.removeCursorServer();
        verify(modal.confirm.subtitle.indexOf("secret") < 0, modal.confirm.subtitle);
        compare(modal.confirm.confirmText, "remove");
        compare(modal.confirm.cancelText, "cancel");
        modal.confirm.close();
    }

    // Review Q10: secrets in plain files get one muted warning row naming the directory,
    // and no row while a keychain holds them.
    function test_the_servers_pane_names_the_plain_secrets_directory() {
        const modal = createTemporaryObject(modalComponent, testCase, {
                                                "plainSecretsPath": ""
                                            });
        modal.open();
        modal.showPane(1);
        const row = findChild(modal, "plainSecretsRow");
        verify(row);
        verify(!row.visible);

        modal.plainSecretsPath = "/home/someone/.config/omaseed/secrets";
        verify(row.visible);
        const text = findChild(modal, "plainSecretsText").text;
        verify(text.indexOf("/home/someone/.config/omaseed/secrets") >= 0, text);
        verify(text.indexOf("plain files") >= 0, text);
        // Not a row the cursor walks: the servers, then "add a server", as before.
        compare(modal.paneRowCount, App.serverList.count + 1);
    }

    function test_the_servers_pane_prints_url_auth_and_count() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        verify(id.length > 0);
        App.serverList.setProbeResult(id, true, "v5.2.3", "2.15.1", 3, 4, "");

        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        // One server plus the "add a server" row.
        compare(modal.paneRowCount, 2);

        const row = testCase.childNamed(modal, "serverRow0");
        verify(row !== null);
        compare(row.title, "playground");
        compare(row.description, "http://127.0.0.1:8081 · API key · v5.2.3 · 3 torrents");
        compare(row.status, "in use"); // the only server is the default one
        compare(row.failing, false);

        const addRow = testCase.childNamed(modal, "serverRowAdd");
        verify(addRow !== null);
        compare(addRow.title, "add a server");
        compare(addRow.status, "a");
        verify(addRow.dashed);

        // #77: each row is a button named by its server, its line and status the description.
        compare(row.Accessible.role, Accessible.Button);
        compare(row.Accessible.name, "playground");
        compare(row.Accessible.description, row.description + " · in use");
        compare(addRow.Accessible.name, "add a server");
        let clicks = 0;
        row.clicked.connect(() => clicks++);
        row.Accessible.pressAction();
        compare(clicks, 1);

        // A failed test turns the row red and says "retrying".
        App.serverList.setProbeResult(id, false, "", "", -1, 12, "connection refused");
        compare(row.failing, true);
        verify(Qt.colorEqual(testCase.childNamed(row, "serverDot").color, Theme.red));
        // ...and keeps the version and count it had.
        compare(row.description, "http://127.0.0.1:8081 · API key · v5.2.3 · 3 torrents");

        // One is "1 torrent" (servers-walk: the pane read "1 torrents").
        App.serverList.setProbeResult(id, true, "v5.2.3", "2.15.1", 1, 4, "");
        compare(row.description, "http://127.0.0.1:8081 · API key · v5.2.3 · 1 torrent");
    }

    // Review RUN-9: a server nobody has tested has a dim dot, not the green of one that
    // answered; one that failed and is not the app's own says "unreachable", not "retrying".
    function test_an_untested_server_is_not_green_and_a_failed_one_is_unreachable() {
        testCase.addTestServer("first", "http://127.0.0.1:8081");
        const id = testCase.addTestServer("dead", "http://127.0.0.1:9");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const row = testCase.childNamed(modal, "serverRow1");
        verify(row.untested);
        verify(Qt.colorEqual(testCase.childNamed(row, "serverDot").color, Theme.dimForeground));
        App.serverList.setProbeResult(id, false, "", "", -1, 12, "connection refused");
        compare(row.status, "unreachable");
        App.serverList.setProbeResult(id, true, "v5.2.3", "2.15.1", 0, 3, "");
        verify(Qt.colorEqual(testCase.childNamed(row, "serverDot").color, Theme.green));
    }

    // Q23: a `test` pressed in a new server's sheet before `save` is not lost with the
    // sheet — `save` hands it to the row the save created. Demo mode's `App.testServer()`
    // answers "5.2.3 / 2.15.1 / 142 torrents / 3 ms" without a network.
    function test_saving_a_new_server_carries_its_test_result_onto_the_row() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "playground";
        sheet.editUrl = "http://127.0.0.1:8081";
        sheet.editAuthMode = "none";

        sheet.actionTriggered("test");
        tryVerify(() => sheet.tested, 2000);
        compare(sheet.testOk, true);
        compare(sheet.testVersion, "5.2.3");
        compare(sheet.testCount, 142);
        compare(sheet._testCountText, "142 torrents");
        compare(App.serverList.count, 0); // nothing saved yet, nothing to attribute it to

        sheet.actionTriggered("save");
        compare(App.serverList.count, 1);
        verify(!sheet.opened);
        const id = App.serverList.idAt(0);
        const map = App.serverList.serverMap(id);
        compare(map.version, "5.2.3");
        compare(map.apiVersion, "2.15.1");
        compare(map.versionText, "v5.2.3");
        compare(map.torrentCount, 142);
        compare(map.reachableText, "yes · 3 ms");

        const row = testCase.childNamed(modal, "serverRow0");
        verify(row !== null);
        compare(row.description, "http://127.0.0.1:8081 · none · v5.2.3 · 142 torrents");
        compare(row.failing, false);
        compare(modal.paneCursor, 0);
    }

    /// A test of the sheet's fields that answers `ok`, as `_runTest()` leaves the sheet and
    /// App answers it (demo mode's own test always passes).
    function answerTest(sheet, ok, error) {
        sheet.testing = true;
        sheet._pendingKey = sheet._connectionKey;
        App.serverTested(sheet.serverId, ok, ok ? "5.2.3" : "", ok ? "2.15.1" : "", ok ? 7 : -1,
                         ok ? 3 : -1, error, "");
    }

    /// The sheet's foot button with this text, or null.
    function sheetButton(sheet, text) {
        const row = sheet.actionsRow;
        for (let i = 0; i < row.children.length; ++i) {
            if (row.children[i].text === text)
                return row.children[i];
        }
        return null;
    }

    // #68 (docs/DECISIONS.md C10): a new server's `save` waits for a test, as first run's
    // `continue` does. Until then it is dimmed, the footer says "test first", and `test` is
    // the primary (`enter` in a field tests).
    function test_a_new_servers_save_waits_for_a_test() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "playground";
        sheet.editUrl = "http://127.0.0.1:8081";

        compare(sheet.saveState, "wait");
        compare(sheet.actions.map(action => action.text), ["cancel", "test", "save"]);
        const save = testCase.sheetButton(sheet, "save");
        verify(save);
        compare(save.enabled, false);
        compare(save.primary, false);
        verify(testCase.sheetButton(sheet, "test").primary);
        compare(sheet.footerRight, "test first");
        compare(sheet.hints[1].label, "test");
        verify(sheet.actionButtons().indexOf(save) < 0, "a dimmed save is in the tab cycle");

        // Neither the button's signal nor `enter` saves it.
        sheet.actionTriggered("save");
        compare(App.serverList.count, 0);
        verify(sheet.opened);

        // `enter` in a field runs the test; its pass makes `save` the primary.
        sheet.triggerPrimary();
        verify(sheet.testing);
        compare(sheet.saveState, "wait");
        tryVerify(() => sheet.tested, 2000);
        compare(sheet.saveState, "save");
        compare(testCase.sheetButton(sheet, "save"), save, "the buttons were built again");
        verify(save.enabled);
        verify(save.primary);
        compare(testCase.sheetButton(sheet, "test").primary, false);
        compare(sheet.footerRight, "");
        compare(sheet.hints[1].label, "save");

        // A pass is for the address it tested: another one waits for its own test.
        sheet.editUrl = "http://127.0.0.1:8082";
        compare(sheet.tested, false);
        compare(sheet.saveState, "wait");
        compare(save.enabled, false);
        // The name is only a label: it keeps a pass.
        testCase.answerTest(sheet, true, "");
        compare(sheet.saveState, "save");
        sheet.editName = "strict";
        compare(sheet.saveState, "save");

        sheet.triggerPrimary();
        compare(App.serverList.count, 1);
        verify(!sheet.opened);
    }

    // #84 (C25 K4, K7, K9): the engine row, recognise first; a test that finds Transmission
    // names it on the chip, gives the auth chips Transmission's two, and refuses the save,
    // keeping what was typed.
    function test_the_engine_row_recognises_and_refuses_an_engine_not_driven_yet() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "seedbox";
        sheet.editUrl = "http://127.0.0.1:9091";
        compare(sheet.editEngineChoice, "recognise");
        compare(sheet.fieldMap().engineChoice, "recognise");
        // Before recognition, qBittorrent's three sign-ins.
        compare(sheet.authModes, ["apiKey", "cookie", "none"]);
        const recognise = findChild(sheet, "engineChip_recognise");
        verify(recognise);
        compare(recognise.text, "recognise");
        verify(findChild(sheet, "engineChip_transmission"));
        verify(findChild(sheet, "engineChip_deluge"));

        sheet.testing = true;
        sheet._pendingKey = sheet._connectionKey;
        App.serverTested("", false, "4.1.3", "6.0.0", -1, 3, "omaseed can't drive Transmission yet",
                         "This build of omaseed drives qBittorrent.", {
                             "engine": "transmission",
                             "engineName": "Transmission",
                             "endpoint": "http://127.0.0.1:9091/transmission/rpc",
                             "refused": true
                         });
        compare(recognise.text, "recognise: Transmission");
        compare(sheet.engine, "transmission");
        compare(sheet.authModes, ["cookie", "none"]);
        verify(findChild(sheet, "authChip_cookie"));
        compare(findChild(sheet, "authChip_apiKey"), null);
        // K9: refused, with no "save anyway"; what was typed stays.
        compare(sheet.saveState, "refused");
        compare(sheet.actions.map(action => action.text), ["cancel", "test", "save"]);
        compare(testCase.sheetButton(sheet, "save").enabled, false);
        compare(sheet.footerRight, "can't be saved");
        compare(sheet.testProblem.headline, "omaseed can't drive Transmission yet");
        verify(sheet.testProblem.line.indexOf("It answered at http://127.0.0.1:9091/transmission/rpc") > 0);
        compare(sheet.editUrl, "http://127.0.0.1:9091");
        compare(sheet.editName, "seedbox");
        sheet.actionTriggered("save");
        compare(App.serverList.count, 0);

        // Picking an engine asks a new test, for that one only.
        findChild(sheet, "engineChip_deluge").clicked();
        compare(sheet.tested, false);
        compare(sheet.fieldMap().engineChoice, "deluge");
        compare(sheet.authModes, ["password"]);
        compare(sheet.editAuthMode, "password");
        verify(findChild(sheet, "lonePassField").visible);
        sheet.close();
    }

    // #68: a failed test offers `save anyway`, so a server that is down now can still be
    // added on purpose; the failure goes onto the new row (Q23).
    function test_a_failed_test_offers_save_anyway() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "away";
        sheet.editUrl = "http://127.0.0.1:9";
        testCase.answerTest(sheet, false, "connection refused");

        compare(sheet.saveState, "anyway");
        compare(sheet.actions.map(action => action.text), ["cancel", "test", "save anyway"]);
        const anyway = testCase.sheetButton(sheet, "save anyway");
        verify(anyway.enabled);
        compare(anyway.primary, false);
        // `enter` tests again; the deliberate save is the button.
        verify(testCase.sheetButton(sheet, "test").primary);
        compare(sheet.footerRight, "");
        verify(sheet.actionButtons().indexOf(anyway) >= 0);

        anyway.clicked();
        compare(App.serverList.count, 1);
        verify(!sheet.opened);
        const map = App.serverList.serverMap(App.serverList.idAt(0));
        compare(map.name, "away");
        compare(map.reachableText, "no · connection refused");
        compare(map.torrentCount, -1);
    }

    // #126 (docs/33 N5, docs/35 N7): a failed test is the sheet's notice under its head, in
    // ErrorWords' words, red for nothing there, yellow for a timeout, details folded.
    function test_a_failed_test_is_the_sheets_notice() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "seedbox";
        sheet.editUrl = "http://127.0.0.1:8095";
        verify(!sheet._testFailed);
        testCase.answerTest(sheet, false, "network: GET app/version: Connection refused");

        const band = findChild(sheet, "testNotice");
        verify(band !== null);
        tryVerify(() => band.shown && band.height > 0, 2000);
        compare(band.title, "Nothing answered at 127.0.0.1:8095");
        compare(band.body, "Check the address and port, and that qBittorrent's Web UI is on (Tools › Options › Web UI).");
        compare(band.kind, "error");
        compare(band.actions.length, 0);
        verify(band.details.indexOf("GET app/version · Connection refused") >= 0);
        compare(band.detailsOpen, false);
        // "save anyway" stays the sheet's button (#68).
        verify(testCase.sheetButton(sheet, "save anyway") !== null);

        testCase.answerTest(sheet, false, "timeout: GET app/version: no reply within 8000 ms");
        compare(band.title, "seedbox isn't answering");
        compare(band.kind, "warning");

        // A pass takes the band away.
        testCase.answerTest(sheet, true, "");
        verify(!sheet._testFailed);
        compare(band.shown, false);
        sheet.close();
    }

    // #68, assumed (confirm): an existing server needs a new test only when its URL, auth
    // mode or credentials changed. A rename, or a new path map, saves at once.
    function test_an_edit_needs_a_test_only_for_a_new_address_or_login() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        sheet.actionTriggered("edit");
        compare(sheet.saveState, "save");
        compare(sheet.footerRight, "");

        sheet.editUrl = "http://127.0.0.1:8082";
        compare(sheet.saveState, "wait");
        compare(testCase.sheetButton(sheet, "save").enabled, false);
        compare(sheet.footerRight, "test first");
        sheet.editUrl = "http://127.0.0.1:8081";
        compare(sheet.saveState, "save");

        sheet.editAuthMode = "none";
        compare(sheet.saveState, "wait");
        sheet.editAuthMode = "apiKey";
        compare(sheet.saveState, "save");
        sheet.editSecret = "a new key";
        compare(sheet.saveState, "wait");
        sheet.editSecret = "";

        sheet.editName = "renamed";
        sheet.editLocal = "/srv/torrents";
        compare(sheet.saveState, "save");
        sheet.triggerPrimary();
        verify(!sheet.opened);
        const map = App.serverList.serverMap(id);
        compare(map.name, "renamed");
        compare(map.url, "http://127.0.0.1:8081");
        compare(map.pathMapLocal, "/srv/torrents");
        // Q23's other half: nothing was tested, so nothing new is on the row.
        compare(map.torrentCount, -1);
    }

    // #68: an edit to another address saves once a test has answered for it.
    function test_an_edit_to_a_new_address_saves_after_its_test() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        sheet.actionTriggered("edit");
        sheet.editUrl = "http://127.0.0.1:8082";
        sheet.actionTriggered("save");
        compare(App.serverList.serverMap(id).url, "http://127.0.0.1:8081");
        verify(sheet.opened);

        sheet.actionTriggered("test");
        tryVerify(() => sheet.tested, 2000);
        compare(sheet.saveState, "save");
        sheet.actionTriggered("save");
        verify(!sheet.opened);
        compare(App.serverList.serverMap(id).url, "http://127.0.0.1:8082");
    }

    function test_a_opens_an_empty_server_sheet_and_enter_opens_the_row() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_2);
        keyClick(Qt.Key_Tab);
        compare(modal.area, "pane");
        compare(modal.paneCursor, 0);

        // `enter` on the first row opens that server, in view mode.
        keyClick(Qt.Key_Return);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.editing, false);
        compare(modal.serverSheet.serverId, id);
        compare(modal.serverSheet.title, "playground");

        // `esc` closes the sheet first; the modal stays up.
        keyClick(Qt.Key_Escape);
        verify(!modal.serverSheet.opened);
        verify(modal.opened);

        // `a` anywhere in the Servers pane opens an empty sheet, already in edit mode.
        keyClick(Qt.Key_A);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.editing, true);
        compare(modal.serverSheet.isNew, true);
        compare(modal.serverSheet.title, "New server");
        modal.serverSheet.close();
    }

    // The "add a server" row prints `a` while the keyboard is still on the nav, as it is
    // straight after `2`: the key works from there too.
    function test_a_adds_a_server_from_the_nav_too() {
        testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_2);
        compare(modal.area, "nav");
        keyClick(Qt.Key_A);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.isNew, true);
        modal.serverSheet.close();

        // Elsewhere `a` is still nothing of the modal's.
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_A);
        verify(!modal.serverSheet.opened);
    }

    // Review RUN-15: saving an empty form says what is missing instead of doing nothing.
    function test_saving_an_empty_server_says_name_and_url_are_required() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.actionTriggered("save");
        verify(sheet.opened);
        compare(App.serverList.count, 0);
        compare(sheet.errorText, "name and URL are required");
        sheet.close();
        sheet.openNew();
        compare(sheet.errorText, "", "the refusal outlived the sheet");
        sheet.close();
    }

    function test_the_server_sheet_masks_the_key_and_switches_to_the_fields() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        verify(sheet.opened);

        // View mode: Fig. 9's five key/value rows, in the figure's order, after the engine
        // (C25 K12: stored, from the last contact; the name alone before one).
        const kv = sheet.body.children[0];
        compare(kv.items.length, 6);
        compare(kv.items.map(item => item.key), ["Engine", "Auth", "Key", "Path map", "Torrents", "Reachable"]);
        compare(kv.items[0].value, "qBittorrent");
        compare(kv.items[1].value, "API key · stored in libsecret");
        // The stored secret is never drawn: the Key row is a fixed mask of bullets.
        verify(kv.items[2].value.length > 0);
        compare(kv.items[2].value.replace(/•/g, ""), "");
        compare(kv.items[3].value, "/downloads → /data/torrents");
        compare(kv.items[4].value, "not tested yet");

        // Fig. 9's three actions in view mode, and three different ones once editing.
        compare(sheet.actions.map(action => action.text), ["test", "remove", "edit"]);
        sheet.actionTriggered("edit");
        compare(sheet.editing, true);
        compare(sheet.actions.map(action => action.text), ["cancel", "test", "save"]);
        compare(sheet.fieldMap().url, "http://127.0.0.1:8081");
        compare(sheet.fieldMap().authMode, "apiKey");
        compare(sheet.fieldMap().pathMapRemote, "/downloads");
        // #84: a saved server is tested as the engine it is stored with (K4).
        compare(sheet.editEngineChoice, "qbittorrent");
        compare(sheet.fieldMap().engine, "qbittorrent");
        sheet.close();
    }

    // Issue #2: a test refused for its certificate shows the fingerprint to trust; trusting
    // saves it with the server and tests again (demo mode's test passes).
    function test_a_refused_certificate_offers_its_fingerprint_to_trust() {
        const id = testCase.addTestServer("tls", "https://127.0.0.1:9");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        const trust = testCase.childNamed(sheet, "certificateTrust");
        verify(!trust.visible);

        sheet.testing = true; // as _runTest() leaves it
        App.serverCertificateRefused(id, "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2", "");
        App.serverTested(id, false, "", "", -1, 4, "untrusted certificate",
                         "certificate: GET app/version: The certificate is self-signed, and untrusted");
        verify(trust.visible);
        compare(trust.changed, false);
        compare(testCase.childNamed(trust, "fingerprintFirstHalf").text,
                "9C:E0:61:8D:3D:BA:07:B6:AB:CE:06:A4:C4:DB:22:22");
        compare(testCase.childNamed(trust, "fingerprintSecondHalf").text,
                "F6:92:96:05:A3:44:B4:F8:EC:21:78:9F:27:61:A2:A2");
        compare(trust.trustButton.text, "trust this certificate");
        verify(trust.trustButton.primary);
        compare(sheet._reachableText, "no · untrusted certificate");
        // The keyboard reaches it like the sheet's other buttons.
        verify(sheet.focusables().indexOf(trust.trustButton) >= 0);

        trust.trustButton.clicked();
        compare(App.serverList.serverMap(id).certificateSha256, "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
        tryVerify(() => sheet.tested && sheet.testOk, 2000);
        verify(!trust.visible);
        const kv = sheet.body.children[0];
        compare(kv.items[kv.items.length - 1].key, "Certificate");
        compare(kv.items[kv.items.length - 1].value, "trusted · 9C:E0:61:8D …");
        sheet.close();
    }

    // Another certificate than the trusted one: said plainly, and the button is not primary.
    function test_a_changed_certificate_is_said_so() {
        const id = App.serverList.add({
            "name": "tls",
            "url": "https://127.0.0.1:9",
            "authMode": "none",
            "certificateSha256": "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2"
        }, "");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        compare(sheet.fieldMap().certificateSha256, "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
        sheet.testing = true;
        App.serverCertificateRefused(id, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
        App.serverTested(id, false, "", "", -1, 4, "the certificate changed", "certificate: …");
        const trust = testCase.childNamed(sheet, "certificateTrust");
        verify(trust.visible);
        verify(trust.changed);
        compare(trust.trustButton.text, "trust the new certificate");
        verify(!trust.trustButton.primary);
        sheet.close();
    }

    // A new server keeps the trusted certificate in its fields until `save`.
    function test_a_new_server_saves_the_certificate_it_trusted() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "tls";
        sheet.editUrl = "https://127.0.0.1:9";
        sheet.editAuthMode = "none";
        sheet.testing = true; // as _runTest() leaves it
        sheet._pendingKey = sheet._connectionKey;
        App.serverCertificateRefused("", "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2", "");
        App.serverTested("", false, "", "", -1, 4, "untrusted certificate", "certificate: …");
        testCase.childNamed(sheet, "certificateTrust").trustButton.clicked();
        compare(sheet.fieldMap().certificateSha256, "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
        compare(App.serverList.count, 0);
        tryVerify(() => sheet.tested && sheet.testOk, 2000);
        sheet.actionTriggered("save");
        compare(App.serverList.count, 1);
        compare(App.serverList.serverMap(App.serverList.idAt(0)).certificateSha256, "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
    }

    // The lost banner's `review certificate`: Servers, the sheet, and a test running.
    function test_review_server_opens_the_sheet_and_tests() {
        const id = testCase.addTestServer("tls", "https://127.0.0.1:9");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.reviewServer(id);
        verify(modal.opened);
        compare(modal.paneIndex, 1);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.serverId, id);
        verify(modal.serverSheet.testing || modal.serverSheet.tested);
        modal.serverSheet.close();
    }

    // Review RUN-9: the footer's `enter` names the button the keyboard is on.
    function test_the_enter_hint_follows_the_focused_button() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const sheet = modal.serverSheet;
        sheet.openFor(App.serverList.serverMap(id));
        const buttons = sheet.actionButtons();
        buttons[0].forceActiveFocus(); // test
        compare(sheet.hints[0].label, "test");
        buttons[2].forceActiveFocus(); // edit
        compare(sheet.hints[0].label, "edit");
        sheet.close();
    }

    // #51 D4: the sheet of a server that is not the default has "switch to it", which does
    // what `d` on its row does; the default one's sheet has no such button.
    function test_the_server_sheet_makes_a_server_the_default() {
        const first = testCase.addTestServer("first", "http://127.0.0.1:8081");
        const second = testCase.addTestServer("second", "http://127.0.0.1:8082");
        compare(App.serverList.defaultId, first);
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        const texts = () => sheet.actionButtons().map(button => String(button.text));

        sheet.openFor(App.serverList.serverMap(first));
        compare(texts(), ["test", "remove", "edit"]);
        sheet.close();

        sheet.openFor(App.serverList.serverMap(second));
        compare(texts(), ["switch to it", "test", "remove", "edit"]);
        const button = sheet.actionButtons()[0];
        // The foot lays its four buttons out on the next polish.
        tryVerify(() => sheet.actionButtons()[3].x > button.x);
        mouseClick(button);
        compare(App.serverList.defaultId, second);
        verify(sheet.opened, "the sheet closed");
        verify(sheet.isDefault);
        compare(texts(), ["test", "remove", "edit"]);
        compare(testCase.childNamed(modal, "serverRow1").status, "in use");
        sheet.close();
    }

    // #51 D4: a right-click on a server row opens its menu (edit, test, switch to it,
    // remove), with the row's keys; "switch to it" is greyed on the default one.
    function test_a_right_click_on_a_server_row_opens_its_menu() {
        const first = testCase.addTestServer("first", "http://127.0.0.1:8081");
        const second = testCase.addTestServer("second", "http://127.0.0.1:8082");
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const menu = testCase.childNamed(modal, "serverMenu");
        let row0 = testCase.childNamed(modal, "serverRow0");
        let row1 = testCase.childNamed(modal, "serverRow1");
        waitForItemPolished(row1.parent);
        const item = action => menu.items.find(entry => entry.action === action);

        mouseClick(row0, row0.width / 2, row0.height / 2, Qt.RightButton);
        verify(menu.opened, "no menu on a server row");
        verify(!modal.serverSheet.opened, "a right-click opened the sheet");
        compare(menu.items.filter(entry => !entry.separator).map(entry => entry.text),
                ["edit", "test", "switch to it", "remove"]);
        compare(item("edit").key, "enter");
        compare(item("default").key, "d");
        compare(item("remove").key, "x");
        verify(item("default").disabled, "make default on the default server");
        keyClick("d");
        verify(menu.opened, "make default ran on the default server");
        keyClick(Qt.Key_Escape);
        verify(!menu.opened);
        verify(modal.opened, "esc closed the modal too");

        mouseClick(row1, row1.width / 2, row1.height / 2, Qt.RightButton);
        verify(menu.opened);
        compare(modal.paneCursor, 1);
        verify(!item("default").disabled);
        const index = menu.items.findIndex(entry => entry.action === "default");
        tryVerify(() => menu.rowsLayout.children[index].y > 0);
        mouseClick(menu.rowsLayout.children[index]);
        verify(!menu.opened);
        compare(App.serverList.defaultId, second);
        // The model is reset by a new default, so the rows are new too.
        row0 = testCase.childNamed(modal, "serverRow0");
        row1 = testCase.childNamed(modal, "serverRow1");
        compare(row1.status, "in use");
        waitForItemPolished(row1.parent);

        // edit opens the row's sheet; remove asks first, as `x` does.
        mouseClick(row0, row0.width / 2, row0.height / 2, Qt.RightButton);
        keyClick(Qt.Key_Return);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.serverId, first);
        modal.serverSheet.close();
        mouseClick(row0, row0.width / 2, row0.height / 2, Qt.RightButton);
        keyClick("x");
        verify(modal.confirm.opened);
        compare(modal.confirm.pendingId, first);
        modal.confirm.close();

        // test opens the sheet and tests, as the lost banner's "review certificate" does.
        mouseClick(row1, row1.width / 2, row1.height / 2, Qt.RightButton);
        const testRow = menu.rowsLayout.children[menu.items.findIndex(entry => entry.action === "test")];
        tryVerify(() => testRow.y > 0);
        mouseClick(testRow);
        verify(modal.serverSheet.opened);
        compare(modal.serverSheet.serverId, second);
        verify(modal.serverSheet.testing || modal.serverSheet.tested);
        modal.serverSheet.close();

        // The dashed "add a server" row has no menu.
        const add = testCase.childNamed(modal, "serverRowAdd");
        mouseClick(add, add.width / 2, add.height / 2, Qt.RightButton);
        verify(!menu.opened);
        verify(!modal.serverSheet.opened);
    }

    function test_the_toggles_round_trip_through_App() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();

        const servers = App.showServersGroup;
        const mapping = App.pathMapEnabled;
        const completed = App.notificationsEnabled["completed"] === true;

        // General: tab into the pane, `enter` on the first row.
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_Tab);
        compare(modal.paneCursor, 0);
        keyClick(Qt.Key_Return);
        compare(App.showServersGroup, !servers);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Space);
        compare(App.pathMapEnabled, !mapping);

        // Notifications: four toggle rows (Q16 dropped the tracker one, docs/28 added the
        // errored one) bound to App.notificationsEnabled, the low-disk threshold field, then
        // the weekly recap (#24 T8). The keyboard is already in the pane — a digit moves the
        // nav without taking it back.
        keyClick(Qt.Key_3);
        compare(modal.paneRowCount, 6);
        compare(modal.area, "pane");
        keyClick(Qt.Key_Return);
        compare(App.notificationsEnabled["completed"] === true, !completed);

        // Appearance: "In the bar" (docs/33 D25) steps through the choices it can have:
        // without the shell, tray icon and none.
        keyClick(Qt.Key_5);
        compare(modal.paneRowCount, 12);
        const tray = App.trayIconMode;
        App.trayIconMode = "auto";
        compare(App.inTheBar, "tray");
        modal.paneCursor = 3;
        modal.area = "pane";
        modal.activateRow();
        compare(App.inTheBar, "none");
        compare(App.trayIconMode, "never");
        modal.activateRow();
        compare(App.inTheBar, "tray");
        compare(App.trayIconMode, "auto");

        // ...and the sixth row swaps the side the hero's speed pair sits on.
        const speeds = App.speedsAlignment;
        modal.paneCursor = 5;
        modal.activateRow();
        verify(App.speedsAlignment !== speeds);
        compare(App.speedsAlignment, speeds === "left" ? "right" : "left");

        // ...and the seventh steps the hero's speed trends (#24 T2): off at first, then both,
        // download, upload, off again.
        compare(App.downloadTrend, false);
        compare(App.uploadTrend, false);
        modal.paneCursor = 6;
        const trends = [[true, true], [true, false], [false, true], [false, false]];
        for (const [down, up] of trends) {
            modal.activateRow();
            compare(App.downloadTrend, down);
            compare(App.uploadTrend, up);
        }

        // ...and the next three cycle the rail's looks (docs/08 §5).
        const hint = App.railScrollHint;
        const tint = App.surfaceTint;
        const hosts = App.trackerHostStyle;
        modal.paneCursor = 7;
        modal.activateRow();
        verify(App.railScrollHint !== hint);
        modal.paneCursor = 8;
        modal.activateRow();
        verify(App.surfaceTint !== tint);
        modal.paneCursor = 9;
        modal.activateRow();
        verify(App.trackerHostStyle !== hosts);

        // ...and the last places Details (docs/10 §5): auto, right, bottom.
        const placement = App.detailsPlacement;
        compare(placement, "auto");
        modal.paneCursor = 10;
        modal.activateRow();
        compare(App.detailsPlacement, "right");
        modal.activateRow();
        compare(App.detailsPlacement, "bottom");

        // Put everything back: App persists these to state.json.
        App.showServersGroup = servers;
        App.pathMapEnabled = mapping;
        App.setNotificationEnabled("completed", completed);
        App.trayIconMode = tray;
        App.speedsAlignment = speeds;
        App.railScrollHint = hint;
        App.surfaceTint = tint;
        App.trackerHostStyle = hosts;
        App.detailsPlacement = placement;
    }

    // T5.5 (Q16): the low-disk threshold is the fourth row of the Notifications pane, a
    // field prefilled with App's value; `enter` on the row focuses it, and accepting the
    // field writes the value back (clamped to 1 or more by App).
    function test_the_notifications_pane_edits_the_low_disk_threshold() {
        const modal = createTemporaryObject(modalComponent, testCase);
        const was = App.lowDiskThresholdGiB;
        compare(was, 2); // the default, nothing persisted by an earlier test

        modal.open();
        keyClick(Qt.Key_3);
        compare(modal.paneIndex, 2);

        const row = testCase.childNamed(modal, "lowDiskThresholdRow");
        verify(row, "the Notifications pane has no threshold row");
        compare(row.title, "Low-disk threshold");
        const field = testCase.childNamed(modal, "lowDiskThresholdField");
        verify(field, "the threshold row has no field");
        compare(field.text, "2"); // prefilled when the modal opened
        compare(field.trailing, "GiB");

        // The disk row's caption says what the figure watches (Q16), and wraps rather than
        // eliding, so the row is taller than a one-line one.
        const diskRow = testCase.childNamed(modal, "notifyRow-diskFull");
        verify(diskRow.description.indexOf("default save path") >= 0);
        const completedRow = testCase.childNamed(modal, "notifyRow-completed");
        verify(diskRow.implicitHeight > completedRow.implicitHeight);

        // `j` x4 from the first row lands on the field's row; `enter` hands it the keyboard.
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        compare(modal.paneCursor, 4);
        compare(row.hasCursor, true);
        keyClick(Qt.Key_Return);
        verify(field.editing);

        // Type a new value; accepting it reaches App and re-syncs the field.
        keyClick(Qt.Key_Backspace);
        keyClick(Qt.Key_5);
        keyClick(Qt.Key_Return);
        compare(App.lowDiskThresholdGiB, 5);
        compare(field.text, "5");
        verify(!field.editing);

        // A value App clamps comes back as what App kept.
        field.text = "0";
        field.accepted();
        compare(App.lowDiskThresholdGiB, 2); // `0` is "unset": the default
        compare(field.text, "2");
        field.text = "-3";
        field.accepted();
        compare(App.lowDiskThresholdGiB, 1);
        compare(field.text, "1");

        App.lowDiskThresholdGiB = was;
    }

    // #24 T5: the rows' pulse is the Appearance pane's last row, off until turned on.
    function test_the_appearance_pane_turns_the_rows_pulse_on() {
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(App.pulseRows, false);
        modal.open();
        keyClick(Qt.Key_5);
        compare(modal.paneIndex, 4);
        const row = testCase.childNamed(modal, "pulseRowsRow");
        verify(row, "the Appearance pane has no pulse row");
        compare(row.title, "Pulse sending rows");
        modal.paneCursor = 11;
        modal.area = "pane";
        compare(row.hasCursor, true);
        modal.activateRow();
        compare(App.pulseRows, true);
        modal.activateRow();
        compare(App.pulseRows, false);
    }

    // #24 T8: the weekly recap is the Notifications pane's last row, off until turned on.
    function test_the_notifications_pane_turns_the_weekly_recap_on() {
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(App.notificationsEnabled.weekly, false);
        modal.open();
        keyClick(Qt.Key_3);
        compare(modal.paneIndex, 2);
        const row = testCase.childNamed(modal, "notifyRow-weekly");
        verify(row, "the Notifications pane has no weekly recap row");
        compare(row.title, "Weekly recap");

        keyClick(Qt.Key_Tab);
        for (let i = 0; i < 5; ++i)
            keyClick(Qt.Key_J);
        compare(modal.paneCursor, 5);
        compare(row.hasCursor, true);
        keyClick(Qt.Key_Return);
        compare(App.notificationsEnabled.weekly, true);
        keyClick(Qt.Key_Return);
        compare(App.notificationsEnabled.weekly, false);
    }

    // T4.8: T4.5 could not wire this row, because T4.3 (which owns the flag) had not landed.
    function test_the_general_pane_remembers_delete_files_by_default() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        compare(modal.paneIndex, 0);
        compare(modal.paneRowCount, 5);

        const row = testCase.childNamed(modal, "deleteFilesRow");
        verify(row, "the General pane has no delete-files row");
        compare(row.title, "Delete files by default");

        const was = App.deleteFilesDefault;
        modal.area = "pane";
        modal.paneCursor = 2;
        compare(row.hasCursor, true);
        modal.activateRow();
        compare(App.deleteFilesDefault, !was);
        modal.activateRow();
        compare(App.deleteFilesDefault, was);
    }

    // Start at login: enter steps off, in the tray, window, off. The test's XDG_CONFIG_HOME is
    // the build's qml-env, so the entry it writes and removes is never the real one.
    function test_the_general_pane_starts_at_login() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const row = testCase.childNamed(modal, "startAtLoginRow");
        verify(row, "the General pane has no start-at-login row");
        compare(App.startAtLogin, "off");
        modal.area = "pane";
        modal.paneCursor = 4;
        compare(row.hasCursor, true);
        for (const want of ["tray", "window", "off"]) {
            modal.activateRow();
            compare(App.startAtLogin, want);
        }
    }

    function test_the_keybindings_pane_is_the_help_sheets_own_rows() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_4);
        compare(modal.paneIndex, 3);
        // Read-only in v1: no cursor rows at all.
        compare(modal.paneRowCount, 0);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_J);
        compare(modal.paneCursor, 0);

        // Batch-2 J4: it says rebinding comes later and that `?` shows the same list.
        const note = testCase.childNamed(modal, "keysNote");
        verify(note !== null && note.visible);
        // The list fits the pane and scrolls itself; the pane does not scroll around it.
        const bar = testCase.childNamed(modal, "helpScrollBar");
        verify(bar !== null);
        const scroller = testCase.childNamed(modal, "paneScroller");
        verify(scroller.contentHeight <= scroller.height + 1);
    }

    // T5.5 Q1: the transparency row's placeholder is the shell's own default, not a
    // literal — 95 under the harness's empty shell.toml — so the user sees what "empty" means.
    function test_the_transparency_placeholder_is_the_shells_default() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_5);
        compare(modal.paneIndex, 4);
        const field = testCase.childNamed(modal, "windowAlphaField");
        verify(field !== null);
        compare(field.placeholder, String(Math.round(Theme.defaultWindowAlpha * 100)));
        compare(field.placeholder, "95");
    }

    // Review RUN-4: an override stored earlier shows in its field, and a refused value does
    // not stay there as if it applied.
    function test_the_appearance_fields_show_the_stored_overrides() {
        App.windowAlphaOverride = 80;
        App.baseSizeOverride = 16;
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_5);
        const field = testCase.childNamed(modal, "windowAlphaField");
        compare(field.text, "80", "the stored transparency does not show");
        compare(testCase.childNamed(modal, "baseSizeField").text, "16");

        field.focusInput();
        field.text = "40"; // outside 50–100: App stores "follow the shell"
        field.accepted();
        compare(App.windowAlphaOverride, 0);
        compare(field.text, "", "the refused 40 stayed in the field");

        App.baseSizeOverride = 0;
        compare(testCase.childNamed(modal, "baseSizeField").text, "");
    }

    // A number typed and clicked away from reaches App as enter would have sent it: it used
    // to show in the field and never apply. What does not parse is the empty meaning, again
    // as on enter.
    function test_the_number_fields_apply_when_the_caret_leaves() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const probe = createTemporaryObject(probeComponent, testCase);

        keyClick(Qt.Key_5);
        const base = testCase.childNamed(modal, "baseSizeField");
        base.focusInput();
        base.text = "18";
        probe.forceActiveFocus();
        verify(!base.editing);
        compare(App.baseSizeOverride, 18);
        compare(base.text, "18");

        const alpha = testCase.childNamed(modal, "windowAlphaField");
        alpha.focusInput();
        alpha.text = "40"; // outside 50–100: "follow the shell", as on enter
        probe.forceActiveFocus();
        compare(App.windowAlphaOverride, 0);
        compare(alpha.text, "");
        alpha.focusInput();
        alpha.text = "85";
        probe.forceActiveFocus();
        compare(App.windowAlphaOverride, 85);

        keyClick(Qt.Key_3);
        const threshold = testCase.childNamed(modal, "lowDiskThresholdField");
        threshold.focusInput();
        threshold.text = "7";
        probe.forceActiveFocus();
        compare(App.lowDiskThresholdGiB, 7);
        threshold.focusInput();
        threshold.text = "x";
        probe.forceActiveFocus();
        compare(App.lowDiskThresholdGiB, 2);
        compare(threshold.text, "2");

        App.baseSizeOverride = 0;
        App.windowAlphaOverride = 0;
    }

    // #51 D16: the number fields step with the wheel only while they have the caret, and the
    // value applies as a typed one does, when the caret leaves; base size steps from the size
    // it follows, and transparency stays in 50–100.
    function test_the_number_fields_step_with_the_wheel_only_with_the_caret() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const probe = createTemporaryObject(probeComponent, testCase);
        keyClick(Qt.Key_5);
        const base = testCase.childNamed(modal, "baseSizeField");
        const alpha = testCase.childNamed(modal, "windowAlphaField");
        waitForItemPolished(base.parent);
        compare(base.text, "");

        mouseWheel(base, base.width / 2, base.height / 2, 0, 120);
        compare(base.text, "", "the wheel changed a field without the caret");

        base.focusInput();
        mouseWheel(base, base.width / 2, base.height / 2, 0, 120);
        compare(base.text, String(Theme.font.baseSize + 1));
        compare(App.baseSizeOverride, 0, "applied before the caret left");
        const stepped = Theme.font.baseSize + 1;
        probe.forceActiveFocus();
        compare(App.baseSizeOverride, stepped);

        alpha.focusInput();
        alpha.text = "98";
        mouseWheel(alpha, alpha.width / 2, alpha.height / 2, 0, 600);
        compare(alpha.text, "100");
        probe.forceActiveFocus();
        compare(App.windowAlphaOverride, 100);

        App.baseSizeOverride = 0;
        App.windowAlphaOverride = 0;
    }

    // A click on the pane's empty space takes the caret out of the field, which applies it;
    // a click in the field leaves it there.
    function test_a_click_beside_a_number_field_applies_it() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_3);
        const threshold = testCase.childNamed(modal, "lowDiskThresholdField");
        waitForItemPolished(threshold.parent);
        threshold.focusInput();
        threshold.text = "9";
        mouseClick(threshold);
        verify(threshold.editing);
        compare(App.lowDiskThresholdGiB, 2);
        const scroller = testCase.childNamed(modal, "paneScroller");
        mouseClick(scroller, scroller.width / 2, scroller.height - 10);
        verify(!threshold.editing);
        compare(App.lowDiskThresholdGiB, 9);
        App.lowDiskThresholdGiB = 2;
    }

    // docs/33 D25 (#73): "In the bar" puts the widget in and out (#17) and sets the tray icon
    // mode, as one choice. The tests have no `omarchy` (tests/qml/CMakeLists.txt), so it starts
    // unavailable; pointed at `true`, "widget" links the widget into the tests' own config
    // folder.
    function test_in_the_bar_is_one_choice_for_the_tray_and_the_widget() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_5);
        compare(modal.paneIndex, 4);
        const mode = App.trayIconMode;
        App.trayIconMode = "auto";
        const row = testCase.childNamed(modal, "inTheBarRow");
        verify(row !== null);
        verify(row.description.indexOf("Both carry the same menu") >= 0, row.description);
        const widget = testCase.childNamed(modal, "inTheBar_widget");
        const both = testCase.childNamed(modal, "inTheBar_both");
        const trayChip = testCase.childNamed(modal, "inTheBar_tray");
        const none = testCase.childNamed(modal, "inTheBar_none");
        verify(widget !== null && both !== null && trayChip !== null && none !== null);
        verify(trayChip.selected);
        // docs/21: customize opens the widget's own settings, so it is on only while the widget
        // is in the bar.
        const customize = testCase.childNamed(modal, "barWidgetCustomize");
        verify(customize !== null);
        verify(!customize.enabled);

        // Without the shell the widget cannot be had.
        verify(!App.barWidget.available);
        verify(!widget.enabled);
        verify(!both.enabled);
        verify(none.enabled);
        mouseClick(none);
        compare(App.inTheBar, "none");
        compare(App.trayIconMode, "never");
        verify(none.selected);
        mouseClick(trayChip);
        compare(App.inTheBar, "tray");

        App.barWidget.setPrograms("true", "true");
        verify(App.barWidget.available);
        verify(widget.enabled);
        mouseClick(widget);
        tryVerify(() => !App.barWidget.busy, 5000);
        verify(App.barWidget.lastOk, App.barWidget.message);
        verify(App.barWidget.installed);
        compare(App.trayIconMode, "auto");
        // `true` writes no shell.json, so the shell never says it is in the bar: the choice
        // shows the shell's answer, not the click.
        compare(App.inTheBar, "tray");
        verify(!customize.enabled);

        App.barWidget.remove();
        tryVerify(() => !App.barWidget.busy, 5000);
        App.barWidget.setPrograms("omaseed-tests-have-no-omarchy", "omaseed-tests-have-no-omarchy");
        verify(!App.barWidget.available);
        App.trayIconMode = mode;
    }

    function test_the_about_pane_prints_the_version_and_the_install_line() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_7);
        compare(modal.paneIndex, 6);
        verify(App.versionText.length > 0);
        verify(App.qtVersionText.length > 0);
        // Batch-2 J8: the Qt version has its own row, so VERSION does not repeat it.
        verify(App.versionText.indexOf("Qt") < 0, App.versionText);
        verify(App.repoUrl.indexOf("http") === 0);
        // #17: the widget is part of the install; this line puts it in the bar from a terminal.
        compare(App.barWidgetCommand, "omaseed --install-bar-widget");
    }

    // Batch-2 J3: one height for every pane, the tallest one's, so j/k through the nav never
    // moves the modal; and never more than 85 % of the window.
    function test_the_modal_keeps_one_height_through_every_pane() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        const heights = [];
        const tops = [];
        for (let i = 0; i < 6; ++i) {
            keyClick(Qt.Key_1 + i);
            compare(modal.paneIndex, i);
            heights.push(modal.sheet.panel.height);
            tops.push(modal.sheet.panel.y);
        }
        for (let i = 1; i < 6; ++i) {
            compare(heights[i], heights[0], "pane " + i + " changed the modal's height");
            compare(tops[i], tops[0], "pane " + i + " moved the modal");
        }
        verify(heights[0] <= testCase.height * 0.85 + 1, heights[0] + " is over 85 % of the window");
    }

    function test_a_pane_taller_than_the_window_allows_scrolls_under_a_bar() {
        const host = createTemporaryObject(shortHostComponent, testCase);
        const modal = createTemporaryObject(modalComponent, host);
        modal.open();
        verify(modal.sheet.panel.height <= host.height * 0.85 + 1);

        keyClick(Qt.Key_5); // Appearance, the tallest pane
        compare(modal.paneIndex, 4);
        const scroller = testCase.childNamed(modal, "paneScroller");
        const bar = testCase.childNamed(modal, "settingsScrollBar");
        verify(scroller !== null && bar !== null);
        verify(bar.needed, "Appearance fits a 420 px window; the test needs a taller pane");
        compare(scroller.contentY, 0);

        // Walking the rows down scrolls the pane so the cursor row stays in view.
        keyClick(Qt.Key_Tab);
        for (let i = 0; i < 10; ++i)
            keyClick(Qt.Key_J);
        compare(modal.paneCursor, 10);
        tryVerify(() => scroller.contentY > 0, 1000, "the last row was left below the fold");

        // Another pane starts at its top.
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_1);
        compare(scroller.contentY, 0);

        // The wheel moves the pane as it moves the library (Kit/WheelScroll; `mouseWheel`
        // does not deliver offscreen, so through the one function its wheel calls), but not
        // over Keybindings, whose list scrolls itself.
        keyClick(Qt.Key_5);
        const wheel = testCase.childNamed(scroller, "settingsPaneWheel");
        verify(wheel !== null && wheel.enabled);
        compare(wheel.flickable, scroller);
        wheel.duration = 0;
        wheel.scrollBy(wheel.step);
        verify(scroller.contentY > 0);
        keyClick(Qt.Key_4);
        verify(!wheel.enabled);
        compare(testCase.childNamed(modal, "settingsNavWheel").flickable,
                testCase.childNamed(modal, "settingsNav"));
    }

    // docs/15 Q3 A: below 700 px the nav folds into tabs across the top, the rows take the
    // sheet's whole width, and h/l or [/] move between the sections.
    function test_a_narrow_window_folds_the_nav_into_tabs() {
        const host = createTemporaryObject(narrowHostComponent, testCase);
        const modal = createTemporaryObject(modalComponent, host);
        modal.open();
        verify(modal.folded);
        const nav = testCase.childNamed(modal, "settingsNav");
        const tabs = testCase.childNamed(modal, "settingsTabs");
        verify(!nav.visible);
        verify(tabs.visible);
        compare(modal.area, "pane");

        // Seven sections (Backup, #31) are wider than the sheet at the floor: the strip slides
        // so the current tab stays in view, as the qBittorrent group's nine do (docs/16 §2).
        compare(tabs._shift, 0);
        keyClick(Qt.Key_7);
        compare(modal.paneIndex, 6);
        verify(tabs._shift > 0, "the About tab is out of view");
        keyClick(Qt.Key_1);
        compare(modal.paneIndex, 0);
        compare(tabs._shift, 0);
        // The pane starts where the tabs do: no nav beside it.
        const scroller = testCase.childNamed(modal, "paneScroller");
        compare(scroller.mapToItem(modal, 0, 0).x, tabs.mapToItem(modal, 0, 0).x);

        keyClick(Qt.Key_L);
        compare(modal.paneIndex, 1);
        compare(tabs.currentIndex, 1);
        keyClick(Qt.Key_BracketRight);
        compare(modal.paneIndex, 2);
        keyClick(Qt.Key_BracketLeft);
        compare(modal.paneIndex, 1);
        keyClick(Qt.Key_H);
        keyClick(Qt.Key_H);
        compare(modal.paneIndex, modal.paneCount - 1); // wraps, as j/k do in the nav
        keyClick(Qt.Key_L);
        compare(modal.paneIndex, 0);
        keyClick(Qt.Key_5);
        compare(modal.paneIndex, 4);
        compare(tabs.currentIndex, 4);
        // j/k still walk the pane's rows, and tab has no nav to go to.
        keyClick(Qt.Key_J);
        compare(modal.paneCursor, 1);
        keyClick(Qt.Key_Tab);
        compare(modal.area, "pane");

        tabs.activated(0);
        compare(modal.paneIndex, 0);

        // The wheel over the strip moves a section a notch, so the clipped ones are in reach
        // of the mouse too.
        verify(tabs.wheelSteps);
        tabs._wheel(0, -120);
        compare(modal.paneIndex, 1);
        tabs._wheel(0, 120);
        compare(modal.paneIndex, 0);

        // At the fold and above, the nav is back.
        host.width = Theme.space(600);
        verify(!modal.folded);
        verify(nav.visible);
        verify(!tabs.visible);
    }

    // A title cut off by a narrow row shows in full in the kit's tooltip; one that fits has
    // none to show. Hover does not reach an offscreen window, so `wanted` is read instead.
    function test_a_cut_off_row_title_has_its_tooltip() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "width": Theme.space(400)
                                          });
        const title = testCase.childNamed(row, "settingsRowTitle");
        const tip = testCase.childNamed(testCase.Window.window.contentItem, "settingsRowTip");
        verify(title !== null && tip !== null);
        compare(tip.parent, testCase.Window.window.contentItem);
        verify(!title.truncated);
        row.title = "A title much too long to fit beside its switch in a narrow settings row";
        row.width = Theme.space(200);
        waitForItemPolished(title.parent);
        verify(title.truncated);
        tip.showFor(title, title.text, false);
        tryCompare(tip, "shown", true);
        compare(tip.text, row.title);
        tip.hide();
    }

    // docs/15 Q3 A: a row under about 380 px (base 14) moves its control under the text,
    // lined up with the text's left edge; wider, the control stays at the row's end.
    function test_a_narrow_row_stacks_its_control_under_the_text() {
        const row = createTemporaryObject(rowComponent, testCase, {
                                              "width": Theme.space(400)
                                          });
        const control = testCase.childNamed(row, "settingsRowControl");
        const text = testCase.childNamed(row, "settingsRowText");
        verify(!row.stacked);
        waitForItemPolished(control.parent);
        const wideAt = control.mapToItem(row, 0, 0);
        verify(wideAt.x + control.width > row.width - Theme.space(12) - 1,
               "the control left the row's end");
        verify(wideAt.x > text.mapToItem(row, 0, 0).x + text.width - 1);
        const wideHeight = row.implicitHeight;

        row.width = Theme.space(325);
        verify(row.stacked);
        waitForItemPolished(control.parent);
        const at = control.mapToItem(row, 0, 0);
        const textAt = text.mapToItem(row, 0, 0);
        compare(at.x, textAt.x);
        verify(at.y >= textAt.y + text.height, "the control is not under the text");
        verify(row.implicitHeight > wideHeight);
    }
}
