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
        view.detailsAddSheet.close();
        view.banPeerConfirm.close();
        view.jumpPalette.close();
        view.categoryPalette.close();
        view.tagPalette.close();
        view.sortPalette.close();
        view.columnsPalette.close();
        view.commandPalette.close();
        view.moreMenu.close();
        view.filterMenu.close();
        view.globalLimitsSheet.close();
        testCase.width = 1360;
        App.filtered.clearCategory();
        view.helpOpen = false;
        view._testServerLost = false;
        App.railHidden = false;
        view.split.folded = false;
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
        // One is "1 torrent", not "1 torrents".
        App.loadDemo(1);
        verify(view.factsText.startsWith("1 torrent · "), view.factsText);
        App.loadDemo(16);
        compare(App.counts.total, 16);
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
        compare(view.split.folded, false);

        keyClick("i");
        compare(view.details.collapsed, true);
        compare(view.split.folded, true);

        keyClick("i");
        compare(view.details.collapsed, false);
        compare(view.split.folded, false);
    }

    // docs/17 M10.3: `i` and the divider's double-click are one fold, and a folded pane at the
    // bottom leaves the strip with the cursor torrent on it; `i` joins the footer (M16).
    function test_i_and_the_divider_share_one_fold_and_the_strip_shows_the_cursor_row() {
        view.split.placement = "bottom";
        const strip = findChild(view, "stripName");
        const iHint = () => view.hintModel.hints.some(h => h.key === "i");
        verify(!iHint(), "the footer teaches i while the pane is open");

        keyClick("i");
        compare(view.split.folded, true);
        verify(strip.visible);
        compare(strip.text, App.torrentRow(view.cursorHash).name);
        verify(findChild(view, "stripStats").text.indexOf(App.torrentRow(view.cursorHash)
                                                          .progressText) === 0);
        verify(iHint(), "the footer does not teach the way back");
        verify(view.table.activeFocus);

        // The same state the divider's double-click unfolds.
        mouseDoubleClickSequence(findChild(view.split, "grip"), 40, 3);
        compare(view.split.folded, false);
        compare(view.details.collapsed, false);
        verify(!strip.visible);

        // …and a click on the strip's ▴, in the window, unfolds it too.
        keyClick("i");
        compare(view.split.folded, true);
        const chevron = findChild(view, "stripChevron");
        mouseClick(chevron, chevron.width / 2, chevron.height / 2);
        compare(view.split.folded, false);
        view.split.placement = Qt.binding(() => App.detailsPlacement);
    }

    // docs/DECISIONS.md M16: the footer's note sits in the footer's own line, never over the pane.
    function test_the_footer_note_stays_in_the_footer() {
        view.flashFooterNote("details: bottom · I to change");
        const note = findChild(view, "footerNote");
        verify(note.visible);
        const top = note.mapToItem(view, 0, 0).y;
        const footerTop = view.height - Math.round(Theme.font.baseSize * 2.2);
        verify(top >= footerTop, "the note is drawn above the footer, at " + top);
        view.footerNote = "";
    }

    // docs/31 F1 (D7): with the schedule on, a hand toggle is said in the footer's note slot
    // once the server has flipped, in the foreground with the set's mark, until the next key.
    function test_the_note_after_A_says_how_long_a_hand_toggle_lasts() {
        const prefs = App.serverPrefs;
        const keys = ["scheduler_enabled", "schedule_from_hour", "schedule_from_min",
                      "schedule_to_hour", "schedule_to_min", "scheduler_days"];
        const before = {};
        for (const key of keys)
            before[key] = prefs.value(key);
        // 23:00 to 07:00 on tomorrow's day only: qBittorrent's backwards days put today all
        // on (diogochaves/upstream#84), so the demo's "off" is a hand toggle, whatever the time.
        const tomorrow = (new Date().getDay() + 1) % 7; // 0 is Sunday
        prefs.apply({
                        "scheduler_enabled": true,
                        "schedule_from_hour": 23,
                        "schedule_from_min": 0,
                        "schedule_to_hour": 7,
                        "schedule_to_min": 0,
                        "scheduler_days": tomorrow === 0 ? 9 : tomorrow + 2
                    });
        compare(App.altSpeed.kind, "turtle"); // the demo's 10 KB/s set
        verify(!App.altSpeed.on);
        verify(App.altSpeed.byHand);
        compare(view.hero.altChip.tooltip.indexOf("off by hand · the schedule starts turtle at "), 0);

        view.footerNote = "";
        view._altFlipFrom = true; // as if A had asked to leave "on" and the poll came back
        view._altFlipped();
        compare(view.footerNote.indexOf("turtle off by hand · back at "), 0);
        verify(view.footerNoteSticky);
        const mark = findChild(view, "footerNoteMark");
        verify(mark.visible);
        compare(mark.text, "▼");
        compare(findChild(view, "footerNote").color, Theme.foreground);
        keyClick("j");
        compare(view.footerNote, "");
        verify(!mark.visible);

        // With the schedule off there is nothing to add.
        prefs.apply({
                        "scheduler_enabled": false
                    });
        view._altFlipFrom = true;
        view._altFlipped();
        compare(view.footerNote, "");
        prefs.apply(before);
    }

    // docs/32 N1 (#55): a move, rename or delete answered "OK" that the log then says failed
    // is a yellow note, and "· i details" says where its block is while the pane is hidden.
    function test_a_logged_failure_is_a_yellow_note() {
        const note = findChild(view, "footerNote");
        const hint = findChild(view, "footerNoteHint");
        App.loggedFailure("log55-move.bin didn't move: Permission denied", [App.filtered.hashAt(0)]);
        compare(view.footerNote, "log55-move.bin didn't move: Permission denied");
        compare(note.color, Theme.yellow);
        verify(!hint.visible); // the pane is on show
        view.toggleDetails();
        verify(view.details.collapsed);
        verify(hint.visible);
        view.toggleDetails();

        // After a delete there is no torrent to show: no hint.
        App.loggedFailure("x was removed, its files weren't: Permission denied", []);
        view.toggleDetails();
        verify(!hint.visible);
        view.toggleDetails();

        // Any other note is the accent again.
        view.flashFooterNote("copied");
        compare(note.color, Theme.accent);
        view.footerNote = "";
    }

    function rowOf(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            if (App.torrentRow(App.filtered.hashAt(row)).name === name)
                return row;
        }
        return -1;
    }

    // docs/32 F1, D3: from the table, `m` on the torrent whose move didn't happen opens the
    // location sheet on its destination; `x` dismisses the block, but only while General
    // shows it.
    function test_m_tries_the_move_again_and_x_dismisses_on_general() {
        App.loadDemo(16);
        const row = testCase.rowOf("Spring (2019) 4K (CC-BY)");
        verify(row >= 0);
        view.table.moveCursorTo(row);
        const hash = view.cursorHash;
        compare(App.torrentRow(hash).failure.kind, "move");

        keyClick("m");
        verify(view.locationSheetPanel.opened);
        compare(view.locationSheetPanel.focusables()[0].text, "/mnt/archive/movies");
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Escape);
        verify(!view.locationSheetPanel.opened);
        view.table.forceActiveFocus();

        view.details.activateTab(1);
        keyClick("x");
        compare(App.torrentRow(hash).failure.kind, "move");
        view.details.activateTab(0);
        keyClick("x");
        verify(App.torrentRow(hash).failure.kind === undefined);
        App.loadDemo(16);
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
        // docs/30 D10: after the command palette's row and its rule.
        compare(actions().slice(0, 4), ["commands", undefined, "settings", "help"]);
        view.moreAction("settings");
        compare(settingsSpy.count, 1);
        view.moreAction("help");
        compare(view.helpOpen, true);
        view.helpOpen = false;
        testCase.width = 1360;
    }

    // docs/30 D10: the ⋯ menu's first row opens the command palette, and the footer and the
    // ? sheet name it.
    function test_the_palette_is_found_without_its_key() {
        compare(view.moreMenu.items[0].action, "commands");
        compare(view.moreMenu.items[0].key, ":");
        view.moreAction("commands");
        compare(view.commandPalette.opened, true);
        view.commandPalette.close();
        compare(view.hintModel.pinnedLead.key, ":");
        verify(view.helpSheetPanel.subtitle.endsWith(": runs any of them by name"));
    }

    // #66: the ? sheet's head is 760 wide; its subtitle takes a second line rather than losing
    // the palette's words at the edge.
    function test_the_help_sheets_whole_subtitle_is_visible() {
        view.helpOpen = true;
        tryCompare(view.helpSheetPanel.panel, "visible", true);
        compare(view.helpSheetPanel.panel.width, Theme.space(760));
        const line = findChild(view.helpSheetPanel, "sheetSubtitle");
        verify(line !== null);
        compare(line.text, view.helpSheetPanel.subtitle);
        tryVerify(() => line.width > 0 && !line.truncated, 1000, "the subtitle is cut");
        compare(line.lineCount, 2);
        // The second line lies inside the panel, above the shortcuts.
        const bottom = line.mapToItem(view.helpSheetPanel.panel, 0, line.height).y;
        verify(bottom < view.helpSheetPanel.panel.height, "the subtitle runs out of the panel");
        view.helpOpen = false;
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
        // Batch-2 D2: opened from "alt-speed limits…", the sheet says so, named for the way
        // the demo's set goes (docs/31: slower than its normal limits, a turtle).
        compare(view.globalLimitsSheet.title, "Turtle limits");
        verify(view.globalLimitsSheet.subtitle.indexOf("turtle is on") >= 0);
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

    // docs/15 Q1 A: at the floor the chips leave the hero; `/` opens one menu with the name
    // filter on top and a row per filter, and its ✕ clears that filter.
    function test_slash_opens_the_filters_menu_on_the_last_step() {
        App.filtered.statusFilter = TorrentFilterModel.Downloading;
        App.filtered.category = "linux";
        keyClick(Qt.Key_B, Qt.ControlModifier);
        testCase.width = Theme.space(480);
        compare(view.hero.level, 4);
        verify(view.hero.filtersInButton);

        view.table.forceActiveFocus();
        keyClick(Qt.Key_Slash);
        verify(view.filterMenu.opened);
        verify(view.overlayOpen);
        compare(view.filterMenu.filters.length, 2);
        keyClick("u");
        compare(App.filtered.text, "u");
        compare(view.hero.searchField.text, "u");

        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_X);
        compare(App.filtered.statusFilter, TorrentFilterModel.All);
        verify(view.filterMenu.opened);
        // "clear all filters" is the row after the last filter.
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Return);
        verify(!view.filterMenu.opened);
        compare(App.filtered.category, undefined);
        compare(App.filtered.text, "");
        compare(view.hero.searchField.text, "");

        testCase.width = 1360;
        view.filterMenu.close();
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

    // ---- docs/17 M9: the selection bar ---------------------------------------------------------
    function selectTwo() {
        view.table.toggleCursorSelection();
        view.table.moveCursor(1);
        view.table.toggleCursorSelection();
        compare(view.table.selectedCount, 2);
    }

    function test_the_selection_bar_shows_with_a_selection_only() {
        verify(!view.selectionBar.visible, "no selection, no bar");
        compare(view.table.bottomRoom, 0);
        view.table.moveCursor(2);
        verify(!view.selectionBar.visible, "the cursor alone is not a selection (M9.2)");
        view.table.toggleCursorSelection();
        verify(view.selectionBar.visible);
        compare(view.selectionBar.count, 1);
        // M9.6: the last row can scroll clear of it.
        compare(view.table.bottomRoom, view.selectionBar.height + Theme.space(12));
        view.table.clearSelection();
        verify(!view.selectionBar.visible);
        compare(view.table.bottomRoom, 0);
    }

    function test_the_selection_bar_floats_centred_over_the_tables_bottom() {
        testCase.selectTwo();
        const bar = view.selectionBar;
        const table = view.table;
        const barAt = bar.mapToItem(view, 0, 0);
        const tableAt = table.mapToItem(view, 0, 0);
        fuzzyCompare(barAt.x + bar.width / 2, tableAt.x + table.width / 2, 1);
        compare(barAt.y + bar.height, tableAt.y + table.height - Theme.space(12));
        verify(bar.width <= table.width - Theme.space(16), "at 1360 the whole bar fits");
        compare(bar.folded, []);
    }

    function test_a_bar_button_acts_on_the_selection_and_keeps_the_keyboard() {
        testCase.selectTwo();
        const hashes = view.table.selectedHashes();
        waitForRendering(view);
        mouseClick(findChild(view.selectionBar, "bar_delete"));
        compare(view.deleteConfirmPanel.opened, true);
        compare(view.deleteConfirmPanel.hashes.slice().sort(), hashes.slice().sort());
        view.deleteConfirmPanel.close();
        verify(view.table.activeFocus, "the bar took the keyboard from the table (M9.4)");
    }

    function test_the_bars_more_hangs_the_menu_over_it() {
        testCase.selectTwo();
        waitForRendering(view);
        mouseClick(findChild(view.selectionBar, "bar_more"));
        compare(view.actionsMenuPanel.opened, true);
        compare(view.actionsMenuPanel.leadActions, []);
        const panel = view.actionsMenuPanel.panel;
        const bottom = panel.mapToItem(view, 0, panel.height).y;
        verify(bottom <= view.selectionBar.mapToItem(view, 0, 0).y,
               "the menu should open above the bar, not over it");
        view.actionsMenuPanel.close();
        verify(view.table.activeFocus);
    }

    function test_the_bars_more_lists_what_folded_first() {
        testCase.width = 620;
        testCase.selectTwo();
        waitForRendering(view);
        const folded = view.selectionBar.folded;
        verify(folded.length > 0, "at 620 some buttons should have folded");
        mouseClick(findChild(view.selectionBar, "bar_more"));
        compare(view.actionsMenuPanel.opened, true);
        const items = view.actionsMenuPanel.items;
        const lead = items.slice(0, folded.length).map(item => item.action);
        compare(lead.slice().sort(), folded.slice().sort());
        compare(items[folded.length].separator, true);
        // Moved, not copied: each folded action is in the menu once.
        for (const action of folded)
            compare(items.filter(item => item.action === action).length, 1, action);
        view.actionsMenuPanel.close();
        // `enter` gets Fig. 11's menu as it always was.
        view.table.forceActiveFocus();
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);
        compare(view.actionsMenuPanel.leadActions, []);
        compare(view.actionsMenuPanel.items[0].action, "stopStart");
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

        // The add sheet is guarded (manual box M5): `esc` goes to `cancel`, then closes.
        keyClick(Qt.Key_Escape);
        compare(view.addSheet.opened, true);
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
        // The add sheet is guarded (manual box M5): the first `esc` goes to `cancel`.
        testCase._checkFocusComesBack(() => App.addRequested(), view.addSheet, "the add sheet",
                                      2);
        testCase._checkFocusComesBack(() => App.openSettings(), view.settingsModal,
                                      "the settings modal");
    }

    // M8: the hero's gear opens settings with the mouse and leaves the keyboard where it was,
    // so closing settings gives it back to the table: no ring or tip left on the gear, and
    // `j` moves the cursor.
    function test_a_click_on_the_gear_gives_the_keyboard_back_to_the_table() {
        const gear = findChild(view.hero, "settingsButton");
        verify(gear && gear.visible, "no settings button in the hero");
        const row = view.table.cursorIndex;
        mouseClick(gear);
        compare(view.settingsModal.opened, true, "the gear did not open settings");
        keyClick(Qt.Key_Escape);
        compare(view.settingsModal.opened, false);
        mouseMove(view.table, view.table.width / 2, view.table.height / 2);
        compare(gear.activeFocus, false, "the gear kept the keyboard");
        compare(findChild(gear, "focusRing").shown, false);
        tryCompare(gear.tooltipItem, "shown", false);
        verify(view.table.activeFocus, "the table did not get the keyboard back");
        keyClick("j");
        compare(view.table.cursorIndex, row + 1);
        view.table.moveCursorTo(row);
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

    // #11 (docs/18 A1): `r` with several files selected opens the rename sheet on them; with
    // one it stays the inline editor; the menu's "rename files…" takes every shown file.
    function test_r_on_several_selected_files_opens_the_rename_sheet() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        tryCompare(App.details.files, "fileCount", 5);
        App.details.files.filterText = "";
        view.details.activateTab(1);
        view.details.forceActiveFocus();

        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(App.details.files.selectedFileCount(), 5);
        keyClick("r");
        compare(view.renameFilesSheet.opened, true);
        compare(view.renameFilesSheet.paths.length, 5);
        compare(view.renameFilesSheet.fromSelection, true);
        verify(view.renameFilesSheet.findField.editing, "the keyboard is not in find");
        keyClick(Qt.Key_Escape);
        compare(view.renameFilesSheet.opened, false);

        App.details.files.clearSelection();
        view.details.fileAction("renameMany");
        compare(view.renameFilesSheet.opened, true);
        compare(view.renameFilesSheet.fromSelection, false);
        compare(view.renameFilesSheet.paths.length, 5);
        view.renameFilesSheet.close();
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
        compare(view.runAction("moveDown"), false);
        compare(view.runAction("columns"), true);
        view.columnsPalette.close();
    }

    // ---- one runner for the `?` sheet and the palette (docs/30 D9, DECISIONS C1) ----------
    function test_every_command_runs_and_every_runner_is_listed() {
        const known = Object.keys(view._runners);
        for (const group of Keymap.commandGroups) {
            for (const action of group.actions)
                verify(known.indexOf(action) >= 0, "runAction does not know " + action);
        }
        const leaders = Keymap.columns.reduce((all, column) => all.concat(Keymap.helpRows(column).map(
                                                                                  row => row.action)), []);
        for (const action of known) {
            const listed = leaders.indexOf(action) >= 0 || Keymap.commandGroups.some(
                             group => group.actions.indexOf(action) >= 0);
            verify(listed, "runAction runs what neither the palette nor the ? sheet lists: " + action);
        }
    }

    function test_run_action_does_what_the_key_does() {
        compare(view.runAction("location"), true);
        compare(view.locationSheetPanel.opened, true, "set location did not open its sheet");
        view.locationSheetPanel.close();
        compare(view.runAction("globalLimits"), true);
        compare(view.globalLimitsSheet.opened, true, "the menu-only limits sheet did not open");
        view.globalLimitsSheet.close();
        compare(view.runAction("add"), true);
        compare(addSpy.count, 1);
        view.addSheet.dismiss();
        view.details.activateTab(1);
        compare(view.runAction("sortFiles"), true);
        compare(view.details.menuOpen, true, "the Files tab's sort menu did not open");
        keyClick(Qt.Key_Escape);
        compare(view.details.menuOpen, false);
        compare(view.details.activeFocus, true, "a Files command left the keyboard elsewhere");
        view.details.activateTab(0);
    }

    // ---- the command palette's keys (docs/30 D1) -------------------------------------------
    function test_colon_opens_the_command_palette_from_the_table() {
        view.table.toggleCursorSelection();
        keyClick(":");
        compare(view.commandPalette.opened, true, ": did not open the command palette");
        compare(view.activeOverlay, view.commandPalette);
        compare(view.commandPalette.context.area, "table");
        compare(view.commandPalette.title, "commands for 1 selected");
        compare(view.commandPalette.query, "", "the : was typed into the query");
        keyClick(Qt.Key_Escape);
        compare(view.commandPalette.opened, false);
        verify(view.table.activeFocus, "the keyboard did not come back to the table");
    }

    // ctrl+shift+p works from a field too, and from the details pane's Files tab its own
    // commands lead.
    function test_ctrl_shift_p_opens_it_from_a_field_and_from_files() {
        keyClick("/");
        verify(view.hero.searchField.editing);
        keyClick(Qt.Key_P, Qt.ControlModifier | Qt.ShiftModifier);
        compare(view.commandPalette.opened, true, "ctrl+shift+p did not open it from the field");
        view.commandPalette.close();
        view.focusHome();

        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(1);
        view.details.forceActiveFocus();
        keyClick(":");
        compare(view.commandPalette.opened, true);
        compare(view.commandPalette.context.area, "files");
        compare(view.commandPalette.items[0].action, "priorityMenu");
        view.commandPalette.close();
        view.details.activateTab(0);
    }

    // enter runs the row as its key would, and it comes first next time (D7).
    function test_a_command_runs_and_is_remembered() {
        keyClick(":");
        testCase._type("set location");
        compare(view.commandPalette.currentItem.action, "location");
        keyClick(Qt.Key_Return);
        compare(view.commandPalette.opened, false);
        compare(view.locationSheetPanel.opened, true, "set location did not run");
        view.locationSheetPanel.close();
        compare(App.recentCommands()[0], "location");
        keyClick(":");
        compare(view.commandPalette.items[0].action, "location");
        view.commandPalette.close();
    }

    // P4: no match, and enter opens the jump palette with the text.
    function test_no_match_opens_the_jump_palette_with_the_text() {
        keyClick(":");
        testCase._type("bunny");
        keyClick(Qt.Key_Return);
        compare(view.commandPalette.opened, false);
        compare(view.jumpPalette.opened, true);
        compare(view.jumpPalette.query, "bunny");
        view.jumpPalette.close();
    }

    // P6: the lost server's guard lets : through, and the palette shows the rest dim.
    function test_colon_gets_through_a_lost_server() {
        view._testServerLost = true;
        view.focusHome();
        keyClick(":");
        compare(view.commandPalette.opened, true, ": did not get through the read-only guard");
        compare(view.commandPalette.items[0].action, "settings");
        compare(view.commandPalette.items.filter(row => !row.disabled).length, 4);
        view.commandPalette.close();
        view._testServerLost = false;
    }

    // D9: the ? sheet's row for the palette runs it.
    function test_the_help_sheet_row_opens_the_palette() {
        compare(view.runAction("commands"), true);
        compare(view.commandPalette.opened, true);
        view.commandPalette.close();
    }

    // P6, Fig. 23: only settings, shortcuts, hide and quit get through a lost server.
    function test_a_lost_server_runs_only_what_its_guard_lets_through() {
        view._testServerLost = true;
        compare(view.runAction("add"), false);
        compare(addSpy.count, 0);
        compare(view.runAction("settings"), true);
        compare(settingsSpy.count, 1);
        view.settingsModal.close();
        compare(view.runAction("help"), true);
        compare(view.helpOpen, true);
        view.helpOpen = false;
        view._testServerLost = false;
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

    function test_tracker_keys_act() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(2);
        view.details.forceActiveFocus();
        keyClick(Qt.Key_R, Qt.ShiftModifier);
        compare(view.footerNote, "reannounced");
        view.details.activateTab(0);
    }

    // ---- ruling Q4: the add sheet and the ban confirm ---------------------------------------
    function _type(text) {
        for (const c of text)
            keyClick(c);
    }

    function test_a_in_trackers_opens_the_add_sheet_and_checks_the_urls() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(2);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(addSpy.count, 0, "`a` in Trackers opened the add-torrent sheet");
        compare(view.detailsAddSheet.opened, true);
        compare(view.detailsAddSheet.kind, "tracker");
        compare(view.detailsAddSheet.hash, App.details.hash);
        verify(view.detailsAddSheet.field.editing, "the field does not have the caret");

        testCase._type("tracker.example/announce");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, true, "a URL without a scheme was sent");
        compare(view.detailsAddSheet.errorText,
                "tracker.example/announce is not a tracker URL");

        // esc cancels on the first press (a form sheet), and the pane gets the keyboard back.
        keyClick(Qt.Key_Escape);
        compare(view.detailsAddSheet.opened, false);
        tryVerify(() => view.details.activeFocus, 1000, "the pane lost the keyboard");

        keyClick("a");
        compare(view.detailsAddSheet.errorText, "", "the last error came back");
        compare(view.detailsAddSheet.field.text, "");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, true, "an empty field was sent");
        verify(view.detailsAddSheet.errorText.length > 0);
        testCase._type("udp://a.example:1337/announce https://b.example/announce");
        compare(view.detailsAddSheet.errorText, "", "typing did not clear the error");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, false);
        view.details.activateTab(0);
    }

    function test_a_in_peers_opens_the_add_sheet_for_host_and_port() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(3);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(view.detailsAddSheet.opened, true);
        compare(view.detailsAddSheet.kind, "peer");
        compare(view.detailsAddSheet.title, "Add peers");

        testCase._type("203.0.113.5");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.errorText, "203.0.113.5 is not a host:port");
        view.detailsAddSheet.field.text = "";
        testCase._type("203.0.113.5:6881, [2001:db8::1]:51413");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, false);
        view.details.activateTab(0);
    }

    // #10: the HTTP sources tab's `a` takes http or https URLs; `d` removes the row's.
    function test_http_sources_add_and_remove() {
        testCase._cursorOn("archlinux-2026.09.01-x86_64.iso");
        view.details.activateTab(4);
        compare(view.details.currentTab, 4, "the demo's Arch ISO has no web seeds");
        view.details.forceActiveFocus();
        keyClick("a");
        compare(addSpy.count, 0, "`a` in HTTP sources opened the add-torrent sheet");
        compare(view.detailsAddSheet.opened, true);
        compare(view.detailsAddSheet.kind, "webseed");
        compare(view.detailsAddSheet.title, "Add HTTP sources");

        testCase._type("udp://mirror.example/pub/");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, true, "a udp URL was sent");
        compare(view.detailsAddSheet.errorText, "udp://mirror.example/pub/ is not an http or https URL");
        view.detailsAddSheet.field.text = "";
        testCase._type("https://mirror.example/pub/");
        keyClick(Qt.Key_Return);
        compare(view.detailsAddSheet.opened, false);
        tryVerify(() => view.details.activeFocus, 1000, "the pane lost the keyboard");

        keyClick("d");
        compare(view.footerNote, "removed https://mirror.example.net/archlinux/iso/2026.09.01/");
        view.details.activateTab(0);
    }

    function test_ctrl_q_quits_from_the_add_sheet() {
        view.details.activateTab(2);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(view.detailsAddSheet.opened, true);
        quitSpy.clear();
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(quitSpy.count, 1, "ctrl+q was swallowed by the sheet");
        view.detailsAddSheet.close();
        view.details.activateTab(0);
    }

    function test_b_in_peers_asks_before_a_permanent_ban() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(3);
        view.details.forceActiveFocus();
        tryVerify(() => App.details.peers.rowCount() > 0, 2000, "the demo torrent has no peers");
        const address = App.details.peers.addressAt(view.details.peersCursor);
        keyClick("b");
        compare(view.banPeerConfirm.opened, true, "`b` banned without asking");
        compare(view.banPeerConfirm.address, address);
        verify(view.banPeerConfirm.subtitle.indexOf("permanent") >= 0);
        verify(view.banPeerConfirm.subtitle.indexOf("every torrent") >= 0);
        compare(view.banPeerConfirm.cursor, 0, "a ban must open on cancel");

        quitSpy.clear();
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(quitSpy.count, 1, "ctrl+q was swallowed by the confirm");

        keyClick(Qt.Key_Escape);
        compare(view.banPeerConfirm.opened, false);
        tryVerify(() => view.details.activeFocus, 1000, "the pane lost the keyboard");

        // Confirmed: the demo App logs "demo: ban peer 1" instead of calling a server.
        keyClick("b");
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_Return);
        compare(view.banPeerConfirm.opened, false);
        view.details.activateTab(0);
    }

    // #51 D15: with peers selected, `b` asks once about all of them, and `d` in Trackers
    // removes every selected tracker.
    function test_b_on_selected_peers_asks_about_them_all() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(3);
        view.details.forceActiveFocus();
        tryVerify(() => App.details.peers.rowCount() > 1, 2000, "the demo torrent has too few peers");
        const peers = App.details.peers;
        const wanted = [peers.addressAt(0), peers.addressAt(1)];
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Space);
        keyClick("b");
        compare(view.banPeerConfirm.opened, true);
        compare(view.banPeerConfirm.addresses, wanted);
        compare(view.banPeerConfirm.message, "Ban 2 peers?");
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Escape); // and the selection
        compare(view.details.peersSelection.count, 0);

        testCase._cursorOn("archlinux-2026.09.01-x86_64.iso");
        view.details.activateTab(2);
        view.details.forceActiveFocus();
        keyClick(Qt.Key_A, Qt.ControlModifier);
        const count = view.details.trackersSelection.count;
        verify(count >= 2, "the demo torrent has too few trackers");
        keyClick("d");
        compare(view.footerNote, "removed " + count + " trackers");
        keyClick(Qt.Key_Escape);
        view.details.activateTab(0);
    }

    // A drop with nothing addable in it (a file that is not a .torrent) says so.
    function test_a_drop_with_nothing_to_add_says_so() {
        view.footerNote = "";
        view.addDropped([]);
        compare(view.footerNote, "nothing to add");
        compare(view.addSheet.opened, false);
        view.footerNote = "";
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
        // #40: the two that waited arrive together, as a batch.
        compare(view.addSheet.batchMode, true);
        compare(view.addSheet.batch[0].text, magnet);
        compare(view.addSheet.batch.length, 2);
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

    // ---- the mouse pass's decisions (#51) -----------------------------------------------------
    function rowItem(index: int): Item {
        const row = findChild(view.table, "row_" + index);
        verify(row, "no row " + index);
        return row;
    }

    // D1: a double-click on a torrent unfolds the details and gives them the keys.
    function test_a_double_click_on_a_torrent_opens_its_details() {
        keyClick("i");
        compare(view.split.folded, true);
        verify(view.table.activeFocus);
        waitForRendering(view, 250);
        const row = rowItem(3);
        mouseDoubleClickSequence(row, Theme.space(40), row.height / 2);
        compare(view.table.cursorHash, App.filtered.hashAt(3));
        compare(view.split.folded, false);
        tryVerify(() => view.details.activeFocus, 1000, "the details did not get the keys");
        compare(view.details.torrent.name, App.torrentRow(App.filtered.hashAt(3)).name);
        // Already unfolded, it only hands the keys over.
        view.table.forceActiveFocus();
        mouseDoubleClickSequence(row, Theme.space(40), row.height / 2);
        compare(view.split.folded, false);
        tryVerify(() => view.details.activeFocus, 1000);
    }

    // D2: the ✕ after the bar's count clears the selection and leaves the keyboard alone.
    function test_the_bars_cross_clears_the_selection() {
        testCase.selectTwo();
        waitForRendering(view, 250);
        const clear = findChild(view.selectionBar, "bar_clear");
        verify(clear && clear.visible);
        mouseClick(clear);
        compare(view.table.selectedCount, 0);
        verify(!view.selectionBar.visible);
        verify(view.table.activeFocus, "the ✕ took the keyboard from the table (M9.4)");
    }

    // D6: a right-click on a header cell opens the columns palette and sorts nothing.
    function test_a_right_click_on_a_header_opens_the_columns_palette() {
        waitForRendering(view, 250);
        const size = findChild(view.table, "header_size");
        verify(size);
        mouseClick(size, size.width / 2, size.height / 2, Qt.RightButton);
        compare(view.columnsPalette.opened, true);
        compare(App.filtered.sortRoleName, "dlspeed");
        keyClick(Qt.Key_Escape);
        compare(view.columnsPalette.opened, false);
    }

    // D8: the mouse's back button is one `esc` wherever the pointer is: the menu on top
    // closes, then the selection clears, then the filter; forward does nothing.
    function test_the_back_button_steps_the_escape_ladder() {
        waitForRendering(view, 250);
        App.filtered.text = "e";
        view.hero.searchField.text = "e";
        view.table.forceActiveFocus();
        verify(App.filtered.count > 2);
        testCase.selectTwo();
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);

        // Over the menu's scrim, which takes every button.
        mouseClick(view, view.width - 20, view.height / 2, Qt.ForwardButton);
        compare(view.actionsMenuPanel.opened, true, "forward did something");
        mouseClick(view, view.width - 20, view.height / 2, Qt.BackButton);
        compare(view.actionsMenuPanel.opened, false);
        compare(view.table.selectedCount, 2);
        verify(view.table.activeFocus);

        // Over a row, whose MouseArea takes the left, right and middle buttons.
        const row = rowItem(1);
        mouseClick(row, Theme.space(40), row.height / 2, Qt.BackButton);
        compare(view.table.selectedCount, 0);
        compare(App.filtered.text, "e");
        mouseClick(row, Theme.space(40), row.height / 2, Qt.BackButton);
        compare(App.filtered.text, "");
    }

    // D8: a middle-click toggles a row in the selection, as ctrl+click does.
    function test_a_middle_click_toggles_a_row() {
        waitForRendering(view, 250);
        mouseClick(rowItem(1), Theme.space(40), rowItem(1).height / 2, Qt.MiddleButton);
        mouseClick(rowItem(4), Theme.space(40), rowItem(4).height / 2, Qt.MiddleButton);
        compare(view.table.selectedHashes().slice().sort(),
                [App.filtered.hashAt(1), App.filtered.hashAt(4)].sort());
        compare(view.table.cursorIndex, 4);
        mouseClick(rowItem(1), Theme.space(40), rowItem(1).height / 2, Qt.MiddleButton);
        compare(view.table.selectedHashes(), [App.filtered.hashAt(4)]);
    }

    // D9: the rows hold their order under the open actions menu too. The pointer waits on
    // the header, off the rows: earlier tests leave it over them, and in CI (Qt 6.12) the rows
    // read as hovered once the menu closed, which holds the order by itself (#59).
    function test_the_open_actions_menu_holds_the_row_order() {
        waitForRendering(view, 250);
        const size = findChild(view.table, "header_size");
        verify(size);
        mouseMove(size, size.width / 2, size.height / 2);
        tryVerify(() => !view.table.orderHeld, 1000, "the pointer on the header held the order");
        verify(!view.table.holdOrder);
        view.table.requestActions();
        verify(view.table.holdOrder);
        verify(view.table.orderHeld);
        tryVerify(() => App.filtered.holdOrder, 1000);
        view.actionsMenuPanel.close();
        verify(!view.table.holdOrder);
        tryVerify(() => !App.filtered.holdOrder, 1000);
    }

    // D17: with the actions menu open, a right-click on another row closes it and opens it
    // again there, by the right-click rule (outside the selection: that row alone).
    function test_a_right_click_on_another_row_reopens_the_menu_there() {
        waitForRendering(view, 250);
        const first = rowItem(1);
        mouseClick(first, Theme.space(40), first.height / 2, Qt.RightButton);
        compare(view.actionsMenuPanel.opened, true);
        compare(view.table.selectedHashes(), [App.filtered.hashAt(1)]);

        // Past the panel, on row 8: the click lands on the menu's scrim.
        const target = rowItem(8);
        const at = target.mapToItem(view, view.table.width - Theme.space(60), target.height / 2);
        const panel = view.actionsMenuPanel.panel;
        verify(at.x > panel.mapToItem(view, panel.width, 0).x, "the point is on the panel");
        const hash = App.filtered.hashAt(8);
        mouseClick(view, at.x, at.y, Qt.RightButton);
        compare(view.actionsMenuPanel.opened, true, "the menu did not open again");
        compare(view.table.cursorHash, hash);
        compare(view.table.selectedHashes(), [hash]);
        compare(view.actionsMenuPanel.heading, view.actionsHeading(hash));
        // It opens afresh: the cursor on the first row, not the one the pointer rests on.
        compare(view.actionsMenuPanel.cursorIndex, view.actionsMenuPanel._firstEnabled());
        wait(50);
        compare(view.actionsMenuPanel.cursorIndex, view.actionsMenuPanel._firstEnabled());

        // A left click on the scrim (above the panel, which now hangs from row 8) still
        // only closes.
        const above = first.mapToItem(view, view.table.width - Theme.space(60), first.height / 2);
        mouseClick(view, above.x, above.y);
        compare(view.actionsMenuPanel.opened, false);
        verify(view.table.activeFocus);
    }

    // D13: the no-match note for a scope with no filter text names the scope and offers no
    // `esc`, which would clear nothing.
    function test_the_scope_only_note_has_no_escape_half() {
        App.filtered.category = "nothing-is-filed-here";
        verify(view.filterEmpty);
        const empty = findChild(view, "filteredEmpty");
        verify(empty.visible);
        compare(empty.message, "Nothing in nothing-is-filed-here");
        verify(!findChild(empty, "clearFilterLink").visible);
        App.filtered.clearCategory();
    }
}
