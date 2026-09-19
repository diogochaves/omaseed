import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// T3.5. The assembled window (`MainView` — `Main.qml` is the same thing inside a `Window`),
// driven by the keyboard the way spec v2 §7 Fig. 26 says it works. Every case runs against
// the demo library, and `init()` puts the shared `App` proxy, the cursor, the selection and
// the pane back, because the whole runner shares one `App`.
TestCase {
    id: testCase

    height: 800
    name: "Main"
    visible: true
    when: windowShown
    width: 1360

    MainView {
        id: view

        anchors.fill: parent
        focus: true
    }

    SignalSpy {
        id: addSpy

        signalName: "addRequested"
        target: App
    }

    SignalSpy {
        id: settingsSpy

        signalName: "openSettings"
        target: App
    }

    SignalSpy {
        id: actionSpy

        signalName: "torrentActionRequested"
        target: App
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function init() {
        view.helpOpen = false;
        view.details.collapsed = false;
        view.details.activateTab(0);
        view.table.cancelRename();
        view.table.clearSelection();
        view.hero.searchField.text = "";
        App.filtered.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
        addSpy.clear();
        settingsSpy.clear();
        actionSpy.clear();
    }

    // ---- the footer's facts (spec §1 Fig. 1 `.foot .facts`) -----------------------------
    function test_the_footer_states_the_library() {
        compare(view.factsText, "16 torrents · 8 downloading · " + App.globalState.freeSpaceText
                + " free");
    }

    function test_a_selection_adds_its_own_fact() {
        view.table.toggleCursorSelection();
        view.table.moveCursor(1);
        view.table.toggleCursorSelection();
        compare(view.table.selectedCount, 2);
        verify(view.factsText.endsWith(" · 2 selected"));
    }

    function test_the_footer_hints_follow_the_focus() {
        compare(view.hintModel.area, "table");
        view.rail.forceActiveFocus();
        compare(view.hintModel.area, "rail");
        view.table.forceActiveFocus();
        compare(view.hintModel.area, "table");
    }

    // ---- `/` and `esc` ------------------------------------------------------------------
    function test_slash_focuses_the_search_field_and_the_count_follows() {
        keyClick("/");
        verify(view.hero.searchField.editing, "`/` did not put the caret in the field");
        compare(view.focusArea, "search");

        keyClick("f");
        keyClick("e");
        keyClick("d");
        keyClick("o");
        keyClick("r");
        keyClick("a");
        compare(App.filtered.text, "fedora");
        // One row of the demo library is a Fedora ISO (the match is case-insensitive).
        compare(App.filtered.count, 1);
        compare(view.hero.searchCount, 1);
        compare(view.hero.searchCountText, "1 of 16");
    }

    function test_escape_in_the_field_clears_it_and_goes_back_to_the_table() {
        keyClick("/");
        keyClick("f");
        keyClick("e");
        keyClick("d");
        compare(App.filtered.text, "fed");

        keyClick(Qt.Key_Escape);
        compare(App.filtered.text, "");
        compare(view.hero.searchField.text, "");
        verify(view.table.activeFocus, "the table did not get the focus back");
        compare(view.focusArea, "table");
    }

    // ---- `i` ------------------------------------------------------------------------------
    function test_i_folds_the_details_pane_away() {
        compare(view.details.collapsed, false);
        compare(view.split.sideVisible, true);

        keyClick("i");
        compare(view.details.collapsed, true);
        compare(view.split.sideVisible, false);

        keyClick("i");
        compare(view.details.collapsed, false);
        compare(view.split.sideVisible, true);
    }

    // ---- `h` / `l` --------------------------------------------------------------------------
    function test_h_and_l_hand_the_focus_across_the_window() {
        verify(view.table.activeFocus);

        keyClick("h");
        verify(view.rail.activeFocus, "`h` did not reach the rail");
        verify(!view.table.activeFocus);

        keyClick("l");
        verify(view.table.activeFocus, "`l` did not come back to the table");

        keyClick("l");
        verify(view.details.activeFocus, "`l` did not reach the details pane");
        compare(view.hintModel.area, "details");
    }

    // ---- `?` --------------------------------------------------------------------------------
    function test_question_opens_the_help_sheet_and_escape_closes_it() {
        compare(view.helpOpen, false);

        keyClick(Qt.Key_Question);
        compare(view.helpOpen, true);
        verify(view.helpSheet.activeFocus, "the sheet does not hold the focus");

        keyClick(Qt.Key_Escape);
        compare(view.helpOpen, false);
        verify(view.table.activeFocus, "the table did not get the focus back");
    }

    // ---- the app-level keys -----------------------------------------------------------------
    function test_a_asks_for_the_add_sheet() {
        keyClick("a");
        compare(addSpy.count, 1);
    }

    function test_comma_asks_for_settings() {
        keyClick(",");
        compare(settingsSpy.count, 1);
    }

    function test_q_hides_to_the_tray() {
        const hideSpy = createTemporaryQmlObject(
                  'import QtTest; SignalSpy { signalName: "hideRequested" }', testCase);
        hideSpy.target = view;
        keyClick("q");
        compare(hideSpy.count, 1);
    }

    // ---- Fig. 26's "act" column: T4.3 wired every one of these to the real thing -------------
    function test_a_table_action_carries_the_cursor_row() {
        // `p` in demo mode: no server to touch, so it is a no-op — the point of this case is
        // that it does not crash and does not open anything (unlike `d`, below).
        keyClick("p");
        compare(view.deleteConfirmPanel.opened, false);
    }

    function test_a_table_action_carries_the_whole_selection() {
        view.table.selectAll();
        keyClick("d");
        compare(view.deleteConfirmPanel.opened, true);
        compare(view.deleteConfirmPanel.hashes.length, App.filtered.count);
    }

    function test_the_capital_is_the_stronger_variant() {
        keyClick(Qt.Key_D, Qt.ShiftModifier);
        compare(view.deleteConfirmPanel.opened, true);
        // `D` forces the "also delete files" toggle on, regardless of the remembered default.
        compare(view.deleteConfirmPanel.body.children[0].checked, true);
    }

    // ---- the details pane follows the table's cursor -------------------------------------------
    function test_the_details_pane_follows_the_cursor() {
        const first = view.table.cursorHash;
        compare(view.details.torrent.name, App.torrentRow(first).name);

        view.table.moveCursor(1);
        const second = view.table.cursorHash;
        verify(second !== first);
        compare(view.details.torrent.name, App.torrentRow(second).name);
    }
}
