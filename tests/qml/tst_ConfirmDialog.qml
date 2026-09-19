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
            message: "Delete 2 torrents and their files?"
            subtitle: "archlinux-2026.09.01-x86_64.iso, debian-13.1.0-amd64-DVD-1.iso · 5.29 GB on disk"

            Toggle {
                checked: true
                text: "also delete files from disk"
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
        compare(dialog.panel.color, Theme.background);
        compare(dialog.panel.border.color, Theme.accent);
        compare(dialog.panel.border.width, 1);
    }

    function test_the_message_is_title_sized_and_bright() {
        const dialog = createTemporaryObject(deleteComponent, testCase);
        const message = dialog.panel.children[0].children[0];
        compare(message.font.pixelSize, Theme.font.title);
        compare(message.color, Theme.brightForeground);
        compare(message.lineHeight, 1.35);
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
