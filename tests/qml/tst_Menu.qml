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
        const rows = menu.rowsLayout.children;
        compare(rows[0].height, Theme.railRowHeight);
        compare(rows[1].height, Theme.railRowHeight);
        compare(rows[2].height, 2 * Theme.space(4) + 1); // the separator
    }

    // Q24: a heading is a dim line above the rows, never a row the cursor can land on.
    function test_a_heading_sits_above_the_rows_and_is_not_a_row() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const heading = findChild(menu, "menuHeading");
        verify(heading);
        menu.open();
        compare(heading.visible, false);
        compare(heading.height, 0);
        const plain = menu.panel.height;

        menu.heading = "2 selected";
        compare(heading.visible, true);
        compare(menu.panel.height, plain + Theme.railRowHeight + 2 * Theme.space(4) + 1);
        compare(menu.rowsScroller.y, heading.y + heading.height);
        compare(heading.children[0].text, "2 selected");
        compare(heading.children[0].dim, true);
        // #91: before the rows in the tree (declaration order), drawn above them.
        const panel = heading.parent;
        let headingAt = -1;
        let rowsAt = -1;
        for (let i = 0; i < panel.children.length; ++i) {
            if (panel.children[i] === heading)
                headingAt = i;
            if (panel.children[i] === menu.rowsScroller)
                rowsAt = i;
        }
        verify(headingAt >= 0 && headingAt < rowsAt, "the heading comes after the rows");
        verify(heading.z > menu.rowsScroller.z);

        compare(menu.cursorIndex, 0);
        keyClick(Qt.Key_Return);
        compare(menu.opened, false);
    }

    // docs/34 D3 a, D5 a: a footer is a dim, wrapped caption under the rows, never a row; a
    // row's note sits where a key would, and no key press runs it; a row's glyph can take a
    // colour (the server list's dots), and its text can be dim and still be picked.
    function test_a_footer_notes_and_coloured_glyphs() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "items": [{
                                                       "text": "public",
                                                       "glyph": "●",
                                                       "glyphColor": Theme.green,
                                                       "note": "on it ✓",
                                                       "action": "a"
                                                   }, {
                                                       "text": "seedbox",
                                                       "glyph": "●",
                                                       "note": "o",
                                                       "dim": true,
                                                       "action": "b"
                                                   }]
                                           });
        const footer = findChild(menu, "menuFooter");
        verify(footer);
        menu.open();
        compare(footer.visible, false);
        compare(footer.height, 0);
        const plain = menu.panel.height;
        menu.footer = "The library and the tray follow, and it becomes the default.";
        compare(footer.visible, true);
        verify(menu.panel.height > plain);
        verify(footer.y >= menu.rowsScroller.y + menu.rowsScroller.height);

        const rows = menu.rowsLayout.children;
        compare(findChild(rows[0], "menuRowGlyph").color, Theme.green);
        compare(findChild(rows[0], "menuRowNote").text, "on it ✓");
        compare(findChild(rows[1], "menuRowGlyph").color, Theme.dimForeground);
        compare(findChild(rows[1], "menuRowText").color, Theme.dimForeground);
        // A note is not a key: "o" picks nothing.
        keyClick("o");
        verify(menu.opened);
        keyClick(Qt.Key_J);
        compare(menu.cursorIndex, 1);
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": menu,
                                              "signalName": "activated"
                                          });
        keyClick(Qt.Key_Return);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "b");
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    // Board 46's app menu (DECISIONS C24 H9): a bright title leads the heading, captions sit
    // between the rows and are never rows, a check row shows ✓ while on and says it is
    // checkable, and a note can take a state's colour.
    function test_a_title_captions_and_check_rows() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "items": [{
                                                       "caption": true,
                                                       "text": "servers"
                                                   }, {
                                                       "text": "public",
                                                       "checked": true,
                                                       "note": "4 torrents",
                                                       "action": "server:a"
                                                   }, {
                                                       "text": "private",
                                                       "checked": false,
                                                       "note": "sign-in refused",
                                                       "noteColor": Theme.red,
                                                       "action": "server:b"
                                                   }, {
                                                       "separator": true
                                                   }, {
                                                       "caption": true,
                                                       "text": "this window"
                                                   }, {
                                                       "text": "log",
                                                       "key": "~",
                                                       "action": "toggleLog"
                                                   }]
                                           });
        menu.headingTitle = "omaseed";
        menu.open();
        const heading = findChild(menu, "menuHeading");
        verify(heading.visible);
        const title = findChild(menu, "menuHeadingTitle");
        verify(title.visible);
        compare(title.text, "omaseed");
        compare(title.color, Theme.brightForeground);
        compare(menu.panel.Accessible.name, "omaseed");
        menu.heading = "0.5.0 · public";
        compare(menu.panel.Accessible.name, "omaseed · 0.5.0 · public");
        verify(heading.children[0].x >= title.x + title.width);

        // The cursor opens on the first row, past the caption, and never lands on one.
        compare(menu.cursorIndex, 1);
        const rows = menu.rowsLayout.children;
        compare(findChild(rows[0], "menuRowCaption").text, "servers");
        verify(findChild(rows[0], "menuRowCaption").visible);
        compare(rows[0].Accessible.role, Accessible.StaticText);
        verify(rows[0].height < Theme.railRowHeight);
        verify(findChild(rows[1], "menuRowCheck").visible);
        verify(!findChild(rows[2], "menuRowCheck").visible);
        verify(!findChild(rows[5], "menuRowCheck").visible);
        compare(rows[1].Accessible.checkable, true);
        compare(rows[1].Accessible.checked, true);
        compare(rows[5].Accessible.checkable, false);
        compare(findChild(rows[2], "menuRowNote").color, Theme.red);
        compare(findChild(rows[1], "menuRowNote").color, Theme.dimForeground);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        compare(menu.cursorIndex, 5);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_K);
        compare(menu.cursorIndex, 1);
        verify(!menu.selectable(0));
        verify(!menu.selectable(4));
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

    // A long menu in a short window: the panel stops at the window's edges and its rows
    // scroll, so the last ones ("open folder", "delete" in the actions menu) stay reachable.
    function test_a_long_menu_is_cut_to_the_window_and_scrolls() {
        const rows = [];
        for (let i = 0; i < 30; ++i)
            rows.push({
                          "text": "row " + i,
                          "action": "a" + i
                      });
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "items": rows
                                           });
        menu.openAt(100, 300);
        const edge = Theme.space(8);
        verify(menu.panel.implicitHeight > testCase.height);
        compare(menu.panel.y, edge);
        compare(menu.panel.height, testCase.height - 2 * edge);
        const scroller = menu.rowsScroller;
        verify(scroller.contentHeight > scroller.height);
        compare(scroller.contentY, 0);
        verify(findChild(menu, "menuScrollBar").visible, "no scroll bar on an overflowing menu");

        // The keyboard cursor keeps itself in view.
        for (let i = 0; i < 29; ++i)
            keyClick(Qt.Key_Down);
        compare(menu.cursorIndex, 29);
        const last = menu.rowsLayout.children[29];
        verify(last.y + last.height <= scroller.contentY + scroller.height + 0.5,
               "the cursor's row is below the fold");
        verify(scroller.contentY > 0);
        for (let i = 0; i < 29; ++i)
            keyClick(Qt.Key_Up);
        compare(scroller.contentY, 0);

        // The wheel scrolls the rows rather than stopping at the panel.
        mouseWheel(scroller, scroller.width / 2, scroller.height / 2, 0, -120);
        tryVerify(() => scroller.contentY > 0, 1000, "the wheel did not scroll the rows");

        // Opening again starts from the top.
        menu.close();
        menu.openAt(100, 300);
        compare(scroller.contentY, 0);
    }

    function test_a_short_menu_does_not_scroll() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.openAt(100, 100);
        compare(menu.panel.height, menu.panel.implicitHeight);
        compare(menu.rowsScroller.interactive, false);
        compare(findChild(menu, "menuScrollBar").visible, false);
    }

    // A row that runs says so to the pointer, as the filter menu's rows do.
    function test_rows_show_a_pointing_hand() {
        const menu = createTemporaryObject(menuComponent, testCase);
        menu.open();
        const row = menu.rowsLayout.children[3];
        const area = row.children[row.children.length - 1];
        compare(area.cursorShape, Qt.PointingHandCursor);
        // The first hover after opening only notes the pointer (it may rest where the menu
        // opened); the pointer moving on is what moves the cursor.
        mouseMove(row, 20, 5);
        compare(menu.cursorIndex, 0, "a pointer that did not move took the cursor");
        mouseMove(row, 22, 5);
        compare(menu.cursorIndex, 3, "hover did not move the cursor");
        mouseClick(row, 20, 5);
        compare(menu.opened, false, "a click did not run the row");
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

    // A key with no text (alt alone, held for the badges) runs no row, even one whose key is
    // empty.
    function test_a_modifier_alone_runs_nothing() {
        const menu = createTemporaryObject(menuComponent, testCase, {
                                               "items": [{
                                                       "text": "stop all",
                                                       "key": "",
                                                       "action": "stop-all"
                                                   }]
                                           });
        menu.open();
        compare(menu.indexOfKey(""), -1);
        keyPress(Qt.Key_Alt);
        keyRelease(Qt.Key_Alt);
        verify(menu.opened);
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
        const rows = menu.rowsLayout.children;
        compare(rows[0].Accessible.role, Accessible.MenuItem);
        compare(rows[0].Accessible.name, "start");
        compare(rows[2].Accessible.role, Accessible.Separator);
        menu.close();
    }

    // #79: a menu with a heading is named by it, so it is said before the rows.
    function test_a_heading_names_the_menu() {
        const menu = createTemporaryObject(menuComponent, testCase);
        compare(menu.panel.Accessible.name, "");
        menu.heading = "switch omaseed to";
        compare(menu.panel.Accessible.name, "switch omaseed to");
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

    // #51 D17: a right-click outside the panel closes the menu as any click does; with
    // `passRightClick` it is then handed on, at its point, for the owner to right-click there.
    function test_a_right_click_outside_closes_and_is_handed_on_when_asked() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": menu
                                               });
        const outside = createTemporaryObject(outsideSpy, testCase, {
                                                  "target": menu
                                              });
        menu.openAt(10, 10);
        waitForRendering(menu, 250);
        mouseClick(menu, testCase.width - 20, testCase.height - 30, Qt.RightButton);
        compare(menu.opened, false);
        compare(rejected.count, 1);
        compare(outside.count, 0, "handed on without passRightClick");

        menu.passRightClick = true;
        menu.openAt(10, 10);
        mouseClick(menu, testCase.width - 20, testCase.height - 30, Qt.RightButton);
        compare(menu.opened, false);
        compare(rejected.count, 2);
        compare(outside.count, 1);
        compare(outside.signalArguments[0][0], testCase.width - 20);
        compare(outside.signalArguments[0][1], testCase.height - 30);
        // A left click outside still only closes.
        menu.openAt(10, 10);
        mouseClick(menu, testCase.width - 20, testCase.height - 30);
        compare(menu.opened, false);
        compare(outside.count, 1);
    }

    Component {
        id: outsideSpy

        SignalSpy {
            signalName: "rightClickedOutside"
        }
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

    // #75: a screen reader's press on a row is its click: the menu closes and says which.
    function test_the_accessible_press_on_a_row_is_the_click() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();
        menu.rowsLayout.children[3].Accessible.pressAction();
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], "recheck");
        compare(menu.opened, false, "the menu stayed open after an action");
    }

    // #75: a disabled row, a separator and a disabled menu ignore a screen reader's press, as
    // they ignore the pointer.
    function test_what_takes_no_click_ignores_the_accessible_press() {
        const menu = createTemporaryObject(menuComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": menu
                                                });
        menu.open();
        menu.rowsLayout.children[1].Accessible.pressAction();
        menu.rowsLayout.children[2].Accessible.pressAction();
        menu.enabled = false;
        menu.rowsLayout.children[0].Accessible.pressAction();
        compare(activated.count, 0);
        compare(menu.opened, true);
        menu.close();
    }
}
