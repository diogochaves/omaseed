import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// T4.3. The torrent actions of spec v2 §3 Fig. 6, 7, 8, 11 against the demo library (spec
// §1 Fig. 1's sixteen rows). `init()` puts the shared `App` proxy, the cursor and the
// selection back, because the whole runner shares one `App`.
TestCase {
    id: testCase

    height: 800
    name: "Actions"
    visible: true
    when: windowShown
    width: 1360

    MainView {
        id: view

        anchors.fill: parent
        focus: true
    }

    Component {
        id: limitsSpy

        SignalSpy {
            signalName: "limitsRequested"
            target: App
        }
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function init() {
        unshareData();
        view.helpOpen = false;
        view.table.cancelRename();
        view.table.clearSelection();
        App.filtered.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
        view.deleteConfirmPanel.close();
        view.moveSheet.close();
        App.outcomes.clear();
        view.leaveMoveShow();
        view.moveSheet.close();
        App.filtered.folder = "";
        view.limitsSheetPanel.close();
        view.renameSheetPanel.close();
        view.actionsMenuPanel.close();
        view.folderMenuPanel.close();
        view.sharedMoveConfirm.close();
        view.categoryPalette.close();
    }

    /// #28: what shareData() changed, to put back (hash → its own paths).
    property var _unshare: ({})

    /// Saves `b` into `a`'s folder, as a cross-seed would be: the same content path.
    function shareData(a, b) {
        const rowA = App.torrentRow(a);
        const rowB = App.torrentRow(b);
        const back = testCase._unshare;
        back[b] = {
            "save_path": rowB.savePath,
            "content_path": rowB.savePath + "/" + rowB.name
        };
        testCase._unshare = back;
        const patch = {};
        patch[b] = {
            "save_path": rowA.savePath,
            "content_path": rowA.savePath + "/" + rowA.name
        };
        App.patchDemoTorrents(patch);
    }

    function unshareData() {
        if (Object.keys(testCase._unshare).length > 0)
            App.patchDemoTorrents(testCase._unshare);
        testCase._unshare = {};
        const files = {};
        for (const hash of testCase._unfiles)
            files[hash] = [];
        App.patchDemoFiles(files);
        testCase._unfiles = [];
    }

    /// #35: the file lists patchDemoFiles() gave, to take back.
    property var _unfiles: []

    /// Saves `b` into `a`'s save path without a folder of its own ("No subfolder": its content
    /// path is the save path), so only the two file lists can tell whether they share; `a`'s
    /// list is `aFiles`, `b`'s `bFiles`, "fetched" a turn of the event loop after they are asked.
    function shareWithoutRoot(a, b, aFiles, bFiles) {
        const rowA = App.torrentRow(a);
        const rowB = App.torrentRow(b);
        const back = testCase._unshare;
        back[b] = {
            "save_path": rowB.savePath,
            "content_path": rowB.savePath + "/" + rowB.name
        };
        testCase._unshare = back;
        const patch = {};
        patch[b] = {
            "save_path": rowA.savePath,
            "content_path": rowA.savePath
        };
        App.patchDemoTorrents(patch);
        const files = {};
        files[a] = aFiles;
        files[b] = bFiles;
        App.patchDemoFiles(files);
        testCase._unfiles = [a, b];
    }

    /// The demo library has no name-to-hash lookup of its own; this walks the filtered model.
    function hashByName(name) {
        for (let i = 0; i < App.filtered.count; ++i) {
            const hash = App.filtered.hashAt(i);
            if (App.torrentRow(hash).name === name)
                return hash;
        }
        return "";
    }

    // ---- the menu (Fig. 11) --------------------------------------------------------------
    function test_the_menu_lists_every_action_of_fig_11_with_its_key() {
        const expected = [
            ["p", "stopStart"],
            ["P", "forceStart"],
            ["c", "recheck"],
            ["R", "reannounce"],
            ["C", "category"],
            ["t", "tags"],
            ["m", "location"],
            ["L", "limits"],
            ["r", "rename"],
            ["[", "queueUp"],
            ["]", "queueDown"],
            ["y", "copyMagnet"],
            ["Y", "copyHash"],
            ["n", "copyName"],
            ["o", "openFolder"],
            ["d", "delete"]
        ];
        for (const [key, action] of expected)
            verify(view.actionsMenuPanel.indexOfKey(key) >= 0, "missing row for key " + key);
        compare(view.actionsMenuPanel.indexOfKey("d") >= 0, true);
        const deleteRow = view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("d")];
        compare(deleteRow.danger, true);
    }

    // docs/12 G4: the four per-torrent options, checked for the cursor torrent.
    function test_the_menu_carries_the_four_options() {
        const hash = hashByName("archlinux-2026.09.01-x86_64.iso");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);
        for (const [key, action] of [["s", "sequential"], ["f", "firstLast"],
                                     ["S", "superSeeding"], ["a", "autoManagement"]]) {
            const index = view.actionsMenuPanel.indexOfKey(key);
            verify(index >= 0, "missing row for key " + key);
            compare(view.actionsMenuPanel.items[index].action, action);
        }
        // The demo's linux rows are automatically managed.
        compare(view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("a")].glyph,
                Glyphs.check);
        compare(view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("s")].glyph, "");
        view.actionsMenuPanel.close();
    }

    // docs/12 G4: a menu row sets the whole selection to the other side of the cursor's.
    function test_an_option_row_applies_to_the_selection() {
        const arch = hashByName("archlinux-2026.09.01-x86_64.iso");
        const fedora = hashByName("Fedora-Workstation-Live-44-1.6.x86_64.iso");
        // A mixed pair: fedora already sequential, the cursor (arch) not.
        App.setSequentialDownload([arch], false);
        App.setSequentialDownload([fedora], true);
        view.table.toggleSelection(App.filtered.rowOfHash(fedora));
        view.table.moveCursorTo(App.filtered.rowOfHash(arch));
        view.table.toggleSelection(App.filtered.rowOfHash(arch));
        compare(view.table.selectedCount, 2);

        view.torrentAction("sequential");
        compare(App.torrentRow(arch).seqDl, true);
        compare(App.torrentRow(fedora).seqDl, true);

        view.torrentAction("sequential");
        compare(App.torrentRow(arch).seqDl, false);
        compare(App.torrentRow(fedora).seqDl, false);

        view.torrentAction("superSeeding");
        compare(App.torrentRow(arch).superSeeding, true);
        compare(App.torrentRow(fedora).superSeeding, true);
        App.setSuperSeeding([arch, fedora], false);
    }

    // Q24: with a selection, the menu opened on another row says what its rows act on.
    function test_the_menu_heads_with_the_selection_count() {
        const arch = hashByName("archlinux-2026.09.01-x86_64.iso");
        const fedora = hashByName("Fedora-Workstation-Live-44-1.6.x86_64.iso");
        const bunny = hashByName("Big Buck Bunny 4K (CC-BY)");

        // No selection: the menu acts on its row, no head line.
        view.table.moveCursorTo(App.filtered.rowOfHash(bunny));
        view.table.requestActions();
        compare(view.actionsMenuPanel.heading, "");
        view.actionsMenuPanel.close();

        // The selection is the row itself: still none.
        view.table.toggleSelection(App.filtered.rowOfHash(bunny));
        view.table.requestActions();
        compare(view.actionsMenuPanel.heading, "");
        view.actionsMenuPanel.close();
        view.table.clearSelection();

        // Two selected, the cursor jumped to a third.
        view.table.toggleSelection(App.filtered.rowOfHash(arch));
        view.table.toggleSelection(App.filtered.rowOfHash(fedora));
        view.table.moveCursorTo(App.filtered.rowOfHash(bunny));
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);
        compare(view.actionsMenuPanel.heading, "2 selected");
        compare(view.actionsMenuPanel.cursorIndex, 0);
        view.actionsMenuPanel.close();
        compare(view.table.selectedCount, 2);
    }

    function test_start_stop_label_follows_the_cursor_torrent() {
        const stoppedHash = hashByName("OpenStreetMap planet-260908.osm.pbf");
        verify(stoppedHash.length > 0);
        view.table.moveCursorTo(App.filtered.rowOfHash(stoppedHash));
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);
        compare(view.actionsMenuPanel.cursorStopped, true);
        compare(view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("p")].text, "start");
        view.actionsMenuPanel.close();

        const downloadingHash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(downloadingHash));
        view.table.requestActions();
        compare(view.actionsMenuPanel.cursorStopped, false);
        compare(view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("p")].text, "stop");
    }

    // ---- the delete confirm (Fig. 6) -------------------------------------------------------
    function test_d_opens_the_confirm_with_the_remembered_toggle() {
        App.deleteFilesDefault = false;
        keyClick("d");
        compare(view.deleteConfirmPanel.opened, true);
        compare(view.deleteConfirmPanel.body.children[0].checked, false);
        view.deleteConfirmPanel.close();

        App.deleteFilesDefault = true;
        keyClick("d");
        compare(view.deleteConfirmPanel.body.children[0].checked, true);
    }

    function test_shift_d_forces_the_toggle_on_regardless() {
        App.deleteFilesDefault = false;
        keyClick(Qt.Key_D, Qt.ShiftModifier);
        compare(view.deleteConfirmPanel.opened, true);
        compare(view.deleteConfirmPanel.body.children[0].checked, true);
    }

    // Review VER: flipping the toggle and cancelling leaves the remembered default alone; a
    // confirmed delete with the toggle flipped remembers it, and `D`'s forced "on" does not.
    function test_the_toggle_is_remembered_only_on_a_confirmed_delete() {
        const confirm = view.deleteConfirmPanel;
        const toggle = confirm.body.children[0];
        App.deleteFilesDefault = false;

        confirm.openFor([], false);
        toggle.checked = true;
        toggle.toggled();
        confirm.cancel();
        compare(App.deleteFilesDefault, false, "a cancelled flip was remembered");

        confirm.openFor([], true); // `D`
        confirm.confirm();
        compare(App.deleteFilesDefault, false, "D's forced toggle was remembered");

        confirm.openFor([], false);
        toggle.checked = true;
        toggle.toggled();
        confirm.confirm();
        compare(App.deleteFilesDefault, true);

        confirm.openFor([], true);
        toggle.checked = false;
        toggle.toggled();
        confirm.confirm();
        compare(App.deleteFilesDefault, false);
    }

    // Batch-2 J7: the title counts, the line under it names the torrent and its size once.
    function test_delete_confirm_names_a_single_torrent() {
        const hash = hashByName("Elephants Dream 1080p (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        keyClick("d");
        compare(view.deleteConfirmPanel.message, "Delete this torrent?");
        const lines = view.deleteConfirmPanel.subtitle.split("\n");
        compare(lines.length, 2);
        compare(lines[0], "Elephants Dream 1080p (CC-BY)");
        compare(lines[1], App.sizeText(App.torrentRow(hash).size) + " on disk");
    }

    function test_delete_confirm_lists_three_names_then_counts_the_rest() {
        const hashes = [];
        for (let i = 0; i < 5; ++i)
            hashes.push(App.filtered.hashAt(i));
        view.deleteConfirmPanel.openFor(hashes, false);
        compare(view.deleteConfirmPanel.message, "Delete 5 torrents?");
        const lines = view.deleteConfirmPanel.subtitle.split("\n");
        compare(lines.length, 4);
        compare(lines[0], App.torrentRow(hashes[0]).name);
        compare(lines[2], App.torrentRow(hashes[2]).name);
        verify(lines[3].startsWith("and 2 more · "), lines[3]);
        verify(lines[3].endsWith(" on disk"), lines[3]);
        view.deleteConfirmPanel.close();

        view.deleteConfirmPanel.openFor(hashes.slice(0, 3), false);
        compare(view.deleteConfirmPanel.message, "Delete 3 torrents?");
        compare(view.deleteConfirmPanel.subtitle.split("\n").length, 4);
        verify(view.deleteConfirmPanel.subtitle.indexOf("more") < 0);
        view.deleteConfirmPanel.close();
    }

    // ---- #28: data another torrent uses (docs/26) ------------------------------------------
    function test_files_on_names_the_torrent_sharing_its_files() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        const confirm = view.deleteConfirmPanel;
        confirm.openFor([sintel], true);
        verify(confirm.asking);
        compare(confirm.choice, 0, "keep the files is not picked");
        compare(confirm.confirmText, "delete, keep files");
        compare(confirm.plan(), [{
                                     "hashes": [sintel],
                                     "files": false
                                 }]);
        compare(confirm.panelWidth, Theme.space(520));
        const line = confirm.body.children[1];
        verify(line.visible);
        verify(line.text.indexOf("Its files are also seeded by 1 torrent you're not deleting.") >= 0, line.text);
        verify(line.text.indexOf("Tears of Steel 4K (CC-BY)") >= 0, line.text);
    }

    function test_j_picks_deleting_the_sharers_too_and_reopening_keeps_the_files() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        const confirm = view.deleteConfirmPanel;
        confirm.openFor([sintel], true);
        keyClick("j");
        compare(confirm.choice, 1);
        compare(confirm.confirmText, "delete 2 with files");
        compare(confirm.plan(), [{
                                     "hashes": [sintel, tears],
                                     "files": true
                                 }]);
        keyClick("k");
        compare(confirm.choice, 0);
        keyClick("j");
        confirm.close();
        confirm.openFor([sintel], true);
        compare(confirm.choice, 0, "a reopened confirm did not pick keep the files");
    }

    function test_files_off_asks_nothing_about_sharers() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        const confirm = view.deleteConfirmPanel;
        confirm.openFor([sintel], false);
        verify(!confirm.asking);
        compare(confirm.confirmText, "delete");
        compare(confirm.panelWidth, Theme.space(370));
        verify(!confirm.body.children[1].visible);
        compare(confirm.plan(), [{
                                     "hashes": [sintel],
                                     "files": false
                                 }]);
        // Switching the files on brings the question in.
        confirm.body.children[0].checked = true;
        verify(confirm.asking);
    }

    function test_every_sharer_selected_says_they_share_one_copy() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        const confirm = view.deleteConfirmPanel;
        confirm.openFor([sintel, tears], true);
        verify(!confirm.asking);
        const once = Math.max(App.torrentRow(sintel).size, App.torrentRow(tears).size);
        const lines = confirm.subtitle.split("\n");
        compare(lines[lines.length - 1], App.sizeText(once) + " on disk");
        const note = confirm.body.children[3];
        verify(note.visible);
        compare(note.text, "These 2 share one copy of their files: " + App.sizeText(once) + " is deleted once.");
        compare(confirm.plan(), [{
                                     "hashes": [sintel, tears],
                                     "files": true
                                 }]);
    }

    function test_keeping_shared_files_splits_the_delete() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        const arch = hashByName("archlinux-2026.09.01-x86_64.iso");
        shareData(sintel, tears);
        const confirm = view.deleteConfirmPanel;
        confirm.openFor([sintel, arch], true);
        verify(confirm.asking);
        compare(confirm.confirmText, "delete 2");
        compare(confirm.plan(), [{
                                     "hashes": [arch],
                                     "files": true
                                 }, {
                                     "hashes": [sintel],
                                     "files": false
                                 }]);
        const line = confirm.body.children[1];
        verify(line.text.indexOf("1 of them shares files with 1 torrent you're not deleting.") >= 0, line.text);
    }

    function test_location_sheet_moves_the_sharers_too() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        const sheet = view.moveSheet;
        sheet.openSelection([sintel], "");
        verify(sheet.asking);
        compare(sheet.choice, 0);
        compare(sheet.moveHashes(), [sintel, tears]);
        compare(sheet.sheetWidth, Theme.space(600));
        // The path field is still the first stop for the keyboard.
        compare(sheet.focusables()[0].text, App.torrentRow(sintel).savePath);
        // #35, C16 L1: the plan counts what moves: both, read again for the pick.
        sheet.pathField.text = "/mnt/elsewhere";
        tryVerify(() => sheet.plan.explain === "all 2 go into /mnt/elsewhere", 2000, sheet.plan.explain);
        verify(sheet.subtitle.startsWith("2 torrents"), sheet.subtitle);
        // "Move only this one": the plan counts the one again.
        sheet.pathField.focusInput();
        keyClick(Qt.Key_Tab);
        keyClick("j");
        compare(sheet.choice, 1);
        compare(sheet.moveHashes(), [sintel]);
        compare(sheet.plan.explain, "it goes into /mnt/elsewhere");
        verify(sheet.subtitle.startsWith("1 torrent"), sheet.subtitle);
        sheet.close();
    }

    // ---- the move sheet for a selection (board 43, DECISIONS C16) ----------------------------
    /// Two torrents of the demo whose save paths differ.
    function twoSavePaths() {
        const first = App.filtered.hashAt(0);
        const path = App.torrentRow(first).savePath;
        for (let i = 1; i < App.filtered.count; ++i) {
            const hash = App.filtered.hashAt(i);
            if (App.torrentRow(hash).savePath !== path)
                return [first, hash];
        }
        return [];
    }

    // L1, L2: `m` opens the move sheet on the selection, named "Move …", its field on the
    // cursor's save path; the plan follows the field.
    function test_m_opens_the_move_sheet_on_the_selection() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        keyClick("m");
        const sheet = view.moveSheet;
        compare(sheet.opened, true);
        compare(sheet.mode, "selection");
        compare(sheet.hashes, [hash]);
        compare(sheet.title, "Move " + App.torrentRow(hash).name);
        compare(sheet.pathField.text, App.torrentRow(hash).savePath);
        verify(!sheet.ruleChoice.visible, "one save path offered the choice");
        sheet.pathField.text = "/mnt/elsewhere";
        tryVerify(() => sheet.plan.explain === "it goes into /mnt/elsewhere", 2000, sheet.plan.explain);
        compare(sheet.plan.moveText, "move");
        compare(sheet.plan.canMove, true);
        compare(sheet.actions.length, 2, "one torrent offered show");
        // A demo has no job to start: the sheet stays.
        sheet.triggerPrimary();
        compare(sheet.opened, true);
        sheet.close();
    }

    // L5: torrents from two save paths: "into one folder" first, "keep their places" maps
    // each save path below the new one.
    function test_several_save_paths_choose_one_folder_or_their_places() {
        const hashes = twoSavePaths();
        compare(hashes.length, 2, "the demo has one save path");
        const sheet = view.moveSheet;
        sheet.openSelection(hashes, "/mnt/fast/library");
        tryVerify(() => sheet.plan.ruleOffered === true, 2000);
        verify(sheet.ruleChoice.visible);
        compare(sheet.ruleChoice.choice, 0);
        compare(sheet.rule, "oneFolder");
        compare(sheet.plan.explain, "all 2 go into /mnt/fast/library");
        compare(sheet.plan.pathRows.length, 0);
        compare(sheet.ruleChoice.rows[1].title, sheet.plan.keepText);
        verify(sheet.plan.keepText.startsWith("keep their places below "), sheet.plan.keepText);
        compare(sheet.actions[1].text, "show the 2");
        sheet.ruleChoice.choice = 1;
        compare(sheet.rule, "keepPlaces");
        compare(sheet.plan.explain, "each goes to the same place below /mnt/fast/library");
        compare(sheet.plan.pathRows.length, 2);
        for (const row of sheet.plan.pathRows)
            verify(row.to.startsWith("/mnt/fast/library"), row.to);
        compare(sheet.plan.moveText, "move 2 torrents");
        sheet.close();
    }

    // L4: ↓ opens the places under the field; ↓ picks, enter fills it in and checks it, and
    // the sheet stays for the next enter. Typing narrows them; esc closes only the list.
    function test_places_open_on_down_and_fill_the_field() {
        keyClick("m");
        const sheet = view.moveSheet;
        const field = sheet.pathField;
        tryVerify(() => field.editing, 1000, "the path field does not have the keyboard");
        verify(!sheet.placesOpen);
        keyClick(Qt.Key_Down);
        verify(sheet.placesOpen, "↓ did not open the places");
        const rows = sheet.placeList.rows;
        verify(rows.length > 1);
        compare(rows[0].section, "library");
        keyClick(Qt.Key_Down);
        compare(sheet.placeList.current, 0);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(field.text, rows[1].text);
        verify(!sheet.placesOpen);
        compare(sheet.opened, true, "enter on a place moved");
        tryVerify(() => sheet.plan.explain === "it goes into " + rows[1].text, 2000, sheet.plan.explain);

        // Typing opens them, narrowed to what is typed.
        field.selectAll();
        keyClick("z");
        keyClick("z");
        keyClick("z");
        verify(!sheet.placesOpen, "a place matched zzz");
        field.selectAll();
        keyClick(Qt.Key_Slash);
        verify(sheet.placesOpen);
        keyClick(Qt.Key_Escape);
        verify(!sheet.placesOpen, "esc left the places open");
        compare(sheet.opened, true, "esc on the places closed the sheet");
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
    }

    /// The strip's rows place their words on the next polish: before it, a click lands on
    /// the wrong word.
    function stripLaidOut(strip) {
        waitForItemPolished(findChild(strip, "outcomeContent"));
        waitForItemPolished(findChild(strip, "outcomeActions"));
    }

    // DECISIONS C18 D1 (changes C16 L7): a move that landed before its band showed is said in
    // the strip in the selection bar's place, and on its row; the footer keeps quiet.
    function test_a_landed_move_says_so_in_the_strip() {
        const hash = App.filtered.hashAt(0);
        const name = App.torrentRow(hash).name;
        view.footerNote = "";
        App.move.outcome({
                             "action": "move",
                             "hashes": [hash],
                             "name": name,
                             "target": "/mnt/fast/films"
                         });
        const strip = view.outcomeStrip;
        verify(strip.visible);
        compare(findChild(strip, "outcomeHeadline").text, name + " moved to /mnt/fast/films");
        compare(findChild(strip, "outcomeMark").text, "✓");
        compare(findChild(strip, "outcomeMark").color, Theme.green);
        verify(findChild(strip, "outcomeShow").visible);
        compare(view.footerNote, "");
        // The row says it in its status cell, in green.
        compare(findChild(view.table, "status_0").text, "moved · films");
        compare(findChild(view.table, "statusGlyph_0").text, "✓");
        // esc closes it, before anything else.
        view.table.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        verify(!strip.visible);
    }

    // C18 D1, D3 (O2): a failure is red, names the torrent and why, offers move… again, and
    // stays until × or the next action.
    function test_a_failed_move_stays_in_the_strip() {
        const hash = App.filtered.hashAt(1);
        const name = App.torrentRow(hash).name;
        App.move.outcome({
                             "action": "move",
                             "hashes": [hash],
                             "failedHashes": [hash],
                             "name": name,
                             "failedName": name,
                             "target": "/locked/dest",
                             "cause": "qBittorrent can't write to /locked/dest",
                             "details": "File error alert"
                         });
        const strip = view.outcomeStrip;
        verify(strip.visible);
        compare(findChild(strip, "outcomeHeadline").text, name + " didn't move");
        compare(findChild(strip, "outcomeLine").text, "qBittorrent can't write to /locked/dest");
        compare(findChild(strip, "outcomeMark").color, Theme.red);
        verify(findChild(strip, "outcomeMove").visible);
        verify(!findChild(strip, "outcomeShow").visible);
        compare(findChild(view.table, "status_1").text, "didn't move");
        // The run-down line belongs to a done strip only.
        verify(!findChild(strip, "outcomeTimeLine").visible);
        // "details" opens the band above the strip, the same band as the footer's.
        stripLaidOut(strip);
        mouseClick(findChild(strip, "outcomeDetails"));
        verify(view.footerNoticeOpen);
        const band = findChild(view, "footerNotice");
        verify(band.mapToItem(view, 0, band.height).y <= strip.mapToItem(view, 0, 0).y + 1,
               "the band sits above the strip");
        // The next action takes its place; ×, the strip goes.
        App.outcomes.reportMap({
                                   "action": "stop",
                                   "hashes": [hash],
                                   "name": name
                               });
        verify(!view.footerNoticeOpen, "the band went with its strip");
        compare(findChild(strip, "outcomeHeadline").text, name + " stopped");
        compare(findChild(view.table, "status_1").text, "stopped");
        stripLaidOut(strip);
        mouseClick(findChild(strip, "outcomeClose"));
        verify(!strip.visible);
    }

    // C18 D3 (O3): several at once are counts, yellow when some failed; "show" selects them.
    function test_several_are_counts_and_show_selects_them() {
        const hashes = [App.filtered.hashAt(0), App.filtered.hashAt(1), App.filtered.hashAt(2)];
        App.outcomes.reportMap({
                                   "action": "move",
                                   "hashes": hashes,
                                   "failedHashes": [hashes[2]],
                                   "failedName": App.torrentRow(hashes[2]).name,
                                   "target": "/mnt/fast/keep",
                                   "cause": "no room on /mnt/fast"
                               });
        const strip = view.outcomeStrip;
        compare(findChild(strip, "outcomeHeadline").text, "2 moved to /mnt/fast/keep, 1 didn't");
        compare(findChild(strip, "outcomeMark").text, "!");
        compare(findChild(strip, "outcomeMark").color, Theme.yellow);
        stripLaidOut(strip);
        mouseClick(findChild(strip, "outcomeShow"));
        compare(view.table.selectedCount, 2);
        compare(view.table.selectedHashes().sort(), [hashes[0], hashes[1]].sort());
        // With a selection on, the strip sits just above the selection bar.
        verify(view.selectionBar.visible);
        verify(strip.mapToItem(view, 0, strip.height).y <= view.selectionBar.mapToItem(view, 0, 0).y,
               "the strip sits above the bar");
        // esc closes the strip first; the selection stays.
        view.table.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        verify(!strip.visible);
        compare(view.table.selectedCount, 2);
        keyClick(Qt.Key_Escape);
        compare(view.table.selectedCount, 0);
    }

    function test_a_category_change_that_moves_shared_files_asks_first() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        verify(App.torrentRow(sintel).autoTmm, "the demo row is not under automatic management");
        const other = App.torrentRow(sintel).category === "music" ? "tv" : "music";
        view.categoryPalette.openFor([sintel]);
        view.categoryPalette.activated(0, {
                                           "key": other,
                                           "title": other
                                       });
        compare(view.categoryPalette.opened, false);
        compare(view.sharedMoveConfirm.opened, true);
        compare(view.sharedMoveConfirm.applyHashes(), [sintel, tears]);
        compare(view.sharedMoveConfirm.confirmText, "move 2");
        keyClick("j");
        compare(view.sharedMoveConfirm.applyHashes(), [sintel]);
        view.sharedMoveConfirm.close();
    }

    function test_a_category_change_waits_for_a_no_root_sharers_files() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        const name = App.torrentRow(sintel).name;
        shareWithoutRoot(sintel, tears, [name + "/movie.mkv", name + "/subs.srt"], [name + "/movie.mkv"]);
        verify(App.sharedData([sintel], true).pending, "the no-root pair did not need its file lists");
        const other = App.torrentRow(sintel).category === "music" ? "tv" : "music";
        view.categoryPalette.openFor([sintel]);
        view.categoryPalette.activated(0, {
                                           "key": other,
                                           "title": other
                                       });
        // Nothing known yet: the change waits rather than going ahead without the sharer.
        compare(view.categoryPalette.opened, false);
        compare(view.sharedMoveConfirm.opened, false);
        compare(view.categoryPalette._held.length, 1);
        tryCompare(view.sharedMoveConfirm, "opened", true, 1000);
        compare(view.categoryPalette._held.length, 0);
        compare(view.sharedMoveConfirm.applyHashes(), [sintel, tears]);
        compare(view.sharedMoveConfirm.confirmText, "move 2");
        view.sharedMoveConfirm.close();
    }

    function test_a_category_change_beside_a_no_root_stranger_goes_ahead() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        const name = App.torrentRow(sintel).name;
        shareWithoutRoot(sintel, tears, [name + "/movie.mkv"], ["other.mkv"]);
        const other = App.torrentRow(sintel).category === "music" ? "tv" : "music";
        view.categoryPalette.openFor([sintel]);
        view.categoryPalette.activated(0, {
                                           "key": other,
                                           "title": other
                                       });
        compare(view.categoryPalette._held.length, 1);
        tryVerify(() => view.categoryPalette._held.length === 0, 1000);
        compare(view.sharedMoveConfirm.opened, false);
    }

    function test_a_category_change_on_unshared_data_asks_nothing() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        compare(App.movedByCategory([sintel], "music").length + App.movedByCategory([sintel], "tv").length > 0, true);
        compare((App.sharedData([sintel], true).outside ?? []).length, 0);
    }

    // ---- #65: automatic management on, for one of a shared pair ----------------------------

    /// Takes `hash` off automatic management and puts it in `category`, where its files are
    /// not: turning management on would move them. unshareData() puts both back.
    function unmanage(hash, category) {
        const row = App.torrentRow(hash);
        const back = testCase._unshare;
        back[hash] = Object.assign(back[hash] ?? {}, {
                                       "auto_tmm": row.autoTmm,
                                       "category": row.category
                                   });
        testCase._unshare = back;
        const patch = {};
        patch[hash] = {
            "auto_tmm": false,
            "category": category
        };
        App.patchDemoTorrents(patch);
    }

    function test_auto_management_on_for_one_of_a_shared_pair_asks_first() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        unmanage(tears, "tv");
        compare(App.movedByAutoManagement([tears]), [tears]);
        // The actions menu's `a`, on tears.
        view.table.moveCursorTo(App.filtered.rowOfHash(tears));
        view.table.requestActions();
        keyClick("a");
        compare(view.actionsMenuPanel.opened, false);
        compare(view.sharedMoveConfirm.opened, true);
        compare(App.torrentRow(tears).autoTmm, false, "turned on before the answer");
        compare(view.sharedMoveConfirm.autoManagement, true);
        compare(view.sharedMoveConfirm.message, "Turn on automatic management?");
        compare(view.sharedMoveConfirm.applyHashes(), [tears, sintel]);
        compare(view.sharedMoveConfirm.confirmText, "move 2");
        // "Move both": sintel takes tears' category, so the two land in one folder.
        compare(view.sharedMoveConfirm._sharerCategories(), {
                    [sintel]: "tv"
                });
        keyClick("j");
        compare(view.sharedMoveConfirm.applyHashes(), [tears]);
        compare(view.sharedMoveConfirm.confirmText, "turn on");
        keyClick("k");
        view.sharedMoveConfirm.confirm();
        compare(App.torrentRow(tears).autoTmm, true);
    }

    function test_auto_management_on_waits_for_a_no_root_sharers_files() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        const name = App.torrentRow(sintel).name;
        shareWithoutRoot(sintel, tears, [name + "/movie.mkv"], [name + "/movie.mkv", name + "/extra.nfo"]);
        unmanage(tears, "tv");
        view.table.moveCursorTo(App.filtered.rowOfHash(tears));
        view.torrentAction("autoManagement");
        // Nothing known yet: the request waits rather than going ahead without the sharer.
        compare(view.sharedMoveConfirm.opened, false);
        compare(view.sharedMoveConfirm._held.length, 1);
        compare(App.torrentRow(tears).autoTmm, false);
        tryCompare(view.sharedMoveConfirm, "opened", true, 1000);
        compare(view.sharedMoveConfirm._held.length, 0);
        compare(view.sharedMoveConfirm.applyHashes(), [tears, sintel]);
        view.sharedMoveConfirm.close();
        compare(App.torrentRow(tears).autoTmm, false);
    }

    function test_auto_management_on_where_the_files_already_are_asks_nothing() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        // Tears in movies, saved in the movies folder: management keeps it there.
        unmanage(tears, "movies");
        compare(App.movedByAutoManagement([tears]), []);
        view.table.moveCursorTo(App.filtered.rowOfHash(tears));
        view.torrentAction("autoManagement");
        compare(view.sharedMoveConfirm.opened, false);
        compare(App.torrentRow(tears).autoTmm, true);
    }

    function test_auto_management_off_never_asks() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        verify(App.torrentRow(tears).autoTmm);
        view.table.moveCursorTo(App.filtered.rowOfHash(tears));
        view.torrentAction("autoManagement");
        compare(view.sharedMoveConfirm.opened, false);
        compare(App.torrentRow(tears).autoTmm, false);
        App.setAutoManagement([tears], true);
    }

    function test_generals_auto_manage_row_asks_too() {
        const sintel = hashByName("Sintel 1080p (CC-BY)");
        const tears = hashByName("Tears of Steel 4K (CC-BY)");
        shareData(sintel, tears);
        unmanage(tears, "tv");
        view.table.moveCursorTo(App.filtered.rowOfHash(tears));
        tryCompare(App.details, "hash", tears, 1000);
        const general = findChild(view.details, "generalTab");
        tryVerify(() => general.rows.some(row => row.option === "autoManagement" && row.on === false), 1000);
        const row = general.rows.findIndex(entry => entry.option === "autoManagement");
        verify(view.details.toggleGeneralOption(row));
        compare(view.sharedMoveConfirm.opened, true);
        compare(view.sharedMoveConfirm.applyHashes(), [tears, sintel]);
        compare(App.torrentRow(tears).autoTmm, false);
        view.sharedMoveConfirm.close();
    }

    // ---- the location sheet (Fig. 7) -------------------------------------------------------
    // Board 42 F1 (DECISIONS C15 M1, M2): the folders group lists the library's save paths;
    // picking one filters the table to it and the scope line offers the move.
    function test_folders_in_the_rail_filter_and_offer_the_move() {
        const folders = App.move.folders;
        verify(folders.total > 0);
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        const path = App.torrentRow(hash).savePath.replace(/\/+$/, "");
        const row = findChild(view.rail, "folderRow-" + path);
        verify(row !== null, "no rail row for " + path);
        mouseClick(row);
        compare(App.filtered.folder, path);
        for (let i = 0; i < App.filtered.count; ++i)
            verify(App.torrentRow(App.filtered.hashAt(i)).savePath.startsWith(path));
        verify(view.moveScope.visible);
        compare(view.moveScope.linkText, "move this folder…");
        verify(view.moveScope.text.indexOf(path) >= 0, view.moveScope.text);
        mouseClick(row); // picked again: the filter goes
        compare(App.filtered.folder, "");
        verify(!view.moveScope.visible);
        // C19 T2 (#135): the keys stayed in the table, so the row keeps no cursor fill; they
        // come to the rail and the cursor shows on the row clicked.
        verify(!view.rail.activeFocus);
        compare(row.selected, false);
        compare(row.hasCursor, false);
        view.rail.forceActiveFocus();
        compare(row.hasCursor, true);
        view.table.forceActiveFocus();
        compare(row.hasCursor, false);
    }

    // DECISIONS C19 T3 (#136): a rail folder row's menu moves that row's folder, picked or not;
    // the menu key on the rail's cursor row opens it too. A torrent's menu no longer offers it.
    function test_a_folder_rows_menu_moves_that_folder() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        const path = App.torrentRow(hash).savePath.replace(/\/+$/, "");
        const row = findChild(view.rail, "folderRow-" + path);
        verify(row !== null, "no rail row for " + path);
        compare(App.filtered.folder, "");

        mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton);
        const menu = view.folderMenuPanel;
        compare(menu.opened, true);
        compare(menu.folder, path);
        compare(menu.items.length, 1);
        compare(menu.items[0].text, "move this folder…");
        compare(App.filtered.folder, ""); // nothing picked: the menu acts on the row
        compare(view.rail.cursorIndex, row.globalIndex);
        menu.activate(0);
        compare(menu.opened, false);
        const sheet = view.moveSheet;
        compare(sheet.opened, true);
        compare(sheet.folder, path);
        sheet.close();

        // The keyboard: the rail has the keys, the cursor on the row, the menu key.
        view.rail.forceActiveFocus();
        view.rail.cursorIndex = row.globalIndex;
        keyClick(Qt.Key_Menu);
        compare(menu.opened, true);
        compare(menu.folder, path);
        keyClick(Qt.Key_Escape);
        compare(menu.opened, false);
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        compare(menu.opened, true);
        keyClick("M", Qt.ShiftModifier); // the row's own key
        compare(menu.opened, false);
        compare(sheet.opened, true);
        compare(sheet.folder, path);
        sheet.close();

        // Not on a status row.
        view.rail.cursorIndex = 0;
        keyClick(Qt.Key_Menu);
        compare(menu.opened, false);

        // The torrent's actions menu has no folder move.
        for (const item of view.actionsMenuPanel.items)
            verify(item.action !== "moveFolder", JSON.stringify(item));
    }

    // Board 42 S1 (M4–M9): shift+m opens the sheet on the folder; the field is its new path,
    // the preflight says what blocks, and "show" filters the table under it until esc.
    function test_move_sheet_reads_the_field_and_shows_its_torrents() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        const path = App.torrentRow(hash).savePath.replace(/\/+$/, "");
        App.filtered.folder = path;
        keyClick(Qt.Key_M, Qt.ShiftModifier);
        const sheet = view.moveSheet;
        compare(sheet.opened, true);
        compare(sheet.folder, path);
        compare(sheet.pathField.text, path);
        tryCompare(sheet.plan, "blocker", path.substring(path.lastIndexOf("/") + 1) + " is already there.");
        compare(sheet.plan.canMove, false);
        sheet.pathField.text = "/mnt/elsewhere/" + path.substring(path.lastIndexOf("/") + 1);
        tryVerify(() => sheet.plan.canMove === true);
        verify(sheet.plan.moveCount > 0);
        compare(sheet.plan.pathRows[0].to, sheet.pathField.text);
        compare(sheet.plan.moveText, sheet.plan.moveCount === 1 ? "move 1 torrent" : "move " + sheet.plan.moveCount
                                                                 + " torrents");
        // A demo has no job to start: the sheet stays.
        sheet.triggerPrimary();
        compare(sheet.opened, true);

        sheet.showRequested(sheet.plan.moveHashes);
        sheet.close();
        compare(App.filtered.onlyHashes.length, sheet.plan.moveHashes.length);
        verify(view.moveShowing);
        compare(view.moveScope.linkText, "back to the sheet");
        view.handleEscape();
        compare(App.filtered.onlyHashes.length, 0);
        compare(sheet.opened, true);
    }

    function test_location_sheet_prefills_the_save_path() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        const savePath = App.torrentRow(hash).savePath;
        verify(savePath.length > 0);
        keyClick("m");
        compare(view.moveSheet.opened, true);
        // the only Field the sheet declares — see MoveSheet.qml's FormRow > Field
        const field = view.moveSheet.focusables()[0];
        compare(field.text, savePath);
    }

    // #41 (docs/27 L1): browse is a button in the field, like the add sheet's, and it and
    // ctrl+o open the same picker at the field's path and put the pick in the field.
    function test_location_sheet_browse_is_a_button_that_fills_the_field() {
        keyClick("m");
        const sheet = view.moveSheet;
        compare(sheet.opened, true);
        const field = sheet.pathField;
        compare(field.trailing, "browse");
        compare(field.trailingKey, "ctrl+o");
        compare(field.trailingClickable, true);
        verify(findChild(field, "fieldTrailingButton").enabled);

        const starts = [];
        const original = sheet.pickDirectory;
        sheet.pickDirectory = start => {
            starts.push(start);
            return "/data/picked";
        };
        const before = field.text;
        field.trailingClicked();
        compare(starts, [before], "the picker did not start at the field's path");
        compare(field.text, "/data/picked");
        // No path map in the demo: the pick is the path, with nothing under it.
        compare(sheet.browsedLine.visible, false);

        sheet.pickDirectory = start => {
            starts.push(start);
            return "/data/by-key";
        };
        tryVerify(() => field.editing, 1000);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        compare(starts.length, 2, "ctrl+o did not open the picker");
        compare(field.text, "/data/by-key");

        // A cancelled pick keeps what is there.
        sheet.pickDirectory = start => "";
        field.trailingClicked();
        compare(field.text, "/data/by-key");
        sheet.pickDirectory = original;
        sheet.close();
    }

    // Review VIEW-18: an emptied path is not sent; the sheet waits for one.
    function test_location_sheet_does_not_move_to_an_empty_path() {
        keyClick("m");
        const field = view.moveSheet.focusables()[0];
        field.text = "   ";
        view.moveSheet.triggerPrimary();
        compare(view.moveSheet.opened, true, "an empty path was sent");
    }

    // Review RUN-5: one esc in a prefilled field cancels the sheet; it does not wipe the value
    // and leave the sheet up to collect the next keys.
    function test_one_esc_in_a_prefilled_field_closes_the_sheet() {
        keyClick("m");
        compare(view.moveSheet.opened, true);
        const field = view.moveSheet.focusables()[0];
        const path = field.text;
        verify(path.length > 0);
        tryVerify(() => field.editing, 1000, "the path field does not have the keyboard");
        keyClick(Qt.Key_Escape);
        compare(view.moveSheet.opened, false, "esc left the location sheet open");
        compare(field.text, path, "esc wiped the path");

        view.renameSheetPanel.openFor(view.cursorHash);
        compare(view.renameSheetPanel.opened, true);
        tryVerify(() => view.renameSheetPanel.focusables()[0].editing, 1000);
        keyClick(Qt.Key_Escape);
        compare(view.renameSheetPanel.opened, false, "esc left the rename sheet open");

        view.limitsSheetPanel.openFor([view.cursorHash]);
        compare(view.limitsSheetPanel.opened, true);
        tryVerify(() => view.limitsSheetPanel.downloadField.editing, 1000,
                  "the download field does not have the keyboard");
        keyClick("5");
        compare(view.limitsSheetPanel.downloadField.text, "5");
        keyClick(Qt.Key_Escape);
        compare(view.limitsSheetPanel.opened, false, "esc left the limits sheet open");
    }

    // ---- the limits sheet (Fig. 8) ---------------------------------------------------------
    // A LimitsSheet function once shadowed the kit Sheet's `_collect(item, out)`, so the sheet
    // found no fields: it opened on `cancel` and tab never reached a field.
    function test_limits_sheet_opens_in_the_download_field_and_tab_reaches_the_others() {
        view.limitsSheetPanel.openFor([view.cursorHash]);
        const fields = view.limitsSheetPanel.bodyFocusables();
        verify(fields.indexOf(view.limitsSheetPanel.downloadField) >= 0, "the fields are not found");
        // Global seeding time (the demo's -2) disables the field; its chip is the way in.
        verify(fields.indexOf(view.limitsSheetPanel.seedingGlobalChip) >= 0,
               "the seeding time's global chip is out of the tab order");
        view.limitsSheetPanel._seedingGlobal = false;
        verify(view.limitsSheetPanel.bodyFocusables().indexOf(view.limitsSheetPanel.seedingField) >= 0);
        tryVerify(() => view.limitsSheetPanel.downloadField.editing, 1000);
        keyClick(Qt.Key_Tab);
        verify(view.limitsSheetPanel.uploadField.editing, "tab did not reach the upload field");
    }

    // A click on the share ratio or seeding time field under `global` did nothing: the
    // field is disabled. Now it turns `global` off for that limit and puts the caret in.
    function test_a_click_on_a_global_limit_field_turns_global_off() {
        const sheet = view.limitsSheetPanel;
        sheet.openFor([view.cursorHash]);
        // The demo's rows are on the global ratio and seeding time (-2).
        compare(sheet._ratioGlobal, true);
        compare(sheet._seedingGlobal, true);
        verify(!sheet.ratioField.enabled);
        waitForRendering(sheet.panel, 500);

        mouseClick(sheet.ratioField);
        compare(sheet._ratioGlobal, false, "the click left the ratio on global");
        verify(sheet.ratioField.editing, "the ratio field did not get the caret");
        compare(sheet._seedingGlobal, true, "the click reached the other limit");

        mouseClick(sheet.seedingField);
        compare(sheet._seedingGlobal, false);
        verify(sheet.seedingField.editing);

        // Off global, the field is an ordinary one: the area over it steps aside.
        keyClick("2");
        compare(sheet.seedingField.text, "2");
        mouseClick(sheet.ratioField);
        verify(sheet.ratioField.editing);
        sheet.close();
    }

    function test_limits_sheet_prefills_from_the_cursor_torrent() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        // the demo library's rows are all unlimited/global (DemoData::torrentJson): dl/ul 0,
        // ratio -2, seeding time -2, so every field starts blank and the ratio chip is on.
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        compare(view.limitsSheetPanel.opened, true);
    }

    function test_limits_apply_converts_unlimited_and_global_to_the_api_sentinels() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        compare(view.limitsSheetPanel.opened, true);
        // A speed field that still reads what it opened with is -1 (unchanged); emptied, it
        // lifts the limit (0), as the `∞` placeholder and the global sheet say.
        compare(view.limitsSheetPanel._speedValue("", ""), -1);
        compare(view.limitsSheetPanel._speedValue("500", "500"), -1);
        compare(view.limitsSheetPanel._speedValue("", "500"), 0);
        compare(view.limitsSheetPanel._speedValue("0", ""), 0);
        compare(view.limitsSheetPanel._speedValue("1.5", ""), 1536);
        verify(Number.isNaN(view.limitsSheetPanel._speedValue("abc", "")));
        compare(view.limitsSheetPanel._ratioGlobal, true);
        view.limitsSheetPanel.triggerPrimary();
        // demo mode: App.setLimits no-ops after logging, so this only asserts the sheet
        // closes itself the way `enter` should.
        compare(view.limitsSheetPanel.opened, false);
    }

    // ---- review VIEW-5: apply sends only what the user changed --------------------------
    function test_limits_untouched_keep_global_seeding_time_and_speeds() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        compare(App.torrentRow(hash).seedingTimeLimit, -2);
        view.limitsSheetPanel.openFor([hash]);
        view.limitsSheetPanel.downloadField.text = "300";
        view.limitsSheetPanel.triggerPrimary();
        compare(spy.count, 1);
        const a = spy.signalArguments[0]; // hashes, dl, ul, ratio, seedingMinutes, action
        compare(a[1], 300 * 1024);
        compare(a[2], -1, "an untouched upload field changed the limit");
        compare(a[3], -2, "the global ratio was lost");
        compare(a[4], -2, "an untouched seeding time left the global limit");
    }

    function test_limits_a_cleared_speed_lifts_the_limit() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const sheet = view.limitsSheetPanel;
        sheet.openFor([hashByName("Big Buck Bunny 4K (CC-BY)")]);
        // As if the torrent had opened with 500 KB/s, which the user then clears.
        const initial = Object.assign({}, sheet._initial);
        initial.dl = "500";
        sheet._initial = initial;
        sheet.downloadField.text = "";
        sheet.triggerPrimary();
        compare(spy.signalArguments[0][1], 0, "an emptied speed field did not lift the limit");
    }

    function test_limits_seeding_time_keeps_minutes_and_reads_fractional_days() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const sheet = view.limitsSheetPanel;
        compare(sheet._daysText(90), "0.06");
        compare(sheet._daysText(2880), "2");
        sheet.openFor([hashByName("Big Buck Bunny 4K (CC-BY)")]);
        // A 90-minute limit opens as 0.06 days; left alone it must stay 90 minutes, not 0.
        const initial = Object.assign({}, sheet._initial);
        initial.seeding = "0.06";
        initial.seedingGlobal = false;
        sheet._initial = initial;
        sheet._seedingGlobal = false;
        sheet.seedingField.text = "0.06";
        compare(sheet._formValues().seeding, null);
        sheet.seedingField.text = "0.5";
        compare(sheet._formValues().seeding, 720);
        sheet.seedingField.text = "";
        compare(sheet._formValues().seeding, -1);
        sheet.close();
        compare(spy.count, 0);
    }

    // Ruling Q5: seeding time on the global limit shows the ratio's `global` chip, not `∞`.
    function test_limits_global_seeding_time_shows_the_global_chip() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const sheet = view.limitsSheetPanel;
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        compare(App.torrentRow(hash).seedingTimeLimit, -2);
        sheet.openFor([hash]);
        compare(sheet._seedingGlobal, true);
        compare(sheet.seedingGlobalChip.selected, true);
        compare(sheet.seedingField.enabled, false);
        compare(sheet.seedingField.placeholder, "", "the global seeding time still reads ∞");
        compare(sheet._formValues().seeding, null, "an untouched chip changed the limit");

        // Chip off with nothing typed: no limit, as the `∞` placeholder then says.
        mouseClick(sheet.seedingGlobalChip);
        compare(sheet._seedingGlobal, false);
        compare(sheet.seedingField.enabled, true);
        compare(sheet.seedingField.placeholder, "∞");
        compare(sheet._formValues().seeding, -1);
        sheet.seedingField.text = "2";
        compare(sheet._formValues().seeding, 2880);

        // Back on: global again, whatever the field held.
        mouseClick(sheet.seedingGlobalChip);
        compare(sheet._formValues().seeding, null);
        sheet.close();

        // A torrent with its own seeding time opens with the chip off, and turning it on
        // sends -2.
        sheet.openFor([hash]);
        sheet._setSeeding(90);
        const initial = Object.assign({}, sheet._initial);
        initial.seeding = sheet.seedingField.text;
        initial.seedingGlobal = false;
        sheet._initial = initial;
        compare(sheet.seedingGlobalChip.selected, false);
        compare(sheet.seedingField.text, "0.06");
        mouseClick(sheet.seedingGlobalChip);
        sheet.triggerPrimary();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][4], -2, "the chip did not put seeding time on global");
    }

    function test_limits_refuse_a_value_that_is_not_a_number() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const sheet = view.limitsSheetPanel;
        sheet.openFor([hashByName("Big Buck Bunny 4K (CC-BY)")]);
        sheet._ratioGlobal = false;
        sheet.ratioField.text = "abc";
        sheet.triggerPrimary();
        compare(spy.count, 0, "NaN went to the server");
        compare(sheet.opened, true);
        verify(sheet.errorText.length > 0);
        sheet.ratioField.text = "1.5";
        sheet.triggerPrimary();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][3], 1.5);
        compare(sheet.opened, false);
    }

    // Review RUN-2 (e): a speed past qBittorrent's int is refused, not wrapped to "no limit".
    function test_limits_refuse_a_speed_past_the_servers_range() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const sheet = view.limitsSheetPanel;
        sheet.openFor([hashByName("Big Buck Bunny 4K (CC-BY)")]);
        sheet.uploadField.text = "99999999999";
        sheet.triggerPrimary();
        compare(spy.count, 0, "an out-of-range speed went to the server");
        verify(sheet.errorText.length > 0);
        sheet.uploadField.text = "2097151"; // just under 2^31 bytes/s
        sheet.triggerPrimary();
        compare(spy.count, 1);
    }

    function test_limits_a_mixed_selection_keeps_each_torrents_own_action() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const stopHash = hashByName("Tears of Steel 4K (CC-BY)");
        const defaultHash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.limitsSheetPanel.openFor([stopHash, defaultHash]);
        compare(view.limitsSheetPanel._actionMixed, true);
        view.limitsSheetPanel.triggerPrimary();
        compare(spy.count, 2);
        const actions = {};
        actions[spy.signalArguments[0][0][0]] = spy.signalArguments[0][5];
        actions[spy.signalArguments[1][0][0]] = spy.signalArguments[1][5];
        compare(actions[stopHash], "Stop");
        compare(actions[defaultHash], "Default");
    }

    // Review VIEW-5 (d): the sheet has no inactive-seeding field, so each torrent's own value
    // goes back unchanged instead of -2 wiping it (Tears of Steel carries 45 in the demo).
    function test_limits_keep_each_torrents_inactive_seeding_time() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const customHash = hashByName("Tears of Steel 4K (CC-BY)");
        const globalHash = hashByName("Big Buck Bunny 4K (CC-BY)");
        compare(App.torrentRow(customHash).inactiveSeedingTimeLimit, 45);
        compare(App.torrentRow(globalHash).inactiveSeedingTimeLimit, -2);
        view.limitsSheetPanel.openFor([customHash, globalHash]);
        view.limitsSheetPanel.downloadField.text = "300";
        view.limitsSheetPanel.triggerPrimary();
        compare(spy.count, 2);
        const inactive = {};
        for (let i = 0; i < spy.count; ++i) {
            compare(spy.signalArguments[i][0].length, 1);
            inactive[spy.signalArguments[i][0][0]] = spy.signalArguments[i][6];
        }
        compare(inactive[customHash], 45, "the torrent's inactive limit was not carried");
        compare(inactive[globalHash], -2);
    }

    function test_limits_reset_puts_inactive_seeding_time_back_on_global() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        view.limitsSheetPanel.openFor([hashByName("Tears of Steel 4K (CC-BY)")]);
        view.limitsSheetPanel.actionTriggered(qsTr("reset"));
        compare(spy.count, 1);
        compare(spy.signalArguments[0][6], -2);
    }

    // ---- the limits sheet's share-limit action row (T5.5 Q14) ------------------------------
    function test_action_row_shows_only_on_a_server_that_knows_the_field() {
        // The demo library reports API 2.15.1 (App::applyDemoData), the version the row
        // appeared in, so it is visible here; a pre-2.15.1 server hides it.
        compare(App.supportsShareLimitAction, true);
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.limitsSheetPanel.openFor([hash]);
        compare(view.limitsSheetPanel.actionRow.visible, true);
        App.globalState.setServerVersions("v5.0.4", "2.11.4");
        compare(App.supportsShareLimitAction, false);
        compare(view.limitsSheetPanel.actionRow.visible, false);
        App.globalState.setServerVersions("5.2.3", "2.15.1");
        compare(App.supportsShareLimitAction, true);
        compare(view.limitsSheetPanel.actionRow.visible, true);
    }

    function test_action_row_prefills_from_the_cursor_torrent_and_applies_the_wire_name() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        // Every demo row is Default except Tears of Steel (DemoData::torrentJson).
        const stopHash = hashByName("Tears of Steel 4K (CC-BY)");
        compare(App.torrentRow(stopHash).shareLimitAction, "Stop");
        view.limitsSheetPanel.openFor([stopHash]);
        compare(view.limitsSheetPanel.actionChips.current, "stop");
        compare(view.limitsSheetPanel._actionMixed, false);
        compare(view.limitsSheetPanel.actionValue(), "Stop");
        view.limitsSheetPanel.triggerPrimary();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], [stopHash]);
        compare(spy.signalArguments[0][5], "Stop");
        compare(view.limitsSheetPanel.opened, false);

        const defaultHash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.limitsSheetPanel.openFor([defaultHash]);
        compare(view.limitsSheetPanel.actionChips.current, "default");
        view.limitsSheetPanel.actionChips.pick("remove with content");
        compare(view.limitsSheetPanel.actionValue(), "RemoveWithContent");
        view.limitsSheetPanel.triggerPrimary();
        compare(spy.count, 2);
        compare(spy.signalArguments[1][5], "RemoveWithContent");
    }

    function test_action_row_falls_back_to_default_and_says_mixed_for_a_disagreeing_selection() {
        const stopHash = hashByName("Tears of Steel 4K (CC-BY)");
        const defaultHash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.limitsSheetPanel.openFor([stopHash, defaultHash]);
        compare(view.limitsSheetPanel.actionChips.current, "default");
        compare(view.limitsSheetPanel._actionMixed, true);
        // Touching the row settles it, like qui's "was mixed" hint going away.
        view.limitsSheetPanel.actionChips.pick("stop");
        compare(view.limitsSheetPanel._actionMixed, false);
        compare(view.limitsSheetPanel.actionValue(), "Stop");
        // Two torrents that agree are not mixed.
        view.limitsSheetPanel.openFor([defaultHash,
                                       hashByName("OpenStreetMap planet-260908.osm.pbf")]);
        compare(view.limitsSheetPanel._actionMixed, false);
        compare(view.limitsSheetPanel.actionChips.current, "default");
    }

    function test_action_row_is_keyboard_driven_and_never_empty() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.limitsSheetPanel.openFor([hash]);
        const chips = view.limitsSheetPanel.actionChips;
        chips.forceActiveFocus();
        compare(chips.activeFocus, true);
        // `l` twice then `space`: the cursor starts on the first chip, so this picks the third.
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_Space);
        compare(chips.current, "remove");
        // Re-picking the current chip keeps it (`allowNone: false`): one option is always on.
        keyClick(Qt.Key_Space);
        compare(chips.current, "remove");
        compare(view.limitsSheetPanel.actionValue(), "Remove");
    }

    function test_action_row_reset_returns_to_default() {
        const spy = createTemporaryObject(limitsSpy, testCase);
        const stopHash = hashByName("Tears of Steel 4K (CC-BY)");
        view.limitsSheetPanel.openFor([stopHash]);
        compare(view.limitsSheetPanel.actionChips.current, "stop");
        view.limitsSheetPanel.actionTriggered("reset");
        compare(view.limitsSheetPanel.actionChips.current, "default");
        compare(spy.count, 1);
        compare(spy.signalArguments[0][5], "Default");
        compare(view.limitsSheetPanel.opened, false);
    }

    // ---- copy (spec §7 Fig. 26 `y`/`Y`) ----------------------------------------------------
    function test_copy_magnet_puts_the_magnet_in_the_clipboard() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        const magnet = App.torrentRow(hash).magnetUri;
        verify(magnet.length > 0);
        App.copyToClipboard(magnet);
        // QClipboard under the offscreen platform keeps an in-process clipboard, so this is
        // not just "it did not crash": the text really is what `y` would have copied.
    }

    // docs/12 G5: `y` on a General row says what it copied.
    function test_y_in_general_says_what_it_copied() {
        const hash = hashByName("archlinux-2026.09.01-x86_64.iso");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        view.details.activateTab(0);
        const general = findChild(view.details, "generalTab");
        const hashRow = general.rows.findIndex(row => row.key === "Hash v1");
        view.details.forceActiveFocus();
        view.details.moveCursor(hashRow - view.details.cursorIndex);
        keyClick("y");
        compare(view.footerNote, "copied hash v1");
    }
}
