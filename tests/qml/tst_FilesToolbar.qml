import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// Board 64 (DECISIONS C47 I4, I6): `Views/FilesToolbar.qml`, the file pane's filter, sort,
// tree · flat, fold all and hide skipped, on the demo `.torrent` loaded into App.addFiles.
TestCase {
    id: testCase

    height: 400
    name: "FilesToolbar"
    visible: true
    when: windowShown
    width: 800

    Component {
        id: toolbarComponent

        FilesToolbar {
            files: App.addFiles.tree
            scope: "add"
            width: 700
        }
    }

    Component {
        id: parsedSpy

        SignalSpy {
            signalName: "addableParsed"
            target: App
        }
    }

    Component {
        id: doneSpy

        SignalSpy {
            signalName: "filterDone"
        }
    }

    function initTestCase() {
        App.loadDemo(16);
        const spy = createTemporaryObject(parsedSpy, testCase);
        // A `.torrent` is read off the GUI thread: its files come with addableParsed.
        App.parseAddable(App.demoTorrentPath());
        spy.wait(5000);
        const token = spy.signalArguments[0][0].token;
        App.loadAddFiles(token);
        compare(App.addFiles.fileCount, 8);
    }

    function cleanup() {
        const files = App.addFiles.tree;
        files.clearFilters();
        files.flat = false;
        files.foldersFirst = true;
        files.sortKey = "";
        files.sortDescending = false;
        files.expandAll();
    }

    function make() {
        const toolbar = createTemporaryObject(toolbarComponent, testCase);
        waitForRendering(toolbar);
        return toolbar;
    }

    function test_slash_focuses_and_typing_filters() {
        const toolbar = testCase.make();
        const done = createTemporaryObject(doneSpy, testCase, {
                                               "target": toolbar
                                           });
        verify(toolbar.run("filterFiles"));
        verify(toolbar.filterField.editing);
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(App.addFiles.tree.filterText, "srt");
        compare(toolbar.filterField.trailing, "3 of 8 files");
        keyClick(Qt.Key_Return);
        compare(done.count, 1);
        // Cleared from elsewhere (× show all), the field follows.
        App.addFiles.tree.clearFilters();
        compare(toolbar.filterField.text, "");
        compare(toolbar.filterField.trailing, "");
    }

    function test_the_sort_menu() {
        const toolbar = testCase.make();
        const files = App.addFiles.tree;
        mouseClick(toolbar.sortButton);
        verify(toolbar.sortMenu !== null);
        const items = toolbar.sortMenu.items;
        compare(items.map(item => item.separator ? "—" : item.text), ["name", "size, biggest first",
                                                                      "kind", "—",
                                                                      "folders first"]);
        compare(items[4].checked, true, "folders first is on by default (I4)");
        compare(items[4].disabled, false);
        keyClick("d"); // kind
        tryCompare(toolbar, "sortMenu", null);
        compare(files.sortKey, "kind");
        compare(toolbar.sortButton.text.indexOf("sort: kind"), 0);

        toolbar.openSortMenu();
        keyClick("f");
        tryCompare(toolbar, "sortMenu", null);
        compare(files.foldersFirst, false);

        files.flat = true;
        toolbar.openSortMenu();
        compare(toolbar.sortMenu.items[4].disabled, true, "flat has no folders");
        keyClick(Qt.Key_Escape);
        tryCompare(toolbar, "sortMenu", null);
    }

    function test_tree_and_flat_flip_the_view() {
        const toolbar = testCase.make();
        const flat = findChild(toolbar, "filesToolbarFlat");
        const tree = findChild(toolbar, "filesToolbarTree");
        verify(tree.selected && !flat.selected);
        mouseClick(flat);
        // Through App.setFilesView("add", true): the add's tree is this model.
        compare(App.addFiles.tree.flat, true);
        verify(flat.selected && !tree.selected);
        verify(!findChild(toolbar, "filesToolbarFold").enabled, "nothing to fold in flat");
        verify(toolbar.run("treeFlat"));
        compare(App.addFiles.tree.flat, false);
    }

    function test_fold_all_and_unfold_all_by_state() {
        const toolbar = testCase.make();
        const fold = findChild(toolbar, "filesToolbarFold");
        compare(fold.text, "fold all");
        mouseClick(fold);
        compare(App.addFiles.tree.count, 5);
        compare(fold.text, "unfold all");
        mouseClick(fold);
        compare(App.addFiles.tree.count, 14);
        compare(fold.text, "fold all");
        verify(toolbar.run("collapseAll"));
        compare(fold.text, "unfold all");
        verify(toolbar.run("expandAll"));
        compare(fold.text, "fold all");
    }

    function test_hide_skipped() {
        const toolbar = testCase.make();
        const hide = findChild(toolbar, "filesToolbarHideSkipped");
        verify(!hide.selected);
        mouseClick(hide);
        compare(App.addFiles.tree.hideSkipped, true);
        verify(hide.selected);
        verify(toolbar.run("hideSkipped"));
        compare(App.addFiles.tree.hideSkipped, false);
        verify(!hide.selected);
    }
}
