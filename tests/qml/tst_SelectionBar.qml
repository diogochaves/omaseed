import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// Board 51 D17 (DECISIONS C32) in the action bar on its own: the torrent menu's groups as
// buttons with icons and words (D17.1 a), the folds at a width (D17.2 a, docs/17 M9.5), its
// form from `Theme.actionBar*`, what a click on each says, `p`'s word for the selection, and
// that it never takes the keyboard (M9.4). Where it floats and when it shows are MainView's
// (tst_Main).
TestCase {
    id: testCase

    height: 300
    name: "SelectionBar"
    visible: true
    when: windowShown
    width: 1200

    // Board 51 `ACT`, `p`'s word for an all-running selection first; more aside.
    readonly property var fullItems: ["stop", "recheck", "location", "category", "tags", "limits",
        "delete"]

    // The torrent menu, for its groups.
    ActionsMenu {
        id: menuData
    }

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
        id: menuSpy

        signalName: "menuRequested"
    }

    function makeBar(properties) {
        const bar = createTemporaryObject(barComponent, testCase, Object.assign({
                                                                                    "groups": menuData.groups
                                                                                }, properties || {}));
        bar.width = Qt.binding(() => bar.implicitWidth);
        bar.height = Qt.binding(() => bar.implicitHeight);
        triggeredSpy.target = bar;
        triggeredSpy.clear();
        menuSpy.target = bar;
        menuSpy.clear();
        return bar;
    }

    function button(bar, action) {
        return findChild(bar, "bar_" + action);
    }

    function step(folds, words, word, ledger, hint) {
        return {
            "folds": folds,
            "words": words,
            "word": word,
            "ledger": ledger,
            "hint": hint
        };
    }

    // ---- D17.1 a: which, in which order ------------------------------------------------------
    function test_a_wide_bar_draws_the_menus_rows_in_its_groups() {
        const bar = makeBar();
        compare(bar.items, testCase.fullItems);
        compare(bar.folded, []);
        verify(bar.fit.word, "the count keeps its word when there is room");
        compare(findChild(bar, "selectionCount").text, "2");
        // #79: the count in words, for a screen reader.
        compare(findChild(bar, "selectionCount").Accessible.role, Accessible.StaticText);
        compare(findChild(bar, "selectionCount").Accessible.name, "2 selected");
        // Icons and words, the menu's own (F15): move…'s, recheck's.
        const move = menuData.groups[1].items[0];
        compare(button(bar, "location").text, move.text);
        compare(button(bar, "location").glyph, move.glyph);
        compare(button(bar, "recheck").glyph, Glyphs.recheck);
        compare(button(bar, "stop").glyph, Glyphs.stop);
        // Category and tags open their lists: the ›, not the menu's "…".
        compare(button(bar, "category").text, "category");
        compare(button(bar, "category").trailingGlyph, Glyphs.chevronRight);
        compare(button(bar, "tags").text, "tags");
        compare(button(bar, "more").text, "more");
        compare(button(bar, "more").trailingGlyph, Glyphs.chevronRight);
        compare(button(bar, "location").trailingGlyph, "");
        compare(button(bar, "clear").text, "esc");
        // The rest of the menu lives in more.
        verify(!button(bar, "reannounce") && !button(bar, "openFolder") && !button(bar, "rename"));
    }

    // Board 51: "limits… more ›" share the transfer group, then delete… alone, then esc.
    function test_the_groups_are_ruled_and_more_ends_transfer() {
        const bar = makeBar();
        waitForRendering(bar);
        const x = action => button(bar, action).mapToItem(bar, 0, 0).x;
        const order = ["stop", "recheck", "location", "category", "tags", "limits", "more", "delete", "clear"];
        for (let i = 1; i < order.length; ++i)
            verify(x(order[i]) > x(order[i - 1]), order[i] + " after " + order[i - 1]);
        // Rules between groups: stop│move, limits│… and more│delete stand apart by a rule.
        const gap = (a, b) => x(b) - (x(a) + button(bar, a).width);
        const plain = gap("stop", "recheck");
        compare(plain, Theme.actionBarGap);
        for (const [a, b] of [["recheck", "location"], ["tags", "limits"], ["more", "delete"], ["delete", "clear"]])
            compare(gap(a, b), 2 * Theme.actionBarGap + bar._ruleWidth, a + " │ " + b);
        compare(gap("limits", "more"), Theme.actionBarGap);
    }

    // `p`'s word follows the selection: "stop" for a running one, "start" for a stopped one,
    // both for a mix (H8).
    function test_p_s_word_follows_the_selection() {
        const bar = makeBar();
        compare(bar.items[0], "stop");
        verify(bar.items.indexOf("start") < 0);
        bar.startable = true;
        compare(bar.items.slice(0, 2), ["start", "stop"]);
        compare(button(bar, "start").glyph, Glyphs.start);
        bar.stoppable = false;
        compare(bar.items[0], "start");
        verify(bar.items.indexOf("stop") < 0);
        compare(bar.menuActionsShown[0], "stopStart");
    }

    function test_each_tooltip_names_its_key() {
        const bar = makeBar({
                                "startable": true
                            });
        compare(button(bar, "recheck").tooltip, "recheck · c");
        compare(button(bar, "start").tooltip, "start · p");
        compare(button(bar, "location").tooltip, "move… · m");
        compare(button(bar, "category").tooltip, "category · C");
        compare(button(bar, "more").tooltip, "more");
        compare(button(bar, "delete").tooltip, "delete… · d");
        compare(button(bar, "clear").tooltip, "clear selection · esc");
        verify(button(bar, "recheck").tooltipAbove, "the bar's tips open over it");
    }

    // #78: a screen reader hears the action; the key is the description, not the name.
    function test_each_button_is_named_by_its_action_and_the_key_is_the_description() {
        const bar = makeBar({
                                "startable": true
                            });
        const cases = [["recheck", "recheck", "shortcut c"], ["start", "start", "shortcut p"],
                       ["location", "move", "shortcut m"], ["category", "category", "shortcut C"],
                       ["more", "more", ""], ["delete", "delete", "shortcut d"],
                       ["clear", "clear selection", ""]];
        for (const [action, name, description] of cases) {
            compare(button(bar, action).Accessible.name, name, action);
            compare(button(bar, action).Accessible.description, description, action);
        }
    }

    function test_delete_is_red_and_the_icons_rest_dim() {
        const bar = makeBar();
        verify(button(bar, "delete").danger);
        verify(!button(bar, "recheck").danger);
        verify(button(bar, "recheck").glyphDim);
    }

    // ---- D17.2 a, M9.5: narrow --------------------------------------------------------------
    // The hint goes first, then the ledger, then the words, then the count's word, then
    // limits, tags, category and recheck fold into more.
    function test_the_hint_goes_first_then_the_ledger() {
        const bar = makeBar({
                                "hint": "ctrl+click add · shift+click range",
                                "summary": {
                                    "sizeText": "81 KB",
                                    "givenText": "194 KB",
                                    "ratioText": "2.1"
                                }
                            });
        verify(bar.showHint && bar.showSummary);
        waitForRendering(bar);
        const hint = findChild(bar, "bar_hint");
        compare(hint.text, "ctrl+click add · shift+click range");
        verify(hint.mapToItem(bar, 0, 0).x > button(bar, "delete").mapToItem(bar, 0, 0).x,
               "the hint is not after the buttons");
        bar.availableWidth = bar.widthOf(step(0, true, true, true, false));
        verify(!bar.showHint && bar.showSummary && bar.showWords);
        bar.availableWidth -= 1;
        verify(!bar.showSummary && bar.showWords);
        compare(bar.folded, []);
    }

    function test_the_words_go_before_any_fold_and_the_tooltip_keeps_them() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(step(0, true, true, false, false)) - 1;
        verify(!bar.showWords);
        verify(bar.fit.word, "the count keeps its word a step longer");
        compare(bar.folded, []);
        compare(bar.items, testCase.fullItems);
        waitForRendering(bar);
        const move = button(bar, "location");
        compare(move.text, "");
        compare(move.width, Theme.actionBarButtonSize);
        compare(move.tooltip, "move… · m");
        compare(move.Accessible.name, "move");
        compare(button(bar, "category").trailingGlyph, "");
    }

    // At 480 board 51 keeps only stop, move, more and delete.
    function test_rows_fold_into_more_from_the_right() {
        const bar = makeBar();
        const order = ["limits", "tags", "category", "recheck"];
        compare(bar.foldOrder, order);
        bar.availableWidth = bar.widthOf(step(0, false, false, false, false));
        verify(!bar.fit.word);
        compare(bar.folded, []);
        for (let folds = 1; folds <= order.length; ++folds) {
            // Exactly the width with `folds` gone: one pixel less would fold one more.
            bar.availableWidth = bar.widthOf(step(folds, false, false, false, false));
            compare(bar.folded, order.slice(0, folds), "at " + folds + " folded");
            for (const action of ["stop", "location", "delete"])
                verify(bar.items.indexOf(action) >= 0, action + " must always stay");
            compare(bar.implicitWidth, bar.availableWidth);
        }
        compare(bar.items, ["stop", "location", "delete"]);
        verify(button(bar, "more").visible, "more stays");
        // More's list leaves out only what the bar still shows.
        compare(bar.menuActionsShown, ["stopStart", "location", "delete"]);
    }

    function test_the_bar_never_wraps() {
        const bar = makeBar({
                                "availableWidth": 10
                            });
        compare(bar.items, ["stop", "location", "delete"]);
        compare(bar.height, bar.implicitHeight);
        compare(bar.implicitHeight, Theme.actionBarHeight);
        verify(button(bar, "more") && button(bar, "clear"), "the end went");
    }

    // Board 51 `.abar`, `.ab2`: 30 px buttons inside 4 px of padding, on the strip's plate.
    function test_its_form() {
        const bar = makeBar();
        waitForRendering(bar);
        compare(Theme.actionBarHeight, Theme.actionBarButtonSize + 2 * Theme.actionBarPadding + 2 * Theme.edgeWidth);
        compare(findChild(bar, "actionBarPlate").fill, Theme.actionBarGround.fill);
        const stop = button(bar, "stop");
        compare(stop.height, Theme.actionBarButtonSize);
        compare(stop.width, bar.buttonWidth({
                                                "text": "stop",
                                                "glyph": Glyphs.stop,
                                                "list": false
                                            }, true));
        verify(stop.implicitWidth <= stop.width + 1, "the button's own content is wider than the room kept");
        compare(stop.sidePadding, Theme.actionBarWordPadding);
        compare(stop.fontSize, Theme.actionBarFontSize);
        verify(stop.mapToItem(bar, 0, 0).y >= 0 && stop.mapToItem(bar, 0, stop.height).y <= bar.height,
               "a button sticks out of the bar");
        const esc = button(bar, "clear");
        compare(Math.round(esc.mapToItem(bar, esc.width, 0).x), bar.width - Theme.actionBarPadding - Theme.edgeWidth);
    }

    // ---- what a click says -----------------------------------------------------------------
    function test_a_button_names_its_action() {
        const bar = makeBar({
                                "startable": true
                            });
        waitForRendering(bar);
        for (const action of ["start", "stop", "recheck", "location", "limits", "delete"]) {
            triggeredSpy.clear();
            mouseClick(button(bar, action));
            compare(triggeredSpy.count, 1, action);
            compare(triggeredSpy.signalArguments[0][0], action);
        }
        compare(menuSpy.count, 0);
    }

    // Category, tags and more ask for their lists over the button, with what folded.
    function test_the_lists_are_asked_for_over_their_button() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(step(1, false, false, false, false));
        waitForRendering(bar);
        for (const action of ["category", "tags", "more"]) {
            menuSpy.clear();
            mouseClick(button(bar, action));
            compare(menuSpy.count, 1, action);
            compare(menuSpy.signalArguments[0][0], action);
            compare(menuSpy.signalArguments[0][1], button(bar, action));
            compare(menuSpy.signalArguments[0][2], ["limits"]);
        }
        compare(triggeredSpy.count, 0);
        // The open one is lit (board 51 `.ab2.open`).
        bar.openAction = "category";
        verify(button(bar, "category").selected);
        verify(!button(bar, "more").selected);
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
        compare(menuSpy.count, 0);
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

    Component {
        id: clearSpyComponent

        SignalSpy {
            signalName: "clearRequested"
        }
    }

    // #77: the ledger is a "seed console" button, the figures its description; a screen
    // reader's press is the click, once; disabled, nothing.
    function test_the_summary_takes_the_accessible_press() {
        const bar = makeBar();
        bar.summary = {
            "sizeText": "3.65 GB",
            "givenText": "572 MB",
            "ratioText": "0.60"
        };
        const summary = findChild(bar, "bar_summary");
        verify(summary.visible);
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

    // DECISIONS C24 H10: alt held, each button with a key shows it over its corner; more and
    // esc have none, and nothing moves.
    function test_alt_badges_every_key_and_moves_nothing() {
        const bar = makeBar({
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
        const keys = {
            "stop": "p",
            "recheck": "c",
            "location": "m",
            "category": "C",
            "tags": "t",
            "limits": "L",
            "delete": "d"
        };
        for (const action of bar.items) {
            const badge = findChild(bar, "barKey_" + action);
            verify(badge.visible, action);
            compare(badge.key, keys[action], action);
        }
    }
}
