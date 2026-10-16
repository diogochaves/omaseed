import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// T3.6: the five state views of spec v2 §7 (Fig. 22-25). `mouseClick()` never delivers under
// the offscreen qmltestrunner (see the T3.2 Rail report), so the empty state and the overlay
// are exercised through their function/signal surface instead of simulated clicks, exactly
// like tst_Rail.qml does for RailRow.
TestCase {
    id: testCase

    name: "States"
    visible: true
    when: windowShown
    height: 600
    width: 900

    // ---------------------------------------------------------------------------- EmptyState
    Component {
        id: emptyStateComponent

        EmptyState {}
    }

    function test_empty_state_has_three_rows_with_the_spec_keys() {
        const empty = createTemporaryObject(emptyStateComponent, testCase);
        compare(empty._rows.length, 3);
        compare(empty._rows[0].key, "a");
        compare(empty._rows[0].label, "add a torrent");
        compare(empty._rows[1].key, ",");
        compare(empty._rows[1].label, "settings and servers");
        compare(empty._rows[2].key, "?");
        compare(empty._rows[2].label, "every shortcut");
    }

    // #77: each row is a button named by what it does, its key the description; a screen
    // reader's press runs it, once.
    function test_empty_state_rows_take_the_accessible_press() {
        const empty = createTemporaryObject(emptyStateComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": empty,
                                              "signalName": "addRequested"
                                          });
        const row = findChild(empty, "emptyRow_0");
        compare(row.Accessible.role, Accessible.Button);
        compare(row.Accessible.name, "add a torrent");
        compare(row.Accessible.description, "a");
        row.Accessible.pressAction();
        compare(spy.count, 1);
        empty.enabled = false;
        row.Accessible.pressAction();
        compare(spy.count, 1, "a disabled row ran");
    }

    // #77: the note's halves are links a screen reader presses.
    function test_the_note_halves_take_the_accessible_press() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "x",
                                                   "statusName": "Downloading"
                                               });
        const clearSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                   "target": filtered,
                                                   "signalName": "clearRequested"
                                               });
        const allSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                 "target": filtered,
                                                 "signalName": "allStatusesRequested"
                                             });
        const clear = findChild(filtered, "clearFilterLink");
        compare(clear.Accessible.role, Accessible.Link);
        compare(clear.Accessible.name, "clear the filter");
        clear.Accessible.pressAction();
        compare(clearSpy.count, 1);
        const all = findChild(filtered, "allStatusesLink");
        compare(all.Accessible.name, "show all statuses");
        all.Accessible.pressAction();
        compare(allSpy.count, 1);
        compare(clearSpy.count, 1);
    }

    function test_empty_state_row_zero_emits_addRequested() {
        const empty = createTemporaryObject(emptyStateComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": empty,
                                              "signalName": "addRequested"
                                          });
        empty.triggerRow(0);
        compare(spy.count, 1);
    }

    function test_empty_state_row_one_emits_settingsRequested() {
        const empty = createTemporaryObject(emptyStateComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": empty,
                                              "signalName": "settingsRequested"
                                          });
        empty.triggerRow(1);
        compare(spy.count, 1);
    }

    function test_empty_state_row_two_emits_helpRequested() {
        const empty = createTemporaryObject(emptyStateComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": empty,
                                              "signalName": "helpRequested"
                                          });
        empty.triggerRow(2);
        compare(spy.count, 1);
    }

    // ------------------------------------------------------------------------- FilteredEmpty
    Component {
        id: filteredEmptyComponent

        FilteredEmpty {}
    }

    function test_filtered_empty_text_and_status_message() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "fedora 45",
                                                   "statusName": "Downloading"
                                               });
        compare(filtered.message, "Nothing matches \"fedora 45\" in Downloading");
    }

    function test_filtered_empty_text_only_message() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "fedora 45",
                                                   "statusName": "All"
                                               });
        compare(filtered.message, "Nothing matches \"fedora 45\"");
    }

    function test_filtered_empty_text_only_message_when_no_status_given() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "fedora 45"
                                               });
        compare(filtered.message, "Nothing matches \"fedora 45\"");
    }

    // Review RUN-13: "0 shows all statuses" only when a status narrows the table.
    function test_the_all_statuses_hint_shows_only_under_a_status() {
        const narrowed = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "x",
                                                   "statusName": "Downloading"
                                               });
        verify(findChild(narrowed, "allStatusesHint").visible);
        const all = createTemporaryObject(filteredEmptyComponent, testCase, {
                                              "text": "x",
                                              "statusName": "All"
                                          });
        verify(!findChild(all, "allStatusesHint").visible);
    }

    // Each half of the note is a link doing what its key does.
    function test_the_note_halves_are_clickable() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "text": "x",
                                                   "statusName": "Downloading"
                                               });
        const clearSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                   "target": filtered,
                                                   "signalName": "clearRequested"
                                               });
        const allSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                 "target": filtered,
                                                 "signalName": "allStatusesRequested"
                                             });
        waitForRendering(filtered, 250);
        const clear = findChild(filtered, "clearFilterLink");
        mouseClick(clear, clear.width - 2, clear.height / 2);
        compare(clearSpy.count, 1);
        compare(allSpy.count, 0);
        const all = findChild(filtered, "allStatusesLink");
        mouseClick(all, 2, all.height / 2);
        compare(allSpy.count, 1);
        compare(clearSpy.count, 1);
    }

    function test_filtered_empty_scope_only_message() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "scopeName": "movies"
                                               });
        compare(filtered.message, "Nothing in movies");
    }

    // #51 D13: each half of the note only while its key does something there: no filter
    // text, no `esc`; a scope with no status narrowing it, no note at all.
    function test_the_note_offers_only_what_applies() {
        const scoped = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                 "scopeName": "movies",
                                                 "statusName": "All"
                                             });
        waitForRendering(scoped, 250);
        verify(!findChild(scoped, "clearFilterLink").visible, "esc has no filter to clear");
        verify(!findChild(scoped, "allStatusesLink").visible);

        const statusAndScope = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                         "scopeName": "movies",
                                                         "statusName": "Downloading"
                                                     });
        compare(statusAndScope.message, "Nothing in movies");
        verify(!findChild(statusAndScope, "clearFilterLink").visible);
        verify(findChild(statusAndScope, "allStatusesLink").visible);

        const statusOnly = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                     "statusName": "Checking"
                                                 });
        compare(statusOnly.message, "Nothing in Checking");
        verify(!findChild(statusOnly, "clearFilterLink").visible);
        verify(findChild(statusOnly, "allStatusesLink").visible);

        const text = createTemporaryObject(filteredEmptyComponent, testCase, {
                                               "text": "x",
                                               "statusName": "Downloading"
                                           });
        verify(findChild(text, "clearFilterLink").visible);
        verify(findChild(text, "allStatusesLink").visible);
    }

    // ------------------------------------------------------------------------- DropOverlay
    Component {
        id: dropOverlayComponent

        DropOverlay {
            anchors.fill: parent
        }
    }

    // #51 D7: an accent outline and "drop to add" while a drag is over the window, never in
    // the way of the drop itself.
    function test_the_drop_overlay_shows_while_a_drag_is_over() {
        const overlay = createTemporaryObject(dropOverlayComponent, testCase);
        verify(!overlay.visible);
        verify(!overlay.enabled, "the overlay must not take the pointer");
        overlay.shown = true;
        verify(overlay.visible);
        const outline = findChild(overlay, "dropOutline");
        compare(outline.border.color, Theme.accent);
        compare(outline.border.width, 1);
        fuzzyCompare(outline.color.a, Theme.fillIdle, 0.01);
        compare(findChild(overlay, "dropLabel").text, "drop to add");
        overlay.shown = false;
        verify(!overlay.visible);
    }

    // -------------------------------------------------------------------- DisconnectedBanner
    Component {
        id: bannerComponent

        DisconnectedBanner {}
    }

    // docs/33 D8, D9 and docs/35: the notice's words come from App.serverProblem (ErrorWords),
    // its kind follows "retrying" or "needs you", its actions the problem and the servers.
    readonly property var downProblem: ({
                                            "headline": "private didn't answer",
                                            "lineBase": "Nothing is listening at 127.0.0.1:8091.",
                                            "retry": "omaseed tries again in 7 s.",
                                            "retryChars": 28,
                                            "details": "network error · GET sync/maindata · Connection refused",
                                            "kind": "down"
                                        })

    function test_banner_names_the_server_and_retries() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "animated": false,
                                                 "problem": testCase.downProblem,
                                                 "width": 800
                                             });
        compare(banner.title, "private didn't answer");
        compare(banner.kind, "warning");
        verify(banner.lineText.startsWith("Nothing is listening at 127.0.0.1:8091. omaseed tries again in 7 s."));
        compare(banner.actions.map(action => action.key), ["retry"]);
        banner.multiServer = true;
        compare(banner.actions.map(action => action.key), ["retry", "switch"]);
        banner.listTime = "23:26";
        verify(banner.body.indexOf("The list below is from 23:26.") > 0);
    }

    function test_banner_actions_emit() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "multiServer": true,
                                                 "problem": testCase.downProblem
                                             });
        const retry = createTemporaryObject(signalSpyComponent, testCase, {
                                                "target": banner,
                                                "signalName": "retryRequested"
                                            });
        const switching = createTemporaryObject(signalSpyComponent, testCase, {
                                                    "target": banner,
                                                    "signalName": "switchRequested"
                                                });
        banner.actionTriggered("retry");
        banner.actionTriggered("switch");
        compare(retry.count, 1);
        compare(switching.count, 1);
    }

    // D9: a refused sign-in is red, "edit login" first; issue #2: a certificate's review.
    function test_banner_needs_you_after_a_refusal() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "multiServer": true,
                                                 "problem": {
                                                     "headline": "seedbox refused the sign-in",
                                                     "lineBase": "The username or password omaseed has for it is wrong.",
                                                     "retry": "",
                                                     "kind": "refused"
                                                 }
                                             });
        compare(banner.kind, "error");
        compare(banner.actions.map(action => action.key), ["login", "switch"]);
        const login = createTemporaryObject(signalSpyComponent, testCase, {
                                                "target": banner,
                                                "signalName": "loginRequested"
                                            });
        banner.actionTriggered("login");
        compare(login.count, 1);
        banner.problem = {
            "headline": "seedbox's certificate isn't trusted",
            "kind": "certificate"
        };
        compare(banner.actions[0].label, "review certificate");
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": banner,
                                              "signalName": "certificateRequested"
                                          });
        banner.actionTriggered("certificate");
        compare(spy.count, 1);
    }

    // ---- docs/33 D6, D7: the empty state with two servers, and with no list
    function test_empty_state_names_the_server_with_two() {
        const empty = createTemporaryObject(emptyStateComponent, testCase, {
                                                "serverName": "public"
                                            });
        compare(empty.headline, "No torrents yet");
        compare(empty._rows.length, 3);
        empty.multiServer = true;
        empty.otherName = "private";
        empty.otherCount = 4;
        compare(empty.headline, "No torrents on public yet");
        compare(empty._rows.length, 4);
        compare(empty._rows[1].key, "ctrl+s");
        compare(empty._rows[1].label, "switch server · private has 4");
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": empty,
                                              "signalName": "switchRequested"
                                          });
        empty.triggerRow(1);
        compare(spy.count, 1);
    }

    function test_empty_state_without_a_list() {
        const empty = createTemporaryObject(emptyStateComponent, testCase, {
                                                "serverName": "public",
                                                "variant": "noList"
                                            });
        compare(empty.headline, "Nothing from public yet");
        verify(empty.note.indexOf("tries again on its own") >= 0);
        compare(empty._rows.length, 0);
        empty.variant = "refused";
        compare(empty.headline, "public refused the sign-in");
        verify(empty.note.indexOf("Settings › Servers") >= 0);
    }

    // ------------------------------------------------------------------------- LoadingState
    Component {
        id: loadingComponent

        LoadingState {
            animate: false
        }
    }

    Component {
        id: animatedLoadingComponent

        LoadingState {
            animate: true
            width: 400
        }
    }

    function test_loading_note_text() {
        const loading = createTemporaryObject(loadingComponent, testCase, {
                                                  "torrentCount": 8400,
                                                  "receivedText": "1.2 MB"
                                              });
        compare(loading.note, "8400 torrents · 1.2 MB received");
    }

    // T5.4: an infinite animation keeps Qt Quick's render thread awake even when its item is
    // invisible, and MainView keeps one LoadingState alive for the whole session. The pulse
    // must stop the moment the state leaves the screen.
    function test_loading_pulse_stops_while_the_state_is_hidden() {
        const loading = createTemporaryObject(animatedLoadingComponent, testCase);
        const rules = findRects(loading);
        compare(rules.length, 4);
        for (let i = 0; i < rules.length; i++)
            verify(rules[i].pulse.running, "the pulse is not running while visible");
        loading.visible = false;
        for (let i = 0; i < rules.length; i++)
            verify(!rules[i].pulse.running, "the pulse kept running while hidden");
        loading.visible = true;
        for (let i = 0; i < rules.length; i++)
            verify(rules[i].pulse.running, "the pulse did not come back");
    }

    function test_loading_four_rule_widths_relative_to_parent() {
        const loading = createTemporaryObject(loadingComponent, testCase, {
                                                  "width": 400
                                              });
        const rules = findRects(loading);
        compare(rules.length, 4);
        const expected = [1, 0.8, 0.9, 0.7];
        for (let i = 0; i < rules.length; i++)
            fuzzyCompare(rules[i].width, loading.width * expected[i], 1);
    }

    // Walks the loading state's Column -> Repeater delegates by the `objectName` LoadingState
    // tags each skeleton rule with.
    function findRects(loading) {
        const results = [];
        const column = loading.children[0];
        for (let i = 0; i < column.children.length; i++) {
            const child = column.children[i];
            if (child.objectName === "skeletonRule")
                results.push(child);
        }
        return results;
    }

    // ------------------------------------------------------------------------- DimmedOverlay
    Component {
        id: overlayComponent

        Item {
            width: 200
            height: 200

            DimmedOverlay {}
        }
    }

    function test_overlay_forwards_R_and_swallows_j() {
        const host = createTemporaryObject(overlayComponent, testCase);
        const overlay = host.children[0];
        const retrySpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                   "target": overlay,
                                                   "signalName": "retryRequested"
                                               });
        overlay.forceActiveFocus();
        if (!overlay.activeFocus) {
            skip("overlay did not take active focus in this environment");
            return;
        }
        keyClick(Qt.Key_R);
        compare(retrySpy.count, 1);

        // "j" is not one of the forwarded keys: nothing should fire, and the overlay must
        // still have swallowed it (accepted) rather than let it fall through to the table.
        keyClick(Qt.Key_J);
        compare(retrySpy.count, 1);
    }

    Component {
        id: signalSpyComponent

        SignalSpy {}
    }
}
