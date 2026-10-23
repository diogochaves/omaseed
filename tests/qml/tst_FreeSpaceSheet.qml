import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Board 44 §2 F2 (DECISIONS C17 D3): the free-the-space sheet's words for what it deletes and
// for a hybrid's files that stay, a v2-only torrent's in its own words (C29, #182), and the
// add-again confirm after it, for one file and for several. The plan is App.planFreeSkipped()'s; here the test hands the sheet its own.
TestCase {
    id: testCase

    height: 800
    name: "FreeSpaceSheet"
    visible: true
    when: windowShown
    width: 1200

    FreeSpaceSheet {
        id: sheet

        anchors.fill: parent
    }

    AddAgainConfirm {
        id: confirm

        anchors.fill: parent
    }

    function cleanup() {
        sheet.close();
        confirm.close();
    }

    /// The sheet open with PLAN (its hash is the sheet's own).
    function openWith(plan) {
        sheet.openFor("feed");
        const full = Object.assign({
                                       "hash": "feed",
                                       "name": "Field Recordings",
                                       "folder": "/home/sbx/Downloads/Field Recordings",
                                       "hybrid": true,
                                       "version": "hybrid",
                                       "running": true,
                                       "deletes": 0,
                                       "recheck": false,
                                       "leftWithPieces": 0,
                                       "items": []
                                   }, plan);
        sheet.plan = full;
        verify(sheet.ready);
    }

    function stepTexts() {
        const texts = [];
        for (let i = 0; i < 4; ++i) {
            const step = findChild(sheet, "freeSpaceStep-" + i);
            if (step)
                texts.push(step.modelData);
        }
        return texts;
    }

    // One hybrid file with downloaded pieces: "1 file stays", "pieces of it", "free it too".
    function test_one_hybrid_file_stays() {
        openWith({
                     "leftWithPieces": 1,
                     "leftText": "9.78 MB"
                 });
        compare(sheet.title, "Nothing to free");
        const note = findChild(sheet, "freeSpaceStaysNote");
        verify(note.visible);
        compare(note.text, "1 file stays (9.78 MB) because this torrent is hybrid and pieces of it were downloaded: "
                + "deleting it safely needs a recheck, and a recheck there hits a libtorrent bug.");
        compare(findChild(sheet, "freeSpaceAddAgain").text, "Free it too by adding the torrent again…");
    }

    function test_several_hybrid_files_stay() {
        openWith({
                     "leftWithPieces": 2,
                     "leftText": "16.8 MB"
                 });
        compare(findChild(sheet, "freeSpaceStaysNote").text,
                "2 files stay (16.8 MB) because this torrent is hybrid and pieces of them were downloaded: "
                + "deleting them safely needs a recheck, and a recheck there hits a libtorrent bug.");
        compare(findChild(sheet, "freeSpaceAddAgain").text, "Free them too by adding the torrent again…");
    }

    // C29 (#182): a v2-only torrent keeps its files with downloaded pieces, as a hybrid does,
    // and says why in its own words; adding it again frees them.
    function test_one_v2_file_stays() {
        openWith({
                     "hybrid": false,
                     "version": "v2",
                     "leftWithPieces": 1,
                     "leftText": "512 kB"
                 });
        compare(sheet.title, "Nothing to free");
        const note = findChild(sheet, "freeSpaceStaysNote");
        verify(note.visible);
        compare(note.text, "1 file stays (512 kB) because this torrent is v2 and pieces of it were downloaded: "
                + "deleting it leaves the torrent short of 100 % for good, a libtorrent bug.");
        const again = findChild(sheet, "freeSpaceAddAgain");
        verify(again.visible);
        compare(again.text, "Free it too by adding the torrent again…");
    }

    function test_several_v2_files_stay() {
        openWith({
                     "hybrid": false,
                     "version": "v2",
                     "deletes": 1,
                     "freedText": "3.00 MB",
                     "leftWithPieces": 3,
                     "leftText": "24.1 MB"
                 });
        compare(sheet.title, "Free 3.00 MB");
        compare(findChild(sheet, "freeSpaceStaysNote").text,
                "3 files stay (24.1 MB) because this torrent is v2 and pieces of them were downloaded: "
                + "deleting them leaves the torrent short of 100 % for good, a libtorrent bug.");
        compare(findChild(sheet, "freeSpaceAddAgain").text, "Free them too by adding the torrent again…");
        // What goes is only what holds no downloaded piece, and nothing is rechecked.
        const steps = stepTexts();
        compare(steps[1], "Delete 1 skipped file from the disk: one that holds no downloaded piece. "
                + "Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
        compare(steps[2], "Start it again. The wanted files carry on. No recheck.");
    }

    // A v2-only torrent whose skipped files hold no downloaded piece frees them, as on v1: no note.
    function test_a_v2_torrent_without_pieces_frees() {
        openWith({
                     "hybrid": false,
                     "version": "v2",
                     "deletes": 2,
                     "freedText": "16.8 MB"
                 });
        compare(sheet.title, "Free 16.8 MB");
        verify(!findChild(sheet, "freeSpaceStaysNote").visible);
        verify(!findChild(sheet, "freeSpaceAddAgain").visible);
    }

    // The delete step counts its files the same way, with or without the recheck.
    function test_the_delete_step_counts_one_or_several() {
        openWith({
                     "hybrid": false,
                     "version": "v1",
                     "deletes": 1,
                     "recheck": true,
                     "freedText": "3.00 MB"
                 });
        let steps = stepTexts();
        compare(steps.length, 3);
        compare(steps[1], "Delete 1 skipped file from the disk, one holding downloaded pieces. "
                + "Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
        sheet.plan = Object.assign({}, sheet.plan, {
                                       "recheck": false
                                   });
        steps = stepTexts();
        compare(steps[1], "Delete 1 skipped file from the disk: one that holds no downloaded piece. "
                + "Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
        sheet.plan = Object.assign({}, sheet.plan, {
                                       "deletes": 2
                                   });
        steps = stepTexts();
        compare(steps[1], "Delete 2 skipped files from the disk: those that hold no downloaded piece. "
                + "Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
        sheet.plan = Object.assign({}, sheet.plan, {
                                       "recheck": true
                                   });
        steps = stepTexts();
        compare(steps[1], "Delete 2 skipped files from the disk, some holding downloaded pieces. "
                + "Only skipped files, only inside this torrent's folder, and none another torrent also uses.");
    }

    function test_add_again_counts_one_or_several() {
        confirm.openFor("feed", "Field Recordings", 1);
        compare(confirm.subtitle, "It is removed from qBittorrent keeping its files, 1 skipped file is deleted, and it "
                + "comes back with the same files ticked, names, category, tags and limits. It loses its ratio, "
                + "seeding time and added date.");
        confirm.files = 3;
        verify(confirm.subtitle.startsWith("It is removed from qBittorrent keeping its files, 3 skipped files are deleted,"));
        confirm.files = 0;
        verify(confirm.subtitle.startsWith("It is removed from qBittorrent keeping its files, and comes back"));
    }
}
