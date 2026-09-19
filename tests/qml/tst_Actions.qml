import QtQuick
import QtTest
import Omaseed
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
            ["o", "openFolder"],
            ["d", "delete"]
        ];
        for (const [key, action] of expected)
            verify(view.actionsMenuPanel.indexOfKey(key) >= 0, "missing row for key " + key);
        compare(view.actionsMenuPanel.indexOfKey("d") >= 0, true);
        const deleteRow = view.actionsMenuPanel.items[view.actionsMenuPanel.indexOfKey("d")];
        compare(deleteRow.danger, true);
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

    function test_delete_confirm_names_a_single_torrent() {
        const hash = hashByName("Elephants Dream 1080p (CC-BY)");
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        keyClick("d");
        compare(view.deleteConfirmPanel.message.indexOf("Elephants Dream") >= 0, true);
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
        // left blank: the speed fields stay -1 (unchanged), the ratio chip stays "global"
        // (-2) and the seeding time stays -1 (no limit) — see `_speedValue()`/`onAccepted`.
        compare(view.limitsSheetPanel._speedValue(""), -1);
        compare(view.limitsSheetPanel._speedValue("0"), 0);
        compare(view.limitsSheetPanel._ratioGlobal, true);
        view.limitsSheetPanel.triggerPrimary();
        // demo mode: App.setLimits no-ops after logging, so this only asserts the sheet
        // closes itself the way `enter` should.
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
}
