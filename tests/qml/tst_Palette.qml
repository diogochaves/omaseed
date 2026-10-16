pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Kit

// T4.1. `Kit/Palette.qml` — the centred palette of spec v2 §4 (Fig. 12, 13, 14): the query
// row's geometry, the cursor's movement and clamping, `space` in a multi-select, and the
// payloads `activated`/`secondary` carry.
TestCase {
    id: testCase

    // Fig. 12's two rows.
    readonly property var jumpRows: [
        {
            "title": "Fedora-Workstation-Live-44-1.6.x86_64.iso",
            "meta": "downloading",
            "detail": "linux · 2.34 GB · 12.4 %"
        },
        {
            "title": "Fedora-Server-44-1.6.x86_64.iso",
            "meta": "seeding",
            "detail": "linux · 2.9 GB · ratio 4.1"
        }
    ]
    // Fig. 13's four.
    readonly property var categoryRows: [
        {
            "title": "none"
        },
        {
            "title": "linux",
            "meta": "8",
            "detail": "/data/torrents/linux"
        },
        {
            "title": "movies",
            "meta": "41",
            "detail": "/data/torrents/movies"
        },
        {
            "title": "tv",
            "meta": "63",
            "detail": "/data/torrents/tv"
        }
    ]

    height: 600
    name: "Palette"
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
        id: paletteComponent

        Palette {
            anchors.fill: parent
            counter: "2 of 142"
            glyph: "?"
            hints: [
                {
                    "key": "↑↓",
                    "label": "move"
                },
                {
                    "key": "enter",
                    "label": "jump"
                }
            ]
            footerRight: "also matches hash and tracker"
            items: testCase.jumpRows
        }
    }

    Component {
        id: multiComponent

        Palette {
            anchors.fill: parent
            items: testCase.categoryRows
            multi: true
            title: "tags for 2 torrents"
        }
    }

    // A view holding the palette, which records every key that gets past it.
    Component {
        id: hostComponent

        Item {
            property alias hosted: hostedPalette
            property var seen: []

            anchors.fill: parent

            Keys.onPressed: event => seen.push(event.key)

            Palette {
                id: hostedPalette

                anchors.fill: parent
                items: testCase.jumpRows
            }
        }
    }

    // ---- geometry ---------------------------------------------------------------------
    function test_the_panel_is_four_hundred_and_eighty_wide_with_an_accent_edge() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        compare(palette.panel.width, Theme.space(480));
        compare(palette.panel.color, Theme.background);
        compare(palette.panel.border.color, Theme.accent);
        compare(palette.panel.border.width, 1);
    }

    function test_the_query_row_is_a_control_plus_eight_tall() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const queryRow = palette.panel.children[0].children[0];
        compare(queryRow.height, Theme.controlHeight + Theme.space(8));
    }

    function test_the_list_never_grows_past_three_hundred_and_sixty() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.categoryRows.concat(
                                                               testCase.categoryRows).concat(
                                                               testCase.categoryRows).concat(
                                                               testCase.categoryRows).concat(
                                                               testCase.categoryRows).concat(
                                                               testCase.categoryRows)
                                              });
        verify(palette.listView.contentHeight > Theme.space(360),
               "the fixture is not tall enough to test the cap");
        compare(palette.listView.height, Theme.space(360));
    }

    // N3: a list taller than the panel says it has more, the way the rail does.
    function longRows() {
        const rows = [];
        for (let i = 0; i < 40; ++i)
            rows.push({
                "title": "row " + i,
                "key": i
            });
        return rows;
    }

    function test_an_overflowing_list_counts_the_rows_below_and_above() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.longRows()
                                              });
        palette.open();
        waitForRendering(palette);
        const hint = findChild(palette, "scrollHint");
        compare(hint.style, "chevrons");
        compare(hint.countAbove, 0);
        verify(hint.countBelow > 0, "no rows counted below");
        compare(findChild(hint, "moreBelow").visible, true);
        compare(findChild(hint, "moreAbove").visible, false);
        // Every row is either counted, or at least partly on screen above the band.
        const rowHeight = palette.listView.contentHeight / 40;
        const shown = Math.floor((palette.listView.height - hint.bottomInset - 1) / rowHeight) + 1;
        compare(hint.countBelow, 40 - shown);

        palette.cursorToKey(39);
        waitForRendering(palette);
        compare(hint.countBelow, 0);
        verify(hint.countAbove > 0);
        compare(findChild(hint, "moreAbove").visible, true);
    }

    // #8: no row peeks past a band's inner edge: a row there is either wholly in view or
    // the band reaches over all of it.
    function test_no_row_peeks_past_a_band() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.longRows()
                                              });
        palette.open();
        waitForRendering(palette);
        const list = palette.listView;
        const hint = findChild(palette, "scrollHint");
        const above = findChild(hint, "moreAbove");
        const below = findChild(hint, "moreBelow");
        const straddles = edge => {
            for (let i = 0; i < list.count; ++i) {
                const row = list.itemAtIndex(i);
                if (row !== null && row.y < edge - 0.5 && row.y + row.height > edge + 0.5)
                    return i;
            }
            return -1;
        };
        const check = (what) => {
            if (above.visible)
                compare(straddles(list.contentY + above.height), -1, what + ": top band");
            if (below.visible)
                compare(straddles(list.contentY + list.height - below.height), -1,
                        what + ": bottom band");
        };
        let sawTop = false;
        for (let i = 0; i < 25; ++i) {
            keyClick(Qt.Key_Down);
            sawTop = sawTop || above.visible;
            check("down " + i);
        }
        verify(sawTop, "the top band never showed");
        for (let i = 0; i < 20; ++i) {
            keyClick(Qt.Key_Up);
            check("up " + i);
        }
        // Between rows, as a wheel leaves it.
        const rowHeight = list.contentHeight / 40;
        list.contentY = Math.round(rowHeight * 6.4);
        check("wheel");
    }

    function test_the_cursor_stays_clear_of_the_bands() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.longRows()
                                              });
        palette.open();
        waitForRendering(palette);
        const list = palette.listView;
        const hint = findChild(palette, "scrollHint");
        for (let i = 0; i < 20; ++i) {
            keyClick(Qt.Key_Down);
            const row = list.itemAtIndex(palette.cursorIndex);
            verify(row.y + row.height <= list.contentY + list.height - hint.clearBottom + 0.5,
                   "row " + palette.cursorIndex + " under the bottom band");
        }
        for (let i = 0; i < 18; ++i) {
            keyClick(Qt.Key_Up);
            const row = list.itemAtIndex(palette.cursorIndex);
            verify(row.y >= list.contentY + hint.clearTop - 0.5, "row " + palette.cursorIndex
                   + " under the top band");
        }
    }

    function test_the_hint_follows_the_style_it_is_given() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.longRows(),
                                                  "scrollHint": "fade"
                                              });
        palette.open();
        waitForRendering(palette);
        const hint = findChild(palette, "scrollHint");
        compare(findChild(hint, "fadeBelow").visible, true);
        compare(findChild(hint, "moreBelow").visible, false);
        palette.scrollHint = "scrollbar";
        compare(findChild(hint, "scrollbar").visible, true, "an open palette shows its bar");
        compare(findChild(hint, "fadeBelow").visible, false);
    }

    function test_a_short_list_says_nothing() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        waitForRendering(palette);
        const hint = findChild(palette, "scrollHint");
        compare(hint.countAbove, 0);
        compare(hint.countBelow, 0);
        compare(hint.overflows, false);
    }

    function test_the_title_stands_in_for_an_empty_query() {
        const palette = createTemporaryObject(multiComponent, testCase);
        palette.open();
        compare(palette.query, "");
        const standIn = titleLabel(palette);
        verify(standIn.visible, "the title does not show while the query is empty");

        palette.query = "li";
        verify(!standIn.visible, "the title still shows over a typed query");
    }

    function titleLabel(palette) {
        const queryRow = palette.panel.children[0].children[0];
        const row = queryRow.children[0];
        const middle = row.children[1];
        return middle.children[1];
    }

    // ---- the cursor -------------------------------------------------------------------
    function test_down_and_up_move_the_cursor_and_clamp() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        compare(palette.cursorIndex, 0);

        keyClick(Qt.Key_Up);
        compare(palette.cursorIndex, 0, "up moved past the first row");
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1);
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1, "down moved past the last row");
        keyClick(Qt.Key_Up);
        compare(palette.cursorIndex, 0);
    }

    function test_ctrl_n_and_ctrl_p_move_the_cursor_too() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();

        keyClick("n", Qt.ControlModifier);
        compare(palette.cursorIndex, 1);
        keyClick("p", Qt.ControlModifier);
        compare(palette.cursorIndex, 0);
    }

    function test_j_and_k_are_typed_text_not_movement() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();

        keyClick("j");
        keyClick("k");
        compare(palette.cursorIndex, 0, "j/k moved the cursor inside a palette");
        compare(palette.query, "jk");
    }

    function test_a_new_query_puts_the_cursor_back_on_the_first_row() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1);

        keyClick("f");
        compare(palette.cursorIndex, 0);
    }

    function test_a_shorter_list_clamps_the_cursor() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1);

        palette.items = [testCase.jumpRows[0]];
        compare(palette.cursorIndex, 0);
    }

    // ---- what the keys emit -----------------------------------------------------------
    function test_enter_activates_the_row_under_the_cursor() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();
        keyClick(Qt.Key_Down);

        keyClick(Qt.Key_Return);
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], 1);
        compare(activated.signalArguments[0][1].title, "Fedora-Server-44-1.6.x86_64.iso");
    }

    function test_tab_asks_for_the_secondary_action() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const secondary = createTemporaryObject(secondarySpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();

        keyClick(Qt.Key_Tab);
        compare(secondary.count, 1);
        compare(secondary.signalArguments[0][0], 0);
    }

    function test_space_toggles_only_in_a_multi_select() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const toggled = createTemporaryObject(toggledSpy, testCase, {
                                                  "target": palette
                                              });
        palette.open();

        keyClick(Qt.Key_Space);
        compare(toggled.count, 0, "space toggled a single-select palette");
        compare(palette.query, " ", "space was not typed into the query");

        const multi = createTemporaryObject(multiComponent, testCase);
        const multiToggled = createTemporaryObject(toggledSpy, testCase, {
                                                       "target": multi
                                                   });
        multi.open();
        keyClick(Qt.Key_Down);

        keyClick(Qt.Key_Space);
        compare(multiToggled.count, 1);
        compare(multiToggled.signalArguments[0][0], 1);
        compare(multiToggled.signalArguments[0][1].title, "linux");
        compare(multi.query, "", "space was typed into a multi-select's query");
    }

    // KIT-4: in a multi-select a click toggles the clicked row; it does not activate (which
    // applies the whole selection and closes the tags picker).
    function test_a_click_in_a_multi_select_toggles_that_row() {
        const multi = createTemporaryObject(multiComponent, testCase);
        const toggled = createTemporaryObject(toggledSpy, testCase, {
                                                  "target": multi
                                              });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": multi
                                                });
        multi.open();
        waitForRendering(multi.listView);
        const row = multi.listView.itemAtIndex(2);
        verify(row, "row 2 is not laid out");
        mouseClick(row);
        compare(toggled.count, 1, "the click did not toggle the row");
        compare(toggled.signalArguments[0][0], 2);
        compare(toggled.signalArguments[0][1].title, "movies");
        compare(activated.count, 0, "the click activated the palette");
        compare(multi.cursorIndex, 2);
        multi.close();
    }

    function test_a_click_in_a_single_select_activates_that_row() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();
        waitForRendering(palette.listView);
        mouseClick(palette.listView.itemAtIndex(1));
        compare(activated.count, 1);
        compare(activated.signalArguments[0][0], 1);
        palette.close();
    }

    // The pointer on a row moves the cursor there, as the kit Menu's rows do, with a pointing
    // hand; rows that move under a still pointer (the keyboard scrolling) do not take it back.
    function test_the_pointer_moves_the_cursor() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        waitForRendering(palette.listView);
        compare(palette.cursorIndex, 0);
        const row = palette.listView.itemAtIndex(1);
        const area = row.children[row.children.length - 1];
        compare(area.cursorShape, Qt.PointingHandCursor);
        // The first hover after opening only notes the pointer; moving on moves the cursor.
        mouseMove(row, 30, row.height / 2);
        compare(palette.cursorIndex, 0, "a pointer that did not move took the cursor");
        mouseMove(row, 32, row.height / 2);
        compare(palette.cursorIndex, 1, "hover did not move the cursor");
        keyClick(Qt.Key_Up);
        compare(palette.cursorIndex, 0);
        // The same pointer position again, as Qt's hover re-delivery sends it: no move.
        palette._pointerOn(1, area);
        compare(palette.cursorIndex, 0, "a still pointer took the cursor back");
        palette.close();
    }

    // A right-click is `tab` on that row where the palette has a secondary action.
    function test_a_right_click_asks_for_the_secondary_action() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const secondary = createTemporaryObject(secondarySpy, testCase, {
                                                    "target": palette
                                                });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();
        waitForRendering(palette.listView);
        mouseClick(palette.listView.itemAtIndex(1), 30, 10, Qt.RightButton);
        compare(palette.cursorIndex, 1, "the right-click did not move the cursor");
        compare(secondary.count, 0, "a palette without a secondary action got one");

        palette.hasSecondary = true;
        mouseClick(palette.listView.itemAtIndex(0), 30, 10, Qt.RightButton);
        compare(palette.cursorIndex, 0);
        compare(secondary.count, 1);
        compare(secondary.signalArguments[0][0], 0);
        compare(secondary.signalArguments[0][1].title, testCase.jumpRows[0].title);
        compare(activated.count, 0, "a right-click activated the row");
        palette.close();
    }

    // A footer hint flagged `clickable` reports its click; the caller runs it.
    function test_a_clickable_hint_reports_its_click() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "hints": [
                                                      {
                                                          "key": "enter",
                                                          "label": "apply",
                                                          "clickable": true
                                                      }
                                                  ]
                                              });
        const clicked = [];
        palette.hintClicked.connect((index, hint) => clicked.push(hint.key));
        palette.open();
        waitForRendering(palette.panel, 500); // the new hints laid out
        mouseClick(findChild(palette, "keyHint_enter"));
        compare(clicked, ["enter"]);
        palette.close();
    }

    // The query row: a press anywhere on it puts the caret in the query, and a right-click
    // opens cut / copy / paste / select all.
    function test_the_query_row_takes_the_mouse() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        palette.query = "fedora";
        waitForRendering(palette.panel, 500);
        const row = findChild(palette, "paletteQueryMouse");
        mouseClick(row, 2, 2); // the left padding, top band
        compare(palette.query, "fedora");
        mouseClick(row, row.width - 2, row.height - 2);
        mouseClick(row, 2, row.height - 2);
        compare(row.input.cursorPosition, 0);
        verify(row.input.activeFocus);
        mouseClick(row, row.width / 2, row.height / 2, Qt.RightButton);
        verify(row.menu !== null, "no menu");
        keyClick(Qt.Key_Escape);
        verify(row.menu === null);
        verify(palette.opened, "esc in the menu closed the palette");
        verify(row.input.activeFocus);
        palette.close();
    }

    // N2: a reorderable list asks to move the cursor's row; the caller owns the order.
    function test_alt_arrows_ask_to_move_the_row_in_a_reorderable_list() {
        const palette = createTemporaryObject(multiComponent, testCase, {
                                                  "reorderable": true
                                              });
        const moved = createTemporaryObject(moveSpy, testCase, {
                                                "target": palette
                                            });
        palette.open();
        keyClick(Qt.Key_Down);

        keyClick(Qt.Key_Down, Qt.AltModifier);
        compare(moved.count, 1);
        compare(moved.signalArguments[0][0], 1);
        compare(moved.signalArguments[0][1].title, "linux");
        compare(moved.signalArguments[0][2], 1);
        compare(palette.cursorIndex, 1, "alt+down moved the cursor as well");

        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(moved.count, 2);
        compare(moved.signalArguments[1][2], -1);

        keyClick("j", Qt.AltModifier);
        keyClick("k", Qt.AltModifier);
        compare(moved.count, 4, "alt+j / alt+k are not aliases");
        compare(moved.signalArguments[2][2], 1);
        compare(moved.signalArguments[3][2], -1);
        compare(palette.query, "", "alt+j / alt+k were typed into the query");
    }

    function test_ctrl_r_asks_for_the_default_order_only_when_reorderable() {
        const plain = createTemporaryObject(multiComponent, testCase);
        const plainReset = createTemporaryObject(resetSpy, testCase, {
                                                     "target": plain
                                                 });
        const plainMoved = createTemporaryObject(moveSpy, testCase, {
                                                     "target": plain
                                                 });
        plain.open();
        keyClick("r", Qt.ControlModifier);
        keyClick(Qt.Key_Down, Qt.AltModifier);
        compare(plainReset.count, 0);
        compare(plainMoved.count, 0);
        plain.close();

        const palette = createTemporaryObject(multiComponent, testCase, {
                                                  "reorderable": true
                                              });
        const reset = createTemporaryObject(resetSpy, testCase, {
                                                "target": palette
                                            });
        palette.open();
        keyClick("r", Qt.ControlModifier);
        compare(reset.count, 1);
    }

    function test_escape_closes_and_rejects() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": palette
                                               });
        palette.open();

        keyClick(Qt.Key_Escape);
        compare(rejected.count, 1);
        compare(palette.opened, false);
    }

    // Review RUN-11: the escape ladder, as in the help sheet and the filter: clear, then close.
    function test_escape_clears_a_typed_query_before_it_closes() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": palette
                                               });
        palette.open();
        keyClick("a");
        compare(palette.query, "a");

        keyClick(Qt.Key_Escape);
        compare(palette.query, "");
        compare(palette.opened, true, "the first esc closed a palette with a query");
        compare(rejected.count, 0);

        keyClick(Qt.Key_Escape);
        compare(palette.opened, false);
        compare(rejected.count, 1);
    }

    function test_the_focus_comes_back_to_what_had_it_before() {
        const probe = createTemporaryObject(probeComponent, testCase);
        probe.forceActiveFocus();

        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        verify(!probe.activeFocus, "the palette did not take the caret");

        palette.close();
        verify(probe.activeFocus, "the focus was not restored");
    }

    // ---- the matcher ------------------------------------------------------------------
    function test_match_is_the_shared_fuzzy_test() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        verify(palette.match("Fedora-Workstation-Live", "fwl"));
        verify(!palette.match("Fedora-Workstation-Live", "ubuntu"));
    }

    // ---- opening on the current value (T5.4) --------------------------------------------
    function test_open_on_puts_the_cursor_on_the_row_carrying_that_key() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": [
                                                      {
                                                          "title": "name",
                                                          "key": "name"
                                                      },
                                                      {
                                                          "title": "size",
                                                          "key": "size"
                                                      },
                                                      {
                                                          "title": "download speed",
                                                          "key": "dlspeed"
                                                      }
                                                  ]
                                              });
        palette.openOn("dlspeed");
        compare(palette.cursorIndex, 2);
        compare(palette.currentItem.title, "download speed");
        palette.close();
    }

    function test_open_on_an_unknown_key_stays_on_the_first_row() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.categoryRows
                                              });
        // categoryRows carry no `key` at all, so nothing can match.
        palette.openOn("movies");
        compare(palette.cursorIndex, 0);
        palette.close();

        // KIT-16: not whatever row the last session left the cursor on.
        palette.cursorIndex = 2;
        palette.openOn("movies");
        compare(palette.cursorIndex, 0, "an unknown key kept the previous cursor");
        palette.close();
    }

    function test_cursor_to_key_moves_an_already_open_palette() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": [
                                                      {
                                                          "title": "a",
                                                          "key": "a"
                                                      },
                                                      {
                                                          "title": "b",
                                                          "key": "b"
                                                      }
                                                  ]
                                              });
        palette.open();
        compare(palette.cursorIndex, 0);
        palette.cursorToKey("b");
        compare(palette.cursorIndex, 1);
        palette.cursorToKey("nope");
        compare(palette.cursorIndex, 1, "an unknown key moved the cursor");
        palette.close();
    }

    // KIT-6: an open palette is modal — what neither it nor its query row uses stops at it,
    // except ctrl+q.
    function test_keys_the_palette_does_not_use_stop_at_it() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.hosted.open();

        keyClick(Qt.Key_F5);
        compare(host.seen.length, 0, "a key reached the view behind the palette");
        compare(host.hosted.opened, true);

        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(host.seen, [Qt.Key_Q], "ctrl+q did not go on");
        host.hosted.close();
    }

    // ---- rows that can't run (docs/30 §3, D4) -------------------------------------------
    // Board 30 P1–P3: a row that can't run now is drawn dim with its reason, the cursor skips
    // it, and `enter` or a click on it does nothing.
    readonly property var commandRows: [
        {
            "title": "stop",
            "meta": "p",
            "detail": "torrents · 2 selected"
        },
        {
            "title": "start",
            "detail": "torrents · both already running",
            "disabled": true
        },
        {
            "title": "sequential download on",
            "via": "enter s",
            "detail": "torrents · 2 selected · now off on Field Recordings"
        },
        {
            "title": "details smaller",
            "meta": "<",
            "detail": "view · details hidden · i shows them",
            "disabled": true
        }
    ]

    function test_the_cursor_skips_rows_that_cannot_run() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.commandRows
                                              });
        palette.open();
        compare(palette.cursorIndex, 0);
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 2, "the cursor landed on a dim row");
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 2, "the cursor went past the last row that can run");
        keyClick("p", Qt.ControlModifier);
        compare(palette.cursorIndex, 0);
        palette.close();
    }

    function test_the_cursor_starts_on_the_first_row_that_can_run() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.commandRows.slice(1)
                                              });
        palette.open();
        compare(palette.cursorIndex, 1);
        compare(palette.currentItem.title, "sequential download on");
        // A new list whose first row can't run moves the cursor down to one that can.
        palette.items = testCase.commandRows.slice(3).concat(testCase.commandRows.slice(0, 1));
        compare(palette.cursorIndex, 1);
        palette.close();
    }

    // P3: with no row able to run there is no cursor, and `enter` does nothing.
    function test_no_row_that_can_run_means_no_cursor() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": [testCase.commandRows[1], testCase.commandRows[3]]
                                              });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();
        compare(palette.cursorIndex, -1);
        compare(palette.currentItem, null);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(activated.count, 0);
        palette.close();
    }

    function test_enter_and_a_click_on_a_row_that_cannot_run_do_nothing() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.commandRows
                                              });
        const activated = createTemporaryObject(activatedSpy, testCase, {
                                                    "target": palette
                                                });
        palette.open();
        waitForRendering(palette.listView);
        mouseClick(palette.listView.itemAtIndex(1));
        compare(activated.count, 0, "a click ran a dim row");
        compare(palette.cursorIndex, 0, "a click put the cursor on a dim row");
        // Even put there by hand, it is no current row.
        palette.cursorIndex = 1;
        compare(palette.currentItem, null);
        keyClick(Qt.Key_Return);
        compare(activated.count, 0, "enter ran a dim row");
        palette.close();
    }

    // §3: a dim row's title is dim and not medium, its key and second line faint; a row
    // that can run keeps the bright, medium title. A menu-only action shows "via" and its path.
    function test_a_row_that_cannot_run_is_drawn_dim() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "items": testCase.commandRows
                                              });
        palette.open();
        waitForRendering(palette.listView);
        const faint = Theme.fill(Theme.faintAlpha);

        const runs = palette.listView.itemAtIndex(0);
        compare(findChild(runs, "paletteRowTitle").color, Theme.brightForeground);
        compare(findChild(runs, "paletteRowTitle").font.weight, Font.Medium);
        compare(findChild(runs, "paletteRowMeta").text, "p");
        compare(findChild(runs, "paletteRowMeta").color, Theme.dimForeground);
        compare(findChild(runs, "paletteRowVia").visible, false);

        const dim = palette.listView.itemAtIndex(3);
        compare(findChild(dim, "paletteRowTitle").color, Theme.dimForeground);
        compare(findChild(dim, "paletteRowTitle").font.weight, Font.Normal);
        compare(findChild(dim, "paletteRowMeta").color, faint);
        compare(findChild(dim, "paletteRowDetail").color, faint);

        const via = palette.listView.itemAtIndex(2);
        compare(findChild(via, "paletteRowVia").visible, true);
        compare(findChild(via, "paletteRowVia").color, faint);
        compare(findChild(via, "paletteRowMeta").text, "enter s");
        palette.close();
    }

    // P4: a dim line over the list says why it is empty.
    function test_the_empty_text_is_a_dim_line_over_the_list() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const line = findChild(palette, "paletteEmptyText");
        verify(line !== null);
        compare(line.visible, false);
        palette.emptyText = "No command matches “ubuntu”.";
        palette.open();
        compare(line.visible, true);
        compare(line.color, Theme.dimForeground);
        palette.close();
    }

    // docs/30 §2: ctrl+shift+p is the command palette's key, not "up" with a shift.
    function test_ctrl_shift_p_does_not_move_the_cursor() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        palette.open();
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1);
        keyClick(Qt.Key_P, Qt.ControlModifier | Qt.ShiftModifier);
        compare(palette.cursorIndex, 1, "ctrl+shift+p moved the cursor up");
        compare(palette.query, "");
        palette.close();
    }

    // ---- inline (the gallery) ---------------------------------------------------------
    function test_inline_drops_the_scrim_and_the_centring() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "inline": true
                                              });
        compare(palette.visible, true);
        compare(palette.panel.x, 0);
        compare(palette.panel.y, 0);
        compare(palette.panel.width, Theme.space(480));
        verify(palette.panel.height > 0, "the inline palette has no height");
    }

    Component {
        id: activatedSpy

        SignalSpy {
            signalName: "activated"
        }
    }

    Component {
        id: secondarySpy

        SignalSpy {
            signalName: "secondary"
        }
    }

    Component {
        id: toggledSpy

        SignalSpy {
            signalName: "toggled"
        }
    }

    Component {
        id: moveSpy

        SignalSpy {
            signalName: "moveRequested"
        }
    }

    Component {
        id: resetSpy

        SignalSpy {
            signalName: "resetRequested"
        }
    }

    Component {
        id: rejectedSpy

        SignalSpy {
            signalName: "rejected"
        }
    }

    // docs/33 S1: a caller's own key comes first (the server palette's `,`), and what it
    // leaves goes on as before.
    function test_a_key_handler_is_asked_first() {
        const palette = createTemporaryObject(paletteComponent, testCase);
        const taken = [];
        palette.keyHandler = event => {
            if (event.key === Qt.Key_Comma) {
                taken.push(event.key);
                event.accepted = true;
            }
        };
        palette.open();
        keyClick(Qt.Key_Comma);
        compare(taken.length, 1);
        compare(palette.query, "");
        keyClick(Qt.Key_Down);
        compare(palette.cursorIndex, 1);
    }
}
