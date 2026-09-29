pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit

// T4.1. `Kit/Menu.qml` — the anchored actions list of spec v2 §3 Fig. 11: row height,
// separators, disabled rows, the cursor's movement, direct activation by a row's own key, and
// the clamp that keeps the panel inside the window.
TestCase {
    id: testCase

    // A cut of Fig. 11.
    readonly property var actionRows: [
        {
            "text": "start",
            "key": "p",
            "action": "start"
        },
        {
            "text": "force start",
            "key": "P",
            "action": "forceStart",
            "disabled": true
        },
        {
            "separator": true
        },
        {
            "text": "recheck",
            "key": "c",
            "action": "recheck"
        },
        {
            "text": "reannounce",
            "key": "R",
            "action": "reannounce"
        },
        {
            "separator": true
        },
        {
            "text": "delete",
            "key": "d",
            "action": "delete",
            "danger": true
        }
    ]

    height: 600
    name: "Menu"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    Component {
        id: menuComponent

        Menu {
            anchors.fill: parent
            items: testCase.actionRows
        }
    }

    // A view holding the menu, which records every key that gets past it.
    Component {
        id: hostComponent

        Item {
            property alias menu: hostedMenu
            property var seen: []

            anchors.fill: parent

            Keys.onPressed: event => seen.push(event.key)

            Menu {
                id: hostedMenu

                anchors.fill: parent
                items: testCase.actionRows
            }
        }
    }

    // ---- geometry ---------------------------------------------------------------------
    function test_the_panel_is_two_hundred_and_eighty_wide_with_an_accent_edge() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(menu.panel.width, Theme.space(280));
        compare(menu.panel.color, Theme.background);
        compare(menu.panel.border.color, Theme.accent);
        compare(menu.panel.border.width, 1);
        compare(menu.panel.padding, Theme.space(6));
    }

    function test_the_rows_are_rail_row_tall() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const rows = menu.panel.children[0].children;
        compare(rows[0].height, Theme.railRowHeight);
        compare(rows[1].height, Theme.railRowHeight);
        compare(rows[2].height, 2 * Theme.space(4) + 1); // the separator
    }

    function test_it_opens_where_the_caller_anchors_it() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.openAt(120, 200);
        compare(menu.panel.x, 120);
        compare(menu.panel.y, 200);
    }

    // docs/17 M9: the selection bar's ⋯ hangs the menu over the button.
    function test_open_above_puts_the_bottom_edge_at_the_anchor() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.openAbove(120, 300);
        compare(menu.panel.x, 120);
        compare(menu.panel.y + menu.panel.height, 300);
        menu.close();
        // Too near the top: clamped inside, as openAt is.
        menu.openAbove(120, 10);
        compare(menu.panel.y, Theme.space(8));
        menu.close();
        // openAt after openAbove anchors the top edge again.
        menu.openAt(120, 200);
        compare(menu.panel.y, 200);
    }

    function test_it_clamps_itself_inside_the_window() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.openAt(testCase.width + 400, testCase.height + 400);
        compare(menu.panel.x, testCase.width - menu.panel.width - Theme.space(8));
        compare(menu.panel.y, testCase.height - menu.panel.height - Theme.space(8));

        menu.openAt(-200, -200);
        compare(menu.panel.x, Theme.space(8));
        compare(menu.panel.y, Theme.space(8));
    }

    // ---- the cursor -------------------------------------------------------------------
    function test_it_opens_on_the_first_row_that_can_be_run() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();
        compare(menu.cursorIndex, 0);
        compare(menu.selectable(0), true);
        compare(menu.selectable(1), false, "a disabled row is selectable");
        compare(menu.selectable(2), false, "a separator is selectable");
    }

    function test_j_and_k_skip_separators_and_disabled_rows() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();

        keyClick("j");
        compare(menu.cursorIndex, 3, "j did not skip the disabled row and the separator");
        keyClick("j");
        compare(menu.cursorIndex, 4);
        keyClick("j");
        compare(menu.cursorIndex, 6, "j did not skip the second separator");
        keyClick("j");
        compare(menu.cursorIndex, 6, "j moved past the last row");

        keyClick("k");
        compare(menu.cursorIndex, 4);
    }

    function test_the_arrows_move_like_j_and_k() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();

        keyClick(Qt.Key_Down);
        compare(menu.cursorIndex, 3);
        keyClick(Qt.Key_Up);
        compare(menu.cursorIndex, 0);
    }

    // ---- what the keys run ------------------------------------------------------------
    function test_enter_runs_the_row_under_the_cursor() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();
        keyClick("j");

        keyClick(Qt.Key_Return);
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], "recheck");
        compare(menu.opened, false, "the menu stayed open after an action");
    }

    function test_a_row_key_runs_that_row_straight_away() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();

        keyClick("d");
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], "delete");
    }

    function test_a_capital_key_is_not_its_lower_case_row() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();

        keyClick("R", Qt.ShiftModifier);
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], "reannounce");
    }

    function test_a_disabled_row_cannot_be_run_by_its_key() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();

        keyClick("P", Qt.ShiftModifier);
        compare(activated.count, 0, "a disabled row ran");
        compare(menu.opened, true);
    }

    function test_the_index_of_a_key_skips_what_cannot_be_run() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(menu.indexOfKey("c"), 3);
        compare(menu.indexOfKey("P"), -1);
        compare(menu.indexOfKey("z"), -1);
    }

    function test_escape_closes_and_rejects() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": menu
                                               });
        menu.open();

        keyClick(Qt.Key_Escape);
        compare(rejected.count, 1);
        compare(menu.opened, false);
    }

    function test_the_focus_comes_back_to_what_had_it_before() {
        const probe = createTemporaryObject(probeComponent, testCase);
        probe.forceActiveFocus();

        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();
        verify(menu.activeFocus, "the menu did not take the focus");

        menu.close();
        verify(probe.activeFocus, "the focus was not restored");
    }

    // KIT-2: `tab` stays in the menu. Unaccepted, Qt moved the focus to the next tab stop
    // behind the scrim and the still-drawn menu stopped hearing esc, j/k and enter.
    function test_tab_keeps_the_keyboard_in_the_menu() {
        const behind = createTemporaryObject(probeComponent, testCase);
        const menu = createTemporaryObject(menuComponent, testCase);
        behind.forceActiveFocus();
        menu.open();
        compare(menu.cursorIndex, 0);

        keyClick(Qt.Key_Tab);
        verify(menu.activeFocus, "tab took the keyboard out of the menu");
        verify(!behind.activeFocus);
        compare(menu.cursorIndex, 3, "tab did not move the cursor down");
        keyClick(Qt.Key_Backtab);
        verify(menu.activeFocus);
        compare(menu.cursorIndex, 0);

        keyClick(Qt.Key_Escape);
        compare(menu.opened, false, "esc no longer reached the menu");
    }

    // KIT-13: a screen reader hears a popup menu of named items.
    function test_the_rows_are_named_menu_items() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();
        compare(menu.panel.Accessible.role, Accessible.PopupMenu);
        const rows = menu.panel.children[0].children;
        compare(rows[0].Accessible.role, Accessible.MenuItem);
        compare(rows[0].Accessible.name, "start");
        compare(rows[2].Accessible.role, Accessible.Separator);
        menu.close();
    }

    // KIT-6: an open menu is modal — what it does not use stops at it, except ctrl+q.
    function test_keys_the_menu_does_not_use_stop_at_it() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.menu.open();

        keyClick("x");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(host.seen.length, 0, "a key reached the view behind the menu");
        compare(host.menu.opened, true);

        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(host.seen, [Qt.Key_Q], "ctrl+q did not go on");
    }

    function test_a_closed_menu_lets_keys_through() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.menu.forceActiveFocus();
        keyClick("x");
        compare(host.seen, [Qt.Key_X]);
    }

    // ---- inline (the gallery) ---------------------------------------------------------
    function test_inline_drops_the_scrim_and_the_anchor() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "inline": true,
                                               "anchorX": 300,
                                               "anchorY": 300
                                           });
        compare(menu.visible, true);
        compare(menu.panel.x, 0);
        compare(menu.panel.y, 0);
        compare(menu.panel.width, Theme.space(280));
        verify(menu.panel.height > 0, "the inline menu has no height");
    }

    Component {
        id: activatedSpy

        SignalSpy {
            signalName: "activated"
        }
    }

    Component {
        id: rejectedSpy

        SignalSpy {
            signalName: "rejected"
        }
    }
}
