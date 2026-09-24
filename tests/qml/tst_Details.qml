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
        // docs/12 G5: General's rows are a list too, and the cursor starts on the first.
        compare(view.cursorIndex, 0);
        compare(view.rowCount, findChild(view, "generalTab").count);
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

    // docs/12 G1: Transfer, Information, Options, in that order, each row in its group.
    function test_general_is_grouped_transfer_information_options() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "generalTab");
        verify(general !== null);

        const groups = general.groups;
        compare(groups.map(group => group.label), ["Transfer", "Information", "Options"]);
        const keys = groups.map(group => group.rows.map(row => row.key));
        compare(keys[0], ["Status", "Downloaded", "Uploaded", "Wasted", "Seeds", "Peers",
                          "Connections", "Availability", "Avg speed", "Time active",
                          "Last seen full", "Reannounce"]);
        // No hash v2, no creation or completion date in the demo's arch row.
        compare(keys[1], ["Save path", "Pieces", "Hash v1", "Magnet", "Private", "Created by",
                          "Comment"]);
        compare(keys[2][0], "Share limit");

        // Each group has its heading on screen.
        for (const label of ["Transfer", "Information", "Options"]) {
            const heading = findChild(view, "generalHeading_" + label);
            verify(heading !== null && heading.visible, "no heading for " + label);
        }

        // The magnet is shown short; its row is dim.
        const magnet = groups[1].rows[3];
        verify(magnet.value.endsWith("…"));
        compare(magnet.dim, true);
    }

    // docs/10 §5 with G1: two columns put Transfer alone on the left.
    function test_two_columns_keep_the_groups_whole() {
        const view = testCase.select(testCase.archName);
        view.width = 1100;
        view.wide = true;
        wait(0); // let the columns lay out
        const general = findChild(view, "generalTab");
        compare(general.columns, 2);
        const transfer = findChild(view, "generalGroup_Transfer");
        const information = findChild(view, "generalGroup_Information");
        const options = findChild(view, "generalGroup_Options");
        verify(transfer.mapToItem(general, 0, 0).x < information.mapToItem(general, 0, 0).x);
        compare(information.mapToItem(general, 0, 0).x, options.mapToItem(general, 0, 0).x);
        verify(options.mapToItem(general, 0, 0).y > information.mapToItem(general, 0, 0).y);
    }

    // docs/13: under the table the head and the strip share one band, and the tabs and
    // their body take the pane's whole width below it.
    function test_wide_puts_the_head_and_strip_in_one_band_over_full_width_tabs() {
        const view = testCase.select(testCase.archName);
        view.width = 1100;
        view.wide = true;
        wait(0);
        const strip = findChild(view, "strip");
        const general = findChild(view, "generalTab");
        const stripAt = strip.mapToItem(view, 0, 0);
        compare(stripAt.x + strip.width, view.width);
        compare(strip.width, Theme.space(440));
        compare(strip.rule, false);
        const generalAt = general.mapToItem(view, 0, 0);
        compare(generalAt.x, 0);
        compare(general.width, view.width);
        verify(generalAt.y >= stripAt.y + strip.height, "the tabs overlap the band");
        const tabs = findChild(view, "detailsTabs");
        verify(tabs.mapToItem(view, 0, 0).y >= stripAt.y + strip.height, "the strip is not in the band");

        // Narrow, the strip gives the name half the pane at least.
        view.width = 600;
        wait(0);
        compare(strip.width, 300);

        // Docked at the side, the strip is back under the head, full width, with its rule.
        view.wide = false;
        wait(0);
        compare(strip.width, view.width);
        compare(strip.rule, true);
    }

    // docs/12 G3: the pieces bar tops General, drawn from the demo's piece states.
    function test_the_pieces_bar_tops_general() {
        const view = testCase.select(testCase.archName);
        const bar = findChild(view, "piecesBar");
        verify(bar !== null);
        verify(bar.visible, "no pieces bar for a torrent with pieces");
        compare(App.details.pieceCount, 1264);
        compare(App.details.piecesHaveCount, 617);
        compare(findChild(view, "piecesLabel").text, "pieces · 617 of 1 264");
        compare(findChild(view, "piecesPercent").text, "48.9 %");

        // Above the Transfer heading.
        wait(0);
        const transfer = findChild(view, "generalHeading_Transfer");
        const general = findChild(view, "generalTab");
        verify(bar.mapToItem(general, 0, 0).y < transfer.mapToItem(general, 0, 0).y);

        // The canvas painted one cell per pixel: some had, some in flight, some missing.
        const canvas = findChild(view, "piecesCanvas");
        tryVerify(() => canvas.cells.length > 0);
        compare(canvas.cells.length, Math.min(1264, Math.floor(canvas.width)));
        verify(canvas.cells.indexOf(2) >= 0);
        verify(canvas.cells.indexOf(1) >= 0);
        verify(canvas.cells.indexOf(0) >= 0);

        // A finished torrent is had from end to end.
        testCase.select(testCase.bbbName);
        compare(App.details.piecesHaveCount, App.details.pieceCount);
    }

    // docs/12 G5: j k walk General's rows across the groups; y copies the whole value.
    function test_general_rows_take_a_cursor_and_y_copies_the_whole_value() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "generalTab");
        view.forceActiveFocus();
        valueSpy.target = view;
        valueSpy.clear();

        compare(view.cursorIndex, 0);
        verify(findChild(view, "generalRow_0").hasCursor);
        keyClick("j");
        compare(view.cursorIndex, 1);
        verify(findChild(view, "generalRow_1").hasCursor);
        verify(!findChild(view, "generalRow_0").hasCursor);
        keyClick("k");
        keyClick("k");
        compare(view.cursorIndex, 0); // clamped at the top

        // G goes to the last row, in Options; the cursor crosses the group headings.
        keyClick(Qt.Key_G, Qt.ShiftModifier);
        compare(view.cursorIndex, general.count - 1);
        compare(general.rows[view.cursorIndex].key, "Auto manage");

        // The hash, whole.
        const hashRow = general.rows.findIndex(row => row.key === "Hash v1");
        verify(hashRow > 0);
        view.moveCursor(hashRow - view.cursorIndex);
        compare(view.cursorIndex, hashRow);
        keyClick("y");
        compare(valueSpy.count, 1);
        compare(valueSpy.signalArguments[0][0], App.details.properties.hashV1);
        compare(valueSpy.signalArguments[0][1], "hash v1");

        // The magnet: the screen shows it short, y copies all of it.
        const magnetRow = general.rows.findIndex(row => row.key === "Magnet");
        view.moveCursor(magnetRow - view.cursorIndex);
        keyClick("y");
        compare(valueSpy.count, 2);
        compare(valueSpy.signalArguments[1][0], App.details.properties.magnet);
        verify(valueSpy.signalArguments[1][0].length > general.rows[magnetRow].value.length);
        compare(valueSpy.signalArguments[1][1], "magnet");
    }

    // A hash that does not fit breaks at its middle, never two characters over; wide, it
    // stays on one line. Either way y copies it whole (above).
    function test_a_hash_that_does_not_fit_breaks_in_half() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "generalTab");
        const hashRow = general.rows.findIndex(row => row.key === "Hash v1");
        const hash = App.details.properties.hashV1;
        compare(hash.length, 40);
        const value = findChild(findChild(view, "generalRow_" + hashRow), "generalValue");
        compare(value.text, hash.slice(0, 20) + "\n" + hash.slice(20));

        view.width = 1400;
        tryCompare(value, "text", hash);
    }

    // docs/12 G5: a click on a row moves the cursor there and copies the value.
    function test_a_click_on_a_general_row_copies_it() {
        const view = testCase.select(testCase.archName);
        const general = findChild(view, "generalTab");
        valueSpy.target = view;
        valueSpy.clear();

        const pathRow = general.rows.findIndex(row => row.key === "Save path");
        // Offscreen delivers no mouse events: the row's own signal stands in for the click.
        findChild(view, "generalRow_" + pathRow).clicked();
        compare(view.cursorIndex, pathRow);
        compare(valueSpy.count, 1);
        compare(valueSpy.signalArguments[0][0], "/data/torrents/linux/");
        compare(valueSpy.signalArguments[0][1], "save path");
    }

    // docs/12 G4: the Options group's on/off rows, and `enter` on one turns it over.
    function test_enter_toggles_an_option_row() {
        const view = testCase.select(testCase.archName);
        const hash = App.details.hash;
        const general = findChild(view, "generalTab");
        const keys = general.groups[2].rows.map(row => row.key);
        compare(keys, ["Share limit", "Sequential", "First / last", "Super seed",
                       "Auto manage"]);

        const before = App.torrentRow(hash).seqDl;
        const sequential = general.rows.findIndex(row => row.option === "sequential");
        compare(general.rows[sequential].value, before ? "on" : "off");
        view.forceActiveFocus();
        view.moveCursor(sequential - view.cursorIndex);
        keyClick(Qt.Key_Return);
        // The demo flips the fictional row; the pane redraws from the next snapshot.
        compare(App.torrentRow(hash).seqDl, !before);
        view.torrent = App.torrentRow(hash);
        compare(general.rows[sequential].value, !before ? "on" : "off");
        compare(general.rows[sequential].on, !before);

        // The cursor stays on the row, so `enter` again turns it back.
        compare(view.cursorIndex, sequential);
        keyClick(Qt.Key_Return);
        compare(App.torrentRow(hash).seqDl, before);

        // Each option reaches its own field.
        for (const [option, field] of [["firstLast", "firstLastPiecePrio"],
                                       ["superSeeding", "superSeeding"],
                                       ["autoManagement", "autoTmm"]]) {
            view.torrent = App.torrentRow(hash);
            const row = general.rows.findIndex(entry => entry.option === option);
            const was = App.torrentRow(hash)[field];
            view.moveCursor(row - view.cursorIndex);
            keyClick(Qt.Key_Enter);
            compare(App.torrentRow(hash)[field], !was, option);
            view.toggleGeneralOption(row); // and back, for the next case
            view.torrent = App.torrentRow(hash);
        }
    }

    // docs/12 G4: `enter` on a row that is not an option is not the pane's to take.
    function test_enter_on_a_plain_row_changes_nothing() {
        const view = testCase.select(testCase.archName);
        compare(view.toggleGeneralOption(0), false); // Status
        const general = findChild(view, "generalTab");
        const shareLimit = general.rows.findIndex(row => row.key === "Share limit");
        compare(view.toggleGeneralOption(shareLimit), false);
    }

    // docs/12 G5: the pane's own hint row teaches General too.
    function test_general_has_hints_of_its_own() {
        const view = testCase.select(testCase.archName);
        const hints = findChild(view, "detailsHints");
        verify(hints !== null && hints.visible);
        compare(hints.model.map(hint => hint.key + " " + hint.label), ["j k move", "y copy value",
                                                                          "enter change"]);
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

    // Review VIEW-13: a shorter list pulls the Trackers cursor back onto a row.
    function test_the_trackers_cursor_stays_on_a_row_when_the_list_shrinks() {
        const view = testCase.select(testCase.archName);
        view.activateTab(2);
        const trackers = App.details.trackers;
        view.moveCursor(trackers.count);
        compare(view.trackersCursor, trackers.count - 1);
        const longest = trackers.count;
        let shorter = "";
        for (let row = 0; row < App.filtered.count && shorter.length === 0; ++row) {
            App.selectTorrent(App.filtered.hashAt(row));
            if (trackers.count > 0 && trackers.count < longest)
                shorter = App.filtered.hashAt(row);
        }
        if (shorter.length === 0)
            skip("the demo library has no torrent with fewer trackers");
        verify(view.trackersCursor < trackers.count, "the cursor was left past the end");
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

    // docs/12 F1: the summary line is always there, and `/` turns the same row into the field.
    function test_the_summary_line_turns_into_the_filter_and_back() {
        const view = testCase.filesView();
        const summary = findChild(view, "filesSummary");
        const field = findChild(view, "filesFilter");
        const header = findChild(view, "fileHeader_name");
        verify(summary !== null && field !== null && header !== null);
        wait(0);

        verify(summary.visible);
        verify(!field.visible);
        compare(findChild(view, "filesSummaryCount").text, "5 files");
        compare(findChild(view, "filesSummaryFacts").text, " · 2 folders · 3.91 GB · 2 skipped");
        verify(findChild(view, "filesSummaryGo").visible);
        const headerY = header.mapToItem(view, 0, 0).y;

        keyClick("/");
        verify(!summary.visible);
        verify(field.visible);
        compare(field.trailingKey, "esc");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        compare(field.trailing, "2 of 5 files");
        // The same row: the headers and the list do not move.
        compare(header.mapToItem(view, 0, 0).y, headerY);

        keyClick(Qt.Key_Escape);
        verify(summary.visible);
        verify(!field.visible);
        compare(header.mapToItem(view, 0, 0).y, headerY);
    }

    // docs/12 F2: one file has nothing to filter; `/` says so instead of opening a field.
    function test_a_single_file_offers_no_filter() {
        const view = testCase.select(testCase.archName);
        view.activateTab(1);
        view.forceActiveFocus();
        compare(App.details.files.fileCount, 1);
        wait(0);

        compare(findChild(view, "filesSummaryCount").text, "1 file");
        compare(findChild(view, "filesSummaryFacts").text, " · 1.31 GB");
        verify(!findChild(view, "filesSummaryGo").visible, "a single file still offers a filter");

        noteSpy.target = view;
        noteSpy.clear();
        keyClick("/");
        verify(!findChild(view, "filesFilter").visible, "`/` opened a field on one file");
        verify(!view.filterEditing);
        verify(findChild(view, "filesSummary").visible);
        compare(noteSpy.count, 1);
        compare(noteSpy.signalArguments[0][0], "one file, nothing to filter");
    }

    // docs/12 F3: `ctrl+a` selects what the filter shows, and `p` then acts on all of it.
    function test_ctrl_a_selects_the_filtered_files_and_p_applies_to_them() {
        const view = testCase.filesView();
        const files = App.details.files;

        keyClick("/");
        keyClick("s");
        keyClick("r");
        keyClick("t");
        keyClick(Qt.Key_Return);
        verify(!view.filterEditing);
        compare(files.matchCount, 2);

        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(files.selectedCount, 2);
        verify(!files.isSelected(0), "a folder around the matches was selected");

        keyClick("p");
        verify(view.editing, "`p` did not open the priority menu");
        keyClick("n");
        // Both subtitles go from skip to normal; the videos stay as they were.
        files.filterText = "";
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/subs/bbb.en.srt")), 1);
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/subs/bbb.pt.srt")), 1);
        compare(files.priority(files.rowOfPath("Big Buck Bunny 4K/bbb_sunflower_2160p_60fps.mp4")),
                6);

        // With no filter, every file (the folders count through them).
        keyClick(Qt.Key_Escape);
        compare(files.selectedCount, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(files.selectedCount, 5);
    }

    // docs/12 F4: the placeholder shows the syntax, and `*` and `|` work as it says.
    function test_the_filter_takes_wildcards_and_alternatives() {
        const view = testCase.filesView();
        const files = App.details.files;
        const field = findChild(view, "filesFilter");

        keyClick("/");
        compare(field.placeholder, "filter files · *.srt · mkv|mp4");
        for (const key of "*.srt")
            keyClick(key);
        compare(files.filterText, "*.srt");
        compare(files.matchCount, 2);
        compare(field.trailing, "2 of 5 files");

        field.text = "txt|2160p";
        compare(files.matchCount, 2);
        compare(files.count, 3); // the root folder and the two files
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

    // Review VIEW-7: a sync that re-sorts the rows under an open rename (sorted by progress,
    // say) must not move the rename or the cursor onto another file.
    function test_a_resort_under_a_rename_keeps_the_file() {
        const view = testCase.filesView();
        const files = App.details.files;
        // A file that the name sort moves.
        let from = -1;
        const before = [];
        for (let r = 0; r < files.count; ++r)
            before.push(files.pathAt(r));
        files.sortBy("name");
        for (let r = 0; r < before.length && from < 0; ++r) {
            if (!files.isFolder(files.rowOfPath(before[r])) && files.rowOfPath(before[r]) !== r)
                from = r;
        }
        files.sortKey = "";
        verify(from >= 0);
        const path = before[from];
        compare(files.pathAt(from), path);
        view.moveCursor(-files.count);
        view.moveCursor(from);
        compare(files.pathAt(view.cursorIndex), path);
        renameSpy.target = App;
        renameSpy.clear();

        keyClick("r");
        verify(view.editing);
        keyClick("L"); // replaces the selected stem
        // What a sync does when the sort key's numbers move: the rows are rebuilt in a new
        // order, without the pane asking for it.
        files.sortBy("name");
        verify(files.rowOfPath(path) !== from, "the re-sort did not move the row");
        compare(files.pathAt(view.cursorIndex), path, "the cursor stayed on the old index");
        verify(view.editing, "the rename closed");
        compare(findChild(view, "filesTab").renamingRow, files.rowOfPath(path),
                "the editor stayed on the old index");
        wait(0); // the editor takes the keyboard back once the reset has settled
        keyClick("x");
        keyClick(Qt.Key_Return);
        compare(renameSpy.count, 1);
        compare(renameSpy.signalArguments[0][1], path, "the rename went to another file");
        compare(renameSpy.signalArguments[0][3], false, "a file was renamed as a folder");
        const dir = path.substring(0, path.lastIndexOf("/") + 1);
        const ext = path.substring(path.lastIndexOf("."));
        compare(renameSpy.signalArguments[0][2], dir + "Lx" + ext);
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
        verify(!added.truncated, "the date was cut");
        verify(labels.truncated, "the tags did not give way");
        verify(added.x + added.width <= view.width);
    }

    // Review RUN-6: narrower still, the labels go with their ` · `, and nothing draws past
    // the head, even where the head has no room at all.
    function test_the_head_drops_the_labels_and_their_separator_when_they_cannot_show() {
        const view = testCase.select(testCase.bbbName);
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        view.width = Theme.space(32) + added.implicitWidth + Theme.space(10);
        verify(!labels.visible, "a sliver of the labels is still drawn");
        verify(!added.text.startsWith(" · "), "the separator stayed without its labels");
        verify(added.x + added.width <= view.width);

        view.width = Theme.space(20);
        verify(added.width <= 0 || added.x + added.width <= view.width, "the date drew past the pane");
    }

    // Review VER-2: the pane is created with the torrent but no width yet (the first
    // layout), then opens. The category and tags must show, not a bare ` · added …`.
    function test_the_head_shows_the_labels_when_the_pane_widens_after_the_torrent() {
        testCase.select(testCase.bbbName);
        const row = App.torrentRow(testCase.hashOf(testCase.bbbName));
        const view = createTemporaryObject(detailsComponent, testCase, {"width": 0, "torrent": row});
        wait(0);
        view.width = 450;
        wait(0);
        const labels = findChild(view, "headLabels");
        const added = findChild(view, "headAdded");
        verify(labels.text.length > 0, "the demo row has no category or tags");
        verify(labels.visible, "the labels are hidden at 450 px");
        verify(labels.width > 0, "the labels are laid out with no width");
        verify(!labels.truncated, "the labels are cut with room to spare");
        verify(added.text.startsWith(" · added"));
        verify(added.x >= labels.width, "the date overlaps the labels");
    }

    // Review VER-2: a fresh pane at a normal width shows the labels straight away.
    function test_a_fresh_head_shows_the_labels() {
        const view = testCase.select(testCase.bbbName);
        wait(0);
        const labels = findChild(view, "headLabels");
        verify(labels.visible);
        verify(labels.width > 0, "the labels are laid out with no width");
    }

    TextMetrics {
        id: sizeSample

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "999.9 MB"
    }

    // Review RUN-6: the Size column holds a whole size in the font on screen, whatever the
    // spacing scale, so the unit is never cut.
    function test_the_size_column_holds_a_whole_size() {
        const view = testCase.filesView();
        wait(0);
        const header = findChild(view, "fileHeader_size");
        verify(header.width >= Math.ceil(sizeSample.advanceWidth),
               header.width + " < " + sizeSample.advanceWidth);
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
        id: valueSpy

        signalName: "valueCopyRequested"
    }

    SignalSpy {
        id: copySpy

        signalName: "copyRequested"
    }

    SignalSpy {
        id: noteSpy

        signalName: "noteRequested"
    }

    SignalSpy {
        id: openSpy

        signalName: "openFolderRequested"
    }
}
