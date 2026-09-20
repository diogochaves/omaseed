import QtQuick
import QtTest
import Omaseed
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

    function test_the_nav_has_the_six_rows_of_the_figure() {
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(modal.navRows.length, 6);
        const texts = modal.navRows.map(row => row.text);
        compare(texts, ["General", "Servers", "Notifications", "Keybindings", "Appearance", "About"]);
        for (let i = 0; i < 6; ++i)
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

        // j and k walk the nav and wrap.
        keyClick(Qt.Key_J);
        compare(modal.paneIndex, 2);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        compare(modal.paneIndex, 5);

        keyClick(Qt.Key_6);
        compare(modal.paneIndex, 5);
        keyClick(Qt.Key_1);
        compare(modal.paneIndex, 0);
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

    function test_the_servers_pane_prints_url_auth_and_count() {
        const id = testCase.addTestServer("playground", "http://127.0.0.1:8081");
        verify(id.length > 0);
        App.serverList.setProbeResult(id, true, 3, 4, "");

        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        modal.showPane(1);
        // One server plus the "add a server" row.
        compare(modal.paneRowCount, 2);

        const row = testCase.childNamed(modal, "serverRow0");
        verify(row !== null);
        compare(row.title, "playground");
        compare(row.description, "http://127.0.0.1:8081 · API key · 3 torrents");
        compare(row.status, "default"); // the only server is the default one
        compare(row.failing, false);

        const addRow = testCase.childNamed(modal, "serverRowAdd");
        verify(addRow !== null);
        compare(addRow.title, "add a server");
        compare(addRow.status, "a");
        verify(addRow.dashed);

        // A failed test turns the row red and says "retrying".
        App.serverList.setProbeResult(id, false, -1, 12, "connection refused");
        compare(row.failing, true);
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

        // Notifications: three rows (Q16 dropped the tracker one), bound to
        // App.notificationsEnabled. The keyboard is already in the pane — a digit moves the
        // nav without taking it back.
        keyClick(Qt.Key_3);
        compare(modal.paneRowCount, 3);
        compare(modal.area, "pane");
        keyClick(Qt.Key_Return);
        compare(App.notificationsEnabled["completed"] === true, !completed);

        // Appearance: the tray-icon row cycles the three chips.
        keyClick(Qt.Key_5);
        compare(modal.paneRowCount, 4);
        const tray = App.trayIconMode;
        modal.paneCursor = 3;
        modal.area = "pane";
        modal.activateRow();
        verify(App.trayIconMode !== tray);

        // Put everything back: App persists these to state.json.
        App.showServersGroup = servers;
        App.pathMapEnabled = mapping;
        App.setNotificationEnabled("completed", completed);
        App.trayIconMode = tray;
    }

    // T4.8: T4.5 could not wire this row, because T4.3 (which owns the flag) had not landed.
    function test_the_general_pane_remembers_delete_files_by_default() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        compare(modal.paneIndex, 0);
        compare(modal.paneRowCount, 4);

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
    }

    function test_the_about_pane_prints_the_version_and_the_install_line() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_6);
        compare(modal.paneIndex, 5);
        verify(App.versionText.length > 0);
        verify(App.qtVersionText.length > 0);
        verify(App.repoUrl.indexOf("http") === 0);
        verify(App.pluginAddCommand.indexOf("omarchy plugin add ") === 0);
    }
}
