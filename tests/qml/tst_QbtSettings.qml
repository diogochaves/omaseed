import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// docs/16: the Settings modal's qBittorrent group, against the demo server's preferences
// (qBittorrent 5.2.3's defaults; App.serverPrefs changes them in memory, at once). The nav's
// second group, the digits inside it, a switch, a field in KB/s, a value refused before it is
// sent, the confirm in front of changes that can cut you off, the list sheet, Advanced's
// defaults and the Web UI section that always asks.
TestCase {
    id: testCase

    name: "QbtSettings"

    height: 900
    visible: true
    when: windowShown
    width: 1300

    Component {
        id: modalComponent

        SettingsModal {
            anchors.fill: parent
        }
    }

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

    function init() {
        App.loadDemo(16);
    }

    /// The modal, open on the qBittorrent section `index` (0 Speed … 8 Advanced), the
    /// keyboard in the pane.
    function openOn(index) {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        keyClick(Qt.Key_1 + index);
        compare(modal.paneIndex, modal.qbtStart + index);
        keyClick(Qt.Key_Tab);
        compare(modal.area, "pane");
        return modal;
    }

    function test_the_nav_has_a_qbittorrent_group_of_nine() {
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(modal.qbtSections.length, 9);
        compare(modal.qbtSections.map(section => section.id), ["speed", "discovery", "connection",
                                                               "proxy", "seeding", "queue",
                                                               "downloads", "webui", "advanced"]);
        for (let i = 0; i < 9; ++i)
            verify(testCase.childNamed(modal, "qbtNavRow" + i) !== null);
        compare(testCase.childNamed(modal, "qbtNavHead").text, "qbittorrent");
        const server = testCase.childNamed(modal, "qbtNavServer");
        verify(server.text.indexOf("home") === 0, server.text);
    }

    function test_ctrl_shift_comma_opens_on_the_group_and_digits_stay_in_it() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        verify(modal.opened);
        compare(modal.paneIndex, modal.qbtStart);
        verify(modal.inQbt);
        keyClick(Qt.Key_9);
        compare(modal.paneIndex, modal.qbtStart + 8);
        compare(modal.qbtSection.id, "advanced");
        keyClick(Qt.Key_2);
        compare(modal.qbtSection.id, "discovery");
        // j from the last omaseed row walks into the group, k back out of it.
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        compare(modal.paneIndex, modal.qbtStart - 1);
        keyClick(Qt.Key_1);
        compare(modal.paneIndex, 0);
    }

    // A long value reads from its start at rest, and editing starts at its end.
    function test_a_long_field_shows_its_start() {
        const modal = openOn(1);
        const field = testCase.childNamed(modal, "field-dht_bootstrap_nodes");
        verify(field.text.length > 40, field.text);
        compare(field.cursorPosition, 0);
        testCase.childNamed(modal, "qbtRow-dht_bootstrap_nodes").activate("enter");
        verify(field.editing);
        compare(field.cursorPosition, field.text.length);
    }

    function test_space_flips_a_switch_on_the_server() {
        const modal = openOn(1); // Discovery and privacy
        compare(App.serverPrefs.values.dht, true);
        compare(modal.paneCursor, 0); // DHT: the note above it is not a row
        keyClick(Qt.Key_Space);
        compare(App.serverPrefs.values.dht, false);
        // Its sub-row dims while it is off.
        verify(!testCase.childNamed(modal, "qbtRow-dht_bootstrap_nodes").enabled);
    }

    function test_anonymous_mode_asks_on_the_way_in_only() {
        const modal = openOn(1);
        const confirm = testCase.childNamed(modal, "qbtConfirm");
        const row = testCase.childNamed(modal, "qbtRow-anonymous_mode");
        row.activate("space");
        verify(confirm.opened);
        compare(App.serverPrefs.values.anonymous_mode, false);
        confirm.confirm();
        compare(App.serverPrefs.values.anonymous_mode, true);
        row.activate("space");
        verify(!confirm.opened);
        compare(App.serverPrefs.values.anonymous_mode, false);
    }

    function test_a_speed_field_writes_bytes_per_second() {
        const modal = openOn(0); // Speed
        const field = testCase.childNamed(modal, "field-dl_limit");
        compare(field.text, ""); // 0: no limit
        keyClick(Qt.Key_Return); // the cursor row's field takes the caret
        verify(field.editing);
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_0);
        keyClick(Qt.Key_0);
        keyClick(Qt.Key_Return);
        compare(App.serverPrefs.values.dl_limit, 100 * 1024);
        verify(!field.editing);
        // The alt limit shows the server's 10240 as 10.
        compare(testCase.childNamed(modal, "field-alt_dl_limit").text, "10");
    }

    function test_a_value_out_of_range_never_leaves_the_row() {
        const modal = openOn(2); // Connection
        const field = testCase.childNamed(modal, "field-listen_port");
        compare(field.text, "51413");
        field.text = "70000";
        field.accepted();
        compare(App.serverPrefs.values.listen_port, 51413);
        compare(field.text, "51413");
        verify(modal.qbtNote.indexOf("65535") >= 0, modal.qbtNote);
    }

    function test_the_port_asks_first_and_cancel_puts_it_back() {
        const modal = openOn(2);
        const confirm = testCase.childNamed(modal, "qbtConfirm");
        const field = testCase.childNamed(modal, "field-listen_port");
        field.text = "60000";
        field.accepted();
        verify(confirm.opened);
        verify(confirm.message.indexOf("60000") >= 0, confirm.message);
        compare(App.serverPrefs.values.listen_port, 51413);
        confirm.cancel();
        compare(App.serverPrefs.values.listen_port, 51413);
        compare(field.text, "51413");

        field.text = "60000";
        field.accepted();
        confirm.confirm();
        compare(App.serverPrefs.values.listen_port, 60000);
    }

    function test_the_list_sheet_writes_one_entry_per_line() {
        const modal = openOn(2);
        const row = testCase.childNamed(modal, "qbtRow-banned_IPs");
        row.activate("enter");
        const sheet = testCase.childNamed(modal, "qbtListSheet");
        verify(sheet.opened);
        compare(sheet.editor.text, "");
        sheet.editor.text = "203.0.113.5\n\n  198.51.100.7 ";
        sheet.triggerPrimary();
        verify(!sheet.opened);
        compare(App.serverPrefs.values.banned_IPs, "203.0.113.5\n198.51.100.7");
    }

    function test_watch_folders_keep_the_object_shape() {
        const modal = openOn(6); // Downloads and files
        const row = testCase.childNamed(modal, "qbtRow-scan_dirs");
        row.activate("enter");
        const sheet = testCase.childNamed(modal, "qbtListSheet");
        sheet.editor.text = "/srv/watch\n/srv/other";
        sheet.triggerPrimary();
        compare(JSON.stringify(App.serverPrefs.values.scan_dirs), JSON.stringify({
                                                                                    "/srv/other": 0,
                                                                                    "/srv/watch": 0
                                                                                }));
    }

    function test_an_advanced_row_goes_back_to_its_default() {
        const modal = openOn(8); // Advanced
        const row = testCase.childNamed(modal, "qbtRow-disk_cache");
        const reset = testCase.childNamed(row, "defaultButton");
        verify(!reset.visible);
        App.serverPrefs.apply({
                                  "disk_cache": 64
                              });
        compare(testCase.childNamed(modal, "field-disk_cache").text, "64");
        verify(reset.visible);
        row.resetDefaults();
        compare(App.serverPrefs.values.disk_cache, -1);
        verify(!reset.visible);
    }

    function test_every_web_ui_change_asks_first() {
        const modal = openOn(7); // Web UI
        // The password is write-only: not in app/preferences, and its row shows all the same.
        verify(App.serverPrefs.values.web_ui_password === undefined);
        verify(testCase.childNamed(modal, "qbtRow-web_ui_password") !== null);
        const confirm = testCase.childNamed(modal, "qbtConfirm");
        testCase.childNamed(modal, "qbtRow-web_ui_upnp").activate("space");
        verify(confirm.opened);
        confirm.cancel();
        compare(App.serverPrefs.values.web_ui_upnp, false);
    }

    function test_a_choice_row_cycles_with_enter() {
        const modal = openOn(1);
        const row = testCase.childNamed(modal, "qbtRow-encryption");
        compare(App.serverPrefs.values.encryption, 0);
        row.activate("enter");
        compare(App.serverPrefs.values.encryption, 1);
    }

    function test_the_proxy_rows_dim_until_a_proxy_is_chosen() {
        const modal = openOn(3); // Proxy
        verify(!testCase.childNamed(modal, "qbtRow-proxy_ip").enabled);
        // Two conditions: a proxy, and BitTorrent through it (which is on).
        compare(App.serverPrefs.values.proxy_bittorrent, true);
        verify(!testCase.childNamed(modal, "qbtRow-proxy_peer_connections").enabled);
        testCase.childNamed(modal, "qbtRow-proxy_type").activate("enter");
        verify(testCase.childNamed(modal, "qbtRow-proxy_peer_connections").enabled);
        compare(App.serverPrefs.values.proxy_type, "SOCKS4");
        verify(testCase.childNamed(modal, "qbtRow-proxy_ip").enabled);
    }

    // docs/16 §2: folded, a switch between the groups, and the section tabs follow it; the
    // current tab stays in view even when nine of them are wider than the sheet.
    function test_folded_tabs_follow_the_group() {
        const host = createTemporaryObject(narrowHost, testCase);
        const modal = createTemporaryObject(modalComponent, host);
        modal.openQbt();
        verify(modal.folded);
        const groups = testCase.childNamed(modal, "settingsGroupTabs");
        const sections = testCase.childNamed(modal, "settingsTabs");
        compare(groups.currentIndex, 1);
        compare(sections.model.length, 9);
        keyClick(Qt.Key_9);
        compare(sections.currentIndex, 8);
        verify(sections._shift > 0, "the Advanced tab is out of view");
        keyClick(Qt.Key_L); // wraps to omaseed's General
        compare(modal.paneIndex, 0);
        compare(groups.currentIndex, 0);
        compare(sections.model.length, 6);
        keyClick(Qt.Key_H);
        compare(groups.currentIndex, 1);
        groups.activated(0);
        compare(modal.paneIndex, 0);
    }

    Component {
        id: narrowHost

        Item {
            height: 800
            width: Theme.space(480)
        }
    }
}
