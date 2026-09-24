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
}
