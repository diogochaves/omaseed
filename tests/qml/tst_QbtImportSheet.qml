pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// `Views/QbtImportSheet.qml` (#32, docs/24 §5): the preview of a qBittorrent settings import.
// Only differences are rows, in the qBittorrent group's own words and units; categories and
// "tags · RSS" are added rows; machine-specific keys sit under their own head, unticked; the
// count on the button follows the ticks (the folders row only says how); "export current
// first" leaves the sheet open; nothing to change says so and offers nothing to apply.
TestCase {
    id: testCase

    height: 900
    name: "QbtImportSheet"
    visible: true
    when: windowShown
    width: 1100

    QbtSettingsSections {
        id: qbtSchema
    }

    Component {
        id: sheetComponent

        QbtImportSheet {
            anchors.fill: parent
            schema: qbtSchema
        }
    }

    function info() {
        return {
            "ok": true,
            "title": "Import settings into home",
            "sub": "From qbittorrent-home-2026-10-03.json (qBittorrent 5.1.2). 7 settings differ from what home has now; 209 are the same and are not listed.",
            "rows": [
                { "key": "max_uploads", "kind": "pref", "group": "", "now": 20, "from": 40, "checked": true },
                { "key": "dont_count_slow_torrents", "kind": "pref", "group": "", "now": false, "from": true, "checked": true },
                { "key": "dl_limit", "kind": "pref", "group": "", "now": 0, "from": 102400, "checked": true },
                { "key": "proxy_type", "kind": "pref", "group": "", "now": "None", "from": "SOCKS5", "checked": true },
                { "key": "a_key_only_qbittorrent_knows", "kind": "pref", "group": "", "now": "", "from": "x", "checked": true },
                { "key": "@categories", "kind": "categories", "label": "categories", "group": "", "now": "2", "from": "+ films, audio", "checked": true },
                { "key": "@tagsRss", "kind": "tagsRss", "label": "tags · RSS", "group": "", "now": "3 · none", "from": "+ 4k · + 2 feeds, 5 rules", "checked": true },
                { "key": "save_path", "kind": "pref", "group": "folders", "now": "/home/d/Torrents", "from": "/data/torrents", "checked": false },
                { "key": "listen_port", "kind": "pref", "group": "network", "now": 6881, "from": 51413, "checked": false },
                { "key": "@folders", "kind": "folders", "label": "category and RSS rule folders", "group": "folders", "now": "under home's save path", "from": "as in the file", "checked": false }
            ],
            "notes": ["2 settings this qBittorrent doesn't have are left out: a, b."],
            "secrets": "passwords were never exported: proxy, e-mail and DynDNS logins and the Web UI API key stay as they are",
            "changeCount": 7
        };
    }

    function row(sheet, key) {
        return findChild(sheet, "importRow-" + key);
    }

    function cell(sheet, key, name) {
        return findChild(row(sheet, key), name).text;
    }

    function test_only_differences_in_the_groups_own_words() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        verify(sheet.opened);
        compare(sheet.title, "Import settings into home");
        compare(cell(sheet, "max_uploads", "importLabel"), "upload slots");
        compare(cell(sheet, "max_uploads", "importNow"), "20");
        compare(cell(sheet, "max_uploads", "importFrom"), "40");
        compare(cell(sheet, "dont_count_slow_torrents", "importLabel"), "don't count slow torrents");
        compare(cell(sheet, "dont_count_slow_torrents", "importNow"), "off");
        compare(cell(sheet, "dont_count_slow_torrents", "importFrom"), "on");
        // A speed in the row's own unit; no limit is the row's ∞.
        compare(cell(sheet, "dl_limit", "importLabel"), "download limit");
        compare(cell(sheet, "dl_limit", "importNow"), "∞");
        compare(cell(sheet, "dl_limit", "importFrom"), "100 KB/s");
        // A choice's word.
        compare(cell(sheet, "proxy_type", "importNow"), "none");
        compare(cell(sheet, "proxy_type", "importFrom"), "SOCKS5");
        // A key no row draws: its name, in plain letters.
        compare(cell(sheet, "a_key_only_qbittorrent_knows", "importLabel"), "a key only qbittorrent knows");
        compare(cell(sheet, "a_key_only_qbittorrent_knows", "importNow"), "none");
        compare(cell(sheet, "@categories", "importFrom"), "+ films, audio");
        compare(cell(sheet, "@tagsRss", "importLabel"), "tags · RSS");
        compare(cell(sheet, "listen_port", "importLabel"), "listening port");
        compare(cell(sheet, "save_path", "importLabel"), "default save path");
        verify(findChild(sheet, "importMachineHead").visible);
        verify(findChild(sheet, "importSecrets").text.indexOf("DynDNS") >= 0);
        compare(findChild(sheet, "importNote").text, "2 settings this qBittorrent doesn't have are left out: a, b.");
        verify(!findChild(sheet, "importNothing").visible);
    }

    // The machine-specific rows come after the head, and start unticked.
    function test_machine_rows_sit_under_their_head_unticked() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        compare(sheet.mainRows.map(r => r.key), ["max_uploads", "dont_count_slow_torrents", "dl_limit",
                                                 "proxy_type", "a_key_only_qbittorrent_knows",
                                                 "@categories", "@tagsRss"]);
        compare(sheet.machineRows.map(r => r.key), ["save_path", "listen_port", "@folders"]);
        const head = findChild(sheet, "importMachineHead");
        tryVerify(() => row(sheet, "save_path").mapToItem(sheet, 0, 0).y > head.mapToItem(sheet, 0, 0).y);
        verify(head.mapToItem(sheet, 0, 0).y > row(sheet, "@tagsRss").mapToItem(sheet, 0, 0).y);
        verify(!row(sheet, "listen_port").ticked);
        verify(!row(sheet, "@folders").ticked);
        verify(row(sheet, "max_uploads").ticked);
    }

    function test_the_button_counts_the_ticked_changes() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        compare(sheet.count, 7);
        compare(sheet.applyText, "apply 7 changes");
        sheet.toggle("listen_port");
        compare(sheet.count, 8);
        sheet.toggle("@folders"); // says how, changes nothing by itself
        compare(sheet.count, 8);
        sheet.toggle("max_uploads");
        sheet.toggle("@categories");
        compare(sheet.count, 6);
        const picked = sheet.choice();
        compare(picked.keys, ["dont_count_slow_torrents", "dl_limit", "proxy_type",
                              "a_key_only_qbittorrent_knows", "listen_port"]);
        compare(picked.categories, false);
        compare(picked.tagsRss, true);
        compare(picked.folders, true);
    }

    function test_space_ticks_the_focused_row() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        const port = row(sheet, "listen_port");
        port.forceActiveFocus();
        verify(port.activeFocus);
        keyClick(Qt.Key_Space);
        verify(port.ticked);
        keyClick(Qt.Key_Space);
        verify(!port.ticked);
    }

    function test_export_current_first_keeps_the_sheet() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const asked = createTemporaryObject(spyComponent, testCase, {
                                                "target": sheet,
                                                "signalName": "exportRequested"
                                            });
        sheet.openFor(info());
        sheet.actionTriggered(sheet.exportText);
        compare(asked.count, 1);
        verify(sheet.opened);
        sheet.note = "Exported to ~/qbittorrent-home-2026-10-15.json.";
        verify(findChild(sheet, "importExportNote").visible);
    }

    function test_apply_closes_and_hands_over() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const applied = createTemporaryObject(spyComponent, testCase, {
                                                  "target": sheet,
                                                  "signalName": "applied"
                                              });
        sheet.openFor(info());
        sheet.actionTriggered(sheet.applyText);
        compare(applied.count, 1);
        verify(!sheet.opened);
    }

    function test_nothing_to_change_offers_nothing() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const empty = info();
        empty.rows = [];
        empty.notes = [];
        sheet.openFor(empty);
        verify(findChild(sheet, "importNothing").visible);
        verify(!findChild(sheet, "importMachineHead").visible);
        compare(sheet.count, 0);
        verify(sheet.actions[2].disabled);
        sheet.actionTriggered(sheet.applyText);
        verify(sheet.opened);
    }

    // docs/34 D2 a, D6 a: App's map names both servers; without those keys the sheet keeps
    // its own words (an older map, the tests above).
    function test_the_heads_name_both_servers() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        compare(findChild(sheet, "importNowHead").text, "now");
        compare(findChild(sheet, "importFromHead").text, "from the file");
        compare(sheet.exportText, "export current first");
        sheet.close();

        const copied = info();
        copied.copy = true;
        copied.title = "Copy settings from private into home";
        copied.nowHead = "home now";
        copied.fromHead = "private";
        copied.exportText = "export home first";
        sheet.openFor(copied);
        compare(sheet.title, "Copy settings from private into home");
        compare(findChild(sheet, "importNowHead").text, "home now");
        compare(findChild(sheet, "importFromHead").text, "private");
        compare(sheet.exportText, "export home first");
        compare(sheet.actions[1].text, "export home first");
        compare(sheet.glyph, "⇣");
        // Its line names a server, a date, a file and the counts: three lines before it cuts.
        compare(sheet.subtitleLines, 3);
        const exported = createTemporaryObject(spyComponent, testCase, {
                                                   "target": sheet,
                                                   "signalName": "exportRequested"
                                               });
        sheet.actionTriggered("export home first");
        compare(exported.count, 1);
        verify(sheet.opened);
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }
}
