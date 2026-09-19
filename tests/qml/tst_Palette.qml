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
        id: rejectedSpy

        SignalSpy {
            signalName: "rejected"
        }
    }
}
