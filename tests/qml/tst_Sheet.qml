import QtQuick
import QtQuick.Layouts
import QtTest
import Omaseed.Kit

// T4.1. `Kit/Sheet.qml` — the centred sheet of spec v2 §3 (Fig. 5, 7, 8, 9, 10, 26): its
// geometry against the Theme tokens, the equal-width buttons of `.acts`, and the focus trap
// (`esc`, `enter`, `shift+enter`, `tab` cycling, focus restored on close). `keyClick` reaches
// a focused item in the offscreen runner; `mouseClick` does not, so button clicks are checked
// through the exposed signals.
TestCase {
    id: testCase

    readonly property int padding: Theme.space(16)

    height: 700
    name: "Sheet"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    // Fig. 7's shape: a head, one form row, one toggle and three buttons.
    Component {
        id: formSheetComponent

        Sheet {
            actions: [
                {
                    "text": "cancel",
                    "cancel": true
                },
                {
                    "text": "add stopped",
                    "secondary": true
                },
                {
                    "text": "move",
                    "primary": true
                }
            ]
            anchors.fill: parent
            footerRight: "server: home"
            glyph: "H"
            hints: [
                {
                    "key": "tab",
                    "label": "fields"
                }
            ]
            sheetWidth: Theme.space(420)
            subtitle: "1 torrent · 1.31 GB moves with it"
            title: "Set location"

            FormRow {
                Layout.fillWidth: true
                label: "New path on home"

                Field {
                    Layout.fillWidth: true
                }
            }

            Toggle {
                text: "sequential download"
            }
        }
    }

    // A sheet whose first focusable is not a text field, so the keys under test reach the
    // sheet itself instead of being eaten by a caret.
    Component {
        id: keySheetComponent

        Sheet {
            actions: [
                {
                    "text": "cancel",
                    "cancel": true
                },
                {
                    "text": "add stopped",
                    "secondary": true
                },
                {
                    "text": "add",
                    "primary": true
                }
            ]
            anchors.fill: parent
            title: "Add torrent"

            Toggle {
                text: "sequential download"
            }
        }
    }

    // A view holding a sheet, which records every key that gets past it.
    Component {
        id: hostComponent

        Item {
            property alias sheet: hostedSheet
            property var seen: []

            anchors.fill: parent

            Keys.onPressed: event => seen.push(event.key)

            Sheet {
                id: hostedSheet

                actions: [
                    {
                        "text": "cancel",
                        "cancel": true
                    },
                    {
                        "text": "add",
                        "primary": true
                    }
                ]
                anchors.fill: parent
                title: "Add torrent"

                Toggle {
                    text: "sequential download"
                }
            }
        }
    }

    // Fig. 10's shape: buttons, and a body that holds nothing the keyboard can reach.
    Component {
        id: confirmSheetComponent

        Sheet {
            actions: [
                {
                    "text": "close",
                    "cancel": true
                },
                {
                    "text": "show it",
                    "primary": true
                }
            ]
            anchors.fill: parent
            title: "Already on home"

            Text {
                text: "This infohash is already in your list."
            }
        }
    }

    // Fig. 26: a sheet with no buttons at all.
    Component {
        id: plainSheetComponent

        Sheet {
            anchors.fill: parent
            sheetWidth: Theme.space(760)
            title: "Shortcuts"
        }
    }

    // ---- geometry ---------------------------------------------------------------------
    function test_the_panel_is_opaque_with_a_one_pixel_accent_edge() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase);
        compare(sheet.panel.color, Theme.background);
        compare(sheet.panel.border.color, Theme.accent);
        compare(sheet.panel.border.width, 1);
    }

    function test_the_panel_is_as_wide_as_sheet_width_and_centred() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        compare(sheet.panel.width, Theme.space(420));
        compare(sheet.panel.x, Math.round((testCase.width - sheet.panel.width) / 2));
    }

    function test_the_panel_never_overflows_a_narrow_window() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase, {
                                                "sheetWidth": Theme.space(2000)
                                            });
        compare(sheet.panel.width, testCase.width - 2 * testCase.padding);
    }

    function test_the_padding_is_sixteen() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase);
        compare(sheet.panel.padding, Theme.space(16));
    }

    function test_the_head_square_is_thirty_with_a_hairline_border() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        const square = findHeadSquare(sheet);
        verify(square, "the head has no icon square");
        compare(square.width, Theme.space(30));
        compare(square.height, Theme.space(30));
        compare(square.border.color, Theme.fill(Theme.borderIdle));
    }

    function findHeadSquare(sheet) {
        const rows = sheet.panel.children[0].children;
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].children.length > 0 && rows[i].children[0].border !== undefined
                    && rows[i].children[0].width === Theme.space(30))
                return rows[i].children[0];
        }
        return null;
    }

    // ---- the buttons (`.acts`) --------------------------------------------------------
    function test_the_actions_share_the_width_equally() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        const buttons = sheet.actionButtons();
        compare(buttons.length, 3);
        compare(buttons[0].width, buttons[1].width);
        compare(buttons[1].width, buttons[2].width);
        verify(buttons[0].width > 0, "the buttons have no width");
        compare(buttons[2].primary, true);
        compare(buttons[0].primary, false);
    }

    // T5.4: spec §3 draws every `.sheet .acts .btn` as `.btn b` — an idle hairline outline.
    function test_every_action_carries_the_idle_outline() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        const buttons = sheet.actionButtons();
        for (let i = 0; i < buttons.length; ++i) {
            compare(buttons[i].bordered, true);
            compare(buttons[i].border.width, 1);
        }
    }

    function test_a_sheet_may_have_no_actions_at_all() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase);
        compare(sheet.actions.length, 0);
        verify(!sheet.actionsRow.visible, "an empty action row still draws");
    }

    // ---- keys -------------------------------------------------------------------------
    function test_escape_closes_and_rejects() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        compare(sheet.opened, true);

        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    function test_enter_runs_the_primary_action() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        const accepted = createTemporaryObject(acceptedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();

        keyClick(Qt.Key_Return);
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "add");
        compare(accepted.count, 1);
    }

    function test_shift_enter_runs_the_secondary_action() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        const accepted = createTemporaryObject(acceptedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();

        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "add stopped");
        compare(accepted.count, 0, "the secondary action is not an accept");
    }

    // KIT-1: a `cancel` button closes the sheet, as `esc` does, and rejects once — Rename,
    // Location and Global limits handle only `accepted` and rely on it.
    function test_the_cancel_action_closes_and_rejects_once() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        const cancel = sheet.actionButtons()[0];
        compare(cancel.text, "cancel");
        cancel.clicked();
        compare(sheet.opened, false, "cancel left the sheet open");
        compare(rejected.count, 1);
    }

    function test_enter_on_the_focused_cancel_button_closes_the_sheet() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const accepted = createTemporaryObject(acceptedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        keyClick(Qt.Key_Tab); // toggle -> cancel
        compare(sheet.focusedIndex(), 1);
        keyClick(Qt.Key_Return);
        compare(sheet.opened, false);
        compare(accepted.count, 0, "enter on cancel ran the primary");
    }

    // KIT-6: an open sheet is modal — a letter pressed on one of its buttons stops at the
    // sheet instead of reaching the view that holds it; ctrl+q still goes on.
    function test_keys_the_sheet_does_not_use_stop_at_it() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.sheet.open();
        keyClick(Qt.Key_Tab); // toggle -> cancel, a Button
        compare(host.sheet.focusedIndex(), 1);

        keyClick("x");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(host.seen.length, 0, "a key reached the view behind the sheet");
        compare(host.sheet.opened, true);

        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(host.seen, [Qt.Key_Q], "ctrl+q did not go on");
    }

    // ---- the focus trap ---------------------------------------------------------------
    function test_open_puts_the_keyboard_on_the_first_focusable_row() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        compare(sheet.focusables().length, 5); // field, toggle, three buttons
        compare(sheet.focusedIndex(), 0);
    }

    function test_tab_cycles_through_the_sheet_and_wraps_inside_it() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        const count = sheet.focusables().length;

        for (let i = 1; i < count; ++i) {
            keyClick(Qt.Key_Tab);
            compare(sheet.focusedIndex(), i);
        }
        keyClick(Qt.Key_Tab);
        compare(sheet.focusedIndex(), 0, "tab left the sheet instead of wrapping");
    }

    function test_shift_tab_cycles_backwards() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        compare(sheet.focusedIndex(), 0);

        keyClick(Qt.Key_Backtab);
        compare(sheet.focusedIndex(), sheet.focusables().length - 1);
        keyClick(Qt.Key_Backtab);
        compare(sheet.focusedIndex(), sheet.focusables().length - 2);
    }

    function test_the_focus_comes_back_to_what_had_it_before() {
        const probe = createTemporaryObject(probeComponent, testCase);
        probe.forceActiveFocus();
        verify(probe.activeFocus, "the probe did not take the focus");

        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        verify(!probe.activeFocus, "the sheet did not take the focus");

        sheet.close();
        verify(probe.activeFocus, "the focus was not restored on close");
    }

    // T4.8: `open()` used to land on the first action button, which is `cancel` in every
    // figure, so `enter` cancelled the sheet it had just opened.
    function test_a_body_with_nothing_focusable_puts_the_keyboard_on_the_primary_action() {
        const sheet = createTemporaryObject(confirmSheetComponent, testCase);
        sheet.open();
        compare(sheet.bodyFocusables().length, 0);
        const primary = sheet.primaryButton();
        verify(primary, "the sheet found no primary button");
        compare(primary.text, "show it");
        compare(sheet.focusedIndex(), 1, "the keyboard did not land on the primary action");
    }

    function test_enter_on_such_a_sheet_runs_the_primary_and_not_the_cancel() {
        const sheet = createTemporaryObject(confirmSheetComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        keyClick(Qt.Key_Return);
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "show it");
        compare(rejected.count, 0, "enter cancelled the sheet");
    }

    function test_a_body_with_a_field_still_wins_over_the_primary_action() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        verify(sheet.bodyFocusables().length > 0);
        compare(sheet.focusedIndex(), 0);
    }

    function test_a_sheet_of_plain_content_still_owns_the_keyboard() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase);
        sheet.open();
        compare(sheet.focusables().length, 0);
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false, "escape did not reach a sheet with nothing focusable");
    }

    // ---- the footer (`.ft`) -----------------------------------------------------------
    function test_the_footer_prints_its_hints_at_caption_size() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        compare(sheet.hints.length, 1);
        compare(sheet.footerRight, "server: home");
    }

    // ---- inline (the gallery) ---------------------------------------------------------
    function test_inline_drops_the_scrim_and_the_centring() {
        const sheet = createTemporaryObject(formSheetComponent, testCase, {
                                                "inline": true
                                            });
        compare(sheet.visible, true, "an inline sheet needs no open()");
        compare(sheet.panel.x, 0);
        compare(sheet.panel.y, 0);
        compare(sheet.panel.width, Theme.space(420));
        verify(sheet.panel.height > 0, "the inline panel has no height");
        compare(sheet.implicitWidth, Theme.space(420));
        compare(sheet.implicitHeight, sheet.panel.implicitHeight);
    }

    Component {
        id: triggeredSpy

        SignalSpy {
            signalName: "actionTriggered"
        }
    }

    Component {
        id: acceptedSpy

        SignalSpy {
            signalName: "accepted"
        }
    }

    Component {
        id: rejectedSpy

        SignalSpy {
            signalName: "rejected"
        }
    }
}
