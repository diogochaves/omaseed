import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Details against the demo library: the head and strip of spec §2 Fig. 1's right column,
// the four tabs and the tracker badge, and the Files tab's tree, folding and priorities
// against Fig. 2 row by row.
TestCase {
    id: testCase

    name: "Details"
    visible: true
    when: windowShown

    readonly property string bbbName: "Big Buck Bunny 4K (CC-BY)"
    readonly property string archName: "archlinux-2026.09.01-x86_64.iso"

    Component {
        id: detailsComponent

        Details {
            details: App.details
            height: 600
            width: 340
        }
    }

    function hashOf(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            const hash = App.filtered.hashAt(row);
            if (App.torrentRow(hash).name === name)
                return hash;
        }
        return "";
    }

    /// Every case starts from the same fictional library and the same selected torrent.
    function select(name) {
        App.loadDemo(16);
        const hash = testCase.hashOf(name);
        verify(hash.length > 0, "the demo library has no row named " + name);
        App.selectTorrent(hash);
        // The demo keeps the same hash between cases, so the files model keeps its state:
        // start every case from the torrent's own order, unfiltered and unselected.
        const files = App.details.files;
        files.filterText = "";
        files.sortKey = "";
        files.sortDescending = false;
        files.clearSelection();
        files.expandAll();
        const view = createTemporaryObject(detailsComponent, testCase);
        view.torrent = App.torrentRow(hash);
        return view;
    }

    function test_head_and_strip_come_from_the_cursor_row() {
        const view = testCase.select(testCase.archName);

        compare(view.torrent.name, testCase.archName);
        compare(view.torrent.category, "linux");
        // Spec §1 Fig. 1's strip: size, done, ratio, eta.
        compare(view.torrent.sizeText, "1.31 GB");
        compare(view.torrent.progressText, "48.9 %");
        compare(view.torrent.etaLongText, "2 min 40 s");
        compare(view.currentTab, 0);
        compare(view.cursorIndex, -1); // General has no list
    }

    function test_the_tracker_badge_follows_the_torrent() {
        const arch = testCase.select(testCase.archName);
        // Spec §2 Fig. 3's middle row times out, so Fig. 1's tab strip badges Trackers.
        verify(App.details.trackers.count > 0);
        verify(arch.hasTrackerError);

        const bbb = testCase.select(testCase.bbbName);
        verify(!bbb.hasTrackerError);
    }

    function test_general_tab_rows_are_the_figure() {
        testCase.select(testCase.archName);
        const properties = App.details.properties;

        compare(properties.savePath, "/data/torrents/linux/");
        compare(properties.pieces, "1 264 × 1 MiB");
        compare(properties.piecesHave, "617 of 1 264");
        compare(properties.availability, "38.20");
        compare(properties.seeds, "21 connected · 1 204 total");
        compare(properties.comment, "Arch Linux 2026.09.01 release");
        compare(properties.createdBy, "mktorrent 1.1");
        // Spec §2 Fig. 1 has no "Created on" row; the demo leaves the date out.
        compare(properties.createdOn, "");
    }

    // docs/12 G2: the demo fills every new row, so `--demo` shows them all.
    function test_the_demo_fills_the_docs12_rows() {
        testCase.select(testCase.archName);
        const properties = App.details.properties;

        compare(properties.connections, "63 of 100");
        compare(properties.avgSpeed, "↓ 1.72 MB/s · ↑ 96 KB/s");
        verify(properties.reannounce.startsWith("in "));
        verify(properties.lastSeen.length > 0);
        verify(properties.private === "yes" || properties.private === "no");
        verify(properties.magnet.startsWith("magnet:?xt=urn:btih:"));
        verify(properties.magnetText.endsWith("…"));

        // A finished one has a seeding time and a completion date as well.
        testCase.select(testCase.bbbName);
        const finished = App.details.properties;
        verify(finished.seedingTime.length > 0, "no seeding time for a complete torrent");
        verify(finished.completedOn.length > 0, "no completion date for a complete torrent");
    }

    function test_files_tab_is_figure_two() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        compare(view.currentTab, 1);

        const files = App.details.files;
        compare(files.count, 7);

        const names = ["Big Buck Bunny 4K/", "bbb_sunflower_2160p_60fps.mp4",
                       "bbb_sunflower_1080p_30fps.mp4", "subs/", "bbb.en.srt", "bbb.pt.srt",
                       "LICENSE.txt"];
        const depths = [0, 1, 1, 1, 2, 2, 1];
        const priorities = ["mixed", "high", "normal", "skip", "skip", "skip", "normal"];

        for (let row = 0; row < files.count; ++row) {
            const index = files.index(row, 0);
            compare(files.data(index, TorrentFilesModel.NameRole), names[row]);
            compare(files.priorityText(files.priority(row)), priorities[row]);
            compare(files.pathAt(row).split("/").length - 1, depths[row]);
        }

        compare(files.isFolder(0), true);
        compare(files.isFolder(1), false);
        compare(files.pathAt(4), "Big Buck Bunny 4K/subs/bbb.en.srt");
    }

    function test_fold_hides_the_subtree() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        compare(App.details.files.count, 7);

        // The cursor starts on the root folder; `h` folds it away.
        compare(view.cursorIndex, 0);
        view.fold();
        compare(App.details.files.count, 1);

        // `l` opens it again.
        view.unfold();
        compare(App.details.files.count, 7);

        // `h` on a file row is not a fold: it hands the focus back to the table.
        focusSpy.target = view;
        focusSpy.clear();
        view.moveCursor(1);
        compare(view.cursorIndex, 1);
        view.fold();
        compare(focusSpy.count, 1);
        compare(App.details.files.count, 7);
    }

    function test_move_cursor_is_clamped_to_the_list() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);

        view.moveCursor(-1);
        compare(view.cursorIndex, 0);
        view.moveCursor(3);
        compare(view.cursorIndex, 3);
        view.moveCursor(99);
        compare(view.cursorIndex, 6);

        // Each tab keeps its own place.
        view.activateTab(2);
        compare(view.cursorIndex, 0);
        view.moveCursor(1);
        compare(view.cursorIndex, 1);
        view.activateTab(1);
        compare(view.cursorIndex, 6);
    }

    function test_trackers_and_peers_tabs_are_figures_three_and_four() {
        const view = testCase.select(testCase.archName);

        view.activateTab(2);
        const trackers = App.details.trackers;
        compare(trackers.count, 5);
        compare(trackers.data(trackers.index(0, 0), TorrentTrackersModel.HostRole),
                "tracker.example.net");
        compare(trackers.data(trackers.index(3, 0), TorrentTrackersModel.HostRole), "DHT");

        view.activateTab(3);
        const peers = App.details.peers;
        compare(peers.count, 4);
        // Spec §2 Fig. 4's first row, fastest first.
        compare(peers.data(peers.index(0, 0), TorrentPeersModel.IpRole), "185.21.216.140");
        compare(peers.addressAt(0), "185.21.216.140:51413");
    }

    function test_copy_emits_the_row_the_cursor_is_on() {
        const view = testCase.select(testCase.archName);
        copySpy.target = view;
        copySpy.clear();

        view.activateTab(2);
        view.copyCurrent();
        compare(copySpy.count, 1);
        compare(copySpy.signalArguments[0][0], "https://tracker.example.net:443/announce");

        view.activateTab(3);
        view.copyCurrent();
        compare(copySpy.count, 2);
        compare(copySpy.signalArguments[1][0], "185.21.216.140:51413");
    }

    function test_open_emits_the_file_path() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        view.moveCursor(4);

        openSpy.target = view;
        openSpy.clear();
        view.forceActiveFocus();
        keyClick(Qt.Key_O);
        compare(openSpy.count, 1);
        compare(openSpy.signalArguments[0][0], "Big Buck Bunny 4K/subs/bbb.en.srt");
    }

    function test_number_keys_switch_tabs() {
        const view = testCase.select(testCase.archName);
        view.forceActiveFocus();

        keyClick(Qt.Key_2);
        compare(view.currentTab, 1);
        keyClick(Qt.Key_3);
        compare(view.currentTab, 2);
        keyClick(Qt.Key_4);
        compare(view.currentTab, 3);
        keyClick(Qt.Key_1);
        compare(view.currentTab, 0);

        // j/k move the cursor of whichever list is on show.
        keyClick(Qt.Key_3);
        keyClick(Qt.Key_J);
        compare(view.cursorIndex, 1);
        keyClick(Qt.Key_K);
        compare(view.cursorIndex, 0);
    }

    function test_collapsed_pane_has_no_width() {
        const view = testCase.select(testCase.archName);
        verify(view.implicitWidth > 0);

        view.collapsed = true;
        compare(view.implicitWidth, 0);
        compare(view.visible, false);
    }

    // ---- N1: the Files tab ------------------------------------------------------------

    function filesView() {
        const view = testCase.select(testCase.bbbName);
        view.activateTab(1);
        view.forceActiveFocus();
        return view;
    }

    function test_slash_filters_the_files_and_esc_clears_it() {
        const view = testCase.filesView();
        const files = App.details.files;

        keyClick("/");
        verify(view.filterEditing, "`/` did not put the caret in the files filter");
        verify(view.editing);
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(files.filterText, "srt");
        // The folders of a match stay, so the tree still reads.
        compare(files.count, 4);
        compare(files.matchCount, 2);

        // `enter` hands the keyboard back to the list and keeps the filter.
        keyClick(Qt.Key_Return);
        verify(!view.filterEditing);
        compare(files.filterText, "srt");
        // `esc` on the list clears it, and the emptied field goes with it.
        keyClick(Qt.Key_Escape);
        compare(files.filterText, "");
        compare(files.count, 7);
        verify(!findChild(view, "filesFilter").visible, "an emptied filter stayed on screen");
    }

    function test_esc_closes_an_empty_files_filter() {
        const view = testCase.filesView();
        const field = findChild(view, "filesFilter");

        keyClick("/");
        verify(field.visible);
        verify(view.filterEditing);
        // Nothing typed: `esc` closes the field and gives the list the keyboard back.
        keyClick(Qt.Key_Escape);
        verify(!field.visible, "`esc` left the empty filter open");
        verify(!view.filterEditing);
        verify(view.activeFocus);
    }

    function test_esc_while_typing_clears_and_closes_the_files_filter() {
        const view = testCase.filesView();
        const files = App.details.files;
        const field = findChild(view, "filesFilter");

        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(files.matchCount, 2);
        keyClick(Qt.Key_Escape);
        compare(files.filterText, "");
        verify(!field.visible);
        verify(!view.filterEditing);
        verify(view.activeFocus);
    }

    function test_space_selects_and_a_priority_applies_to_the_selection() {
        const view = testCase.filesView();
        const files = App.details.files;

        view.moveCursor(1);
        keyClick(Qt.Key_Space);
        view.moveCursor(5); // LICENSE.txt
        keyClick(Qt.Key_Space);
        compare(files.selectedCount, 2);

        // `x` skips both, wherever the cursor is (the demo applies it locally).
        keyClick(Qt.Key_X);
        compare(files.priority(1), 0);
        compare(files.priority(6), 0);
        compare(files.priority(2), 1); // not selected, untouched

        // `v` selects from the last toggled row to the cursor.
        view.moveCursor(-2);
        keyClick(Qt.Key_V);
        compare(files.selectedCount, 4);

        keyClick(Qt.Key_Escape);
        compare(files.selectedCount, 0);
    }

    function test_p_opens_the_priority_menu_and_its_letters_apply() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(2); // bbb_sunflower_1080p, normal

        keyClick("p");
        verify(view.editing, "`p` did not open the priority menu");
        keyClick("m");
        verify(!view.editing);
        compare(files.priority(2), 7);
    }

    function test_s_sorts_and_the_cursor_follows_its_row() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(6); // LICENSE.txt
        compare(files.pathAt(view.cursorIndex), "Big Buck Bunny 4K/LICENSE.txt");

        keyClick("s");
        keyClick("s"); // size, largest first
        compare(files.sortKey, "size");
        verify(files.sortDescending);
        compare(files.pathAt(view.cursorIndex), "Big Buck Bunny 4K/LICENSE.txt");
        compare(files.pathAt(1), "Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4");

        // The same key again reverses.
        keyClick("s");
        keyClick("s");
        verify(!files.sortDescending);
    }

    function test_r_renames_a_file_inline() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.moveCursor(4); // subs/bbb.en.srt
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        verify(view.editing, "`r` did not open the editor");
        // The name is selected up to its extension, so typing replaces "bbb.en".
        keyClick("e");
        keyClick("n");
        keyClick(Qt.Key_Return);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][2], "Big Buck Bunny 4K/subs/en.srt");
        verify(!view.editing);
        compare(files.nameAt(4), "en.srt");
    }

    function test_H_and_L_fold_and_open_every_folder() {
        const view = testCase.filesView();
        const files = App.details.files;
        view.foldAll(true);
        compare(files.count, 1);
        view.foldAll(false);
        compare(files.count, 7);
    }

    function test_o_and_O_ask_for_the_folder_and_the_file() {
        const view = testCase.filesView();
        view.moveCursor(1);
        fileSpy.target = view;
        fileSpy.clear();
        view.fileAction("openFile");
        compare(fileSpy.count, 1);
        compare(fileSpy.signalArguments[0][0], "Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4");

        // A folder has nothing to open with an application.
        view.moveCursor(-1);
        view.fileAction("openFile");
        compare(fileSpy.count, 1);
    }

    function test_y_Y_n_copy_the_path_the_local_path_and_the_name() {
        const view = testCase.filesView();
        view.moveCursor(6);
        copySpy.target = view;
        copySpy.clear();
        view.copyCurrent();
        view.copyLocalPath();
        view.copyName();
        compare(copySpy.count, 3);
        compare(copySpy.signalArguments[0][0], "Big Buck Bunny 4K/LICENSE.txt");
        verify(copySpy.signalArguments[1][0].endsWith("/Big Buck Bunny 4K/LICENSE.txt"));
        verify(copySpy.signalArguments[1][0].startsWith("/"));
        compare(copySpy.signalArguments[2][0], "LICENSE.txt");
    }

    // docs/11 J8: the tags give way before the added date does.
    function test_the_head_keeps_the_added_date_whole() {
        const view = testCase.select(testCase.bbbName);
        view.width = 220;
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        verify(labels !== null && added !== null);
        verify(added.text.startsWith(" · added"));
        compare(added.width, added.implicitWidth);
        verify(labels.truncated, "the tags did not give way");
        verify(added.x + added.width <= view.width);
    }

    // docs/11 J2: every header starts where its column's text does.
    function test_file_headers_line_up_with_the_rows() {
        const view = testCase.filesView();
        wait(0);
        const header = findChild(view, "fileHeader_name");
        const name = findChild(view, "fileName_1");
        verify(header !== null && name !== null);
        // A file row at depth 1 is indented one step; the header lines up with depth 0,
        // where the folder's glyph starts. Compare against the row's own left edge.
        const headerX = header.mapToItem(view, 0, 0).x;
        const rowX = name.mapToItem(view, 0, 0).x - Theme.space(14);
        compare(Math.round(headerX), Math.round(rowX));
    }

    SignalSpy {
        id: renameSpy

        signalName: "torrentFileRenameRequested"
    }

    SignalSpy {
        id: fileSpy

        signalName: "openFileRequested"
    }

    SignalSpy {
        id: focusSpy

        signalName: "focusTable"
    }

    SignalSpy {
        id: copySpy

        signalName: "copyRequested"
    }

    SignalSpy {
        id: openSpy

        signalName: "openFolderRequested"
    }
}
