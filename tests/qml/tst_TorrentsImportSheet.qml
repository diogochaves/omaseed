pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// `Views/TorrentsImportSheet.qml` (#33, docs/24 §5): the preview of a torrents import. The plan
// as the mock's table (new, skipped, links, unfinished); "from → to" rows prefilled from a path
// map, edited in place, "+ another" for more, and the "!" line following them; D6 a's "then"
// choice; the "+" line for categories and tags; the button counts what is sent, and nothing to
// send offers nothing.
TestCase {
    id: testCase

    height: 900
    name: "TorrentsImportSheet"
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: sheetComponent

        TorrentsImportSheet {
            anchors.fill: parent
            // App's count, without a file read: rows covering "/data/torrents" leave none.
            notesFor: list => {
                const active = list.filter(r => r.from.length > 0 && r.to.length > 0);
                if (active.length === 0)
                    return "save paths are kept as they are in the file";
                return active.some(r => r.from === "/data/torrents") ? ""
                                                                     : "21 save paths don't start with " + active[0].from + " and are kept as they are";
            }
        }
    }

    function info() {
        return {
            "ok": true,
            "title": "Import 312 torrents into home",
            "sub": "From torrents-home-2026-10-03.omaseed-torrents (qBittorrent 5.1.2).",
            "plan": [
                { "key": "new", "label": "new to home", "verb": "added", "count": 296, "sizeText": "2.3 TB", "dim": false },
                { "key": "present", "label": "already in home (same hash)", "verb": "skipped", "count": 13, "sizeText": "96 GB", "dim": true },
                { "key": "magnets", "label": "magnets without metadata", "verb": "added as links", "count": 3, "sizeText": "—", "dim": false }
            ],
            "paths": [{ "from": "/data/torrents", "to": "/mnt/media/torrents", "source": "home" }],
            "created": "4 categories and 7 tags they use are created first",
            "importCount": 299,
            "check": true
        };
    }

    function cell(sheet, key, name) {
        return findChild(findChild(sheet, "planRow-" + key), name).text;
    }

    function test_the_plan_as_the_mock_has_it() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        verify(sheet.opened);
        compare(sheet.title, "Import 312 torrents into home");
        compare(cell(sheet, "new", "planLabel"), "new to home");
        compare(cell(sheet, "new", "planVerb"), "added");
        compare(cell(sheet, "new", "planCount"), "296");
        compare(cell(sheet, "new", "planSize"), "2.3 TB");
        compare(cell(sheet, "present", "planVerb"), "skipped");
        compare(cell(sheet, "magnets", "planVerb"), "added as links");
        compare(cell(sheet, "magnets", "planSize"), "—");
        compare(findChild(sheet, "importCreated").text, "4 categories and 7 tags they use are created first");
        compare(sheet.importText, "import 299");
        verify(!sheet.actions[1].disabled);
    }

    // D7 a: the prefilled row says where it came from; an edit is the sheet's at once, and the
    // "!" line follows it.
    function test_path_rows_follow_edits() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        const from = sheet.fieldAt(0, "from");
        const to = sheet.fieldAt(0, "to");
        compare(from.text, "/data/torrents");
        compare(to.text, "/mnt/media/torrents");
        compare(findChild(sheet, "pathSource").text, "from home's path map");
        verify(!findChild(sheet, "pathsNote").visible);
        from.text = "/data";
        compare(sheet.rows[0].from, "/data");
        compare(findChild(sheet, "pathsNote").text, "21 save paths don't start with /data and are kept as they are");
        tryVerify(() => findChild(sheet, "pathsNote").visible);
        // #79: the note is text a screen reader hears.
        compare(findChild(sheet, "pathsNote").Accessible.role, Accessible.StaticText);
        compare(findChild(sheet, "pathsNote").Accessible.name, findChild(sheet, "pathsNote").text);
        to.text = "";
        compare(findChild(sheet, "pathsNote").text, "save paths are kept as they are in the file");
        // Half a row sends nothing.
        compare(sheet.choice().rows, []);
    }

    // Opened again (the same file once more), the rows are the new read's, not what was typed.
    function test_opened_again_shows_the_new_rows() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const first = info();
        first.paths = [{ "from": "/data/torrents", "to": "", "source": "" }];
        sheet.openFor(first);
        sheet.fieldAt(0, "to").text = "/mnt/typed";
        compare(sheet.rows[0].to, "/mnt/typed");
        sheet.close();
        sheet.openFor(first);
        compare(sheet.rows[0].to, "");
        tryCompare(sheet.fieldAt(0, "to"), "text", "");
    }

    function test_another_row_is_added_and_focused() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        sheet.addRow();
        compare(sheet.rows.length, 2);
        tryVerify(() => sheet.fieldAt(1, "from") !== null && sheet.fieldAt(1, "from").editing);
        // The first row kept what it had.
        compare(sheet.fieldAt(0, "to").text, "/mnt/media/torrents");
        sheet.fieldAt(1, "from").text = "/data/music";
        sheet.fieldAt(1, "to").text = "/srv/music";
        compare(sheet.choice().rows, [{ "from": "/data/torrents", "to": "/mnt/media/torrents" },
                                      { "from": "/data/music", "to": "/srv/music" }]);
    }

    // D6 a: checking and starting the complete ones is the default; the other chip leaves all
    // of them stopped.
    function test_then_picks_one() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openFor(info());
        verify(findChild(sheet, "thenCheck").selected);
        verify(!findChild(sheet, "thenStopped").selected);
        const stopped = findChild(sheet, "thenStopped");
        stopped.forceActiveFocus();
        keyClick(Qt.Key_Space);
        verify(stopped.selected);
        verify(!findChild(sheet, "thenCheck").selected);
        compare(sheet.choice().check, false);
    }

    function test_import_closes_and_hands_over() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const applied = createTemporaryObject(spyComponent, testCase, {
                                                  "target": sheet,
                                                  "signalName": "applied"
                                              });
        sheet.openFor(info());
        sheet.actionTriggered(sheet.importText);
        compare(applied.count, 1);
        verify(!sheet.opened);
    }

    // Everything already there: nothing to import, and the button says so.
    function test_nothing_to_import_offers_nothing() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        const all = info();
        all.plan[0].count = 0;
        all.importCount = 0;
        all.created = "";
        sheet.openFor(all);
        compare(sheet.importText, "nothing to import");
        verify(sheet.actions[1].disabled);
        verify(!findChild(sheet, "importCreated").visible);
        sheet.actionTriggered(sheet.importText);
        verify(sheet.opened);
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }
}
