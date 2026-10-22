pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// `Views/MoveRecheckSheet.qml` (board 42 E3, DECISIONS C15 M16, C20, C21): the failed ones of a
// move, rechecked smallest first. A hybrid (v1+v2) is not rechecked (C21, #144): it is listed
// apart with why, and offered move again and move back; with only hybrids the button says there
// is nothing to recheck.
TestCase {
    id: testCase

    height: 900
    name: "MoveRecheckSheet"
    visible: true
    when: windowShown
    width: 1100

    readonly property string why: "Its files may be split between the two places, and on a hybrid torrent (v1 and v2) a recheck would put it in an error that only adding it again clears."

    Component {
        id: sheetComponent

        MoveRecheckSheet {
            anchors.fill: parent
        }
    }

    // As MoveController::recheckRows() words them, smallest first.
    function rows() {
        return [
            { "hash": "b2", "name": "Demo Disc", "bytes": "200 B", "starts": false, "hybrid": true, "then": "not rechecked", "why": testCase.why },
            { "hash": "a1", "name": "Open Arena", "bytes": "300 B", "starts": true, "hybrid": false, "then": "starts if complete", "why": "" },
            { "hash": "c3", "name": "Homebrew Pack", "bytes": "400 B", "starts": false, "hybrid": false, "then": "stays stopped", "why": "" }
        ];
    }

    function test_a_hybrid_is_listed_apart() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith(["b2", "a1", "c3"], rows());
        verify(sheet.opened);
        compare(sheet.title, "Recheck 2 torrents");
        compare(sheet.subtitle, "smallest first, one at a time");
        compare(sheet.actions[2].text, "recheck 2");
        verify(!sheet.actions[2].disabled);
        // The rechecked rows keep their line; the hybrid has none there.
        verify(findChild(sheet, "recheckRow-a1") !== null);
        compare(findChild(sheet, "recheckRow-a1").Accessible.name, "Open Arena, 300 B, starts if complete");
        verify(findChild(sheet, "recheckRow-b2") === null);
        const hybrid = findChild(sheet, "hybridRow-b2");
        verify(hybrid !== null);
        compare(findChild(hybrid, "why").text, testCase.why);
        const again = findChild(hybrid, "moveAgain");
        const back = findChild(hybrid, "moveBack");
        compare(again.text, "move again");
        compare(back.text, "move back");
        verify(again.visible && back.visible);
        // Screen readers: one line for the row, a name for each button.
        compare(again.Accessible.name, "move Demo Disc again");
        compare(back.Accessible.name, "move Demo Disc back");
        compare(again.Accessible.role, Accessible.Button);
        verify(!findChild(hybrid, "sent").visible);
        // enter still rechecks: the keyboard is on the primary, not on move again.
        verify(sheet.primaryButton().activeFocus);
        verify(!again.activeFocus);
    }

    function test_a_move_sent_says_so() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith(["b2", "a1", "c3"], rows());
        // The demo library has no job: nothing starts, and the row keeps its buttons.
        findChild(findChild(sheet, "hybridRow-b2"), "moveAgain").clicked();
        verify(findChild(findChild(sheet, "hybridRow-b2"), "moveAgain").visible);
        // Once one starts, the row says what was sent instead.
        sheet.sent = { "b2": "again" };
        const hybrid = findChild(sheet, "hybridRow-b2");
        tryVerify(() => findChild(hybrid, "sent").visible);
        compare(findChild(hybrid, "sent").text, "moving again");
        verify(!findChild(hybrid, "moveAgain").visible);
        verify(!findChild(hybrid, "moveBack").visible);
        sheet.sent = { "b2": "back" };
        compare(findChild(hybrid, "sent").text, "moving back");
        // The recheck is still there for the others.
        compare(sheet.actions[2].text, "recheck 2");
    }

    function test_only_hybrids_nothing_to_recheck() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith(["b2"], [rows()[0]]);
        compare(sheet.title, "Nothing to recheck");
        compare(sheet.subtitle, "move it again or back instead");
        compare(sheet.actions[2].text, "nothing to recheck");
        verify(sheet.actions[2].disabled);
        // enter runs nothing: the sheet stays.
        sheet.triggerPrimary();
        verify(sheet.opened);
        verify(findChild(sheet, "hybridRow-b2") !== null);
    }

    function test_no_hybrid_reads_as_before() {
        const sheet = createTemporaryObject(sheetComponent, testCase);
        sheet.openWith(["a1", "c3"], rows().slice(1));
        compare(sheet.title, "Recheck 2 torrents");
        compare(sheet.actions[2].text, "recheck 2");
        verify(findChild(sheet, "hybridRow-b2") === null);
    }
}
