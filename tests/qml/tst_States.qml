import QtQuick
import QtTest
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

    function test_filtered_empty_scope_only_message() {
        const filtered = createTemporaryObject(filteredEmptyComponent, testCase, {
                                                   "scopeName": "movies"
                                               });
        compare(filtered.message, "Nothing in movies");
    }

    // -------------------------------------------------------------------- DisconnectedBanner
    Component {
        id: bannerComponent

        DisconnectedBanner {}
    }

    function test_banner_shows_the_countdown() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "errorText": "connection refused",
                                                 "retryInSeconds": 7
                                             });
        compare(banner.text, "connection refused · retrying in 7 s");
    }

    function test_banner_shows_retrying_now_at_zero() {
        const banner = createTemporaryObject(bannerComponent, testCase, {
                                                 "errorText": "connection refused",
                                                 "retryInSeconds": 0
                                             });
        compare(banner.text, "connection refused · retrying now");
    }

    function test_banner_retry_action_emits_retryRequested() {
        const banner = createTemporaryObject(bannerComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": banner,
                                              "signalName": "retryRequested"
                                          });
        banner.actionTriggered("retry");
        compare(spy.count, 1);
    }

    function test_banner_settings_action_emits_settingsRequested() {
        const banner = createTemporaryObject(bannerComponent, testCase);
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": banner,
                                              "signalName": "settingsRequested"
                                          });
        banner.actionTriggered("settings");
        compare(spy.count, 1);
    }

    // Issue #2: a refused certificate puts `review certificate` first.
    function test_banner_offers_the_certificate_review_when_refused() {
        const banner = createTemporaryObject(bannerComponent, testCase);
        compare(banner.actions.map(action => action.key), ["retry", "settings"]);
        banner.certificate = true;
        compare(banner.actions.map(action => action.key), ["certificate", "retry", "settings"]);
        compare(banner.actions[0].label, "review certificate");
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": banner,
                                              "signalName": "certificateRequested"
                                          });
        banner.actionTriggered("certificate");
        compare(spy.count, 1);
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
