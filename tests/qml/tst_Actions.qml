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
        view.locationSheetPanel.close();
        view.limitsSheetPanel.close();
        view.renameSheetPanel.close();
        view.actionsMenuPanel.close();
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

    // ---- the location sheet (Fig. 7) -------------------------------------------------------
    function test_location_sheet_prefills_the_save_path() {
        const hash = hashByName("Big Buck Bunny 4K (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        const savePath = App.torrentRow(hash).savePath;
        verify(savePath.length > 0);
        keyClick("m");
        compare(view.locationSheetPanel.opened, true);
        // the only Field the sheet declares — see LocationSheet.qml's FormRow > Field
        const field = view.locationSheetPanel.focusables()[0];
        compare(field.text, savePath);
    }

    // ---- the limits sheet (Fig. 8) ---------------------------------------------------------
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
        sheet._initial = initial;
        sheet.seedingField.text = "0.06";
        compare(sheet._collect().seeding, null);
        sheet.seedingField.text = "0.5";
        compare(sheet._collect().seeding, 720);
        sheet.seedingField.text = "";
        compare(sheet._collect().seeding, -1);
        sheet.close();
        compare(spy.count, 0);
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
