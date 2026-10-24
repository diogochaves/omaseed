import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// docs/17 M9 in the action bar (DECISIONS C24 H8, board 46's word buttons, #170): the bar on
// its own. What it draws at a width (M9.3, M9.5), its form from `Theme.actionBar*`, what a
// click on each word says, `p`'s word for the selection, the note in the hint's place, and
// that it never takes the keyboard (M9.4). Where it docks and when it shows are MainView's
// (tst_Main).
TestCase {
    id: testCase

    height: 300
    name: "SelectionBar"
    visible: true
    when: windowShown
    width: 1200

    // Board 46's middle group, `p`'s word for an all-running selection first.
    readonly property var fullItems: ["stop", "recheck", "location", "category", "tags", "limits",
        "delete"]

    Component {
        id: barComponent

        SelectionBar {
            availableWidth: 2000
            count: 2
        }
    }

    // Holds the keyboard while a button is clicked (the table, in the app).
    Component {
        id: keeperComponent

        Item {
            focus: true
            height: 10
            width: 10
        }
    }

    SignalSpy {
        id: triggeredSpy

        signalName: "triggered"
    }

    SignalSpy {
        id: moreSpy

        signalName: "moreRequested"
    }

    function makeBar(properties) {
        const bar = createTemporaryObject(barComponent, testCase, properties || {});
        bar.width = Qt.binding(() => bar.implicitWidth);
        bar.height = Qt.binding(() => bar.implicitHeight);
        triggeredSpy.target = bar;
        triggeredSpy.clear();
        moreSpy.target = bar;
        moreSpy.clear();
        return bar;
    }

    function button(bar, action) {
        return findChild(bar, "bar_" + action);
    }

    // ---- M9.3: which, in which order ------------------------------------------------------
    // DECISIONS C18 D6 (board 45 M1): the mouse's hint after the buttons, while there is room;
    // it goes before the ledger or any button.
    function test_the_mouse_hint_follows_the_buttons_and_goes_first() {
        const plain = makeBar();
        const bar = makeBar({
                                "hint": "ctrl+click add · shift+click range"
                            });
        verify(bar.showHint);
        const hint = findChild(bar, "bar_hint");
        compare(hint.text, "ctrl+click add · shift+click range");
        verify(bar.implicitWidth > plain.implicitWidth);
        waitForRendering(bar);
        const last = button(bar, "delete");
        verify(hint.mapToItem(bar, 0, 0).x > last.mapToItem(bar, 0, 0).x, "the hint is not after the buttons");
        // Just too narrow for the hint: it goes, and every button stays.
        bar.availableWidth = plain.contentWidth + 1;
        verify(!bar.showHint);
        compare(bar.folded, []);
        compare(bar.implicitWidth, plain.implicitWidth);
    }

    function test_a_wide_bar_draws_every_word_in_the_boards_order() {
        const bar = makeBar();
        compare(bar.items, testCase.fullItems);
        compare(bar.folded, []);
        verify(bar.fit.word, "the count should keep its word when there is room");
        compare(findChild(bar, "selectionCount").text, "2");
        // #79: the count in words, for a screen reader.
        compare(findChild(bar, "selectionCount").Accessible.role, Accessible.StaticText);
        compare(findChild(bar, "selectionCount").Accessible.name, "2 selected");
        // Words, not glyphs (board 46 `.ab`).
        compare(button(bar, "location").text, "move…");
        compare(button(bar, "more").text, "all actions");
        compare(button(bar, "clear").text, "esc");
        compare(button(bar, "recheck").glyph, "");
        // reannounce and open folder live in all actions.
        verify(!button(bar, "reannounce") && !button(bar, "openFolder"));
    }

    // Board 46: "all actions" and "esc" at the right end, past the hint's room.
    function test_all_actions_and_esc_end_the_bar() {
        const bar = makeBar();
        bar.width = 1100;
        waitForRendering(bar);
        const esc = button(bar, "clear");
        const more = button(bar, "more");
        compare(Math.round(esc.mapToItem(bar, esc.width, 0).x), bar.width - Theme.actionBarInsetEnd);
        compare(Math.round(more.mapToItem(bar, more.width, 0).x + Theme.actionBarGap),
                Math.round(esc.mapToItem(bar, 0, 0).x));
        verify(more.mapToItem(bar, 0, 0).x - button(bar, "delete").mapToItem(bar, button(bar, "delete").width, 0).x
               >= Theme.actionBarEndGap, "all actions sits on the middle group");
    }

    // `p`'s word follows the selection: board 46's "stop" for a running one, "start" for a
    // stopped one, both for a mix.
    function test_p_s_word_follows_the_selection() {
        const bar = makeBar();
        compare(bar.items[0], "stop");
        verify(bar.items.indexOf("start") < 0);
        bar.startable = true;
        compare(bar.items.slice(0, 2), ["start", "stop"]);
        bar.stoppable = false;
        compare(bar.items[0], "start");
        verify(bar.items.indexOf("stop") < 0);
    }

    function test_each_tooltip_names_its_key() {
        const bar = makeBar({
                                "startable": true
                            });
        compare(button(bar, "recheck").tooltip, "recheck · c");
        compare(button(bar, "start").tooltip, "start · p");
        compare(button(bar, "location").tooltip, "move… · m");
        compare(button(bar, "more").tooltip, "all actions · enter");
        compare(button(bar, "delete").tooltip, "delete · d");
        compare(button(bar, "clear").tooltip, "clear selection · esc");
        verify(button(bar, "recheck").tooltipAbove, "the bar's tips open over it");
    }

    // #78: a screen reader hears the action; the key is the description, not the name.
    function test_each_button_is_named_by_its_action_and_the_key_is_the_description() {
        const bar = makeBar({
                                "startable": true
                            });
        const cases = [["recheck", "recheck", "shortcut c"], ["start", "start", "shortcut p"],
                       ["location", "move…", "shortcut m"],
                       ["more", "all actions", "shortcut enter"],
                       ["delete", "delete", "shortcut d"],
                       ["clear", "clear selection", "shortcut esc"]];
        for (const [action, name, description] of cases) {
            compare(button(bar, action).Accessible.name, name);
            compare(button(bar, action).Accessible.description, description);
        }
    }

    function test_delete_is_red() {
        const bar = makeBar();
        verify(button(bar, "delete").danger);
        verify(!button(bar, "recheck").danger);
    }

    // ---- M9.5: narrow windows --------------------------------------------------------------
    // Board 46's narrow bar keeps stop, move… and delete: the others fold from the right.
    function test_words_fold_into_all_actions_from_the_right() {
        const bar = makeBar();
        const order = ["limits", "tags", "category", "recheck"];
        compare(bar.foldOrder, order);
        for (let folds = 1; folds <= order.length; ++folds) {
            // Exactly the width of the bar with `folds` gone: one pixel less would fold one more.
            bar.availableWidth = bar.widthOf(folds, true, false, false);
            compare(bar.folded, order.slice(0, folds), "at " + folds + " folded");
            for (const action of order.slice(0, folds))
                verify(bar.items.indexOf(action) < 0, action + " should have folded");
            for (const action of ["stop", "location", "delete"])
                verify(bar.items.indexOf(action) >= 0, action + " must always stay");
            compare(bar.contentWidth, bar.availableWidth);
        }
        compare(bar.items, ["stop", "location", "delete"]);
    }

    // The ledger goes after the hint and before any word.
    function test_the_ledger_goes_before_any_word() {
        const bar = makeBar({
                                "summary": {
                                    "sizeText": "81 KB",
                                    "givenText": "0 B",
                                    "ratioText": "0.00"
                                }
                            });
        verify(bar.showSummary);
        bar.availableWidth = bar.widthOf(0, true, true, false) - 1;
        verify(!bar.showSummary);
        compare(bar.folded, []);
    }

    function test_the_count_drops_its_word_last() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(4, true, false, false);
        verify(bar.fit.word, "the word goes only after every word that can fold has");
        bar.availableWidth = bar.widthOf(4, true, false, false) - 1;
        verify(!bar.fit.word);
        compare(bar.folded.length, 4);
        verify(bar.contentWidth < bar.widthOf(4, true, false, false));
    }

    function test_the_bar_never_wraps() {
        const bar = makeBar({
                                "availableWidth": 10
                            });
        // Nothing left to fold: it keeps its one row and is simply wider than asked.
        compare(bar.items, ["stop", "location", "delete"]);
        compare(bar.height, bar.implicitHeight);
        compare(bar.implicitHeight, Theme.actionBarHeight);
        verify(button(bar, "more") && button(bar, "clear"), "the end went");
    }

    // C24 H8: the status line's own height and insets, a fill for its ground, word buttons
    // inside the line: the bar is the line, acting, and nothing above it moves.
    function test_its_form_is_the_status_lines() {
        const bar = makeBar();
        waitForRendering(bar);
        compare(Theme.actionBarHeight, Theme.statusLineHeight);
        compare(bar.color, Theme.fill(Theme.actionBarFill));
        compare(bar.radius, 0);
        compare(bar.border.width, 0);
        const count = findChild(bar, "selectionCount");
        compare(Math.round(count.mapToItem(bar, 0, 0).x), Theme.actionBarInsetStart);
        const stop = button(bar, "stop");
        compare(stop.height, Theme.actionBarButtonSize);
        compare(stop.width, bar.wordWidth("stop"));
        compare(stop.sidePadding, Theme.actionBarWordPadding);
        compare(stop.fontSize, Theme.actionBarFontSize);
        verify(stop.mapToItem(bar, 0, 0).y >= 0 && stop.mapToItem(bar, 0, stop.height).y <= bar.height,
               "a button sticks out of the line");
        compare(bar.implicitWidth, Theme.actionBarInsetStart + bar.contentWidth + Theme.actionBarInsetEnd);
    }

    // The status line's note ("copied", a failure) takes the hint's place while the bar hides
    // the line, in the line's colours; it goes when there is no room, as the hint does.
    function test_the_note_takes_the_hints_place() {
        const bar = makeBar({
                                "hint": "ctrl+click add · shift+click range",
                                "note": "copied"
                            });
        verify(bar.showHint);
        verify(!findChild(bar, "bar_hint"), "the hint showed beside the note");
        const note = findChild(bar, "bar_note");
        compare(note.text, "copied");
        // The bar's words on its own ground, the hover fill (DECISIONS C30 F1).
        compare(note.color, Theme.actionBarGround.accent);
        compare(note.Accessible.role, Accessible.AlertMessage);
        bar.noteWarn = true;
        compare(note.color, Theme.actionBarGround.yellow);
        bar.note = "";
        compare(findChild(bar, "bar_hint").text, "ctrl+click add · shift+click range");
        compare(findChild(bar, "bar_hint").color, Theme.actionBarGround.dimForeground);
    }

    // ---- what a click says -----------------------------------------------------------------
    function test_a_button_names_its_action() {
        const bar = makeBar({
                                "startable": true
                            });
        waitForRendering(bar);
        for (const action of ["start", "stop", "recheck", "category", "tags", "location", "limits",
                              "delete"]) {
            triggeredSpy.clear();
            mouseClick(button(bar, action));
            compare(triggeredSpy.count, 1, action);
            compare(triggeredSpy.signalArguments[0][0], action);
        }
        compare(moreSpy.count, 0);
    }

    function test_all_actions_asks_for_the_menu_with_what_folded() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(3, true, false, false);
        waitForRendering(bar);
        mouseClick(button(bar, "more"));
        compare(moreSpy.count, 1);
        compare(moreSpy.signalArguments[0][0], button(bar, "more"));
        compare(moreSpy.signalArguments[0][1], ["limits", "tags", "category"]);
        compare(triggeredSpy.count, 0);
    }

    // ---- M9.4: mouse only ------------------------------------------------------------------
    function test_a_click_leaves_the_keyboard_where_it_was() {
        const keeper = createTemporaryObject(keeperComponent, testCase);
        const bar = makeBar();
        waitForRendering(bar);
        keeper.forceActiveFocus();
        verify(keeper.activeFocus);
        mouseClick(button(bar, "recheck"));
        compare(triggeredSpy.count, 1);
        verify(keeper.activeFocus, "the click took the keyboard from where it was");
        verify(!button(bar, "recheck").activeFocus);
    }

    function test_tab_never_lands_on_the_bar() {
        const bar = makeBar();
        for (const action of testCase.fullItems.concat(["more", "clear"]))
            verify(!button(bar, action).activeFocusOnTab, action + " is a tab stop");
    }

    function test_a_click_on_the_bars_ground_goes_no_further() {
        const bar = makeBar();
        waitForRendering(bar);
        // Its own padding: nothing runs, and (in the app) the row under it is not clicked.
        mouseClick(bar, 2, bar.height / 2);
        compare(triggeredSpy.count, 0);
        compare(moreSpy.count, 0);
    }

    // ---- #51 D2: "esc" at the end clears -------------------------------------------------------
    function test_esc_clears_and_keeps_the_keyboard() {
        const keeper = createTemporaryObject(keeperComponent, testCase);
        const bar = makeBar();
        const spy = createTemporaryObject(clearSpyComponent, testCase, {
                                              "target": bar
                                          });
        waitForRendering(bar);
        const clear = button(bar, "clear");
        verify(clear.visible);
        verify(clear.tooltipAbove);
        verify(!clear.activeFocusOnTab);
        keeper.forceActiveFocus();
        mouseClick(clear);
        compare(spy.count, 1);
        compare(triggeredSpy.count, 0);
        verify(keeper.activeFocus, "esc took the keyboard (M9.4)");
    }

    // The end stays when the count drops its word, and the bar's sums count it.
    function test_the_end_stays_on_a_narrow_bar() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(4, true, false, false) - 1;
        verify(!bar.fit.word);
        waitForRendering(bar);
        verify(button(bar, "clear").visible && button(bar, "more").visible);
        compare(bar.contentWidth, bar.widthOf(4, false, false, false));
    }

    Component {
        id: clearSpyComponent

        SignalSpy {
            signalName: "clearRequested"
        }
    }

    // #77: the sums are a "seed console" button, the figures its description; a screen
    // reader's press is the click, once; disabled, nothing.
    function test_the_summary_takes_the_accessible_press() {
        const bar = makeBar();
        bar.summary = {
            "sizeText": "3.65 GB",
            "givenText": "572 MB",
            "ratioText": "0.60"
        };
        const summary = findChild(bar, "bar_summary");
        compare(summary.Accessible.role, Accessible.Button);
        compare(summary.Accessible.name, "seed console");
        compare(summary.Accessible.description, "3.65 GB · given 572 MB · ratio 0.60");
        let clicks = 0;
        bar.summaryClicked.connect(() => clicks++);
        summary.Accessible.pressAction();
        compare(clicks, 1);
        bar.enabled = false;
        summary.Accessible.pressAction();
        compare(clicks, 1, "a disabled bar took the press");
    }

    // DECISIONS C24 H10: alt held, each word of the middle group shows its key over its corner
    // (board 46 `.ab .kb`); "all actions" and "esc" have none, and nothing moves.
    function test_alt_badges_every_words_key_and_moves_nothing() {
        const bar = createTemporaryObject(barComponent, testCase, {
                                              "availableWidth": 600
                                          });
        waitForRendering(bar);
        const items = bar.items.join();
        const stopX = button(bar, "stop").mapToItem(bar, 0, 0).x;
        verify(!findChild(bar, "barKey_stop").visible);
        bar.keysShown = true;
        compare(bar.items.join(), items);
        compare(button(bar, "stop").mapToItem(bar, 0, 0).x, stopX);
        verify(!findChild(bar, "barKey_more").visible);
        verify(!findChild(bar, "barKey_clear").visible);
        for (const action of bar.items) {
            const badge = findChild(bar, "barKey_" + action);
            verify(badge.visible, action);
            compare(badge.key, bar.buttons[action].key, action);
        }
    }
}
