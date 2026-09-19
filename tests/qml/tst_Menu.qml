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
