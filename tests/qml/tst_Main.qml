import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
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
        id: quitSpy

        signalName: "quitRequested"
        target: view
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
        // The add sheet prefills itself from the real clipboard, which another test file may
        // have filled with a magnet (`y` copies one): prose parses as nothing, so the sheet
        // opens empty and its first `esc` closes it instead of clearing the field.
        App.copyToClipboard("omaseed test: nothing to add here");
        // Every overlay: a case that leaves one up would swallow the next case's keys, since
        // the whole file shares one `MainView`.
        view.addSheet.dismiss();
        view.duplicateSheet.close();
        view.settingsModal.close();
        view.actionsMenuPanel.close();
        view.deleteConfirmPanel.close();
        view.locationSheetPanel.close();
        view.limitsSheetPanel.close();
        view.renameSheetPanel.close();
        view.jumpPalette.close();
        view.categoryPalette.close();
        view.tagPalette.close();
        view.sortPalette.close();
        view.columnsPalette.close();
        view.moreMenu.close();
        view.globalLimitsSheet.close();
        view.helpOpen = false;
        view._testServerLost = false;
        App.railHidden = false;
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
    // Seven, not the rail's eight: the demo's stopped download is not "downloading" here.
    function test_the_footer_states_the_library() {
        compare(App.counts.downloading, 8);
        compare(view.factsText, "16 torrents · 7 downloading · " + App.globalState.freeSpaceText
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

    // Review RUN-12: enter keeps the filter and hands the keyboard back to the table.
    function test_enter_in_the_field_keeps_the_filter_and_goes_back_to_the_table() {
        keyClick("/");
        keyClick("f");
        keyClick("e");
        keyClick("d");
        keyClick(Qt.Key_Return);
        compare(App.filtered.text, "fed");
        verify(view.table.activeFocus, "the caret stayed in the filter");
        compare(view.focusArea, "table");
        keyClick("j");
        compare(App.filtered.text, "fed", "j was typed into the filter");
        view.clearFilter();
    }

    // docs/13: under the table the pane starts 260 tall, room for the band over the tabs.
    function test_the_bottom_pane_defaults_to_two_hundred_and_sixty() {
        compare(view.split.defaultSideHeight, Theme.space(260));
        compare(view.split.sideHeight, Theme.space(260));
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

    // docs/08 §4: typing filters the sheet, the first match has the cursor, enter runs it.
    function test_the_help_sheet_filters_and_runs_the_match() {
        keyClick(Qt.Key_Question);
        compare(view.helpOpen, true);
        keyClick("c");
        keyClick("o");
        keyClick("l");
        compare(view.helpSheet.query, "col");
        verify(view.helpSheet.matchedRows.length > 0);
        compare(view.helpSheet.cursorRow.action, "columns");

        keyClick(Qt.Key_Return);
        compare(view.helpOpen, false);
        compare(view.columnsPalette.opened, true);

        // A new opening starts on the whole map again.
        view.columnsPalette.close();
        keyClick(Qt.Key_Question);
        compare(view.helpSheet.query, "");
        compare(view.helpSheet.cursorRow, null);
        keyClick(Qt.Key_Escape);
        compare(view.helpOpen, false);
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

    // Each jump starts empty: the last query used to stay, so the next name was appended to it.
    function test_the_jump_palette_opens_with_an_empty_query() {
        keyClick(Qt.Key_K, Qt.ControlModifier);
        keyClick("f");
        keyClick("e");
        compare(view.jumpPalette.query, "fe");
        keyClick(Qt.Key_Return);
        compare(view.jumpPalette.opened, false);
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(view.jumpPalette.query, "");
    }

    // The details pane takes bare letters (`k` moves its cursor), but ctrl chords still
    // reach the window: `ctrl+k` from the pane opens jump.
    function test_ctrl_chords_pass_through_the_details_pane() {
        view.details.forceActiveFocus();
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(view.jumpPalette.opened, true, "ctrl+k in the details pane did not open jump");
    }

    // docs/08 §4: `ctrl+,` is the desktop's settings key, so it opens them too.
    function test_ctrl_comma_asks_for_settings() {
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        compare(settingsSpy.count, 1);
        compare(view.columnsPalette.opened, false);
    }

    // docs/10 §2: once the hero is too narrow for the gear and `?`, they lead the ⋯ menu,
    // and their rows run what the buttons did.
    function test_a_narrow_hero_moves_settings_and_help_into_the_more_menu() {
        const actions = () => view.moreMenu.items.map(item => item.action);
        verify(actions().indexOf("settings") < 0);
        testCase.width = Theme.space(800); // the view fills the case, the hero spans the view
        verify(view.hero.compact);
        compare(actions().slice(0, 2), ["settings", "help"]);
        view.moreAction("settings");
        compare(settingsSpy.count, 1);
        view.moreAction("help");
        compare(view.helpOpen, true);
        view.helpOpen = false;
        testCase.width = 1360;
    }

    // docs/08 §3: ctrl+. opens the ⋯ menu under its button; its rows reach the limits sheet,
    // the columns palette and select-all.
    function test_ctrl_period_opens_the_more_menu_and_its_rows_run() {
        keyClick(Qt.Key_Period, Qt.ControlModifier);
        compare(view.moreMenu.opened, true);
        compare(view.activeOverlay, view.moreMenu);

        view.moreAction("altLimits");
        compare(view.moreMenu.opened, false);
        compare(view.globalLimitsSheet.opened, true);
        // The demo answers with its own alt limits (1 MB/s, 200 KB/s).
        tryCompare(view.globalLimitsSheet, "loaded", true);
        compare(view.globalLimitsSheet.altDownloadField.text, "1024");
        compare(view.globalLimitsSheet.downloadField.text, "");
        // Batch-2 D2: opened from "alt-speed limits…", the sheet says so.
        compare(view.globalLimitsSheet.title, "Alt-speed limits");
        verify(view.globalLimitsSheet.subtitle.indexOf("alt-speed is on") >= 0);
        view.globalLimitsSheet.close();

        view.moreAction("globalLimits");
        compare(view.globalLimitsSheet.opened, true);
        tryCompare(view.globalLimitsSheet, "loaded", true);
        compare(view.globalLimitsSheet.title, "Speed limits");
        compare(view.globalLimitsSheet.subtitle, "the server's global limits");
        view.globalLimitsSheet.close();

        view.moreAction("selectAll");
        compare(view.table.selectedHashes().length, App.filtered.count);
    }

    // docs/08 §5: ctrl+b hides the rail; its filters then show as chips in the hero, and a
    // chip's ✕ clears that filter. `h` from the table brings the rail back.
    function test_ctrl_b_hides_the_rail_and_its_filters_become_chips() {
        App.filtered.statusFilter = TorrentFilterModel.Downloading;
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(App.railHidden, true);
        compare(view.rail.visible, false);
        compare(view.activeFilterChips.length, 1);
        compare(view.activeFilterChips[0].key, "status");

        view.clearOneFilter("status");
        compare(App.filtered.statusFilter, TorrentFilterModel.All);
        compare(view.activeFilterChips.length, 0);

        view.table.forceActiveFocus();
        keyClick("h");
        compare(App.railHidden, false);
        verify(view.rail.activeFocus);
    }

    function test_pipe_opens_the_columns_palette() {
        keyClick(Qt.Key_Bar, Qt.ShiftModifier);
        compare(view.columnsPalette.opened, true);
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

    // ---- overlays are modal, and give the keyboard back (T4.8) -------------------------------
    function test_an_overlay_keeps_the_windows_keys_to_itself() {
        keyClick("s"); // the sort palette
        compare(view.sortPalette.opened, true);
        compare(view.overlayOpen, true);
        compare(view.activeOverlay, view.sortPalette);

        // `Kit/Palette` leaves ctrl chords and `i` unaccepted; they used to travel on to the
        // window and open a second overlay underneath the first.
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(view.jumpPalette.opened, false, "ctrl+k opened a palette under the palette");
        const collapsed = view.details.collapsed;
        keyClick("i");
        compare(view.details.collapsed, collapsed, "`i` reached the window under the palette");

        // `i` was typed into the query: the first esc clears it (review RUN-11), the second closes.
        keyClick(Qt.Key_Escape);
        compare(view.sortPalette.query, "");
        keyClick(Qt.Key_Escape);
        compare(view.sortPalette.opened, false);
        compare(view.overlayOpen, false);
    }

    function test_escape_closes_the_topmost_overlay_only() {
        App.openSettings();
        compare(view.settingsModal.opened, true);

        // A modal keeps the window's keys: `a` must not open the add sheet underneath it.
        addSpy.clear();
        keyClick("a");
        compare(addSpy.count, 0);
        compare(view.addSheet.opened, false);

        // A hand-off from the browser or the tray does stack over it, and is drawn on top.
        App.addRequested();
        compare(view.addSheet.opened, true);
        verify(view.addSheet.z > view.settingsModal.z);
        compare(view.activeOverlay, view.addSheet);

        keyClick(Qt.Key_Escape);
        compare(view.addSheet.opened, false);
        compare(view.settingsModal.opened, true, "esc closed more than the topmost overlay");
        compare(view.activeOverlay, view.settingsModal);

        keyClick(Qt.Key_Escape);
        compare(view.settingsModal.opened, false);
        compare(view.overlayOpen, false);
        verify(view.table.activeFocus, "the table did not get the keyboard back");
    }

    /// Open the overlay `open()` opens, close it with `esc`, and check `j` moves the table's
    /// cursor straight afterwards. `escapes` is 2 for a sheet whose field opens prefilled:
    /// Fig. 26's escape ladder clears the field first and closes the sheet second.
    function _checkFocusComesBack(open, overlay, what, escapes) {
        const row = view.table.cursorIndex;
        open();
        compare(overlay.opened, true, what + " did not open");
        for (let i = 0; i < (escapes === undefined ? 1 : escapes); ++i)
            keyClick(Qt.Key_Escape);
        compare(overlay.opened, false, what + " did not close on esc");
        verify(view.table.activeFocus, what + " did not give the keyboard back");
        keyClick("j");
        compare(view.table.cursorIndex, row + 1, "`j` did not move after " + what);
        view.table.moveCursorTo(row);
    }

    function test_every_overlay_gives_the_keyboard_back_to_the_table() {
        testCase._checkFocusComesBack(() => keyClick(Qt.Key_Return), view.actionsMenuPanel,
                                      "the actions menu");
        testCase._checkFocusComesBack(() => keyClick("s"), view.sortPalette, "the sort palette");
        testCase._checkFocusComesBack(() => keyClick(Qt.Key_K, Qt.ControlModifier),
                                      view.jumpPalette, "the jump palette");
        testCase._checkFocusComesBack(() => keyClick("d"), view.deleteConfirmPanel,
                                      "the delete confirm");
        // The location sheet opens with the torrent's save path already in its field.
        testCase._checkFocusComesBack(() => keyClick("m"), view.locationSheetPanel,
                                      "the location sheet", 2);
        testCase._checkFocusComesBack(() => keyClick(Qt.Key_C, Qt.ShiftModifier),
                                      view.categoryPalette, "the category palette");
        testCase._checkFocusComesBack(() => keyClick("t"), view.tagPalette, "the tag palette");
        testCase._checkFocusComesBack(() => App.addRequested(), view.addSheet, "the add sheet");
        testCase._checkFocusComesBack(() => App.openSettings(), view.settingsModal,
                                      "the settings modal");
    }

    // Batch-2 D3: with a sheet, palette, modal or menu up, the window footer keeps its facts
    // and prints no hints, so nothing repeats the overlay's own row or contradicts it; the
    // table's hints come back when it closes.
    function _checkFooterStandsAside(open, overlay, what) {
        open();
        compare(overlay.opened, true, what + " did not open");
        compare(view.hintModel.area, "overlay", what);
        compare(view.hintModel.hints.length, 0, "the footer still teaches keys under " + what);
        compare(view.hintModel.pinned, null, what);
        verify(view.factParts.length > 0, "the facts went with " + what);
        overlay.close();
        compare(view.hintModel.area, "table", what);
        verify(view.hintModel.hints.length > 0, "the hints did not come back after " + what);
        verify(view.hintModel.pinned !== null, "`? help` did not come back after " + what);
    }

    function test_the_window_footer_stands_aside_for_every_overlay() {
        testCase._checkFooterStandsAside(() => App.addRequested(), view.addSheet, "the add sheet");
        testCase._checkFooterStandsAside(() => keyClick("L", Qt.ShiftModifier),
                                         view.limitsSheetPanel, "the limits sheet");
        testCase._checkFooterStandsAside(() => keyClick("m"), view.locationSheetPanel,
                                         "the location sheet");
        testCase._checkFooterStandsAside(() => App.openSettings(), view.settingsModal,
                                         "the settings modal");
        // The help sheet is driven by `helpOpen`; this stands in for its panel.
        const help = {
            get opened() {
                return view.helpOpen;
            },
            close() {
                view.helpOpen = false;
            }
        };
        testCase._checkFooterStandsAside(() => view.helpOpen = true, help, "the help sheet");
        testCase._checkFooterStandsAside(() => keyClick("s"), view.sortPalette,
                                         "the sort palette");
        testCase._checkFooterStandsAside(() => keyClick(Qt.Key_K, Qt.ControlModifier),
                                         view.jumpPalette, "the jump palette");
        testCase._checkFooterStandsAside(() => keyClick("d"), view.deleteConfirmPanel,
                                         "the delete confirm");
        testCase._checkFooterStandsAside(() => keyClick(Qt.Key_Return), view.actionsMenuPanel,
                                         "the actions menu");
        testCase._checkFooterStandsAside(() => view.moreMenu.open(), view.moreMenu, "the ⋯ menu");
        testCase._checkFooterStandsAside(() => view.globalLimitsSheet.openFor(true),
                                         view.globalLimitsSheet, "the speed limits sheet");
    }

    // The Files tab's own menus (N1) are overlays too, as far as the footer goes.
    function test_the_window_footer_stands_aside_for_the_files_menus() {
        view.details.currentTab = 1;
        view.details.openPriorityMenu();
        tryVerify(() => view.details.menuOpen, 1000, "the priority menu did not open");
        compare(view.hintModel.area, "overlay");
        compare(view.hintModel.hints.length, 0, "the footer still teaches keys under the menu");
        verify(view.factParts.length > 0, "the facts went with the menu");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !view.details.menuOpen, 1000, "esc did not close the priority menu");
        verify(view.hintModel.area !== "overlay", "the footer stayed in overlay mode");
        view.details.currentTab = 0;
    }

    // ---- one Connections block, and the jump palette's `tab` (T4.8) --------------------------
    function test_show_actions_requested_opens_the_menu_on_that_row() {
        const hash = App.filtered.hashAt(3);
        verify(hash.length > 0);
        verify(view.table.cursorHash !== hash);

        // What `JumpPalette`'s `tab` raises once it has closed itself.
        App.showActionsRequested(hash);

        compare(view.table.cursorHash, hash, "the cursor did not land on the row");
        compare(view.actionsMenuPanel.opened, true, "the actions menu did not open");
        // Fig. 11's first row reads "start" or "stop" for that very torrent.
        compare(view.actionsMenuPanel.cursorStopped, App.torrentRow(hash).stopped === true);

        keyClick(Qt.Key_Escape);
        compare(view.actionsMenuPanel.opened, false);
        verify(view.table.activeFocus, "the table did not get the keyboard back");
    }

    function test_show_torrent_requested_moves_the_cursor_once() {
        const hash = App.filtered.hashAt(2);
        App.showTorrentRequested(hash);
        compare(view.table.cursorHash, hash);
    }

    function test_show_torrent_requested_clears_a_filter_that_hides_the_row() {
        const hash = App.filtered.hashAt(2);
        App.filtered.text = "archlinux";
        verify(App.filtered.rowOfHash(hash) < 0 || App.filtered.hashAt(0) === hash);

        App.showTorrentRequested(hash);
        compare(App.filtered.text, "");
        compare(view.table.cursorHash, hash);
    }

    // ---- ruling Q12: ctrl+q quits from the Add and Duplicate sheets, like every overlay -------
    function test_ctrl_q_quits_from_the_add_and_duplicate_sheets() {
        const quit = quitSpy;
        quit.clear();

        App.addRequested();
        verify(view.addSheet.opened);
        compare(quit.count, 0);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(quit.count, 1, "ctrl+q did not quit from the add sheet");
        view.addSheet.dismiss();

        view.duplicateSheet.openFor(App.torrentRow(App.filtered.hashAt(0)));
        verify(view.duplicateSheet.opened);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(quit.count, 2, "ctrl+q did not quit from the duplicate sheet");
        view.duplicateSheet.close();
    }

    // ---- the add sheet's `…` chips open T4.4's palettes over it (T4.8) ------------------------
    function test_the_category_chip_opens_the_palette_over_the_sheet_and_comes_back() {
        App.addRequested();
        verify(view.addSheet.opened, "`a` did not open the add sheet");

        const chips = view.addSheet.categoryChips;
        chips.cursorIndex = chips.options.length; // the `…` chip
        chips.activateCursor();
        verify(view.categoryPalette.opened, "the `…` chip did not open the palette");
        verify(view.categoryPalette.choiceMode, "the palette is applying to torrents");
        verify(view.categoryPalette.z > view.addSheet.z, "the palette is drawn under the sheet");
        verify(view.addSheet.opened, "the sheet went away under the palette");

        view.categoryPalette.query = "movies";
        const row = view.categoryPalette.items.find(item => item.key === "movies");
        view.categoryPalette.activated(view.categoryPalette.items.indexOf(row), row);

        verify(!view.categoryPalette.opened);
        compare(view.addSheet.categoryChips.current, "movies");
        verify(view.addSheet.opened, "the sheet closed with the palette");
        view.addSheet.dismiss();
    }

    function test_the_tags_chip_opens_the_tag_palette_with_what_the_sheet_carries() {
        App.addRequested();
        view.addSheet.tagChips.pick("keep");

        const chips = view.addSheet.tagChips;
        chips.cursorIndex = chips.options.length;
        chips.activateCursor();
        verify(view.tagPalette.opened);
        const keep = view.tagPalette.items.find(item => item.key === "keep");
        compare(keep.checked, true, "the sheet's own chip is not checked in the palette");

        const other = view.tagPalette.items.find(item => item.key === "private");
        view.tagPalette.toggled(view.tagPalette.items.indexOf(other), other);
        view.tagPalette.activated(0, view.tagPalette.items[0]);

        compare(view.addSheet.tagChips.selected.length, 2);
        verify(view.addSheet.tagChips.selected.indexOf("private") >= 0);
        view.addSheet.dismiss();
    }

    // ---- the details pane follows the table's cursor -------------------------------------------
    /// Puts the table's cursor on the demo row called `name`.
    function _cursorOn(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            if (App.torrentRow(App.filtered.hashAt(row)).name === name) {
                view.table.moveCursorTo(row);
                return;
            }
        }
        fail("the demo library has no row named " + name);
    }

    // docs/12 F2: `/` on a single-file torrent's Files tab flashes the footer note.
    function test_slash_on_a_single_file_says_there_is_nothing_to_filter() {
        testCase._cursorOn("archlinux-2026.09.01-x86_64.iso");
        tryCompare(App.details.files, "fileCount", 1);
        view.details.activateTab(1);
        view.details.forceActiveFocus();
        view.footerNote = "";

        keyClick("/");
        compare(view.footerNote, "one file, nothing to filter");
        verify(!view.hero.searchField.editing, "`/` went on to the window's search");
        view.details.activateTab(0);
    }

    // docs/12 F3: the Files tab's selection is a fact of the footer while the tab shows.
    function test_the_files_selection_adds_its_own_fact() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        tryCompare(App.details.files, "fileCount", 5);
        App.details.files.filterText = "";
        view.details.activateTab(1);
        view.details.forceActiveFocus();

        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(App.details.files.selectedCount, 5);
        verify(view.factsText.endsWith(" · 5 files selected"), view.factsText);
        verify(view.table.selectedCount === 0, "ctrl+a reached the table");

        App.details.files.clearSelection();
        verify(!view.factsText.includes("files selected"));
        view.details.activateTab(0);
    }

    // ---- focusHome (review VIEW-1) -------------------------------------------------------
    /// True when the window's keyboard is on `item` or inside it.
    function _focusInside(item) {
        let node = view.Window.activeFocusItem;
        while (node) {
            if (node === item)
                return true;
            node = node.parent;
        }
        return false;
    }

    function test_a_server_dropping_and_coming_back_leaves_an_open_sheet_its_keyboard() {
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        compare(view.limitsSheetPanel.opened, true);
        verify(testCase._focusInside(view.limitsSheetPanel), "the sheet did not take the keyboard");

        view._testServerLost = true;
        verify(testCase._focusInside(view.limitsSheetPanel), "a lost server took the sheet's keyboard");
        view._testServerLost = false;
        verify(testCase._focusInside(view.limitsSheetPanel), "the server's return took the sheet's keyboard");
        verify(!view.table.activeFocus);
    }

    function test_an_overlay_closed_while_the_server_is_lost_leaves_the_table_read_only() {
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        compare(view.limitsSheetPanel.opened, true);
        view._testServerLost = true;
        view.limitsSheetPanel.close();
        wait(0);
        verify(!view.table.activeFocus, "the table took the keyboard past the read-only guard");
        keyClick("d");
        compare(view.deleteConfirmPanel.opened, false, "`d` reached a disconnected table");

        view._testServerLost = false;
        verify(view.table.activeFocus, "the table did not get the keyboard back with the server");
    }

    function test_help_closed_while_the_server_is_lost_stays_behind_the_guard() {
        view._testServerLost = true;
        keyClick(Qt.Key_Question);
        compare(view.helpOpen, true);
        keyClick(Qt.Key_Escape);
        compare(view.helpOpen, false);
        wait(0);
        verify(!view.table.activeFocus, "closing help handed a disconnected table the keyboard");
        keyClick("p");
        compare(actionSpy.count, 0);
    }

    // ---- show (review VIEW-2) ------------------------------------------------------------
    function test_show_torrent_reaches_a_row_the_rail_status_hides() {
        let hash = "";
        for (let r = 0; r < App.filtered.count; ++r) {
            const h = App.filtered.hashAt(r);
            const state = App.torrentRow(h).stateText;
            if (state === "seeding" || state === "completed") {
                hash = h;
                break;
            }
        }
        verify(hash.length > 0);
        App.filtered.statusFilter = TorrentFilterModel.Downloading;
        verify(App.filtered.rowOfHash(hash) < 0);

        App.showTorrentRequested(hash);
        compare(view.cursorHash, hash, "show did not reach a torrent the status filter hides");
        compare(App.filtered.statusFilter, TorrentFilterModel.All);
    }

    function test_show_torrent_under_an_open_sheet_keeps_the_sheets_keyboard() {
        const hash = App.filtered.hashAt(3);
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        compare(view.limitsSheetPanel.opened, true);
        App.showTorrentRequested(hash);
        compare(view.cursorHash, hash);
        verify(testCase._focusInside(view.limitsSheetPanel), "a notice's show took the sheet's keyboard");
    }

    function test_the_jump_palette_clears_the_heros_field_with_the_filter() {
        const hash = App.filtered.hashAt(2);
        view.hero.searchField.text = "archlinux";
        verify(App.filtered.rowOfHash(hash) < 0 || App.filtered.hashAt(0) === hash);
        view.jumpPalette.jumpTo(hash);
        compare(view.cursorHash, hash);
        compare(App.filtered.text, "");
        compare(view.hero.searchField.text, "", "the hero kept a word that no longer filters");
    }

    // ---- server switch (review VIEW-3) ---------------------------------------------------
    function test_a_server_switch_restores_that_servers_layout_and_saves_nothing_before() {
        const saved = App.tableState("demo");
        const savedColumns = saved.columns !== undefined ? JSON.stringify(saved.columns) : "";
        const before = view.table.visibleColumns;
        // As if the table still showed another server's layout when the key moved to "demo".
        view._stateKey = "elsewhere";
        view.table.visibleColumns = before.filter(c => c !== "eta");
        const after = App.tableState("demo");
        compare(after.columns !== undefined ? JSON.stringify(after.columns) : "", savedColumns,
                "the old layout was saved under the new server's key");

        App.serverChanged();
        compare(view._stateKey, "demo");
        view.table.visibleColumns = before;
    }

    // ---- review RUN-7: an empty value is not "copied" -----------------------------------
    function test_copying_nothing_does_not_say_copied() {
        view.footerNote = "";
        view.copyText("", "copied");
        compare(view.footerNote, "nothing to copy");
        view.copyText("fixture", "copied name");
        compare(view.footerNote, "copied name");
        App.copyToClipboard("omaseed test: nothing to add here");
    }

    // Ruling Q14: y / Y / n copy one line per selected torrent, else the cursor row.
    function test_copy_takes_the_selection_one_line_each() {
        const first = App.filtered.hashAt(0);
        const second = App.filtered.hashAt(1);
        view.table.selectOnly(0);
        view.table.toggleSelection(1);
        compare(view.table.selectedCount, 2);
        compare(view.actionHashes().length, 2);

        const names = view.copyLines("copyName", view.actionHashes());
        compare(names.length, 2);
        verify(names.indexOf(App.torrentRow(first).name) >= 0);
        verify(names.indexOf(App.torrentRow(second).name) >= 0);
        const hashes = view.copyLines("copyHash", [first]);
        compare(hashes, [App.torrentRow(first).infohashV1 || App.torrentRow(first).infohashV2
                         || first]);

        view.footerNote = "";
        view.torrentAction("copyName");
        compare(view.footerNote, "copied 2 names");

        // A torrent with nothing to copy is skipped, and the note says so.
        view.copyTorrents("copyName", [first, "not-a-torrent"]);
        compare(view.footerNote, "copied 1 of 2 names");
        view.copyTorrents("copyName", ["not-a-torrent", "nor-this"]);
        compare(view.footerNote, "nothing to copy");

        // No selection: the cursor row alone, and a plain "copied".
        view.table.clearSelection();
        compare(view.actionHashes(), [view.cursorHash]);
        view.torrentAction("copyHash");
        compare(view.footerNote, "copied");
        App.copyToClipboard("omaseed test: nothing to add here");
    }

    // ---- the `?` sheet closes and runs (review VIEW-8, VIEW-9) ---------------------------
    function test_question_and_f1_close_the_help_sheet_they_opened() {
        keyClick(Qt.Key_Question);
        compare(view.helpOpen, true);
        keyClick(Qt.Key_Question);
        compare(view.helpSheet.query, "", "? was typed into the filter");
        compare(view.helpOpen, false, "? did not close the sheet");

        keyClick(Qt.Key_F1);
        compare(view.helpOpen, true);
        keyClick(Qt.Key_F1);
        compare(view.helpOpen, false, "F1 did not close the sheet");
    }

    function test_enter_on_the_details_placement_row_moves_the_pane() {
        const before = App.detailsPlacement;
        keyClick(Qt.Key_Question);
        for (const letter of "right")
            keyClick(letter);
        compare(view.helpSheet.cursorRow.action, "cycleDetailsPlacement");
        keyClick(Qt.Key_Return);
        compare(view.helpOpen, false);
        verify(App.detailsPlacement !== before, "enter on the row did nothing");
        App.detailsPlacement = before;
    }

    function test_a_movement_row_runs_nothing_and_says_so() {
        compare(view.runKeymapAction("moveDown"), false);
        compare(view.runKeymapAction("columns"), true);
        view.columnsPalette.close();
    }

    // ---- ctrl chords from the rail (review VIEW-10) -------------------------------------
    function test_ctrl_k_from_the_rail_opens_jump() {
        view.rail.forceActiveFocus();
        const cursor = view.rail.cursorIndex;
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(view.jumpPalette.opened, true, "ctrl+k in the rail did not open jump");
        compare(view.rail.cursorIndex, cursor, "ctrl+k moved the rail's cursor");
    }

    // ---- the details pane's row keys (review VIEW-6) ------------------------------------
    function test_a_in_the_general_tab_still_adds_a_torrent() {
        view.details.activateTab(0);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(addSpy.count, 1, "the details pane swallowed the window's `a`");
    }

    function test_tracker_keys_act_and_the_unbuilt_ones_say_so() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(2);
        view.details.forceActiveFocus();
        keyClick(Qt.Key_R, Qt.ShiftModifier);
        compare(view.footerNote, "reannounced");
        keyClick("a");
        compare(view.footerNote, "not in this version yet");
        compare(addSpy.count, 0, "`a` in Trackers opened the add sheet");
        view.details.activateTab(3);
        keyClick("b");
        compare(view.footerNote, "not in this version yet");
        view.details.activateTab(0);
    }

    // ---- first run (review VIEW-4) -------------------------------------------------------
    function test_a_link_handed_over_during_first_run_waits_for_setup() {
        const magnet = "magnet:?xt=urn:btih:0102030405060708090a0b0c0d0e0f1011121314&dn=waiting";
        view._firstRunDetect = false;
        view.firstRun = true;
        verify(view.firstRunLoader.item !== null);
        App.addUrisRequested([magnet]);
        compare(view.addSheet.opened, false, "the add sheet opened under the first-run page");
        view.addDropped([magnet + "2"]);
        compare(view.addSheet.opened, false, "a drop opened the add sheet under the page");

        (view.firstRunLoader.item as FirstRunPage).finished();
        compare(view.firstRun, false);
        compare(view.addSheet.opened, true, "the waiting link did not open once setup was done");
        compare(view.addSheet.sourceField.text, magnet);
        compare(view.addSheet.queue.length, 1);
        view.addSheet.dismiss();
    }

    // REG-6: the tray's `add…` and `settings…` during first run would open under the page
    // and take its keyboard; they wait for setup like a hand-off does.
    function test_the_trays_add_during_first_run_waits_for_setup() {
        view._firstRunDetect = false;
        view.firstRun = true;
        verify(view.firstRunLoader.item !== null);
        App.addRequested();
        compare(view.addSheet.opened, false, "the tray's add… opened under the first-run page");
        App.openSettings();
        compare(view.settingsModal.opened, false, "settings… opened under the first-run page");

        (view.firstRunLoader.item as FirstRunPage).finished();
        compare(view.firstRun, false);
        compare(view.addSheet.opened, true, "the waiting add… did not open once setup was done");
        compare(view.settingsModal.opened, false, "two overlays opened at once");
        view.addSheet.dismiss();
    }

    function test_the_trays_settings_during_first_run_waits_for_setup() {
        view._firstRunDetect = false;
        view.firstRun = true;
        verify(view.firstRunLoader.item !== null);
        App.openSettings();
        compare(view.settingsModal.opened, false, "settings… opened under the first-run page");

        (view.firstRunLoader.item as FirstRunPage).finished();
        compare(view.firstRun, false);
        compare(view.settingsModal.opened, true, "the waiting settings… did not open after setup");
        view.settingsModal.close();
    }

    function test_the_details_pane_follows_the_cursor() {
        const first = view.table.cursorHash;
        compare(view.details.torrent.name, App.torrentRow(first).name);

        view.table.moveCursor(1);
        const second = view.table.cursorHash;
        verify(second !== first);
        compare(view.details.torrent.name, App.torrentRow(second).name);
    }
}
