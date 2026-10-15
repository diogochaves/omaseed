import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// #11 (docs/18): the rename-many-files sheet on the demo's Big Buck Bunny. The plan comes from
// App.planFileRenames(), so this checks what the sheet does with it: the title, the button's
// words and whether it runs, `ctrl+e`, the preview's rows, a name already on disk (#64), and
// the run.
TestCase {
    id: testCase

    height: 800
    name: "RenameFilesSheet"
    visible: true
    when: windowShown
    width: 1200

    property string hash: ""

    RenameFilesSheet {
        id: sheet

        anchors.fill: parent
    }

    SignalSpy {
        id: renamedSpy

        signalName: "torrentFilesRenamed"
        target: App
    }

    function initTestCase() {
        App.loadDemo(16);
        for (let i = 0; i < App.filtered.count; ++i) {
            const hash = App.filtered.hashAt(i);
            if (App.torrentRow(hash).name === "Big Buck Bunny 4K (CC-BY)")
                testCase.hash = hash;
        }
        verify(testCase.hash.length > 0);
    }

    function init() {
        App.selectTorrent(testCase.hash);
        tryVerify(() => App.details.files.filePaths().length >= 2);
        sheet.findField.text = "";
        sheet.replaceField.text = "";
        sheet.regex = false;
        sheet.includeExtension = false;
        sheet.everyToggle.checked = false;
        sheet.caseToggle.checked = false;
        renamedSpy.clear();
    }

    function cleanup() {
        App.setDemoDiskFolder("");
        sheet.close();
    }

    function test_it_opens_on_find_with_nothing_to_run() {
        const paths = App.details.files.filePaths();
        sheet.openFor(testCase.hash, paths, true);
        compare(sheet.title, "Rename " + paths.length + " files");
        verify(sheet.subtitle.endsWith("· the selected files"));
        compare(sheet.primaryText, "nothing changes");
        compare(sheet.primaryButton().enabled, false);
        verify(sheet.findField.editing, "the keyboard is not in find");
        compare(sheet.preview.count, paths.length);
    }

    function test_ctrl_e_switches_to_regex_and_the_preview_follows() {
        const paths = App.details.files.filePaths();
        sheet.openFor(testCase.hash, paths, false);
        verify(sheet.subtitle.endsWith("· the shown files"));
        keyClick(Qt.Key_E, Qt.ControlModifier);
        compare(sheet.regex, true);
        sheet.findField.text = "^";
        sheet.replaceField.text = "{nn} ";
        compare(sheet.plan.changed, paths.length);
        compare(sheet.primaryText, "rename " + paths.length);
        compare(sheet.primaryButton().enabled, true);
        // The first row's new name carries the counter, tinted.
        waitForRendering(sheet.preview, 500);
        const first = findChild(sheet.preview.itemAtIndex(0), "newName");
        verify(first.text.indexOf("01 ") >= 0, first.text);
        verify(first.text.indexOf("<font") >= 0, "the replaced part is not tinted");
        keyClick(Qt.Key_E, Qt.ControlModifier);
        compare(sheet.regex, false);
    }

    function test_collisions_and_a_bad_regex_block_the_button() {
        const paths = App.details.files.filePaths();
        sheet.openFor(testCase.hash, paths, true);
        sheet.regex = true;
        sheet.includeExtension = true;
        sheet.findField.text = ".+";
        sheet.replaceField.text = "same";
        compare(sheet.plan.collisions, paths.length);
        compare(sheet.primaryText, paths.length + " names collide");
        compare(sheet.primaryButton().enabled, false);
        keyClick(Qt.Key_Return);
        compare(renamedSpy.count, 0);
        compare(sheet.opened, true);

        sheet.findField.text = "(";
        verify(sheet.plan.error.length > 0);
        compare(sheet.primaryText, "fix the regex");
        verify(findChild(sheet, "regexError").visible);
        compare(sheet.preview.visible, false);
    }

    // #64: a new name that a file the torrent does not own already has on disk is marked and
    // refused like a clash with the torrent's own files. The demo has no disk, so the test
    // gives it one (App.setDemoDiskFolder) holding a stray subtitle beside the torrent's.
    function test_a_name_already_on_disk_is_marked_and_refused() {
        const folder = TestFiles.folder(); // qmllint disable unqualified
        verify(TestFiles.write("Big Buck Bunny 4K/subs/film.en.srt", "keep me")); // qmllint disable unqualified
        App.setDemoDiskFolder(folder);
        const paths = ["Big Buck Bunny 4K/subs/bbb.en.srt", "Big Buck Bunny 4K/subs/bbb.pt.srt"];
        sheet.openFor(testCase.hash, paths, true);
        sheet.findField.text = "bbb";
        sheet.replaceField.text = "film";
        compare(sheet.plan.changed, 1);
        compare(sheet.plan.collisions, 1);
        compare(sheet.plan.rows[0].status, "collides");
        compare(sheet.primaryText, "1 names collide");
        compare(sheet.primaryButton().enabled, false);
        waitForRendering(sheet.preview, 500);
        const marked = findChild(sheet.preview.itemAtIndex(0), "newName");
        compare(marked.text, "film.en.srt · collides");
        verify(Qt.colorEqual(marked.color, Theme.red), "the row is not marked");
        const free = findChild(sheet.preview.itemAtIndex(1), "newName");
        verify(!Qt.colorEqual(free.color, Theme.red));
        sheet.findField.focusInput();
        keyClick(Qt.Key_Return);
        compare(renamedSpy.count, 0);
        compare(sheet.opened, true);

        // Another name is free: the row clears and enter runs.
        sheet.replaceField.text = "movie";
        compare(sheet.plan.collisions, 0);
        compare(sheet.primaryText, "rename 2");
        // Without a disk to read, the stray is not seen: as before #64.
        App.setDemoDiskFolder("");
        sheet.replaceField.text = "film";
        compare(sheet.plan.collisions, 0);
    }

    function test_enter_renames_and_closes() {
        const paths = App.details.files.filePaths();
        sheet.openFor(testCase.hash, paths, true);
        sheet.regex = true;
        sheet.findField.text = "^";
        sheet.replaceField.text = "z-";
        sheet.findField.focusInput();
        App.details.files.selectAllFiles();
        keyClick(Qt.Key_Return);
        compare(renamedSpy.count, 1);
        compare(App.details.files.selectedCount, 0, "a run from the selection leaves it selected");
        compare(renamedSpy.signalArguments[0][1], paths.length);
        compare(sheet.opened, false);
        const renamed = App.details.files.filePaths();
        for (const path of renamed)
            verify(path.slice(path.lastIndexOf("/") + 1).startsWith("z-"), path);

        // The runner shares one App: put the demo's names back for the other tests.
        sheet.openFor(testCase.hash, renamed, true);
        sheet.findField.text = "^z-";
        sheet.replaceField.text = "";
        keyClick(Qt.Key_Return);
        compare(App.details.files.filePaths(), paths);
    }
}
