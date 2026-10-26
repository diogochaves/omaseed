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
        view.moveSheet.close();
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
        view.appMenu.close();
        view.globalLimitsSheet.close();
        view.createSheet.close();
        view.addMenu.close();
        testCase.width = 1360;
        App.filtered.clearCategory();
        App.filtered.clearTag();
        App.filtered.clearTracker();
        App.filtered.folder = "";
        view.helpOpen = false;
        view._testServerLost = false;
        App.railHidden = false;
        App.railWidth = 0;
        view.railHeldOpen = false;
        view.split.folded = false;
        view.details.activateTab(view.details.tabStory);
        view.table.cancelRename();
        view.table.clearSelection();
        view.titleBar.searchField.text = "";
        App.filtered.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.setSortByRoleName("dlspeed");
        App.filtered.sortOrder = Qt.DescendingOrder;
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
        addSpy.clear();
        settingsSpy.clear();
        actionSpy.clear();
    }

    // DECISIONS C24 H2, H5, H14, H15: the rail runs the window's whole height; the title bar,
    // the bands, the body and the status line sit beside it, and the mark's block is the title
    // bar's height.
    function test_the_rail_is_full_height_and_the_rest_sits_beside_it() {
        compare(view.rail.y, 0);
        compare(view.rail.height, view.height);
        compare(view.rail.width, Theme.railWidth);
        compare(view.titleBar.x, view.rail.width);
        compare(view.titleBar.y, 0);
        compare(view.titleBar.width, view.width - view.rail.width);
        compare(view.serverNotice.x, view.rail.width);
        compare(view.moveBand.x, view.rail.width);
        compare(view.split.mapToItem(view, 0, 0).x, view.rail.width);
        compare(view.footerNoticeItem.x, view.rail.width);
        compare(view.statusLine.x, view.rail.width);
        compare(view.statusLine.y + view.statusLine.height, view.height);
        compare(view.statusLine.width, view.width - view.rail.width);
        compare(findChild(view.rail, "railMark").height, view.titleBar.height);
        compare(view.titleBar.height, Theme.titleBarHeight);
        compare(view.serverNotice.y, view.titleBar.height);
    }

    // C24 H2: 1–5 pick the questions from anywhere, 0 is the library (every rail filter off),
    // 6–9 do nothing.
    function test_digits_pick_questions_and_zero_the_library() {
        view.table.forceActiveFocus();
        keyClick(Qt.Key_3);
        compare(App.filtered.question, TorrentFilterModel.LandedToday);
        verify(findChild(view.rail, "railQuestion-landedToday").selected);
        keyClick(Qt.Key_7);
        compare(App.filtered.question, TorrentFilterModel.LandedToday);
        App.filtered.category = "linux";
        keyClick(Qt.Key_0);
        compare(App.filtered.question, TorrentFilterModel.AnyQuestion);
        compare(App.filtered.category, undefined);
        verify(findChild(view.rail, "railView-library").current);
        keyClick(Qt.Key_1);
        compare(App.filtered.question, TorrentFilterModel.NeedsYou);
        compare(App.filtered.count, App.counts.needsYou);
    }

    // The palette's "show stalled…" types `is:stalled` into the filter, caret in it.
    function test_a_state_row_types_its_is_word() {
        view.runCommand(Keymap.showStatePrefix + "stalled");
        compare(App.filtered.text, "is:stalled");
        compare(App.filtered.count, App.counts.stalled);
        verify(view.titleBar.searchField.editing);
        view.clearFilter();
    }

    // C24 H9: the mark opens the omaseed menu under it.
    function test_the_mark_opens_the_app_menu() {
        findChild(view.rail, "railMark").Accessible.pressAction();
        verify(view.appMenu.opened);
        compare(view.activeOverlay, view.appMenu);
        view.appMenu.close();
    }

    // DECISIONS C24 H13 (#113): the window's ground is opaque unless Appearance says
    // translucent, which reads the launcher's alpha, or the transparency set there.
    function test_the_ground_is_opaque_unless_translucent() {
        compare(App.windowGround, "opaque");
        compare(Theme.windowAlpha, 1);
        App.windowGround = "translucent";
        fuzzyCompare(Theme.windowAlpha, Theme.defaultWindowAlpha, 0.001);
        App.windowAlphaOverride = 80;
        fuzzyCompare(Theme.windowAlpha, 0.8, 0.001);
        App.windowGround = "opaque";
        compare(Theme.windowAlpha, 1);
        App.windowAlphaOverride = 0;
    }

    // ---- the status line (board 46 `statusLine()`, DECISIONS C24 H15) ---------------------
    // Seven, not the rail's eight: the demo's stopped download is not "downloading" here.
    function test_the_status_line_states_the_library() {
        compare(App.counts.downloading, 8);
        compare(view.factsText, "16 torrents · 7 downloading · " + App.globalState.freeSpaceText
                + " free · synced now");
        // One is "1 torrent", not "1 torrents".
        App.loadDemo(1);
        verify(view.factsText.startsWith("1 torrent · "), view.factsText);
        App.loadDemo(16);
        compare(App.counts.total, 16);
    }

    // C24 H8: a selection is the action bar's to count, in the line's place; the facts
    // don't say it.
    function test_a_selection_is_the_action_bars_to_count() {
        view.table.toggleCursorSelection();
        view.table.moveCursor(1);
        view.table.toggleCursorSelection();
        compare(view.table.selectedCount, 2);
        verify(view.factsText.indexOf("selected") < 0, view.factsText);
        compare(findChild(view.selectionBar, "selectionCount").Accessible.name, "2 selected");
    }

    // #79: the facts are one label in whole words, the ↓ and the narrow steps' "sel" spelled
    // out; a note is an alert by its words.
    function test_a_screen_reader_hears_the_status_line() {
        const facts = findChild(view, "statusFacts");
        compare(facts.Accessible.role, Accessible.StaticText);
        compare(facts.Accessible.name, "16 torrents, 7 downloading, " + App.globalState.freeSpaceText
                + " free, synced now");
        compare(view.factsSpoken, facts.Accessible.name);
        view.flashFooterNote("copied");
        const note = findChild(view, "footerNote");
        compare(note.Accessible.role, Accessible.AlertMessage);
        compare(note.Accessible.name, "copied");
        view.footerNote = "";
    }

    // C24 H10: keys are taught on demand; the status line teaches none but "? keys", whose
    // click is `?`.
    function test_the_status_line_teaches_keys_on_demand() {
        verify(findChild(view, "footerHints") === null);
        const keys = findChild(view.statusLine, "statusKeys");
        compare(keys.text, "? keys");
        compare(keys.Accessible.name, "keys");
        compare(keys.Accessible.description, "shortcut ?");
        mouseClick(keys);
        compare(view.helpOpen, true);
        view.helpOpen = false;
        view.focusHome();
    }

    // The lane's words are MainView.serverLaneText() (one place, for C25 K11); one server is
    // named, with no ▾ and no click (C24 H15, docs/33 D19), and ctrl+s says why in the note.
    function test_one_server_is_named_in_the_lane_without_a_switch() {
        compare(App.serverCount <= 1, true);
        compare(view.statusLine.serverText, view.serverLaneText());
        verify(view.serverLaneText().startsWith(App.serverName), view.serverLaneText());
        verify(!view.statusLine.serverSwitchable);
        verify(!findChild(view.statusLine, "statusServerChevron").visible);
        const lane = view.statusLine.serverLane;
        compare(lane.Accessible.role, Accessible.StaticText);
        mouseClick(lane);
        verify(!view.serverPalette.opened, "one server opened the palette");
        keyClick(Qt.Key_S, Qt.ControlModifier);
        compare(view.footerNote, "one server · add another in Settings › Servers");
        view.footerNote = "";
    }

    // DECISIONS C32 D8 a: the status line's chip opens the choice of presets, over the chip;
    // esc closes it. A still flips the last one (the demo writes nothing).
    function test_the_alt_chip_opens_the_presets() {
        mouseClick(view.statusLine.altChip);
        const menu = findChild(view, "altSpeedMenu");
        verify(menu !== null && menu.opened);
        compare(menu.current, "off");
        verify(menu.panel.mapToItem(view, 0, 0).y + menu.panel.height
               <= view.statusLine.altChip.mapToItem(view, 0, 0).y);
        keyClick(Qt.Key_Escape);
        verify(!menu.opened);
        view.focusHome();
    }

    // #24: the status line's ↑ opens the seed console, as the rail's "seed" does.
    function test_the_up_figure_opens_the_seed_console() {
        const up = findChild(view.statusLine, "statusUp");
        compare(up.Accessible.name, "seed console");
        up.Accessible.pressAction();
        compare(view.view, "seed");
        view.setView("library");
        view.focusHome();
    }

    // ---- `/` and `esc` ------------------------------------------------------------------
    function test_slash_focuses_the_search_field_and_the_count_follows() {
        keyClick("/");
        verify(view.titleBar.searchField.editing, "`/` did not put the caret in the field");
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
        compare(view.titleBar.searchCountText, "1 of 16");
        compare(findChild(view.titleBar, "titleFilter").trailing, "1 of 16");
    }

    function test_escape_in_the_field_clears_it_and_goes_back_to_the_table() {
        keyClick("/");
        keyClick("f");
        keyClick("e");
        keyClick("d");
        compare(App.filtered.text, "fed");

        keyClick(Qt.Key_Escape);
        compare(App.filtered.text, "");
        compare(view.titleBar.searchField.text, "");
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
    // bottom leaves the strip with the cursor torrent on it.
    function test_i_and_the_divider_share_one_fold_and_the_strip_shows_the_cursor_row() {
        view.split.placement = "bottom";
        const strip = findChild(view, "stripName");

        keyClick("i");
        compare(view.split.folded, true);
        verify(strip.visible);
        compare(strip.text, App.torrentRow(view.cursorHash).name);
        verify(findChild(view, "stripStats").text.indexOf(App.torrentRow(view.cursorHash)
                                                          .progressText) === 0);
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

    // docs/DECISIONS.md M16: the note sits in the status line, never over the pane.
    function test_the_note_stays_in_the_status_line() {
        view.flashFooterNote("details: bottom · I to change");
        const note = findChild(view, "footerNote");
        verify(note.visible);
        const top = note.mapToItem(view, 0, 0).y;
        const lineTop = view.height - Theme.statusLineHeight;
        verify(top >= lineTop, "the note is drawn above the status line, at " + top);
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
        // on, so the demo's "off" is a hand toggle, whatever the time.
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
        compare(view.statusLine.altChip.tooltip.indexOf("off by hand · the schedule starts turtle at "), 0);

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

        // With the schedule off there is nothing to add...
        prefs.apply({
                        "scheduler_enabled": false
                    });
        view._altFlipFrom = true;
        view._altFlipped();
        compare(view.footerNote, "");
        // ...until the preferences the flip re-reads (D13) say a schedule is on after all.
        prefs.apply({
                        "scheduler_enabled": true
                    });
        compare(view.footerNote.indexOf("turtle off by hand · back at "), 0);
        compare(view._altFlipFrom, null);
        // A note that no longer applies goes.
        prefs.apply({
                        "scheduler_enabled": false
                    });
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
        // #77: "· i details" is a "show details" button; a screen reader's press shows them.
        compare(hint.Accessible.role, Accessible.Button);
        compare(hint.Accessible.name, "show details");
        hint.Accessible.pressAction();
        verify(!view.details.collapsed, "the press did not show the details");

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

    // DECISIONS C30 F13: shift+f (and the band's action) goes straight to the add-again confirm
    // while the reading says only adding it again frees them; otherwise to the free sheet.
    function test_free_the_space_goes_straight_to_the_confirm_when_only_readd_frees() {
        App.loadDemo(16);
        const hash = App.filtered.hashAt(0);
        view.freeSpaceFor(hash, {
                              "hash": hash,
                              "name": "Prairie Sky",
                              "outlook": "readd",
                              "version": "hybrid",
                              "readdFiles": 2,
                              "readdText": "1.03 GB"
                          });
        tryVerify(() => view.addAgainConfirm.opened, 1000, "no confirm");
        verify(!view.freeSpaceSheet.opened);
        compare(view.addAgainConfirm.files, 2);
        compare(view.addAgainConfirm.version, "hybrid");
        verify(view.addAgainConfirm.subtitle.indexOf("(1.03 GB)") > 0, view.addAgainConfirm.subtitle);
        view.addAgainConfirm.close();
        tryVerify(() => !view.addAgainConfirm.opened, 1000);

        view.freeSpaceFor(hash, {
                              "hash": hash,
                              "outlook": "mixed"
                          });
        tryVerify(() => view.freeSpaceSheet.opened, 1000, "no sheet");
        verify(!view.addAgainConfirm.opened);
        view.freeSpaceSheet.close();
        tryVerify(() => !view.freeSpaceSheet.opened, 1000);

        // Another torrent's reading: the sheet, which reads this one's first (F16).
        view.freeSpaceFor(hash, {
                              "hash": "other",
                              "outlook": "readd"
                          });
        tryVerify(() => view.freeSpaceSheet.opened, 1000, "no sheet for another torrent's reading");
        view.freeSpaceSheet.close();
        tryVerify(() => !view.freeSpaceSheet.opened, 1000);
    }

    function rowOf(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            if (App.torrentRow(App.filtered.hashAt(row)).name === name)
                return row;
        }
        return -1;
    }

    // DECISIONS C20 (#107): an errored torrent's actions menu reads "stop" (`p` stops it, as
    // the selection bar's stop does) with "retry" (`e`) under it; a healthy one has no retry.
    function test_an_errored_torrent_stops_and_has_its_own_retry() {
        App.loadDemo(16);
        const row = testCase.rowOf("Elephants Dream 1080p (CC-BY)");
        verify(row >= 0);
        view.table.moveCursorTo(row);
        const errored = App.torrentRow(view.cursorHash);
        compare(errored.stateText, "error");
        compare(errored.stopped, false, "p would start an errored torrent");
        compare(errored.errorStopped, false);
        view.table.forceActiveFocus();
        view.table.requestActions();
        compare(view.actionsMenuPanel.opened, true);
        const items = view.actionsMenuPanel.items;
        compare(items[0].text, "stop");
        compare(items[1].action, "retry");
        compare(items[1].key, "e");
        view.actionsMenuPanel.close();

        view.table.moveCursorTo(0);
        verify(App.torrentRow(view.cursorHash).stateText !== "error");
        view.table.forceActiveFocus();
        view.table.requestActions();
        compare(view.actionsMenuPanel.items.filter(item => item.action === "retry").length, 0);
        view.actionsMenuPanel.close();
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
        verify(view.moveSheet.opened);
        compare(view.moveSheet.focusables()[0].text, "/mnt/archive/movies");
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Escape);
        verify(!view.moveSheet.opened);
        view.table.forceActiveFocus();

        view.details.activateTab(1);
        keyClick("x");
        compare(App.torrentRow(hash).failure.kind, "move");
        view.details.activateTab(view.details.tabStory);
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
        compare(view.focusArea, "details");
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

    // C24 H9: the omaseed menu is C13's rows in C13's order (one server: no servers block),
    // then this window's block before quit; settings and help, which the hero's gear and `?`
    // were, run from it.
    function test_the_app_menu_is_c13s_rows_and_the_windows() {
        const actions = view.appMenu.items.map(item => item.separator ? "|" : item.caption ? "caption"
                                                                                                 : item.action);
        compare(actions, ["alt-speed", "stop-all", "start-all", "|", "add", "settings", "|", "caption",
                          "toggleConsole", "toggleLog", "commands", "help", "hide", "|", "quit"]);
        // D14 b: the version leads, the server after it; no "omaseed" head.
        verify(/^\d+\.\d+/.test(view.appMenu.headingTitle), view.appMenu.headingTitle);
        verify(view.appMenu.heading.indexOf(App.serverName) >= 0, view.appMenu.heading);
        const quit = view.appMenu.items[view.appMenu.items.length - 1];
        compare(quit.text, "quit omaseed");
        compare(quit.key, "ctrl+q");
        view.appMenuAction("settings");
        compare(settingsSpy.count, 1);
        view.appMenuAction("help");
        compare(view.helpOpen, true);
        view.helpOpen = false;
    }

    // docs/30 D10: the command palette is found without its key, in the omaseed menu, and the
    // ? sheet names it.
    function test_the_palette_is_found_without_its_key() {
        const row = view.appMenu.items.find(item => item.action === "commands");
        compare(row.key, ":");
        view.appMenuAction("commands");
        compare(view.commandPalette.opened, true);
        view.commandPalette.close();
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

    // C24 H9: ctrl+. opens the omaseed menu under the mark; a row's own key runs it (`?`
    // opens help), `esc` closes it, its quit asks as ctrl+q does. What only the old ⋯ menu
    // held (the limit sheets, select all) runs from the palette and the keys.
    function test_ctrl_period_opens_the_app_menu_and_its_rows_run() {
        keyClick(Qt.Key_Period, Qt.ControlModifier);
        compare(view.appMenu.opened, true);
        compare(view.activeOverlay, view.appMenu);
        // Board 51 D14 b: it grows out of the lit mark, flush with its bottom-left corner,
        // its top side open under the mark, whose ring closes the outline.
        const mark = findChild(view.rail, "railMark");
        const corner = mark.mapToItem(view, 0, mark.height);
        compare(view.appMenu.panel.x, Math.round(corner.x));
        compare(view.appMenu.panel.y, Math.round(corner.y));
        compare(view.appMenu.panel.width, Theme.appMenuWidth);
        compare(view.appMenu.jointWidth, Math.round(mark.width) - Theme.menuEdgeWidth);
        compare(findChild(view.appMenu, "menuJointEdge").visible, true);
        compare(view.rail.markLit, true);
        compare(findChild(view.rail, "railMarkRing").visible, true);
        compare(mark.color, Theme.on.selected.fill);
        keyClick(Qt.Key_Question, Qt.ShiftModifier);
        compare(view.appMenu.opened, false);
        compare(view.helpOpen, true);
        view.helpOpen = false;
        keyClick(Qt.Key_Period, Qt.ControlModifier);
        compare(view.appMenu.opened, true);
        keyClick(Qt.Key_Escape);
        compare(view.appMenu.opened, false);

        quitSpy.clear();
        view.appMenuAction("quit");
        compare(quitSpy.count, 1);

        view.runCommand("altLimits");
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

        view.runCommand("globalLimits");
        compare(view.globalLimitsSheet.opened, true);
        tryCompare(view.globalLimitsSheet, "loaded", true);
        compare(view.globalLimitsSheet.title, "Speed limits");
        compare(view.globalLimitsSheet.subtitle, "the server's global limits");
        view.globalLimitsSheet.close();

        view.runCommand("selectAll");
        compare(view.table.selectedHashes().length, App.filtered.count);
    }

    // DECISIONS C24 H10: alt held alone a moment badges the frame's keys: the mark, the
    // views and the questions, the title bar's tools and filter; with two servers the rail's
    // servers and the status line's lane; with a selection the action bar's buttons, which
    // stay where they were. Its release takes them away; alt+j's alt never shows them; an
    // overlay keeps them hidden.
    function test_alt_held_badges_the_frames_keys() {
        const key = (item, name) => findChild(item, name);
        const mark = key(view.rail, "railMarkKey");
        const seed = key(findChild(view.rail, "railView-seed"), "railRowKey");
        const arriving = key(findChild(view.rail, "railQuestion-arriving"), "railRowKey");
        const add = key(view.titleBar.addTool, "keyBadge");
        const filter = key(view.titleBar, "titleFilterKey");
        verify(!mark.visible && !arriving.visible && !add.visible);

        keyPress(Qt.Key_Alt);
        verify(view.altWatch.altHeld);
        verify(!view.keysShown, "badges before the delay");
        tryVerify(() => view.keysShown, 2000);
        verify(mark.visible);
        compare(mark.key, "ctrl+.");
        verify(seed.visible);
        compare(seed.key, "`");
        verify(arriving.visible);
        compare(arriving.key, "2");
        verify(add.visible);
        compare(add.key, "a");
        verify(filter.visible);
        compare(filter.key, "/");
        // One server: no lane badge, nothing to switch to (D19).
        verify(!key(view.statusLine, "statusServerKey").visible);
        keyRelease(Qt.Key_Alt);
        verify(!view.keysShown);
        verify(!mark.visible && !arriving.visible && !add.visible);

        // alt+j: alt goes with j's press, before the delay; nothing ever shows.
        let flashed = false;
        const watch = () => flashed = flashed || view.keysShown;
        view.keysShownChanged.connect(watch);
        keyPress(Qt.Key_Alt);
        keyClick(Qt.Key_J, Qt.AltModifier);
        wait(Theme.keyBadgeDelay * 2);
        keyRelease(Qt.Key_Alt);
        view.keysShownChanged.disconnect(watch);
        verify(!flashed, "alt+j flashed the badges");

        // Under an overlay its own keys are written in it.
        view.openAppMenu();
        keyPress(Qt.Key_Alt);
        wait(Theme.keyBadgeDelay * 2);
        verify(view.altWatch.altHeld);
        verify(!view.keysShown);
        keyRelease(Qt.Key_Alt);
        view.appMenu.close();

        // Two servers: the rail's block and the status line's lane say ctrl+s.
        testCase._twoServers();
        keyPress(Qt.Key_Alt);
        tryVerify(() => view.keysShown, 2000);
        verify(key(view.statusLine, "statusServerKey").visible);
        verify(key(view.rail, "serversKey").visible);
        keyRelease(Qt.Key_Alt);
        testCase._noServers();

        // A selection: the action bar's buttons, each with its key, none moved.
        view.table.selectOnly(0);
        view.table.toggleSelection(1);
        tryVerify(() => view.selectionBar.visible);
        const stop = findChild(view.selectionBar, "bar_stop");
        const del = findChild(view.selectionBar, "bar_delete");
        // The ledger fills in once the selection's figures are read: measure after it.
        tryVerify(() => view.selectionBar.summary.sizeText !== undefined);
        wait(50);
        const before = [stop.mapToItem(view, 0, 0).x, del.mapToItem(view, 0, 0).x, view.selectionBar.items.join()];
        keyPress(Qt.Key_Alt);
        tryVerify(() => view.keysShown, 2000);
        verify(findChild(view.selectionBar, "barKey_stop").visible);
        compare(findChild(view.selectionBar, "barKey_delete").key, "d");
        // "esc" is its own key's word (board 46 badges neither it nor "all actions").
        verify(!findChild(view.selectionBar, "barKey_clear").visible);
        compare([stop.mapToItem(view, 0, 0).x, del.mapToItem(view, 0, 0).x, view.selectionBar.items.join()], before);
        keyRelease(Qt.Key_Alt);
        view.table.clearSelection();
    }

    // #167 assumed 4: ctrl+b folds the rail to its glyphs; the questions keep theirs, and the
    // title bar names the place the folded rail no longer shows (C24 H5; the hero's chips
    // went). `h` from the table gives the folded rail the keys; ctrl+b again opens it.
    function test_ctrl_b_folds_the_rail_and_the_title_names_the_scope() {
        App.filtered.question = TorrentFilterModel.Arriving;
        App.filtered.category = "linux";
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(App.railHidden, true);
        verify(view.rail.visible);
        verify(view.rail.compact);
        compare(view.rail.width, Theme.railFoldedWidth);
        compare(view.titleBar.x, view.rail.width);
        const arriving = findChild(view.rail, "railQuestion-arriving");
        verify(arriving.selected && arriving.compact);
        verify(findChild(view.rail, "railGroupsRow").visible);
        compare(view.titleBar.title, "linux");
        verify(view.titleBar.sentence.startsWith("arriving · "), view.titleBar.sentence);
        verify(findChild(view.titleBar, "filterChip_category") === null);

        App.filtered.clearCategory();
        compare(view.titleBar.title, "Arriving");

        view.table.forceActiveFocus();
        keyClick("h");
        compare(App.railHidden, true);
        verify(view.rail.activeFocus);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(App.railHidden, false);
        verify(!view.rail.compact);
        compare(view.rail.width, Theme.railWidth);
    }

    // DECISIONS C32 D9: the rail folds by itself when the list would get narrower than 520
    // (the details pane counted while open beside it) and opens again when there is room; a
    // hand unfold or fold holds until the next hand one.
    function test_the_rail_folds_by_itself_when_the_list_would_crowd() {
        const placement = App.detailsPlacement;
        App.detailsPlacement = "right";
        testCase.width = Theme.space(1000);
        verify(view.split.docked);
        verify(view.rail.compact);
        compare(App.railHidden, false);
        compare(view.titleBar.x, Theme.railFoldedWidth);
        // The details folded leave the list its room: open again.
        view.split.folded = true;
        verify(!view.rail.compact);
        view.split.folded = false;
        verify(view.rail.compact);
        testCase.width = 1360;
        verify(!view.rail.compact);

        // A hand unfold in a crowded window holds, through a widen and a narrow.
        testCase.width = Theme.space(1000);
        verify(view.rail.compact);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        verify(!view.rail.compact);
        compare(App.railHidden, false);
        testCase.width = 1360;
        testCase.width = Theme.space(1000);
        verify(!view.rail.compact);
        // A hand fold holds too, with room again.
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(App.railHidden, true);
        testCase.width = 1360;
        verify(view.rail.compact);
        // The edge tab is the same hand.
        view.rail.foldToggleRequested();
        verify(!view.rail.compact);
        compare(App.railHidden, false);
        App.detailsPlacement = placement;
    }

    // C32 D1 a: the rail's foot opens Settings, as `,` does.
    function test_the_rails_foot_opens_settings() {
        const row = findChild(view.rail, "railSettings");
        verify(row.visible);
        mouseClick(row, row.width / 2, row.height / 2);
        compare(settingsSpy.count, 1);
    }

    // C32 D3 a: the dragged width is the window's (App.railWidth, in space units); the
    // title bar and the body follow it; a double-click's default stores 0; let go under 120,
    // the rail folds by hand.
    function test_the_rails_width_is_kept_and_a_drag_under_120_folds_it() {
        view.rail.widthChosen(Theme.space(300), false);
        verify(App.railWidth > 0);
        verify(Math.abs(view.rail.width - Theme.space(300)) <= 1, view.rail.width);
        compare(view.titleBar.x, view.rail.width);
        compare(view.split.mapToItem(view, 0, 0).x, view.rail.width);
        // Kept out of bounds by the host too.
        App.railWidth = 9000;
        compare(view.rail.width, view.rail.maximumOpenWidth);
        view.rail.widthChosen(Theme.railWidth, true);
        compare(App.railWidth, 0);
        compare(view.rail.width, Theme.railWidth);
        view.rail.foldDragged();
        compare(App.railHidden, true);
        verify(view.rail.compact);
        view.rail.unfoldDragged();
        compare(App.railHidden, false);
        verify(!view.rail.compact);
    }

    // At the narrowest step the filter folds into its `/` tool; `/` opens it over the title,
    // and leaving it folds it again, lit while it filters.
    function test_slash_opens_the_folded_filter_over_the_title() {
        testCase.width = Theme.space(480);
        compare(view.titleBar.level, 2);
        verify(view.titleBar.searchFolded);
        verify(view.titleBar.filterTool.visible);

        view.table.forceActiveFocus();
        keyClick(Qt.Key_Slash);
        verify(view.titleBar.searchField.editing);
        verify(!findChild(view.titleBar, "titleText").visible);
        keyClick("u");
        compare(App.filtered.text, "u");
        keyClick(Qt.Key_Return);
        verify(view.table.activeFocus);
        verify(view.titleBar.searchFolded);
        verify(view.titleBar.filterTool.selected);
        verify(findChild(view.titleBar, "titleText").visible);
        view.clearFilter();
        testCase.width = 1360;
    }

    // C24 H5 (board 46 `titleBar()`): the title is where the window is, the sentence what the
    // scope holds, from the library to a question, a category, a tag, a face.
    function test_the_title_bar_names_the_scope() {
        compare(view.titleBar.title, "Library");
        compare(view.titleBar.sentence, App.scopeSentence);
        verify(view.titleBar.sentence.indexOf(" arriving") >= 0, view.titleBar.sentence);
        keyClick(Qt.Key_5);
        compare(view.titleBar.title, "Resting");
        verify(view.titleBar.sentence.endsWith("not sharing") || view.titleBar.sentence === "nothing resting",
               view.titleBar.sentence);
        keyClick(Qt.Key_0);
        App.filtered.tag = "";
        compare(view.titleBar.title, "Untagged");
        verify(view.titleBar.sentence.endsWith(" · tag"), view.titleBar.sentence);
        App.filtered.clearTag();
        // The words narrow the rows, never the place.
        App.filtered.text = "fedora";
        compare(view.titleBar.title, "Library");
        App.filtered.text = "";
        // The faces.
        view.setView("seed");
        compare(view.titleBar.title, "Seed console");
        verify(view.titleBar.sentence.indexOf("every torrent") >= 0, view.titleBar.sentence);
        view.setView("log");
        compare(view.titleBar.title, "Log");
        verify(view.titleBar.sentence.startsWith("qBittorrent's log"), view.titleBar.sentence);
        verify(!findChild(view.titleBar, "titleFilter").visible);
        view.setView("library");
        // A lost server: the list's time and the notice's headline.
        view._testServerLost = true;
        compare(view.titleBar.sentence, App.lostSentence(App.globalState.lastSyncText,
                                                         view.serverProblem.headline ?? ""));
        view._testServerLost = false;
    }

    // H5: a folder picked: its name the title, its facts the sentence, "move this folder…" a
    // title-bar tool.
    function test_a_folder_picked_gives_the_title_its_move() {
        const folder = App.move.folderOf(view.cursorHash);
        verify(folder.length > 0);
        App.filtered.folder = folder;
        compare(view.titleBar.title, folder.substring(folder.lastIndexOf("/") + 1));
        verify(view.titleBar.sentence.endsWith(folder), view.titleBar.sentence);
        verify(view.titleBar.moveFolderShown);
        verify(view.titleBar.moveFolderTool.visible);
        view.titleBar.moveFolderTool.Accessible.pressAction();
        compare(view.moveSheet.opened, true);
        view.moveSheet.close();
        App.filtered.folder = "";
        verify(!view.titleBar.moveFolderShown);
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

    // DECISIONS C18 D6 (board 45 M1): the bar's mouse hint, the first 3 times it opens after a
    // plain click; then never again.
    function test_the_selection_bar_hints_the_mouse_three_times() {
        const hint = "ctrl+click add · shift+click range";
        const click = index => {
            const row = findChild(view.table, "row_" + index);
            mouseClick(row, Theme.space(80), row.height / 2);
        };
        App.selectionHintsShown = 0;
        view.table.clearSelection();
        for (let i = 0; i < 3; ++i) {
            click(i);
            compare(view.selectionBar.hint, hint, "time " + (i + 1));
            // A plain click with the bar already open is no new opening.
            click(i + 1);
            compare(App.selectionHintsShown, i + 1);
            view.table.clearSelection();
            compare(view.selectionBar.hint, "", "the hint outlived the bar");
        }
        click(0);
        compare(view.selectionBar.hint, "");
        compare(App.selectionHintsShown, 3);
        view.table.clearSelection();
    }

    function test_the_selection_bar_shows_with_a_selection_only() {
        verify(!view.selectionBar.visible, "no selection, no bar");
        verify(view.statusLine.visible);
        compare(view.table.bottomRoom, 0);
        view.table.moveCursor(2);
        verify(!view.selectionBar.visible, "the cursor alone is not a selection (M9.2)");
        view.table.toggleCursorSelection();
        verify(view.selectionBar.visible);
        compare(view.selectionBar.count, 1);
        // C24 H8: the status line becomes the bar, so the line itself goes, and the bar is
        // docked under the rows: they need no room past the last one for it.
        verify(!view.statusLine.visible, "the status line showed under the action bar");
        compare(view.table.bottomRoom, 0);
        view.table.clearSelection();
        verify(!view.selectionBar.visible);
        verify(view.statusLine.visible);
        compare(view.table.bottomRoom, 0);
    }

    // #170: the bar's `p` word follows what is selected: "stop" for running torrents,
    // "start" for stopped ones, both for a mix (board 46 draws "stop" for three seeding).
    function test_the_action_bars_p_word_follows_the_selection() {
        const all = App.filtered.count;
        let running = "";
        let stopped = "";
        for (let i = 0; i < all; ++i) {
            view.table.selectOnly(i);
            const hash = view.table.selectedHashes()[0];
            if (App.torrentRow(hash).stopped === true)
                stopped = stopped || hash;
            else
                running = running || hash;
        }
        verify(running.length > 0 && stopped.length > 0, "the demo has no mix");
        view.table.selectHashes([running]);
        tryVerify(() => view.selectionBar.items[0] === "stop" && view.selectionBar.items.indexOf("start") < 0);
        view.table.selectHashes([stopped]);
        tryVerify(() => view.selectionBar.items[0] === "start" && view.selectionBar.items.indexOf("stop") < 0);
        view.table.selectHashes([running, stopped]);
        tryCompare(view.selectionBar, "startable", true);
        compare(view.selectionBar.items.slice(0, 2), ["start", "stop"]);
        view.table.clearSelection();
    }

    // DECISIONS C24 H8 (supersedes docs/17's floating bar): the bar is docked in the status
    // line's place, across the content beside the rail, and the rows above it don't move.
    function test_the_action_bar_docks_in_the_status_lines_place() {
        const table = view.table;
        const tableHeight = table.height;
        const line = view.statusLine;
        const lineAt = line.mapToItem(view, 0, 0);
        testCase.selectTwo();
        const bar = view.selectionBar;
        const barAt = bar.mapToItem(view, 0, 0);
        compare(barAt.x, lineAt.x);
        compare(barAt.y, lineAt.y);
        compare(bar.width, line.width);
        compare(bar.height, line.height);
        compare(barAt.y + bar.height, view.height);
        fuzzyCompare(barAt.x, view.contentLeft, 1);
        compare(table.height, tableHeight, "the rows moved when the bar came");
        verify(table.mapToItem(view, 0, table.height).y <= barAt.y, "the rows run under the bar");
        compare(bar.folded, [], "at 1360 the whole bar fits");
        // Its count is the line's facts' no more: the facts went with the line.
        verify(view.factsText.indexOf("selected") < 0);
    }

    // The status line's note ("copied") shows in the bar while the bar hides the line.
    function test_a_note_shows_in_the_action_bar() {
        testCase.selectTwo();
        view.flashFooterNote("copied");
        compare(view.selectionBar.note, "copied");
        compare(findChild(view.selectionBar, "bar_note").text, "copied");
        view.footerNote = "";
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
        // The move sheet opens with the torrent's save path already in its field.
        testCase._checkFocusComesBack(() => keyClick("m"), view.moveSheet,
                                      "the move sheet", 2);
        testCase._checkFocusComesBack(() => keyClick(Qt.Key_C, Qt.ShiftModifier),
                                      view.categoryPalette, "the category palette");
        testCase._checkFocusComesBack(() => keyClick("t"), view.tagPalette, "the tag palette");
        // The add sheet is guarded (manual box M5): the first `esc` goes to `cancel`.
        testCase._checkFocusComesBack(() => App.addRequested(), view.addSheet, "the add sheet",
                                      2);
        testCase._checkFocusComesBack(() => App.openSettings(), view.settingsModal,
                                      "the settings modal");
    }

    // M8: the title bar's "+ add ▾" (its menu's add row, #146) opens the add sheet with the
    // mouse and leaves the keyboard where it was, so closing the sheet gives it back to the
    // table: no ring or tip left on the tool, and `j` moves the cursor.
    function test_a_click_on_add_gives_the_keyboard_back_to_the_table() {
        const add = view.titleBar.addTool;
        verify(add && add.visible, "no + add in the title bar");
        const row = view.table.cursorIndex;
        mouseClick(add);
        verify(view.addMenu.opened, "+ add ▾ did not open its menu");
        view.addMenu.activate(0);
        compare(view.addSheet.opened, true, "+ add did not open the add sheet");
        keyClick(Qt.Key_Escape);
        if (view.addSheet.opened)
            keyClick(Qt.Key_Escape); // the add sheet's guard (manual box M5)
        compare(view.addSheet.opened, false);
        mouseMove(view.table, view.table.width / 2, view.table.height / 2);
        compare(add.activeFocus, false, "+ add kept the keyboard");
        compare(findChild(add, "focusRing").shown, false);
        tryCompare(view.titleBar.tip, "shown", false);
        verify(view.table.activeFocus, "the table did not get the keyboard back");
        keyClick("j");
        compare(view.table.cursorIndex, row + 1);
        view.table.moveCursorTo(row);
    }

    // Batch-2 D3: with a sheet, palette, modal or menu up, the status line keeps its facts
    // under it; the overlay's own row is the only one that teaches keys (the line teaches
    // none, C24 H10).
    function _checkFooterStandsAside(open, overlay, what) {
        open();
        compare(overlay.opened, true, what + " did not open");
        verify(view.overlayOpen, what);
        verify(view.factParts.length > 0, "the facts went with " + what);
        verify(view.statusLine.visible, "the status line went with " + what);
        overlay.close();
        verify(!view.overlayOpen, what + " stayed open");
    }

    function test_the_status_line_stays_under_every_overlay() {
        testCase._checkFooterStandsAside(() => App.addRequested(), view.addSheet, "the add sheet");
        testCase._checkFooterStandsAside(() => keyClick("L", Qt.ShiftModifier),
                                         view.limitsSheetPanel, "the limits sheet");
        testCase._checkFooterStandsAside(() => keyClick("m"), view.moveSheet,
                                         "the move sheet");
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
        testCase._checkFooterStandsAside(() => view.openAppMenu(), view.appMenu, "the omaseed menu");
        testCase._checkFooterStandsAside(() => view.globalLimitsSheet.openFor(true),
                                         view.globalLimitsSheet, "the speed limits sheet");
    }

    // The Files tab's own menus (N1) are overlays too: the facts stay under them.
    function test_the_status_line_stays_under_the_files_menus() {
        view.details.currentTab = 1;
        view.details.openPriorityMenu();
        tryVerify(() => view.details.menuOpen, 1000, "the priority menu did not open");
        verify(view.factParts.length > 0, "the facts went with the menu");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !view.details.menuOpen, 1000, "esc did not close the priority menu");
        view.details.currentTab = view.details.tabStory;
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
        verify(!view.titleBar.searchField.editing, "`/` went on to the window's search");
        view.details.activateTab(view.details.tabStory);
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
        view.details.activateTab(view.details.tabStory);
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
        view.details.activateTab(view.details.tabStory);
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

    function test_the_jump_palette_clears_the_title_bars_field_with_the_filter() {
        const hash = App.filtered.hashAt(2);
        view.titleBar.searchField.text = "archlinux";
        verify(App.filtered.rowOfHash(hash) < 0 || App.filtered.hashAt(0) === hash);
        view.jumpPalette.jumpTo(hash);
        compare(view.cursorHash, hash);
        compare(App.filtered.text, "");
        compare(view.titleBar.searchField.text, "", "the title bar kept a word that no longer filters");
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
        compare(view.moveSheet.opened, true, "move… did not open its sheet");
        view.moveSheet.close();
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
        view.details.activateTab(view.details.tabStory);
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
        verify(view.titleBar.searchField.editing);
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
        view.details.activateTab(view.details.tabStory);
    }

    // enter runs the row as its key would, and it comes first next time (D7).
    function test_a_command_runs_and_is_remembered() {
        keyClick(":");
        testCase._type("set location");
        compare(view.commandPalette.currentItem.action, "location");
        keyClick(Qt.Key_Return);
        compare(view.commandPalette.opened, false);
        compare(view.moveSheet.opened, true, "move… did not run (found by its old name)");
        view.moveSheet.close();
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
        view.details.activateTab(view.details.tabStory);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(addSpy.count, 1, "the details pane swallowed the window's `a`");
    }

    function test_tracker_keys_act() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(view.details.tabTrackers);
        view.details.forceActiveFocus();
        keyClick(Qt.Key_R, Qt.ShiftModifier);
        compare(view.footerNote, "reannounced");
        view.details.activateTab(view.details.tabStory);
    }

    // ---- ruling Q4: the add sheet and the ban confirm ---------------------------------------
    function _type(text) {
        for (const c of text)
            keyClick(c);
    }

    function test_a_in_trackers_opens_the_add_sheet_and_checks_the_urls() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(view.details.tabTrackers);
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
        view.details.activateTab(view.details.tabStory);
    }

    function test_a_in_peers_opens_the_add_sheet_for_host_and_port() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(view.details.tabSwarm);
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
        view.details.activateTab(view.details.tabStory);
    }

    // #10: the HTTP sources tab's `a` takes http or https URLs; `d` removes the row's.
    function test_http_sources_add_and_remove() {
        testCase._cursorOn("archlinux-2026.09.01-x86_64.iso");
        view.details.activateTab(view.details.tabWebSeeds);
        compare(view.details.currentTab, view.details.tabWebSeeds, "the demo's Arch ISO has no web seeds");
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
        view.details.activateTab(view.details.tabStory);
    }

    function test_ctrl_q_quits_from_the_add_sheet() {
        view.details.activateTab(view.details.tabTrackers);
        view.details.forceActiveFocus();
        keyClick("a");
        compare(view.detailsAddSheet.opened, true);
        quitSpy.clear();
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(quitSpy.count, 1, "ctrl+q was swallowed by the sheet");
        view.detailsAddSheet.close();
        view.details.activateTab(view.details.tabStory);
    }

    function test_b_in_peers_asks_before_a_permanent_ban() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(view.details.tabSwarm);
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
        view.details.activateTab(view.details.tabStory);
    }

    // #51 D15: with peers selected, `b` asks once about all of them, and `d` in Trackers
    // removes every selected tracker.
    function test_b_on_selected_peers_asks_about_them_all() {
        testCase._cursorOn("Big Buck Bunny 4K (CC-BY)");
        view.details.activateTab(view.details.tabSwarm);
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
        view.details.activateTab(view.details.tabTrackers);
        view.details.forceActiveFocus();
        keyClick(Qt.Key_A, Qt.ControlModifier);
        const count = view.details.trackersSelection.count;
        verify(count >= 2, "the demo torrent has too few trackers");
        keyClick("d");
        compare(view.footerNote, "removed " + count + " trackers");
        keyClick(Qt.Key_Escape);
        view.details.activateTab(view.details.tabStory);
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
        view.titleBar.searchField.text = "e";
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

    // ---- docs/33 (#69): servers in any state --------------------------------------------
    function _twoServers() {
        App.addServer({
                          "name": "public",
                          "url": "http://10.99.0.2:8080",
                          "authMode": "none"
                      }, "");
        App.addServer({
                          "name": "private",
                          "url": "http://10.99.0.3:8080",
                          "authMode": "none"
                      }, "");
    }

    function _noServers() {
        view.serverPalette.close();
        while (App.serverList.count > 0)
            App.serverList.remove(App.serverList.idAt(0));
    }

    // D19: one server, nothing switches; ctrl+s answers in the footer.
    function test_ctrl_s_with_one_server_answers_in_the_footer() {
        testCase._noServers();
        keyClick(Qt.Key_S, Qt.ControlModifier);
        compare(view.serverPalette.opened, false);
        compare(view.footerNote, "one server · add another in Settings › Servers");
        verify(!view.statusLine.serverSwitchable);
    }

    // D2, D3, C24 H15: two servers, the palette, and the status line's lane with its ▾, which
    // opens it too: a button named "switch server", its words the description.
    function test_ctrl_s_opens_the_server_palette_with_two() {
        testCase._twoServers();
        verify(view.statusLine.serverSwitchable);
        verify(findChild(view.statusLine, "statusServerChevron").visible);
        const lane = view.statusLine.serverLane;
        compare(lane.Accessible.role, Accessible.Button);
        compare(lane.Accessible.name, "switch server");
        compare(lane.Accessible.description, view.statusLine.serverShortText.length > 0
                && view.statusLine.level >= 1 ? view.statusLine.serverShortText : view.serverLaneText());
        verify(view.statusLine.serverTipText.endsWith("\nswitch server · ctrl+s"),
               view.statusLine.serverTipText);
        mouseClick(lane);
        compare(view.serverPalette.opened, true);
        view.serverPalette.close();
        view.focusHome();
        keyClick(Qt.Key_S, Qt.ControlModifier);
        compare(view.serverPalette.opened, true);
        compare(view.serverPalette.items.length, 2);
        compare(view.serverPalette.items.map(row => row.title), ["public", "private"]);
        // The cursor starts on the next one.
        compare(view.serverPalette.cursorIndex, 1);
        verify(view.serverPalette.items[1].detail.indexOf("10.99.0.3:8080") === 0);
        // `,` hands it to Settings › Servers on the cursor's row.
        keyClick(Qt.Key_Comma);
        compare(view.serverPalette.opened, false);
        compare(view.settingsModal.opened, true);
        compare(view.settingsModal.paneIndex, 1);
        view.settingsModal.close();
        // Settings remembers where it was; the next case expects it as it starts.
        view.settingsModal.showPane(0);
        view.settingsModal.area = "nav";
        testCase._noServers();
    }

    // Board 51 D4 a (DECISIONS C32): a right-click on a server in the rail opens its menu:
    // "switch to it" first on another; the current one's limits, stop all and start all; both
    // then edit…, test, web UI, copy its address, remove…. The menu key opens it too.
    function test_a_right_click_on_a_rail_server_opens_its_menu() {
        testCase._twoServers();
        const ids = [App.serverList.idAt(0), App.serverList.idAt(1)];
        const current = ids.find(id => App.serverList.serverMap(id).isDefault === true);
        const other = ids.find(id => id !== current);
        const otherName = App.serverList.serverMap(other).name;
        const row = findChild(view.rail, "serverRow-" + otherName);
        verify(row !== null && row.visible, "no rail row for " + otherName);
        compare(row.hasMenu, true);
        const currentRow = findChild(view.rail, "serverRow-" + App.serverList.serverMap(current).name);
        // The rail's column lays the two rows out on its next polish.
        tryVerify(() => currentRow.mapToItem(null, 0, 0).y !== row.mapToItem(null, 0, 0).y);

        mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton);
        const menu = view.serverMenuPanel;
        compare(menu.opened, true);
        compare(view.activeOverlay, menu);
        compare(menu.serverId, other);
        compare(menu.headingTitle, otherName);
        verify(menu.heading.indexOf("10.99.0.") === 0, menu.heading);
        const actions = menu.items.map(item => item.separator ? "|" : item.action);
        compare(actions, ["switch", "|", "edit", "test", "web", "copy", "|", "remove"]);
        verify(menu.items.every(item => item.separator === true || item.glyph.length > 0));
        compare(menu.items[menu.items.length - 1].danger, true);
        compare(menu.cursorIndex, 0);
        compare(view.rail.cursorIndex, row.globalIndex);
        // copy its address: the URL, said in the footer.
        menu.activate(menu.items.findIndex(item => item.action === "copy"));
        compare(menu.opened, false);
        compare(view.footerNote, "copied " + otherName + "'s address");

        // The current one: its own rows first.
        mouseClick(currentRow, currentRow.width / 2, currentRow.height / 2, Qt.RightButton);
        compare(menu.opened, true);
        compare(menu.items.slice(0, 4).map(item => item.action ?? item.text),
                ["the current server", "alt-speed", "stop-all", "start-all"]);
        compare(menu.items[0].disabled, true);
        compare(menu.cursorIndex, 1);
        // edit… opens Settings › Servers on that server, its form.
        menu.activate(menu.items.findIndex(item => item.action === "edit"));
        compare(view.settingsModal.opened, true);
        compare(view.settingsModal.paneIndex, 1);
        compare(App.serverList.idAt(view.settingsModal.paneCursor), current);
        compare(view.settingsModal.serverSheet.opened, true);
        compare(view.settingsModal.serverSheet.editing, true);
        view.settingsModal.serverSheet.close();
        view.settingsModal.close();
        view.settingsModal.showPane(0);
        view.settingsModal.area = "nav";

        // The keyboard: the rail's cursor on the other row, the menu key.
        view.rail.forceActiveFocus();
        view.rail.cursorIndex = row.globalIndex;
        keyClick(Qt.Key_Menu);
        compare(menu.opened, true);
        compare(menu.serverId, other);
        menu.close();
        view.focusHome();
        testCase._noServers();
    }

    // D4, D23: a lost server lets ctrl+s through and says why another key waits.
    function test_the_guard_lets_the_switch_through_and_says_why_others_wait() {
        testCase._twoServers();
        view._testServerLost = true;
        wait(0);
        verify(view.serverNotice.shown);
        keyClick("p");
        compare(actionSpy.count, 0);
        verify(view.footerNote.indexOf("p waits until it answers") >= 0, view.footerNote);
        // The status line: the lane takes C12's words, its dot red (or yellow while retried,
        // docs/35 N3), and the facts give the list's time.
        verify(view.statusLine.serverText.startsWith(App.serverName + " · "), view.statusLine.serverText);
        compare(view.statusLine.serverShortText, "");
        verify(Qt.colorEqual(view.statusLine.dotColor, view.serverRetrying ? Theme.yellow : Theme.red));
        verify(/^(list from \d\d:\d\d|not synced yet)$/.test(view.factsText), view.factsText);
        keyClick(Qt.Key_S, Qt.ControlModifier);
        compare(view.serverPalette.opened, true);
        view.serverPalette.close();
        view._testServerLost = false;
        testCase._noServers();
    }

    // D1: the empty library keeps the rail's servers block with two servers, and none with one.
    function test_the_empty_library_keeps_the_servers_block() {
        App.loadEmptyDemo();
        compare(view.libraryEmpty, true);
        // C24 H2: the rail keeps its mark and views; one server has no block, nothing filters.
        compare(view.rail.visible, true);
        compare(view.rail.serversOnly, true);
        compare(findChild(view.rail, "serversBlock").visible, false);
        testCase._twoServers();
        compare(view.rail.visible, true);
        compare(view.rail.serversOnly, true);
        compare(findChild(view.rail, "serversBlock").visible, true);
        compare(findChild(view, "emptyState").headline.indexOf("No torrents on"), 0);
        testCase._noServers();
        App.loadDemo(16);
        compare(view.rail.serversOnly, false);
    }

    // docs/35 N8: an action's failure stays in the footer, and its details dock above it
    // over the table; esc closes them, nothing moves.
    function test_a_failures_details_dock_above_the_footer() {
        const table = view.table.y;
        view.flashFooterNote("qBittorrent can't create /proc/omaseed: check that the drive is mounted and its parent folder exists");
        view.footerNoteSticky = true;
        view.footerFailure = {
            "headline": "qBittorrent can't create /proc/omaseed",
            "line": "Check that the drive is mounted and its parent folder exists.",
            "details": "conflict (409) · POST torrents/setLocation · server replied: Conflict",
            "sentence": "x"
        };
        verify(findChild(view, "footerFailureDetails").visible);
        // #77: "details" is a button a screen reader presses: it opens the band and closes it.
        const detailsButton = findChild(view, "footerFailureDetails");
        compare(detailsButton.Accessible.role, Accessible.Button);
        compare(detailsButton.Accessible.name, "details");
        detailsButton.Accessible.pressAction();
        compare(view.footerNoticeOpen, true);
        detailsButton.Accessible.pressAction();
        compare(view.footerNoticeOpen, false);
        view.footerNoticeOpen = true;
        const band = findChild(view, "footerNotice");
        verify(band.shown);
        verify(band.opaque);
        compare(band.edge, "top");
        compare(view.table.y, table);
        keyClick(Qt.Key_Escape);
        compare(view.footerNoticeOpen, false);
        view.flashFooterNote("");
    }

    // #145 (C24 H16, DECISIONS C26 SR14): ctrl+f from anywhere opens search, the rail's view
    // after log; esc on an empty field goes back to where the window was.
    function test_ctrl_f_opens_search_and_esc_comes_back() {
        App.loadDemoSearch("done");
        verify(findChild(view.rail, "railView-search") !== null);
        keyClick(Qt.Key_F, Qt.ControlModifier);
        compare(view.view, "search");
        verify(view.searchShown);
        const search = findChild(view, "searchView");
        verify(search.visible);
        verify(search.field.editing);
        compare(view.scopeTitle, "Search");
        verify(view.scopeSentence.indexOf("“open movie night”") === 0, view.scopeSentence);
        verify(!view.split.visible);
        // `/` filters the results, not the library.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Slash);
        verify(view.titleBar.searchField.editing);
        keyClick("1");
        keyClick("0");
        keyClick("8");
        tryCompare(App.searchResults, "filterText", "108");
        compare(App.filtered.text, "");
        keyClick(Qt.Key_Escape);
        compare(App.searchResults.filterText, "");
        // esc back out: the rows, the field, then the library.
        search.focusField();
        search.field.text = "";
        keyClick(Qt.Key_Escape);
        compare(view.view, "library");
        App.loadDemoSearch("off");
    }

    // #146 (C24 H17, C27 CR1): ctrl+n opens the create sheet; "+ add ▾" has add, create and
    // search; `:` has "create torrent…" and, on a picked folder, "create torrent from NAME…";
    // a folder row's menu opens it on that folder.
    function test_ctrl_n_and_add_menu_open_create() {
        verify(App.createOffered);
        keyClick(Qt.Key_N, Qt.ControlModifier);
        verify(view.createSheet.opened);
        compare(view.activeOverlay, view.createSheet);
        compare(view.createSheet.folder, "");
        view.createSheet.close();

        compare(view.titleBar.addTool.text, "add ▾");
        mouseClick(view.titleBar.addTool);
        verify(view.addMenu.opened);
        compare(view.addMenu.items.map(row => row.action ?? "-"), ["add", "create", "-", "search"]);
        view.addMenu.activate(1);
        verify(view.createSheet.opened);
        view.createSheet.close();

        const path = App.torrentRow(view.table.cursorHash).savePath.replace(/\/+$/, "");
        App.filtered.folder = path;
        const rows = Keymap.commandRows(view.commandContext()).map(row => row.title);
        verify(rows.indexOf("create torrent…") >= 0, rows);
        const name = path.substring(path.lastIndexOf("/") + 1);
        verify(rows.indexOf("create torrent from " + name + "…") >= 0, rows);
        view.runCommand("createFromFolder");
        verify(view.createSheet.opened);
        compare(view.createSheet.folder, path);
        view.createSheet.close();

        view.folderMenuPanel.folder = path;
        view.folderMenuPanel.openAt(100, 100);
        view.folderMenuPanel.activate(1);
        verify(view.createSheet.opened);
        compare(view.createSheet.folderField.text, path + "/");
        view.createSheet.close();
    }

    // C26 SR14: the palette's "search…" row and the no-match row reach search.
    function test_the_palette_reaches_search() {
        App.loadDemoSearch("done");
        view.runCommand("search");
        compare(view.view, "search");
        view.setView("library");
        view.runCommand(Keymap.searchForPrefix + "open movie");
        compare(view.view, "search");
        compare(findChild(view, "searchView").field.text, "open movie");
        view.setView("library");
        App.loadDemoSearch("off");
    }
}
