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
        compare(row.status, "default"); // the only server is the default one
        compare(row.failing, false);

        const addRow = testCase.childNamed(modal, "serverRowAdd");
        verify(addRow !== null);
        compare(addRow.title, "add a server");
        compare(addRow.status, "a");
        verify(addRow.dashed);

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

    // The other half of Q23: a save without a test carries nothing, and the row says so.
    function test_saving_an_untested_server_leaves_the_row_untested() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        const sheet = modal.serverSheet;
        sheet.openNew();
        sheet.editName = "playground";
        sheet.editUrl = "http://127.0.0.1:8081";
        sheet.actionTriggered("save");
        compare(App.serverList.count, 1);
        const map = App.serverList.serverMap(App.serverList.idAt(0));
        compare(map.versionText, "");
        compare(map.torrentCount, -1);
        compare(map.reachableText, "");
        const row = testCase.childNamed(modal, "serverRow0");
        compare(row.description, "http://127.0.0.1:8081 · none");
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

        // View mode: Fig. 9's five key/value rows, in the figure's order.
        const kv = sheet.body.children[0];
        compare(kv.items.length, 5);
        compare(kv.items.map(item => item.key), ["Auth", "Key", "Path map", "Torrents", "Reachable"]);
        compare(kv.items[0].value, "API key · stored in libsecret");
        // The stored secret is never drawn: the Key row is a fixed mask of bullets.
        verify(kv.items[1].value.length > 0);
        compare(kv.items[1].value.replace(/•/g, ""), "");
        compare(kv.items[2].value, "/downloads → /data/torrents");
        compare(kv.items[3].value, "not tested yet");

        // Fig. 9's three actions in view mode, and three different ones once editing.
        compare(sheet.actions.map(action => action.text), ["test", "remove", "edit"]);
        sheet.actionTriggered("edit");
        compare(sheet.editing, true);
        compare(sheet.actions.map(action => action.text), ["cancel", "test", "save"]);
        compare(sheet.fieldMap().url, "http://127.0.0.1:8081");
        compare(sheet.fieldMap().authMode, "apiKey");
        compare(sheet.fieldMap().pathMapRemote, "/downloads");
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
        sheet.testing = true;
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

        // Notifications: three toggle rows (Q16 dropped the tracker one) bound to
        // App.notificationsEnabled, then the low-disk threshold field. The keyboard is
        // already in the pane — a digit moves the nav without taking it back.
        keyClick(Qt.Key_3);
        compare(modal.paneRowCount, 4);
        compare(modal.area, "pane");
        keyClick(Qt.Key_Return);
        compare(App.notificationsEnabled["completed"] === true, !completed);

        // Appearance: the tray-icon row cycles the three chips.
        keyClick(Qt.Key_5);
        compare(modal.paneRowCount, 11);
        const tray = App.trayIconMode;
        modal.paneCursor = 3;
        modal.area = "pane";
        modal.activateRow();
        verify(App.trayIconMode !== tray);

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

        // `j` x3 from the first row lands on the field's row; `enter` hands it the keyboard.
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        compare(modal.paneCursor, 3);
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

    // #17: the bar widget row. The tests have no `omarchy` (tests/qml/CMakeLists.txt), so it
    // starts unavailable; pointed at `true`, the switch links the widget into the tests' own
    // config folder and takes it out again.
    function test_the_bar_widget_row_puts_the_widget_in_and_out() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_5);
        compare(modal.paneIndex, 4);
        const toggle = testCase.childNamed(modal, "barWidgetToggle");
        verify(toggle !== null);
        // docs/21: customize opens the widget's own settings, so it shows only while the
        // widget is in the bar.
        const customize = testCase.childNamed(modal, "barWidgetCustomize");
        verify(customize !== null);
        verify(!customize.visible);
        verify(!App.barWidget.available);
        verify(!toggle.enabled);
        modal.paneCursor = 4;
        modal.area = "pane";
        modal.activateRow(); // unavailable: nothing happens
        verify(!App.barWidget.busy);

        App.barWidget.setPrograms("true", "true");
        verify(App.barWidget.available);
        verify(toggle.enabled);
        modal.activateRow();
        tryVerify(() => !App.barWidget.busy, 5000);
        verify(App.barWidget.lastOk, App.barWidget.message);
        verify(App.barWidget.installed);
        // `true` writes no shell.json, so the shell never says it is in the bar: the switch
        // shows the shell's answer, not the click.
        verify(!toggle.checked);
        verify(!customize.visible);
        const row = testCase.childNamed(modal, "barWidgetRow");
        verify(row.description.indexOf("the gear in its popup") >= 0, row.description);

        App.barWidget.remove();
        tryVerify(() => !App.barWidget.busy, 5000);
        verify(!App.barWidget.installed);
        App.barWidget.setPrograms("omaseed-tests-have-no-omarchy", "omaseed-tests-have-no-omarchy");
        verify(!App.barWidget.available);
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

        // At the fold and above, the nav is back.
        host.width = Theme.space(600);
        verify(!modal.folded);
        verify(nav.visible);
        verify(!tabs.visible);
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
