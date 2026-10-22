import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// docs/17 M9: the selection bar on its own. What it draws at a width (M9.3, M9.5), what a
// click on each button says (M9.3), and that it never takes the keyboard (M9.4). Where it
// sits and when it shows are MainView's (tst_Main).
TestCase {
    id: testCase

    height: 300
    name: "SelectionBar"
    visible: true
    when: windowShown
    width: 1200

    readonly property var fullItems: ["start", "stop", "|", "recheck", "reannounce", "|", "category",
        "tags", "location", "limits", "|", "openFolder", "more", "|", "delete"]

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
    // it goes before any button folds.
    function test_the_mouse_hint_follows_the_buttons_and_goes_first() {
        const plain = makeBar();
        const bar = makeBar({
                                "hint": "ctrl+click add · shift+click range"
                            });
        verify(bar.showHint);
        const hint = findChild(bar, "bar_hint");
        compare(hint.text, "ctrl+click add · shift+click range");
        verify(bar.implicitWidth > plain.implicitWidth);
        const last = button(bar, "delete");
        verify(hint.mapToItem(bar, 0, 0).x > last.mapToItem(bar, 0, 0).x, "the hint is not after the buttons");
        // Just too narrow for the hint: it goes, and every button stays.
        bar.availableWidth = plain.implicitWidth + 1;
        verify(!bar.showHint);
        compare(bar.folded, []);
        compare(bar.implicitWidth, plain.implicitWidth);
    }

    function test_a_wide_bar_draws_every_button_in_the_boards_order() {
        const bar = makeBar();
        compare(bar.items, testCase.fullItems);
        compare(bar.folded, []);
        verify(bar.fit.word, "the count should keep its word when there is room");
        compare(findChild(bar, "selectionCount").text, "2");
        // #79: the count in words, for a screen reader.
        compare(findChild(bar, "selectionCount").Accessible.role, Accessible.StaticText);
        compare(findChild(bar, "selectionCount").Accessible.name, "2 selected");
    }

    function test_each_tooltip_names_its_key() {
        const bar = makeBar();
        compare(button(bar, "recheck").tooltip, "recheck · c");
        compare(button(bar, "start").tooltip, "start · p");
        compare(button(bar, "location").tooltip, "move… · m");
        compare(button(bar, "more").tooltip, "all actions · enter");
        compare(button(bar, "delete").tooltip, "delete · d");
        verify(button(bar, "recheck").tooltipAbove, "the bar's tips open over it");
    }

    // #78: a screen reader hears the action; the key is the description, not the name. The
    // hairlines between the groups are no buttons.
    function test_each_button_is_named_by_its_action_and_the_key_is_the_description() {
        const bar = makeBar();
        const cases = [["recheck", "recheck", "shortcut c"], ["start", "start", "shortcut p"],
                       ["location", "move…", "shortcut m"],
                       ["more", "all actions", "shortcut enter"],
                       ["delete", "delete", "shortcut d"],
                       ["clear", "clear selection", "shortcut esc"]];
        for (const [action, name, description] of cases) {
            compare(button(bar, action).Accessible.name, name);
            compare(button(bar, action).Accessible.description, description);
            verify(!button(bar, action).Accessible.ignored);
        }
        compare(button(bar, "|").Accessible.ignored, true);
    }

    function test_delete_is_red() {
        const bar = makeBar();
        verify(button(bar, "delete").danger);
        verify(!button(bar, "recheck").danger);
    }

    // ---- M9.5: narrow windows --------------------------------------------------------------
    function test_buttons_fold_into_more_in_the_boards_order() {
        const bar = makeBar();
        const order = ["openFolder", "limits", "location", "reannounce", "recheck", "tags",
                       "category"];
        compare(bar.foldOrder, order);
        for (let folds = 1; folds <= order.length; ++folds) {
            // Exactly the width of the bar with `folds` gone: one pixel less would fold one more.
            bar.availableWidth = bar.widthOf(bar.itemsFor(folds), true);
            compare(bar.folded, order.slice(0, folds), "at " + folds + " folded");
            for (const action of order.slice(0, folds))
                verify(bar.items.indexOf(action) < 0, action + " should have folded");
            for (const action of ["start", "stop", "more", "delete"])
                verify(bar.items.indexOf(action) >= 0, action + " must always stay");
            compare(bar.implicitWidth, bar.availableWidth);
        }
    }

    function test_empty_groups_leave_no_hairline() {
        const bar = makeBar();
        compare(bar.itemsFor(7), ["start", "stop", "|", "more", "|", "delete"]);
        compare(bar.itemsFor(2), ["start", "stop", "|", "recheck", "reannounce", "|", "category",
                                  "tags", "location", "|", "more", "|", "delete"]);
    }

    function test_the_count_drops_its_word_last() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(bar.itemsFor(7), true);
        verify(bar.fit.word, "the word goes only after every button that can fold has");
        bar.availableWidth = bar.widthOf(bar.itemsFor(7), true) - 1;
        verify(!bar.fit.word);
        compare(bar.folded.length, 7);
        verify(bar.implicitWidth < bar.widthOf(bar.itemsFor(7), true));
    }

    function test_the_bar_never_wraps() {
        const bar = makeBar({
                                "availableWidth": 10
                            });
        // Nothing left to fold: it keeps its one row and is simply wider than asked.
        compare(bar.items, ["start", "stop", "|", "more", "|", "delete"]);
        compare(bar.height, bar.implicitHeight);
        compare(bar.implicitHeight, Theme.controlHeight + 2 * Theme.space(4));
    }

    // ---- what a click says -----------------------------------------------------------------
    function test_a_button_names_its_action() {
        const bar = makeBar();
        waitForRendering(bar);
        for (const action of ["start", "stop", "recheck", "reannounce", "category", "tags",
                              "location", "limits", "openFolder", "delete"]) {
            triggeredSpy.clear();
            mouseClick(button(bar, action));
            compare(triggeredSpy.count, 1, action);
            compare(triggeredSpy.signalArguments[0][0], action);
        }
        compare(moreSpy.count, 0);
    }

    function test_more_asks_for_the_menu_with_what_folded() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(bar.itemsFor(3), true);
        waitForRendering(bar);
        mouseClick(button(bar, "more"));
        compare(moreSpy.count, 1);
        compare(moreSpy.signalArguments[0][0], button(bar, "more"));
        compare(moreSpy.signalArguments[0][1], ["openFolder", "limits", "location"]);
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
        for (const action of testCase.fullItems) {
            if (action !== "|")
                verify(!button(bar, action).activeFocusOnTab, action + " is a tab stop");
        }
    }

    function test_a_click_on_the_bars_ground_goes_no_further() {
        const bar = makeBar();
        waitForRendering(bar);
        // Its own padding: nothing runs, and (in the app) the row under it is not clicked.
        mouseClick(bar, 2, bar.height / 2);
        compare(triggeredSpy.count, 0);
        compare(moreSpy.count, 0);
    }

    // ---- #51 D2: the ✕ after the count -------------------------------------------------------
    function test_the_cross_after_the_count_clears_and_keeps_the_keyboard() {
        const keeper = createTemporaryObject(keeperComponent, testCase);
        const bar = makeBar();
        const spy = createTemporaryObject(clearSpyComponent, testCase, {
                                              "target": bar
                                          });
        waitForRendering(bar);
        const clear = button(bar, "clear");
        verify(clear.visible);
        compare(clear.tooltip, "clear selection · esc");
        verify(clear.tooltipAbove);
        verify(!clear.activeFocusOnTab);
        compare(clear.glyph, Glyphs.close);
        // Quiet: dim until the pointer is on it, then the accent.
        verify(!clear.accent);
        // Right after the count, before the first hairline.
        const count = findChild(bar, "selectionCount");
        verify(clear.mapToItem(bar, 0, 0).x > count.mapToItem(bar, 0, 0).x + count.width);
        verify(clear.mapToItem(bar, 0, 0).x + clear.width <= button(bar, "start").mapToItem(bar, 0, 0).x);
        mouseMove(clear, clear.width / 2, clear.height / 2);
        tryVerify(() => clear.accent, 1000, "no accent on hover");
        keeper.forceActiveFocus();
        mouseClick(clear);
        compare(spy.count, 1);
        compare(triggeredSpy.count, 0);
        verify(keeper.activeFocus, "the ✕ took the keyboard (M9.4)");
    }

    // The ✕ stays when the count drops its word, and the bar's sums count it.
    function test_the_cross_stays_on_a_narrow_bar() {
        const bar = makeBar();
        bar.availableWidth = bar.widthOf(bar.itemsFor(7), true) - 1;
        verify(!bar.fit.word);
        waitForRendering(bar);
        verify(button(bar, "clear").visible);
        compare(bar.implicitWidth, bar.widthOf(bar.items, false));
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
}
