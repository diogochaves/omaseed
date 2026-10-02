import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// #11 (docs/18): the rename-many-files sheet on the demo's Big Buck Bunny. The plan comes from
// App.planFileRenames(), so this checks what the sheet does with it: the title, the button's
// words and whether it runs, `ctrl+e`, the preview's rows, and the run.
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

    function test_enter_renames_and_closes() {
        const paths = App.details.files.filePaths();
        sheet.openFor(testCase.hash, paths, true);
        sheet.regex = true;
        sheet.findField.text = "^";
        sheet.replaceField.text = "z-";
        sheet.findField.focusInput();
        keyClick(Qt.Key_Return);
        compare(renamedSpy.count, 1);
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
