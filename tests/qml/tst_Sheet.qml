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
            glyph: "H"
            note: "waves of 3, smallest first"
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
        // The skin's Plate draws it (#171): Omarchy's is a rectangle.
        const plate = findChild(sheet, "sheetPlate");
        compare(plate.fill, Theme.background);
        compare(plate.edgeColor, Theme.edgeColor);
        compare(plate.edgeWidth, Theme.edgeWidth);
        compare(plate.height, sheet.panel.height);
        if (Theme.skin.name === "omarchy") {
            compare(plate.edgeColor, Theme.accent);
            compare(plate.edgeWidth, 1);
            compare(plate.face.objectName, "plateFaceOmarchy");
        } else {
            compare(plate.face.objectName, "plateFaceConsole");
        }
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

    // #66: a subtitle longer than Fig. 26's 760 head is cut with an ellipsis, or, with
    // subtitleWraps, takes a second line.
    function test_a_long_subtitle_elides_or_wraps_onto_a_second_line() {
        const long = "single letters act on the table \u00b7 capitals are the stronger variant \u00b7 ctrl is app-level \u00b7 : runs any of them by name";
        const cut = createTemporaryObject(plainSheetComponent, testCase, {
                                              "subtitle": long
                                          });
        const cutLine = findChild(cut, "sheetSubtitle");
        tryVerify(() => cutLine.truncated, 1000);
        compare(cutLine.lineCount, 1);
        const wrapped = createTemporaryObject(plainSheetComponent, testCase, {
                                                  "subtitle": long,
                                                  "subtitleWraps": true
                                              });
        const line = findChild(wrapped, "sheetSubtitle");
        tryVerify(() => line.lineCount === 2, 1000);
        verify(!line.truncated);
        // Two lines at most, unless the sheet allows more (docs/34: the import sheet's three).
        compare(wrapped.subtitleLines, 2);
        wrapped.subtitle = long + " · " + long + " · " + long;
        tryVerify(() => line.truncated, 1000);
        compare(line.lineCount, 2);
        wrapped.subtitle = long + " · " + long;
        wrapped.subtitleLines = 3;
        tryVerify(() => line.lineCount === 3, 1000);
        verify(!line.truncated);
    }

    // Board 46 `.shh`, `.shb`, `.shf`: one inset at the sides, the head and the foot each on a
    // hairline, from the `Theme.sheet*` tokens (frame plan, "form in tokens").
    function test_the_form_is_the_sheet_tokens() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        waitForRendering(sheet);
        compare(findChild(sheet, "sheetPlate").edgeWidth, Theme.sheetEdge);
        compare(findChild(sheet, "sheetPlate").radius, Theme.sheetRadius);
        const title = findChild(sheet, "sheetTitle");
        const at = title.mapToItem(sheet.panel, 0, 0);
        compare(Math.round(at.x), Theme.sheetEdge + Theme.sheetInset);
        compare(title.font.pixelSize, Theme.sheetTitleSize);
        compare(title.font.weight, Theme.sheetTitleWeight);
        const headRule = findChild(sheet, "sheetHeadRule");
        const footRule = findChild(sheet, "sheetFootRule");
        verify(headRule.visible && footRule.visible, "the head or the foot has no rule");
        compare(headRule.width, sheet.panel.width - 2 * Theme.sheetEdge);
        const body = findChild(sheet, "sheetBody");
        const bodyAt = body.mapToItem(sheet.panel, 0, 0);
        compare(Math.round(bodyAt.x), Theme.sheetEdge + Theme.sheetInset - Theme.focusExtent);
        verify(bodyAt.y > headRule.mapToItem(sheet.panel, 0, 0).y, "the rows are over the head's rule");
    }

    // Board 46 draws no glyph square in a sheet's head (`Theme.sheetHeadGlyph`).
    function test_the_head_draws_no_glyph_square_in_this_skin() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        compare(Theme.sheetHeadGlyph, false);
        verify(!findChild(sheet, "sheetGlyph").visible, "the glyph square shows");
    }

    // Board 46's move sheet: "Move 3 torrents  81 KB on disk · on public" on one line; a
    // subtitle that doesn't fit goes under the title.
    function test_a_short_subtitle_sits_beside_the_title_a_long_one_under_it() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        waitForRendering(sheet);
        const title = findChild(sheet, "sheetTitle");
        const line = findChild(sheet, "sheetSubtitle");
        tryVerify(() => line.visible, 1000);
        compare(Math.round(line.mapToItem(sheet.panel, 0, line.baselineOffset).y),
                Math.round(title.mapToItem(sheet.panel, 0, title.baselineOffset).y));
        verify(line.mapToItem(sheet.panel, 0, 0).x >= title.mapToItem(sheet.panel, title.width, 0).x,
               "the subtitle is not after the title");
        compare(line.font.pixelSize, Theme.sheetSubtitleSize);
        sheet.subtitle = "a name long enough that it never fits beside the title of a sheet this narrow, said twice over";
        tryVerify(() => findChild(sheet, "sheetSubtitle").mapToItem(sheet.panel, 0, 0).y
                  > title.mapToItem(sheet.panel, 0, title.height / 2).y, 1000);
    }

    // ---- the buttons (board 46 `.shf .btn`) ------------------------------------------
    // Each as wide as its words, at the foot's right, in the order given; spec v2's equal
    // widths made the mixed free sheet 880 wide (#169).
    function test_the_actions_are_as_wide_as_their_words_at_the_right() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        waitForRendering(sheet);
        const buttons = sheet.actionButtons();
        compare(buttons.length, 3);
        for (let i = 0; i < buttons.length; ++i) {
            compare(buttons[i].width, Math.max(Theme.sheetButtonMinWidth, buttons[i].implicitWidth));
            compare(buttons[i].sidePadding, Theme.sheetButtonPadding);
        }
        verify(buttons[1].width > buttons[0].width, "\"add stopped\" is no wider than \"cancel\"");
        const last = buttons[2].mapToItem(sheet.panel, buttons[2].width, 0);
        compare(Math.round(last.x), sheet.panel.width - Theme.sheetEdge - Theme.sheetInset);
        for (let i = 1; i < buttons.length; ++i) {
            const gap = buttons[i].mapToItem(sheet.panel, 0, 0).x
                    - buttons[i - 1].mapToItem(sheet.panel, buttons[i - 1].width, 0).x;
            verify(Math.abs(gap - Theme.sheetButtonGap) <= 1, "the gap is " + gap);
        }
        compare(buttons[2].primary, true);
        compare(buttons[0].primary, false);
    }

    // Board 46 `.btn.pri`: the primary filled with the accent, its words in the ground.
    function test_the_primary_is_filled_with_the_accent() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        const primary = sheet.primaryButton();
        compare(primary.filled, Theme.sheetPrimaryFilled);
        verify(Qt.colorEqual(primary.color, Theme.accent), "the primary is not filled");
        const cancel = sheet.actionButtons()[0];
        verify(!Qt.colorEqual(cancel.color, Theme.accent), "cancel is filled too");
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

    // ---- clicks -----------------------------------------------------------------------
    // The control for the next test: a click does arrive here, and on the scrim it closes.
    function test_a_click_on_the_scrim_closes_and_rejects() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        mouseClick(sheet, 4, 4);
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    // Manual box, M4: the panel's padding, its head and the gaps around the buttons let the
    // click through to the scrim, so a click inside the sheet closed it.
    function test_a_click_on_the_panel_itself_keeps_it_open() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        const panel = sheet.panel;
        const inPadding = [[4, 4], [4, panel.height / 2], [panel.width - 4, panel.height / 2],
                           [panel.width / 2, panel.height - 4]];
        for (let i = 0; i < inPadding.length; ++i) {
            mouseClick(panel, inPadding[i][0], inPadding[i][1]);
            compare(sheet.opened, true, "a click at " + inPadding[i] + " closed the sheet");
        }
        // The head's title, which is text and takes no click of its own.
        mouseClick(panel, panel.width / 2, testCase.padding + Theme.space(8));
        compare(sheet.opened, true, "a click on the head closed the sheet");
        compare(rejected.count, 0);
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

    // ---- guarded (manual box M5) ------------------------------------------------------
    function test_guarded_escape_goes_to_cancel_first_and_closes_from_there() {
        const sheet = createTemporaryObject(keySheetComponent, testCase, {
                                                "guarded": true
                                            });
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        verify(sheet.cancelButton() !== null);
        verify(sheet.focusables()[sheet.focusedIndex()] !== sheet.cancelButton());

        keyClick(Qt.Key_Escape);
        compare(sheet.opened, true, "the first esc closed a guarded sheet");
        compare(sheet.focusables()[sheet.focusedIndex()], sheet.cancelButton());
        compare(rejected.count, 0);

        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    function test_guarded_ignores_a_click_on_the_scrim() {
        const sheet = createTemporaryObject(keySheetComponent, testCase, {
                                                "guarded": true
                                            });
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        mouseClick(sheet, 4, 4);
        compare(sheet.opened, true);
        compare(rejected.count, 0);
    }

    function test_guarded_without_a_cancel_action_closes_on_the_first_escape() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase, {
                                                "guarded": true
                                            });
        sheet.open();
        keyClick(Qt.Key_Escape);
        compare(sheet.opened, false, "a guarded sheet with no cancel could never close");
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

    // KIT-7: keypad enter on a focused button runs that button, as Return does, not the
    // sheet's primary action.
    function test_keypad_enter_on_the_focused_cancel_runs_cancel() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const accepted = createTemporaryObject(acceptedSpy, testCase, {
                                                   "target": sheet
                                               });
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        sheet.open();
        keyClick(Qt.Key_Tab); // toggle -> cancel
        keyClick(Qt.Key_Enter, Qt.KeypadModifier);
        compare(triggered.count, 1);
        compare(triggered.signalArguments[0][0], "cancel");
        compare(accepted.count, 0, "keypad enter ran the primary");
        compare(sheet.opened, false);
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

    // Review Q6: the body clips, and the focus ring draws outside its control. The body leaves
    // room for it, so a control flush with the body's edge keeps the whole ring.
    function test_the_body_does_not_clip_a_focus_ring() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        sheet.open();
        const body = findChild(sheet, "sheetBody");
        verify(body && body.clip);
        const toggle = sheet.focusables()[0];
        verify(toggle.activeFocus);
        const ring = findChild(toggle, "focusRing");
        verify(ring.visible);
        const at = ring.mapToItem(body, 0, 0);
        verify(at.x >= 0 && at.y >= 0, "the ring is cut at the body's top or left");
        verify(at.x + ring.width <= body.width, "the ring is cut at the body's right");
        verify(at.y + ring.height <= body.height, "the ring is cut at the body's bottom");
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

    // The server sheet's "trust this certificate" (settings-walk, 2026-10-03): with the
    // keyboard put straight on the primary action, `tab` left the sheet for whatever came
    // next in the window's own tab chain (the hero's `+`), and `enter` opened the add sheet.
    function test_tab_from_the_primary_action_stays_inside_the_sheet() {
        const outside = createTemporaryObject(probeComponent, testCase);
        const sheet = createTemporaryObject(confirmSheetComponent, testCase);
        sheet.open();
        compare(sheet.focusedIndex(), 1);
        keyClick(Qt.Key_Tab);
        verify(!outside.activeFocus, "tab left the sheet");
        compare(sheet.focusedIndex(), 0, "tab did not wrap to the first action");
        keyClick(Qt.Key_Backtab);
        compare(sheet.focusedIndex(), 1);
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

    // #11: a disabled action is dimmed, out of the tab cycle, and enter (triggerPrimary) does
    // not run it.
    function test_a_disabled_primary_does_not_run() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        sheet.actions = [
            {
                "text": "cancel",
                "cancel": true
            },
            {
                "text": "move",
                "primary": true,
                "disabled": true
            }
        ];
        sheet.open();
        const primary = sheet.primaryButton();
        verify(primary);
        compare(primary.enabled, false);
        verify(sheet.focusables().indexOf(primary) < 0);
        sheet.triggerPrimary(); // what enter in a body field asks for
        compare(triggered.count, 0);
        compare(sheet.opened, true);
        sheet.actions = [
            {
                "text": "cancel",
                "cancel": true
            },
            {
                "text": "move",
                "primary": true
            }
        ];
        sheet.triggerPrimary(); // what enter in a body field asks for
        compare(triggered.count, 1);
    }

    // #68: a sheet that relabels its actions (the server sheet's `save` turning into `save
    // anyway` after a failed test) keeps its buttons, so the keyboard stays where it was.
    function test_relabelled_actions_keep_the_focused_button() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        const triggered = createTemporaryObject(triggeredSpy, testCase, {
                                                    "target": sheet
                                                });
        sheet.actions = [
            {
                "text": "cancel",
                "cancel": true
            },
            {
                "text": "test",
                "primary": true
            },
            {
                "text": "save",
                "disabled": true
            }
        ];
        sheet.open();
        const buttons = sheet.actionButtons();
        compare(buttons.length, 2);
        buttons[1].forceActiveFocus();
        verify(buttons[1].activeFocus);
        sheet.actions = [
            {
                "text": "cancel",
                "cancel": true
            },
            {
                "text": "test",
                "primary": true
            },
            {
                "text": "save anyway"
            }
        ];
        verify(buttons[1].activeFocus, "the relabel took the keyboard off `test`");
        const after = sheet.actionButtons();
        compare(after.length, 3);
        compare(after[1], buttons[1]);
        compare(after[2].text, "save anyway");
        compare(after[2].primary, false);
        verify(after[2].enabled);
        after[2].clicked();
        compare(triggered.signalArguments[0][0], "save anyway");
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

    // ---- the foot's note (board 46 `.shf`'s dim words) --------------------------------
    // C24 H10: words at the foot's left, never key hints.
    function test_the_note_sits_at_the_foots_left_and_the_foot_has_no_key_hints() {
        const sheet = createTemporaryObject(formSheetComponent, testCase);
        sheet.open();
        waitForRendering(sheet);
        const note = findChild(sheet, "sheetNote");
        compare(note.text, "waves of 3, smallest first");
        compare(note.font.pixelSize, Theme.sheetNoteSize);
        compare(Math.round(note.mapToItem(sheet.panel, 0, 0).x), Theme.sheetEdge + Theme.sheetInset);
        verify(note.mapToItem(sheet.panel, note.width, 0).x
               <= sheet.actionButtons()[0].mapToItem(sheet.panel, 0, 0).x, "the note runs under the buttons");
        verify(sheet.hints === undefined, "the sheet still takes key hints");
        verify(!findChild(sheet, "keyHint_tab"), "a key hint is drawn");
    }

    function test_no_actions_and_no_note_draw_no_foot() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase);
        sheet.open();
        verify(!findChild(sheet, "sheetFootRule").visible, "an empty foot draws its rule");
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

    // `keyHandler`: a sheet's own keys come first; what it leaves goes on to the kit's.
    function test_the_key_handler_is_asked_first() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        let taken = 0;
        let triggered = "";
        sheet.actionTriggered.connect(text => triggered = text);
        sheet.keyHandler = event => {
            if (event.key === Qt.Key_S && (event.modifiers & Qt.AltModifier)) {
                ++taken;
                event.accepted = true;
            } else if (event.key === Qt.Key_Return) {
                ++taken; // seen, but not taken: enter still runs the primary action
            }
        };
        sheet.open();

        keyClick(Qt.Key_S, Qt.AltModifier);
        compare(taken, 1);
        compare(sheet.opened, true);

        keyClick(Qt.Key_Return);
        compare(taken, 2);
        compare(triggered, "add");
    }

    // The head's `esc` looks like a close control, so it is one: a click does what the key
    // does for that sheet.
    function test_a_click_on_esc_closes_and_rejects() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        compare(sheet.escButton.cursorShape, Qt.PointingHandCursor);
        mouseClick(sheet.escButton);
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    // M5: on the guarded add sheet the first click on `esc` only puts the keyboard on
    // `cancel`, as the first key does; the next one closes.
    function test_a_click_on_esc_of_a_guarded_sheet_goes_to_cancel_first() {
        const sheet = createTemporaryObject(keySheetComponent, testCase, {
                                                "guarded": true
                                            });
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        mouseClick(sheet.escButton);
        compare(sheet.opened, true, "the first click closed a guarded sheet");
        compare(sheet.focusables()[sheet.focusedIndex()], sheet.cancelButton());
        compare(rejected.count, 0);
        mouseClick(sheet.escButton);
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    // The sheet's own key handler hears the click's `esc` first, as it hears the key.
    function test_a_click_on_esc_asks_the_key_handler_first() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        let seen = 0;
        sheet.keyHandler = event => {
            if (event.key === Qt.Key_Escape) {
                ++seen;
                event.accepted = true;
            }
        };
        sheet.open();
        mouseClick(sheet.escButton);
        compare(seen, 1);
        compare(sheet.opened, true, "the handler took esc, but the sheet closed");
    }

    Component {
        id: tallSheetComponent

        Sheet {
            anchors.fill: parent
            title: "Tall"

            Repeater {
                model: 40

                Text {
                    required property int index

                    text: "row " + index
                }
            }
        }
    }

    // A body taller than its room scrolls, and a bar says so; a short one has no bar.
    function test_an_overflowing_body_shows_a_scroll_bar() {
        const tall = createTemporaryObject(tallSheetComponent, testCase);
        tall.open();
        const scroller = findChild(tall, "sheetBody");
        verify(scroller.contentHeight > scroller.height, "the body does not overflow");
        verify(tall.bodyScrollBar.visible, "no scroll bar on an overflowing body");
        verify(tall.body.width + tall.bodyScrollBar.width <= scroller.width,
               "the rows run under the bar");
        tall.close();

        const short = createTemporaryObject(keySheetComponent, testCase);
        short.open();
        compare(short.bodyScrollBar.visible, false);
    }

    // docs/33 A1: the add sheet's server chip sits on the title's line, right after it.
    Component {
        id: chipComponent

        Rectangle {
            objectName: "titleChip"
            implicitHeight: 10
            implicitWidth: 40
        }
    }

    function test_a_title_trailer_follows_the_title() {
        const sheet = createTemporaryObject(plainSheetComponent, testCase, {
                                                "title": "Add torrent to"
                                            });
        sheet.open();
        verify(!findChild(sheet, "sheetTitleTrailer").active);
        sheet.titleTrailer = chipComponent;
        const loader = findChild(sheet, "sheetTitleTrailer");
        verify(loader.active);
        const title = findChild(sheet, "sheetTitle");
        tryVerify(() => loader.x >= title.x + title.width, 1000);
    }

    // #75: a screen reader's press on the head's `esc` is its click: what `esc` does.
    function test_the_accessible_press_on_esc_is_the_click() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        findChild(sheet, "sheetEsc").Accessible.pressAction();
        compare(sheet.opened, false);
        compare(rejected.count, 1);
    }

    // #78: the head's `esc` is named by what it does; the key is its description.
    function test_the_esc_chip_is_named_close_with_its_key_as_the_description() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const esc = findChild(sheet, "sheetEsc");
        compare(esc.Accessible.name, "close");
        compare(esc.Accessible.description, "shortcut esc");
    }

    // #79: the title is the sheet's heading, the line under it text, both by their words.
    function test_a_screen_reader_hears_the_title_and_the_subtitle() {
        const sheet = createTemporaryObject(keySheetComponent, testCase, {
                                                "title": "Set location",
                                                "subtitle": "1 torrent · 1.31 GB moves with it"
                                            });
        const title = findChild(sheet, "sheetTitle");
        compare(title.Accessible.role, Accessible.Heading);
        compare(title.Accessible.name, "Set location");
        const subtitle = findChild(sheet, "sheetSubtitle");
        compare(subtitle.Accessible.role, Accessible.StaticText);
        compare(subtitle.Accessible.name, "1 torrent · 1.31 GB moves with it");
    }

    // #75: disabled, the sheet ignores a screen reader's press, as it ignores the pointer.
    function test_a_disabled_sheet_ignores_the_accessible_press() {
        const sheet = createTemporaryObject(keySheetComponent, testCase);
        const rejected = createTemporaryObject(rejectedSpy, testCase, {
                                                   "target": sheet
                                               });
        sheet.open();
        sheet.enabled = false;
        findChild(sheet, "sheetEsc").Accessible.pressAction();
        compare(sheet.opened, true);
        compare(rejected.count, 0);
        sheet.enabled = true;
        sheet.close();
    }
}
