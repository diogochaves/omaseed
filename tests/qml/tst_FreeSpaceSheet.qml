import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Board 44 §2 F2 (DECISIONS C17 D3): the free-the-space sheet's words for what it deletes and
// for a hybrid's files that stay, a v2-only torrent's in its own words (C29, #182), and the
// add-again confirm after it, for one file and for several. DECISIONS C30 F13–F15: the sheet
// titled "Only adding it again frees this" with the re-add as its main button when nothing else
// frees anything, the re-add beside "free SIZE" when some can go, and the confirm's one line
// why. The plan is App.planFreeSkipped()'s; here the test hands the sheet its own.
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

    function actionTexts() {
        return sheet.actions.map(action => action.text);
    }

    SignalSpy {
        id: readdSpy

        signalName: "addAgainRequested"
        target: sheet
    }

    // One hybrid file with downloaded pieces: "1 file stays", "pieces of it"; C30 F14: only
    // adding it again frees this, the re-add the main button.
    function test_one_hybrid_file_stays() {
        openWith({
                     "leftWithPieces": 1,
                     "leftText": "9.78 MB",
                     "allText": "9.78 MB"
                 });
        compare(sheet.title, "Only adding it again frees this");
        const note = findChild(sheet, "freeSpaceStaysNote");
        verify(note.visible);
        compare(note.text, "1 file stays (9.78 MB) because this torrent is hybrid and pieces of it were downloaded: "
                + "deleting it safely needs a recheck, and a recheck there hits a libtorrent bug.");
        compare(actionTexts(), ["cancel", "show in folder", "Re-add to delete it…"]);
        compare(sheet.actions[2].primary, true);
        compare(sheet.actions[2].disabled, false);
        verify(findChild(sheet, "freeSpaceReaddNote").visible);
        verify(findChild(sheet, "freeSpaceReaddNote").text.startsWith("It is removed keeping its files, the file is deleted,"));

        // The main button opens the confirm, with the size and the version for its line why.
        readdSpy.clear();
        sheet.accepted();
        compare(readdSpy.count, 1);
        compare(Array.from(readdSpy.signalArguments[0]), ["feed", "Field Recordings", 1, "9.78 MB", "hybrid"]);
    }

    function test_several_hybrid_files_stay() {
        openWith({
                     "leftWithPieces": 2,
                     "leftText": "16.8 MB"
                 });
        compare(findChild(sheet, "freeSpaceStaysNote").text,
                "2 files stay (16.8 MB) because this torrent is hybrid and pieces of them were downloaded: "
                + "deleting them safely needs a recheck, and a recheck there hits a libtorrent bug.");
        compare(actionTexts(), ["cancel", "show in folder", "Re-add to delete them…"]);
        compare(findChild(sheet, "freeSpaceReaddNote").text.indexOf("the 2 files are deleted") > 0, true);
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
        const note = findChild(sheet, "freeSpaceStaysNote");
        verify(note.visible);
        compare(note.text, "1 file stays (512 kB) because this torrent is v2 and pieces of it were downloaded: "
                + "deleting it leaves the torrent short of 100 % for good, a libtorrent bug.");
        compare(sheet.title, "Only adding it again frees this");
        compare(actionTexts(), ["cancel", "show in folder", "Re-add to delete it…"]);
    }

    function test_several_v2_files_stay() {
        openWith({
                     "hybrid": false,
                     "version": "v2",
                     "deletes": 1,
                     "freedText": "3.00 MB",
                     "leftWithPieces": 3,
                     "leftText": "24.1 MB",
                     "allText": "27.1 MB"
                 });
        compare(sheet.title, "Free 3.00 MB");
        compare(findChild(sheet, "freeSpaceStaysNote").text,
                "3 files stay (24.1 MB) because this torrent is v2 and pieces of them were downloaded: "
                + "deleting them leaves the torrent short of 100 % for good, a libtorrent bug.");
        // C30 F15: the main button frees what it can; the re-add of all of it sits beside it.
        compare(actionTexts(), ["cancel", "show in folder", "re-add to free all 27.1 MB…", "free 3.00 MB"]);
        compare(sheet.actions[3].primary, true);
        verify(!findChild(sheet, "freeSpaceReaddNote").visible);
        readdSpy.clear();
        sheet.actionTriggered("re-add to free all 27.1 MB…");
        compare(readdSpy.count, 1);
        compare(Array.from(readdSpy.signalArguments[0]), ["feed", "Field Recordings", 4, "27.1 MB", "v2"]);
        openWith({
                     "hybrid": false,
                     "version": "v2",
                     "deletes": 1,
                     "freedText": "3.00 MB",
                     "leftWithPieces": 3,
                     "leftText": "24.1 MB",
                     "allText": "27.1 MB"
                 });
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
        compare(actionTexts(), ["cancel", "show in folder", "free 16.8 MB"]);
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

    // C30 F13: straight from shift+f, the confirm says why in one line and names the size; its
    // "why?" opens the longer words in place.
    function test_the_confirm_says_why_in_one_line() {
        confirm.openWhy("feed", "Prairie Sky", 2, "1.03 GB", "hybrid");
        verify(confirm.subtitle.startsWith("It is removed from qBittorrent keeping its files, 2 skipped files are deleted (1.03 GB),"),
               confirm.subtitle);
        const line = findChild(confirm, "addAgainWhyLine");
        verify(line.visible);
        compare(line.text, "Nothing else frees them: this torrent is hybrid, and pieces of them were downloaded.");
        const more = findChild(confirm, "addAgainWhyText");
        verify(!more.visible);
        findChild(confirm, "addAgainWhy").Accessible.pressAction();
        verify(more.visible);
        verify(more.text.indexOf("a recheck on a hybrid hits a libtorrent bug") > 0);
        confirm.close();

        confirm.openWhy("feed", "Prairie Sky", 1, "512 kB", "v2");
        compare(findChild(confirm, "addAgainWhyLine").text,
                "Nothing else frees it: this torrent is v2, and pieces of it were downloaded.");
        verify(!findChild(confirm, "addAgainWhyText").visible, "why stays open from the last time");
        confirm.close();

        // From the error block (board 28): no line why.
        confirm.openFor("feed", "Prairie Sky", 0);
        verify(!findChild(confirm, "addAgainWhyLine").visible);
        verify(!findChild(confirm, "addAgainWhy").visible);
    }
}
