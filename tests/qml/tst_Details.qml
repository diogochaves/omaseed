import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// Details against the demo library: the head and strip of spec §2 Fig. 1's right column,
// the four tabs and the tracker badge, and the Files tab's tree, folding and priorities
// against Fig. 2 row by row.
TestCase {
    id: testCase

    name: "Details"

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
        compare(properties.pieces, "1 264 × 1 MiB · 617 have");
        compare(properties.availability, "38.20");
        compare(properties.seeds, "21 connected · 1 204 total");
        compare(properties.comment, "Arch Linux 2026.09.01 release");
        compare(properties.createdBy, "mktorrent 1.1");
        // Spec §2 Fig. 1 has no "Created on" row; the demo leaves the date out.
        compare(properties.createdOn, "");
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
