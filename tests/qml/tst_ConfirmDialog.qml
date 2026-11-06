import QtQuick
import QtTest
import Omaseed.Kit

// T4.1. `Kit/ConfirmDialog.qml` — the shell's confirm card of spec v2 §3 Fig. 6: its
// geometry, the `h`/`l` cursor between the two buttons, the red confirm of a destructive
// question, and the `enter`/`esc` routing.
TestCase {
    id: testCase

    height: 500
    name: "ConfirmDialog"
    visible: true
    when: windowShown
    width: 700

    Component {
        id: probeComponent

        Item {
            activeFocusOnTab: true
        }
    }

    // Fig. 6 exactly.
    Component {
        id: deleteComponent

        ConfirmDialog {
            anchors.fill: parent
            cancelText: "Cancel"
            confirmText: "Delete"
            destructive: true
            message: "Delete 2 torrents?"
            subtitle: "archlinux-2026.09.01-x86_64.iso\ndebian-13.1.0-amd64-DVD-1.iso\n5.29 GB on disk"

            Toggle {
                checked: true
                text: "also delete files from disk"
            }
        }
    }

    // A view holding the dialog, which records every key that gets past it.
    Component {
        id: hostComponent

        Item {
            property alias dialog: hostedDialog
            property var seen: []

            anchors.fill: parent

            Keys.onPressed: event => seen.push(event.key)

            ConfirmDialog {
                id: hostedDialog

                anchors.fill: parent
                destructive: true
                message: "Delete 2 torrents?"

                Toggle {
                    text: "also delete files from disk"
                }
            }
        }
    }

    Component {
        id: plainComponent

        ConfirmDialog {
            anchors.fill: parent
            confirmText: "Apply"
            message: "Apply the new limits?"
        }
    }

    // ---- geometry ---------------------------------------------------------------------
    function test_the_card_is_three_hundred_and_seventy_wide() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        compare(dialog.panel.width, Theme.space(370));
        compare(dialog.panel.padding, Theme.space(18));
        // The skin's Plate draws the ground and edge (#171).
        const plate = findChild(dialog, "confirmPlate");
        compare(plate.fill, Theme.background);
        compare(plate.edgeColor, Theme.accent);
        compare(plate.edgeWidth, 1);
    }

    function test_the_message_is_title_sized_and_bright() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const message = dialog.panel.children[0].children[0];
        compare(message.font.pixelSize, Theme.font.title);
        compare(message.color, Theme.brightForeground);
        compare(message.lineHeight, 1.35);
    }

    // #79: the card is an alert named by its question; the line under it (the torrents it is
    // about) is text by its words. The question's own label is not said a second time.
    function test_a_screen_reader_hears_the_question_and_the_line_under_it() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        compare(dialog.panel.Accessible.role, Accessible.AlertMessage);
        compare(dialog.panel.Accessible.name, "Delete 2 torrents?");
        const subtitle = dialog.panel.children[0].children[1];
        compare(subtitle.Accessible.role, Accessible.StaticText);
        compare(subtitle.Accessible.name, dialog.subtitle);
    }

    function test_the_two_buttons_are_eighty_eight_wide_at_caption_size() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        compare(dialog.cancelButton.width, Theme.space(88));
        compare(dialog.confirmButton.width, Theme.space(88));
        compare(dialog.cancelButton.fontSize, Theme.font.caption);
        compare(dialog.confirmButton.fontSize, Theme.font.caption);
    }

    // T5.4: Fig. 6 draws both as `.btn b` — an idle hairline outline, not bare text.
    function test_both_buttons_carry_the_idle_outline() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        compare(dialog.cancelButton.bordered, true);
        compare(dialog.confirmButton.bordered, true);
        compare(dialog.cancelButton.border.width, 1);
        compare(dialog.confirmButton.border.width, 1);
    }

    function test_the_caller_row_sits_between_the_message_and_the_buttons() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();
        const rows = dialog.panel.children[0].children;
        compare(rows[2], dialog.body); // the content slot, between the subtitle and the buttons
        compare(dialog.body.children.length, 1);
        compare(dialog.body.children[0].text, "also delete files from disk");
    }

    // ---- the cursor -------------------------------------------------------------------
    function test_a_destructive_question_opens_on_cancel() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();
        compare(dialog.cursor, 0);
        verify(dialog.cancelButton.hasCursor, "the cursor is not on cancel");
        verify(!dialog.confirmButton.hasCursor, "the cursor is on the destructive button");
    }

    function test_a_harmless_question_opens_on_confirm() {
        const dialog = createTemporaryObject(plainComponent, testCase);
        dialog.open();
        compare(dialog.cursor, 1);
        verify(dialog.confirmButton.primary, "a harmless confirm is not the primary button");
    }

    function test_h_and_l_move_the_cursor_and_do_not_wrap() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();

        keyClick("h");
        compare(dialog.cursor, 0, "`h` moved past cancel");
        keyClick("l");
        compare(dialog.cursor, 1);
        keyClick("l");
        compare(dialog.cursor, 1, "`l` moved past confirm");
        keyClick("h");
        compare(dialog.cursor, 0);
    }

    function test_tab_and_the_arrows_move_the_cursor_too() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();

        keyClick(Qt.Key_Tab);
        compare(dialog.cursor, 1);
        keyClick(Qt.Key_Backtab);
        compare(dialog.cursor, 0);
        keyClick(Qt.Key_Right);
        compare(dialog.cursor, 1);
        keyClick(Qt.Key_Left);
        compare(dialog.cursor, 0);
    }

    // KIT-3: the keyboard reaches Fig. 6's "also delete files" toggle, so a remembered "on"
    // can be turned off without the mouse.
    function test_shift_tab_from_cancel_reaches_the_body_toggle_and_space_flips_it() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const toggle = dialog.body.children[0];
        const toggled = createTemporaryObject(toggledSpy, testCase, {
                                                  "target": toggle
                                              });
        dialog.open();
        compare(dialog.cursor, 0);

        keyClick(Qt.Key_Backtab);
        verify(toggle.activeFocus, "shift+tab did not reach the toggle");
        compare(dialog.bodyFocusIndex(), 0);
        keyClick(Qt.Key_Space);
        compare(toggle.checked, false);
        compare(toggled.count, 1);
        compare(dialog.opened, true, "space on the toggle closed the dialog");
    }

    function test_tab_cycles_toggle_cancel_confirm_and_wraps() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const toggle = dialog.body.children[0];
        dialog.open();

        keyClick(Qt.Key_Tab);
        compare(dialog.cursor, 1);
        verify(dialog.activeFocus, "the buttons lost the keyboard");
        keyClick(Qt.Key_Tab);
        verify(toggle.activeFocus, "tab from confirm did not wrap to the toggle");
        keyClick(Qt.Key_Tab);
        verify(dialog.activeFocus, "tab from the toggle left the dialog");
        compare(dialog.cursor, 0);
        compare(dialog.opened, true);
    }

    function test_enter_on_the_toggle_runs_the_button_under_the_cursor() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": dialog
                                               });
        dialog.open();
        keyClick(Qt.Key_Backtab); // cancel -> toggle
        keyClick(Qt.Key_Return);
        compare(rejected.count, 1, "enter on the toggle did not run cancel");
        compare(dialog.opened, false);
    }

    function test_l_from_the_toggle_goes_back_to_the_buttons() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();
        keyClick(Qt.Key_Backtab);
        keyClick("l");
        verify(dialog.activeFocus, "`l` left the keyboard on the toggle");
        compare(dialog.cursor, 1);
    }

    function test_a_dialog_without_rows_still_clamps_tab_between_its_buttons() {
        const dialog = createTemporaryObject(plainComponent, testCase);
        dialog.open();
        compare(dialog.cursor, 1);
        keyClick(Qt.Key_Tab);
        compare(dialog.cursor, 1);
        verify(dialog.activeFocus);
        keyClick(Qt.Key_Backtab);
        keyClick(Qt.Key_Backtab);
        compare(dialog.cursor, 0);
        verify(dialog.activeFocus);
    }

    // KIT-6: an open dialog is modal — even from its body's toggle, what it does not use
    // stops at it; ctrl+q still goes on.
    function test_keys_the_dialog_does_not_use_stop_at_it() {
        const host = createTemporaryObject(hostComponent, testCase);
        host.dialog.open();
        keyClick("x");
        keyClick(Qt.Key_Backtab); // cancel -> toggle
        keyClick("x");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(host.seen.length, 0, "a key reached the view behind the dialog");
        compare(host.dialog.opened, true);

        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(host.seen, [Qt.Key_Q], "ctrl+q did not go on");
    }

    // ---- the destructive colours ------------------------------------------------------
    function test_the_destructive_button_reads_red_and_outlines_red_under_the_cursor() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();
        verify(dialog.confirmButton.danger, "the destructive confirm is not danger");
        compare(dialog.confirmButton._textColor, Theme.red);

        dialog.cursor = 1;
        compare(dialog.confirmButton._borderColor, Theme.red);
        compare(dialog.confirmButton.selected, true);
    }

    function test_a_harmless_confirm_is_never_red() {
        const dialog = createTemporaryObject(plainComponent, testCase);
        dialog.open();
        verify(!dialog.confirmButton.danger, "a harmless confirm reads as danger");
        compare(dialog.confirmButton._textColor, Theme.accent);
    }

    // ---- keys -------------------------------------------------------------------------
    function test_enter_runs_the_button_under_the_cursor() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const accepted = createTemporaryObject(acceptedSpy, testCase, {
                                                   "target": dialog
                                               });
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": dialog
                                               });
        dialog.open();

        keyClick(Qt.Key_Return);
        compare(rejected.count, 1, "enter on cancel did not reject");
        compare(accepted.count, 0);
        compare(dialog.opened, false);

        dialog.open();
        keyClick("l");
        keyClick(Qt.Key_Return);
        compare(accepted.count, 1);
        compare(dialog.opened, false);
    }

    function test_escape_cancels_whatever_the_cursor_is_on() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": dialog
                                               });
        dialog.open();
        dialog.cursor = 1;

        keyClick(Qt.Key_Escape);
        compare(rejected.count, 1);
        compare(dialog.opened, false);
    }

    function test_the_focus_comes_back_to_what_had_it_before() {
        const probe = createTemporaryObject(probeComponent, testCase);
        probe.forceActiveFocus();

        const dialog = createTemporaryObject(deleteComponent, testCase);
        dialog.open();
        verify(dialog.activeFocus, "the dialog did not take the focus");

        dialog.close();
        verify(probe.activeFocus, "the focus was not restored");
    }

    // ---- inline (the gallery) ---------------------------------------------------------
    // Manual box, M4: a click on the card's own ground (its padding, the message) fell
    // through to the scrim and cancelled it. The scrim itself still cancels.
    function test_a_click_on_the_card_keeps_it_open_and_the_scrim_cancels() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": dialog
                                               });
        dialog.open();
        mouseClick(dialog.panel, 4, 4);
        mouseClick(dialog.panel, dialog.panel.width / 2, Theme.space(18) + Theme.space(8));
        compare(dialog.opened, true, "a click on the card closed it");
        compare(rejected.count, 0);

        mouseClick(dialog, 4, 4);
        compare(dialog.opened, false);
        compare(rejected.count, 1);
    }

    function test_inline_drops_the_scrim_and_the_centring() {
        const dialog = createTemporaryObject(deleteComponent, testCase, {
                                                 "inline": true
                                             });
        compare(dialog.visible, true);
        compare(dialog.panel.x, 0);
        compare(dialog.panel.y, 0);
        compare(dialog.panel.width, Theme.space(370));
        verify(dialog.panel.height > 0, "the inline card has no height");
    }

    // #28 (docs/26): a caller's rows can make the card wider, and a longer caption widens the
    // confirm button instead of being cut.
    function test_panel_width_and_a_long_caption_fit() {
        const dialog = createTemporaryObject(deleteComponent, testCase, {
                                                 "inline": true,
                                                 "panelWidth": Theme.space(520),
                                                 "confirmText": "delete, keep files"
                                             });
        compare(dialog.panel.width, Theme.space(520));
        verify(dialog.confirmButton.width >= dialog.confirmButton.implicitWidth,
               "the confirm caption does not fit its button");
        compare(dialog.cancelButton.width, Theme.space(88));
    }

    // #28: `keyHandler` sees keys first; true takes the key, false leaves it to the dialog.
    function test_key_handler_takes_keys_before_the_dialog() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const seen = [];
        dialog.keyHandler = event => {
            seen.push(event.key);
            return event.key === Qt.Key_J;
        };
        dialog.open();
        keyClick("j");
        compare(seen, [Qt.Key_J]);
        compare(dialog.opened, true);
        keyClick(Qt.Key_Escape);
        compare(seen, [Qt.Key_J, Qt.Key_Escape]);
        compare(dialog.opened, false, "a key the handler left alone did not reach the dialog");
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

    Component {
        id: toggledSpy

        SignalSpy {
            signalName: "toggled"
        }
    }
}
