pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// `Views/MoveLooseList.qml` (board 42 S2, DECISIONS C15 M9): the files no torrent owns in a
// folder, and afterwards the summary's record of what was carried and what stayed. Each row
// reads as one line in the accessibility tree (#201 F24), as the move sheet's rows do.
TestCase {
    id: testCase

    height: 900
    name: "MoveLooseList"
    visible: true
    when: windowShown
    width: 1100

    Component {
        id: sheetComponent

        MoveLooseList {
            anchors.fill: parent
        }
    }

    // As MoveController::carryRows() words them: what stayed first, then what was carried.
    function rows() {
        return [
            { "path": "extras/notes/readme.txt", "size": "2.0 KB", "what": "stays: changed after the move", "kind": "warn" },
            { "path": "extras/manual.pdf", "size": "18.0 KB", "what": "carried", "kind": "ok" }
        ];
    }

    function test_record_rows_have_accessible_names() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.record = true;
        sheet.folder = "/library/extras";
        sheet.rows = rows();
        sheet.open();
        verify(sheet.opened);
        compare(sheet.title, "Loose files of extras");
        const names = [];
        const walk = item => {
            for (const child of item.children) {
                if (child.objectName === "moveLoosePath")
                    names.push(child.Accessible.name);
                walk(child);
            }
        };
        walk(sheet);
        compare(names, ["extras/notes/readme.txt, 2.0 KB: stays: changed after the move",
                        "extras/manual.pdf, 18.0 KB: carried"]);
    }
}
