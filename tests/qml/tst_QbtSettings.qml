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

    function cleanup() {
        // The servers a test added: App is a singleton shared with every other test file.
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
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

    // Nine sections of settings, then Backup (#32, docs/24 §4 D1 a) closing the group.
    function test_the_nav_has_a_qbittorrent_group_of_ten() {
        const modal = createTemporaryObject(modalComponent, testCase);
        compare(modal.qbtSections.length, 10);
        compare(modal.qbtSections.map(section => section.id), ["speed", "discovery", "connection",
                                                               "proxy", "seeding", "queue",
                                                               "downloads", "webui", "advanced",
                                                               "backup"]);
        for (let i = 0; i < 10; ++i)
            verify(testCase.childNamed(modal, "qbtNavRow" + i) !== null);
        compare(testCase.childNamed(modal, "qbtNavHead").text, "qbittorrent");
        const server = testCase.childNamed(modal, "qbtNavServer");
        compare(server.currentName, "home");
        // One server: the plain label, nothing to pick (docs/34 D3 a).
        verify(!server.switches);
        verify(testCase.childNamed(server, "serverSelectPlain").text.indexOf("home") === 0);
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

    // #32, #33, docs/24 §4: the group's Backup pane, on the server the group is about: export
    // and import rows for its settings, then for its torrents, the server and its version in
    // the head, `0` to reach it.
    function test_the_backup_pane_closes_the_group() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        keyClick(Qt.Key_0);
        compare(modal.qbtSection.id, "backup");
        verify(modal.inQbtBackup);
        const pane = testCase.childNamed(modal, "qbtBackupPane");
        verify(pane.visible);
        verify(!testCase.childNamed(modal, "qbtPane").visible);
        const exportRow = testCase.childNamed(modal, "qbtExportRow");
        // docs/34 D2 a: every row names the server it acts on.
        compare(exportRow.title, "Export home's settings");
        compare(exportRow.description, App.qbtExportSummary);
        verify(exportRow.description.indexOf("Passwords are left out. Saved as qbittorrent-home-") > 0,
               exportRow.description);
        compare(exportRow.opacity, 1);
        compare(testCase.childNamed(modal, "qbtImportRow").title, "Import settings into home");
        const copyRow = testCase.childNamed(modal, "qbtCopyRow");
        compare(copyRow.title, "Copy settings from another server");
        // One server: nothing to copy from, and the row says how to get one.
        verify(copyRow.description.indexOf("Add one in Settings › Servers first.") > 0,
               copyRow.description);
        verify(copyRow.opacity < 1);
        verify(!testCase.childNamed(modal, "copySourceButton").visible);
        compare(testCase.childNamed(modal, "qbtHeadNote").text, "home · qBittorrent 5.2.3");
        const torrentsRow = testCase.childNamed(modal, "torrentsExportRow");
        compare(torrentsRow.title, "Export home's torrents");
        compare(torrentsRow.description, App.torrentsExportSummary);
        verify(/^\d+ torrents with their save paths, categories, tags and limits\. The .+ of files stay where they are\./
               .test(torrentsRow.description), torrentsRow.description);
        compare(testCase.childNamed(modal, "torrentsExportButton").text, "export…");
        compare(testCase.childNamed(modal, "torrentsImportRow").title, "Import torrents into home");
        compare(testCase.childNamed(modal, "torrentsImportButton").text, "import…");
        // D2 a's scope line, with no other server to point at.
        const scope = testCase.childNamed(modal, "qbtScopeLine");
        verify(scope.visible);
        compare(scope.text, "home's own settings and torrents, read from and sent to home only.");
        compare(scope.linkText, "");
        compare(modal.paneRowCount, 5);
        keyClick(Qt.Key_Tab);
        compare(modal.area, "pane");
        verify(exportRow.hasCursor);
        keyClick(Qt.Key_J);
        verify(testCase.childNamed(modal, "qbtImportRow").hasCursor);
        keyClick(Qt.Key_J);
        verify(copyRow.hasCursor);
        keyClick(Qt.Key_J);
        verify(torrentsRow.hasCursor);
        keyClick(Qt.Key_J);
        verify(testCase.childNamed(modal, "torrentsImportRow").hasCursor);
        keyClick(Qt.Key_J);
        verify(exportRow.hasCursor);
        // The footer: rows to act on, nothing to toggle or edit; one server, no `s`.
        verify(modal.sheet.hints.some(hint => hint.key === "enter" && hint.label === "act"));
        verify(!modal.sheet.hints.some(hint => hint.key === "space"));
        verify(!modal.sheet.hints.some(hint => hint.key === "s"));
    }

    /// A second (and third) server for the demo, in memory only.
    function addServer(name, port) {
        const id = App.serverList.add({
                                          "name": name,
                                          "url": "http://127.0.0.1:" + port,
                                          "authMode": "none"
                                      }, "");
        verify(id.length > 0);
        return id;
    }

    // docs/34 D1 a: omaseed's Backup says what it holds and where torrents are; `t` (or the
    // link) goes to qBittorrent's, the cursor on Export torrents.
    function test_omaseeds_backup_says_what_it_holds() {
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.open();
        keyClick(Qt.Key_6);
        compare(modal.paneIndex, 5);
        const scope = testCase.childNamed(modal, "backupScopeLine");
        verify(scope.visible);
        verify(scope.text.indexOf("omaseed's own:") === 0, scope.text);
        verify(scope.text.indexOf("Torrents aren't in it") > 0, scope.text);
        compare(scope.linkKey, "t");
        compare(scope.linkText, "qBittorrent › Backup ›");
        const head = testCase.childNamed(modal, "qbtHeadNote");
        verify(head.visible);
        compare(head.text, "omaseed · on this machine");
        verify(modal.sheet.hints.some(hint => hint.key === "t"
                                      && hint.label === "torrents: qBittorrent › Backup"));
        compare(modal.paneRowCount, 3);
        keyClick("t");
        verify(modal.inQbtBackup);
        compare(modal.area, "pane");
        compare(modal.paneCursor, 3);
        verify(testCase.childNamed(modal, "torrentsExportRow").hasCursor);
        // The link does the same.
        modal.showPane(5);
        scope.linkActivated();
        verify(modal.inQbtBackup);
        compare(modal.paneCursor, 3);
    }

    // docs/34 D3 a, D4 a, D5 a: with servers to switch to, the select is a nav stop between
    // About and Speed; enter opens its list; `s` in every qBittorrent pane (Backup's too) and
    // on the select goes straight to the next server, and the footer says so.
    function test_the_select_and_s() {
        const home = App.serverList.add({
                                            "name": "home",
                                            "url": "http://demo.example.org",
                                            "authMode": "none"
                                        }, "");
        App.serverList.setDefault(home);
        const second = testCase.addServer("private", 8091);
        testCase.addServer("seedbox", 8092);
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        const select = testCase.childNamed(modal, "qbtNavServer");
        verify(select.switches);
        verify(testCase.childNamed(select, "serverSelectBox").visible);
        compare(select.currentId, home);
        // Every footer of the group has `s`.
        verify(modal.sheet.hints.some(hint => hint.key === "s" && hint.label === "next server"));
        keyClick(Qt.Key_0);
        verify(modal.inQbtBackup);
        verify(modal.sheet.hints.some(hint => hint.key === "s" && hint.label === "next server"));
        compare(testCase.childNamed(modal, "qbtScopeLine").text,
                "home's own settings and torrents, read from and sent to home only. The other servers have their own:");

        // The nav: k from Speed lands on the select, which takes the cursor from the rows.
        keyClick(Qt.Key_1);
        compare(modal.area, "nav");
        keyClick(Qt.Key_K);
        verify(modal.navOnServer);
        verify(select.hasCursor);
        compare(modal.paneIndex, modal.qbtStart);
        verify(!testCase.childNamed(modal, "qbtNavRow0").hasCursor);
        verify(modal.sheet.hints.some(hint => hint.key === "enter" && hint.label === "servers"));
        keyClick(Qt.Key_K);
        verify(!modal.navOnServer);
        compare(modal.paneIndex, modal.qbtStart - 1);
        keyClick(Qt.Key_J);
        verify(modal.navOnServer);

        // enter: the list, three servers with their state, the cursor on the next one.
        keyClick(Qt.Key_Return);
        verify(select.listOpen);
        compare(select.list.items.map(item => item.text), ["home", "private", "seedbox"]);
        compare(select.list.items[0].note, "on it ✓");
        compare(select.list.cursorIndex, 1);
        keyClick(Qt.Key_Escape);
        verify(!select.listOpen);
        verify(modal.opened);
        compare(App.serverList.defaultId, home);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        verify(!select.listOpen);
        verify(App.serverList.defaultId !== home);
        verify(modal.sheet.footerRight.indexOf("omaseed is on seedbox now · the library too") === 0,
               modal.sheet.footerRight);

        // `s` from the select itself, then from a pane: one step each.
        keyClick("s");
        compare(App.serverList.defaultId, home);
        compare(modal.sheet.footerRight, "omaseed is on home now · the library too");
        keyClick(Qt.Key_0);
        keyClick("s");
        compare(App.serverList.defaultId, second);
        compare(modal.sheet.footerRight, "omaseed is on private now · the library too");
    }

    // docs/34 D6 a: copy settings from another server, on the demo's fiction of one: space
    // picks which, enter reads it and opens the import sheet, titled for the copy.
    function test_copy_settings_from_another_server() {
        const home = App.serverList.add({
                                            "name": "home",
                                            "url": "http://demo.example.org",
                                            "authMode": "none"
                                        }, "");
        App.serverList.setDefault(home);
        testCase.addServer("private", 8091);
        const seedbox = testCase.addServer("seedbox", 8092);
        const modal = createTemporaryObject(modalComponent, testCase);
        modal.openQbt();
        keyClick(Qt.Key_0);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        const copyRow = testCase.childNamed(modal, "qbtCopyRow");
        verify(copyRow.hasCursor);
        compare(copyRow.opacity, 1);
        verify(copyRow.description.indexOf("Read from that server just now") === 0, copyRow.description);
        const source = testCase.childNamed(modal, "copySourceButton");
        verify(source.visible);
        compare(modal.copySourceName, "private");
        // space: the other servers, under the button; pick seedbox.
        keyClick(Qt.Key_Space);
        const menu = testCase.childNamed(modal, "copyMenu");
        verify(menu.opened);
        compare(menu.items.map(item => item.text), ["private", "seedbox"]);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        compare(modal.copySource, seedbox);
        compare(modal.copySourceName, "seedbox");
        // enter: the sheet, the app still on home.
        keyClick(Qt.Key_Return);
        const sheet = testCase.childNamed(modal, "qbtImportSheet");
        tryVerify(() => sheet.opened, 5000);
        compare(sheet.title, "Copy settings from seedbox into home");
        verify(sheet.subtitle.indexOf("Read from seedbox just now") === 0, sheet.subtitle);
        verify(sheet.subtitle.indexOf("seedbox is not changed.") > 0, sheet.subtitle);
        compare(testCase.childNamed(sheet, "importNowHead").text, "home now");
        compare(testCase.childNamed(sheet, "importFromHead").text, "seedbox");
        compare(sheet.exportText, "export home first");
        compare(App.serverList.defaultId, home);
        sheet.close();
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

    // docs/31 §5 P1, P3: a line under the four limits says which way the set goes, and the rows
    // follow the word. The demo's set is qBittorrent's default turtle (10 KB/s, no normal
    // limits); someone who uses it that way keeps today's rows, named "turtle".
    function test_the_speed_page_names_the_set() {
        const modal = openOn(0);
        const setLine = testCase.childNamed(modal, "qbtLine-set");
        verify(setLine.visible);
        compare(testCase.childNamed(setLine, "qbtLineText").text,
                "Slower than your limits above, so omaseed calls this set turtle.");
        compare(testCase.childNamed(setLine, "qbtLineGlyph").text, "▼");
        const schedule = testCase.childNamed(modal, "qbtRow-scheduler_enabled");
        compare(schedule.title, "Schedule turtle");
        compare(testCase.childNamed(modal, "qbtRow-alt_dl_limit").description,
                "In force while turtle is on: A, the hero's chip, or the schedule below.");
        compare(testCase.childNamed(modal, "qbtRow-alt_dl_limit").glyph, Glyphs.altSpeed);

        // A daytime upload cap with the alternative set open: burst, as it is typed.
        App.serverPrefs.apply({
                                  "up_limit": 1200 * 1024,
                                  "alt_dl_limit": 0,
                                  "alt_up_limit": 0
                              });
        compare(testCase.childNamed(setLine, "qbtLineText").text,
                "Faster than your limits above, so omaseed calls this set burst.");
        compare(testCase.childNamed(setLine, "qbtLineGlyph").text, "▲");
        compare(schedule.title, "Schedule burst");
        compare(schedule.description,
                "qBittorrent switches to burst by itself between two times, and back after.");
        compare(testCase.childNamed(modal, "qbtRow-alt_up_limit").glyph, Glyphs.burst);

        // The same both ways: switching changes nothing; the words go back to alt-speed.
        App.serverPrefs.apply({
                                  "alt_up_limit": 1200 * 1024
                              });
        compare(testCase.childNamed(setLine, "qbtLineText").text,
                "The same as your limits above: switching changes nothing.");
        compare(schedule.title, "Schedule alt-speed");
    }

    // docs/31 D8, D9 (P1, P2): `n` fills the schedule with nights in one press, every key at
    // once, and the summary under Days says what qBittorrent will do, warning when the days
    // run backwards past midnight.
    function test_nights_and_the_summary_under_days() {
        const modal = openOn(0);
        const summary = testCase.childNamed(modal, "qbtLine-schedule");
        verify(summary.visible); // what it would do, dimmed while the schedule is off
        verify(summary.opacity < 1);
        // While the schedule is off its rows (and the preset) do nothing.
        keyClick(Qt.Key_N);
        compare(App.serverPrefs.values.schedule_from_hour, 8);
        testCase.childNamed(modal, "qbtRow-scheduler_enabled").activate("space");
        compare(App.serverPrefs.values.scheduler_enabled, true);
        compare(summary.opacity, 1);
        const preset = testCase.childNamed(testCase.childNamed(modal, "qbtRow-schedule_from_hour"),
                                           "presetChip");
        verify(preset.visible);
        compare(preset.text, "nights");
        compare(preset.keyText, "n");
        App.serverPrefs.apply({
                                  "scheduler_days": 2
                              });
        keyClick(Qt.Key_N);
        compare(App.serverPrefs.values.schedule_from_hour, 23);
        compare(App.serverPrefs.values.schedule_from_min, 0);
        compare(App.serverPrefs.values.schedule_to_hour, 7);
        compare(App.serverPrefs.values.schedule_to_min, 0);
        compare(App.serverPrefs.values.scheduler_days, 0);
        compare(testCase.childNamed(modal, "field-schedule_from_hour").text, "23:00");
        const text = testCase.childNamed(summary, "qbtLineText");
        verify(text.text.indexOf("Turtle every night from 23:00 to 07:00, by the server's clock.") === 0,
               text.text);
        compare(testCase.childNamed(summary, "qbtLineNote").text,
                "A switches it by hand. The schedule takes over at its next change.");
        compare(testCase.childNamed(summary, "qbtLineGlyph").text, "◷");

        // P2: weekends past midnight read backwards: the line says so, in yellow.
        App.serverPrefs.apply({
                                  "scheduler_days": 2
                              });
        compare(text.text, "Turtle all day Monday to Friday, and Saturday and Sunday nights.");
        compare(testCase.childNamed(summary, "qbtLineGlyph").text, "!");
        compare(testCase.childNamed(summary, "qbtLineGlyph").color, Theme.yellow);
        verify(testCase.childNamed(summary, "qbtLineNote").text.indexOf("the other way round") > 0);

        // P3: from and to the same never fires.
        App.serverPrefs.apply({
                                  "schedule_to_hour": 23
                              });
        compare(text.text, "Never on: from and to are the same time.");
    }

    // qBittorrent 5 ignores a share limit's switch sent alone while its value is ∞ (-1), and
    // turns random_port off only with a port to go back to (tst_qbtsettings_live).
    function test_a_switch_sends_what_qbittorrent_needs_with_it() {
        const modal = openOn(4); // Seeding
        compare(App.serverPrefs.values.max_ratio, -1);
        testCase.childNamed(modal, "qbtRow-max_ratio_enabled").activate("space");
        compare(App.serverPrefs.values.max_ratio_enabled, true);
        compare(App.serverPrefs.values.max_ratio, 1);
        testCase.childNamed(modal, "qbtRow-max_seeding_time_enabled").activate("space");
        compare(App.serverPrefs.values.max_seeding_time, 1440);
        // A value already there is kept.
        App.serverPrefs.apply({
                                  "max_inactive_seeding_time": 30,
                                  "max_inactive_seeding_time_enabled": false
                              });
        testCase.childNamed(modal, "qbtRow-max_inactive_seeding_time_enabled").activate("space");
        compare(App.serverPrefs.values.max_inactive_seeding_time_enabled, true);
        compare(App.serverPrefs.values.max_inactive_seeding_time, 30);

        const connection = openOn(2);
        const random = testCase.childNamed(connection, "qbtRow-random_port");
        random.activate("space");
        compare(App.serverPrefs.values.random_port, true);
        compare(App.serverPrefs.values.listen_port, 51413); // the demo keeps it; qBittorrent says 0
        App.serverPrefs.apply({
                                  "listen_port": 0
                              });
        random.activate("space");
        compare(App.serverPrefs.values.random_port, false);
        const port = App.serverPrefs.values.listen_port;
        verify(port >= 1024 && port <= 65535, port);
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

    // docs/32 E1 (#55): the demo's last .torrent copy into its export folder failed. The note
    // sits under the row while one of its two folders is that folder.
    function test_a_failed_export_shows_under_its_folder() {
        const modal = openOn(6); // Downloads and files
        const row = testCase.childNamed(modal, "qbtRow-export_dir");
        const note = testCase.childNamed(row, "settingsRowWarning");
        verify(note.visible);
        verify(row.warning.indexOf("The last copy failed: Read-only file system, writing "
                                   + "omarchy-4.0.4-x86_64.iso.torrent · ") === 0, row.warning);

        App.serverPrefs.apply({
                                  "export_dir": "/data/torrents/elsewhere"
                              });
        compare(row.warning, "");
        verify(!note.visible);
        // The finished-only folder set to it (with a trailing slash) shows it again.
        App.serverPrefs.apply({
                                  "export_dir_fin": "/data/torrents/.torrent-copies/"
                              });
        verify(row.warning.length > 0);
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

    // Enter in a field whose change asks first leaves the keyboard in the confirm: esc
    // cancels it, and Settings stays open (it used to close Settings and strand the confirm).
    function test_the_keyboard_stays_in_a_confirm_a_field_opened() {
        const modal = openOn(7); // Web UI: the Address, port row first
        const confirm = testCase.childNamed(modal, "qbtConfirm");
        const port = App.serverPrefs.values.web_ui_port;
        keyClick(Qt.Key_Return); // the address
        keyClick(Qt.Key_Return); // the port
        const field = testCase.childNamed(modal, "field-web_ui_port");
        verify(field.editing);
        field.text = "9999";
        keyClick(Qt.Key_Return);
        verify(confirm.opened);
        keyClick(Qt.Key_Escape);
        verify(!confirm.opened);
        verify(modal.opened);
        compare(App.serverPrefs.values.web_ui_port, port);
        compare(field.text, String(port));
    }

    // HTTPS with no certificate and key: qBittorrent stays on HTTP with a secure-only login
    // cookie, and every new login is shut out (tst_qbtsettings_live). The switch waits for both.
    function test_https_waits_for_a_certificate_and_key() {
        const modal = openOn(7); // Web UI
        const confirm = testCase.childNamed(modal, "qbtConfirm");
        const https = testCase.childNamed(modal, "qbtRow-use_https");
        // The paths can be set while HTTPS is still off.
        verify(testCase.childNamed(modal, "qbtRow-web_ui_https_cert_path").enabled);
        https.activate("space");
        verify(!confirm.opened);
        compare(App.serverPrefs.values.use_https, false);
        verify(modal.qbtNote.indexOf("certificate") >= 0, modal.qbtNote);

        App.serverPrefs.apply({
                                  "web_ui_https_cert_path": "/etc/qbt/cert.pem",
                                  "web_ui_https_key_path": "/etc/qbt/key.pem"
                              });
        https.activate("space");
        verify(confirm.opened);
        // Issue #2: it says what follows with a self-signed certificate.
        compare(confirm.message, "Turn on HTTPS for the Web UI?");
        verify(confirm.subtitle.indexOf("fingerprint to trust") >= 0, confirm.subtitle);
        confirm.confirm();
        compare(App.serverPrefs.values.use_https, true);
    }

    // #78: a row's switch is named by the row's title, a sibling text; a pick is named by
    // the setting, its value the description, and its ▾ in neither.
    function test_a_switch_and_a_pick_are_named_by_their_row() {
        let modal = openOn(1);
        const dhtRow = testCase.childNamed(modal, "qbtRow-dht");
        const toggle = testCase.childNamed(dhtRow, "rowToggle");
        verify(dhtRow.title.length > 0);
        compare(toggle.Accessible.name, dhtRow.title);
        modal.destroy();

        modal = openOn(0);
        const daysRow = testCase.childNamed(modal, "qbtRow-scheduler_days");
        const pick = testCase.childNamed(daysRow, "pickButton");
        compare(pick.Accessible.name, "Days");
        compare(pick.Accessible.description, pick.valueText);
        verify(pick.valueText.length > 0);
        verify(pick.valueText.indexOf("▾") < 0);
        compare(pick.text, pick.valueText + "  ▾");
    }

    function test_a_choice_row_cycles_with_enter() {
        const modal = openOn(1);
        const row = testCase.childNamed(modal, "qbtRow-encryption");
        compare(App.serverPrefs.values.encryption, 0);
        row.activate("enter");
        compare(App.serverPrefs.values.encryption, 1);
    }

    // #150: a row whose keys the server does not report is left out. The demo is a 5.2 server
    // (mail over SSL is a switch, .torrent copies have folders); qBittorrent 5.3 dropped those
    // keys and reports the mail's encryption type instead, a choice of three.
    function test_rows_follow_the_keys_the_server_has() {
        let modal = openOn(6); // Downloads and files
        verify(testCase.childNamed(modal, "qbtRow-export_dir") !== null);
        verify(testCase.childNamed(modal, "qbtRow-mail_notification_ssl_enabled") !== null);
        compare(testCase.childNamed(modal, "qbtRow-mail_notification_encryption_type"), null);
        modal.destroy();

        App.patchDemoPrefs({
                               "mail_notification_encryption_type": "SMTPS"
                           }, ["export_dir", "export_dir_fin", "mail_notification_ssl_enabled"]);
        modal = openOn(6);
        compare(testCase.childNamed(modal, "qbtRow-export_dir"), null);
        compare(testCase.childNamed(modal, "qbtRow-mail_notification_ssl_enabled"), null);
        const row = testCase.childNamed(modal, "qbtRow-mail_notification_encryption_type");
        verify(row !== null);
        compare(row.title, "Encryption");
        // Under "Send an email", dim while it is off, like the rest of the mail rows.
        verify(!row.enabled);
        App.serverPrefs.apply({
                                  "mail_notification_enabled": true
                              });
        verify(row.enabled);
        row.activate("enter");
        compare(App.serverPrefs.values.mail_notification_encryption_type, "STARTTLS");
        row.activate("enter");
        compare(App.serverPrefs.values.mail_notification_encryption_type, "None");
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
        compare(sections.model.length, 10);
        keyClick(Qt.Key_0);
        compare(sections.currentIndex, 9);
        verify(sections._shift > 0, "the Backup tab is out of view");
        keyClick(Qt.Key_L); // wraps to omaseed's General
        compare(modal.paneIndex, 0);
        compare(groups.currentIndex, 0);
        compare(sections.model.length, 7);
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
