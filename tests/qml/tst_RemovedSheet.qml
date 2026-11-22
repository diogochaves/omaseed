pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// `Views/RemovedSheet.qml` (DECISIONS C50 GT6, board 67 §5, #295): what an agent removed, kept
// 30 days, each row added back paused where it was; one from another server listed with its
// button off until that server is on show (PLAN Q12); every button off while an export or
// import runs; the sheet closes once nothing is left.
TestCase {
    id: testCase

    height: 800
    name: "RemovedSheet"
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: sheetComponent

        RemovedSheet {
            anchors.fill: parent
        }
    }

    SignalSpy {
        id: addBackSpy

        signalName: "addBackRequested"
    }

    // As App::removedKept words them, newest first.
    function entries() {
        return [
            { "id": "20261121-9c40e7a2", "hash": "9c40e7a2", "name": "Night Market Field Recordings", "savePath": "/data/archive/field",
              "server": "home", "here": true, "removed": "today 15:20", "until": "21 Dec" },
            { "id": "20261120-5b17d3f0", "hash": "5b17d3f0", "name": "Harbour Bells at Dawn", "savePath": "/srv/attic/lib",
              "server": "attic", "here": false, "removed": "yesterday 09:41", "until": "20 Dec" }
        ];
    }

    function test_each_row_says_where_it_was_and_adds_back() {
        const sheet = createTemporaryObject(sheetComponent, testCase, { "entries": entries() });
        addBackSpy.target = sheet;
        addBackSpy.clear();
        sheet.open();
        verify(sheet.opened);
        compare(sheet.title, "Add back");
        compare(sheet.subtitle, "2 torrents an agent removed · each kept 30 days");
        const row = findChild(sheet, "removedRow-20261121-9c40e7a2");
        verify(row !== null);
        compare(findChild(row, "name").text, "Night Market Field Recordings");
        compare(findChild(row, "line").text, "was in /data/archive/field · removed today 15:20 · kept until 21 Dec");
        verify(!findChild(row, "elsewhere").visible);
        const button = findChild(row, "addBack");
        verify(button.enabled);
        compare(button.Accessible.name, "add Night Market Field Recordings back");
        button.clicked();
        compare(addBackSpy.count, 1);
        compare(addBackSpy.signalArguments[0][0], "20261121-9c40e7a2");
    }

    function test_another_servers_waits_for_it() {
        const sheet = createTemporaryObject(sheetComponent, testCase, { "entries": entries() });
        sheet.open();
        const row = findChild(sheet, "removedRow-20261120-5b17d3f0");
        const elsewhere = findChild(row, "elsewhere");
        verify(elsewhere.visible);
        compare(elsewhere.text, "on attic: switch to it first");
        verify(!findChild(row, "addBack").enabled);
    }

    function test_busy_turns_every_button_off() {
        const sheet = createTemporaryObject(sheetComponent, testCase, { "entries": entries() });
        sheet.open();
        sheet.busy = true;
        verify(!findChild(findChild(sheet, "removedRow-20261121-9c40e7a2"), "addBack").enabled);
        sheet.busy = false;
        verify(findChild(findChild(sheet, "removedRow-20261121-9c40e7a2"), "addBack").enabled);
    }

    SignalSpy {
        id: allSpy

        signalName: "addBackAllRequested"
    }

    // #305: two or more kept here: "add back all" adds back those, never another server's, and
    // leaves the sheet open; off while busy; with one kept here, no such button.
    function test_add_back_all_takes_only_the_server_on_show() {
        const kept = entries();
        kept.push({ "id": "20261119-1a2b3c4d", "hash": "1a2b3c4d", "name": "Lantern Festival", "savePath": "/data/archive/field",
                    "server": "home", "here": true, "removed": "2 days ago", "until": "19 Dec" });
        const sheet = createTemporaryObject(sheetComponent, testCase, { "entries": kept });
        allSpy.target = sheet;
        allSpy.clear();
        sheet.open();
        waitForRendering(sheet);
        const all = sheet.actionButtons().filter(button => button.text === "add back all");
        compare(all.length, 1);
        verify(all[0].enabled);
        mouseClick(all[0]);
        compare(allSpy.count, 1);
        compare(allSpy.signalArguments[0][0], ["20261121-9c40e7a2", "20261119-1a2b3c4d"]);
        verify(sheet.opened, "add back all closed the sheet");
        sheet.busy = true;
        verify(!all[0].enabled, "add back all on while an export or import runs");
        sheet.busy = false;
        sheet.entries = entries();
        compare(sheet.actionButtons().filter(button => button.text === "add back all").length, 0);
    }

    function test_it_closes_once_nothing_is_left() {
        const sheet = createTemporaryObject(sheetComponent, testCase, { "entries": entries() });
        sheet.open();
        sheet.entries = [entries()[0]];
        verify(sheet.opened);
        compare(sheet.subtitle, "1 torrent an agent removed · kept 30 days");
        sheet.entries = [];
        verify(!sheet.opened);
    }
}
