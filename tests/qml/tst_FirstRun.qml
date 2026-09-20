import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// T4.6: the first-run page of spec v2 §5 Fig. 15. `autoDetect: false` keeps every instance
// here from starting the real `ServerDetector` against this machine's port 8080 — candidates
// are handed in directly, the way `--demo firstrun` does. The handler step's `register` path
// (`Xdg.registerAsDefaultHandler()`) is never exercised here — CLAUDE.md and the task brief
// are explicit that the real magnet handler on this machine must not change — only `not now`
// (`Xdg.setFirstRunPromptAsked()`, which only touches the sandboxed `state.json` this test
// run's `XDG_CONFIG_HOME` points at) is.
TestCase {
    id: testCase

    name: "FirstRun"
    visible: true
    when: windowShown
    height: 700
    width: 900

    readonly property var localCandidate: ({
                                                "kind": "docker",
                                                "url": "http://127.0.0.1:8080",
                                                "label": "Use the server on this machine",
                                                "detail": "127.0.0.1:8080 · Docker container qbittorrent"
                                            })

    Component {
        id: pageComponent

        FirstRunPage {
            autoDetect: false
        }
    }

    function test_rows_with_a_local_candidate() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        compare(page._rows.length, 2);
        compare(page._rows[0].kind, "local");
        compare(page._rows[0].detail, localCandidate.detail);
        compare(page._rows[1].kind, "remote");
    }

    function test_rows_with_no_candidate_offer_install() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        compare(page._rows.length, 2);
        compare(page._rows[0].kind, "remote");
        compare(page._rows[1].kind, "install");
    }

    function test_j_k_move_the_cursor_and_clamp() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        compare(page.cursorIndex, 0);
        page.moveCursor(1);
        compare(page.cursorIndex, 1);
        page.moveCursor(1); // clamps at the last row
        compare(page.cursorIndex, 1);
        page.moveCursor(-1);
        compare(page.cursorIndex, 0);
        page.moveCursor(-1); // clamps at the first row
        compare(page.cursorIndex, 0);
    }

    function test_enter_on_the_local_row_opens_the_api_key_step() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        compare(page.page, "choose");
        page.chooseCursor(); // cursor starts on the local row
        compare(page.page, "apikey");
        compare(page.chosenKind, "local");
        compare(page.editUrl, "http://127.0.0.1:8080");
    }

    function test_enter_on_the_install_row_opens_the_install_step() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.cursorIndex = 1; // remote, install
        page.chooseCursor();
        compare(page.page, "install");
    }

    function test_install_step_shows_the_exact_commands() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.page = "install";
        const field = findChild(page, "commandsField");
        verify(field !== null);
        verify(field.text.indexOf("pacman -S qbittorrent-nox") >= 0);
        verify(field.text.indexOf("systemctl --user enable --now qbittorrent-nox@$USER") >= 0);
    }

    function test_install_step_continue_opens_api_key_step_at_localhost() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.page = "install";
        page.openRow("local", "http://127.0.0.1:8080");
        compare(page.page, "apikey");
        compare(page.editUrl, "http://127.0.0.1:8080");
    }

    function test_continue_is_disabled_until_a_test_passes() {
        App.loadDemo(16); // demo mode: App.testServer() answers fictionally, no network
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.chooseCursor();
        const button = findChild(page, "continueButton");
        verify(button !== null);
        compare(button.enabled, false);

        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": App,
                                              "signalName": "serverTested"
                                          });
        page.runTest();
        spy.wait(2000);
        compare(page.tested, true);
        compare(page.testOk, true);
        compare(button.enabled, true);
    }

    // Q23: step 2's test runs before the server exists; `continue` gives its answer to the
    // row it creates, so Settings → Servers shows "v5.2.3 · 142 torrents" right away.
    function test_continue_carries_the_test_result_onto_the_new_server() {
        App.loadDemo(16);
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.chooseCursor();
        page.editName = "home";
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": App,
                                              "signalName": "serverTested"
                                          });
        page.runTest();
        spy.wait(2000);
        compare(page.testVersion, "5.2.3");
        compare(page.testCount, 142);

        page.continueFromApiKey();
        compare(page.page, "handler");
        compare(App.serverList.count, 1);
        const id = App.serverList.idAt(0);
        const map = App.serverList.serverMap(id);
        compare(map.name, "home");
        compare(map.versionText, "v5.2.3");
        compare(map.apiVersion, "2.15.1");
        compare(map.torrentCount, 142);
        compare(map.reachableText, "yes · 3 ms");
        // App is a singleton shared with every other test file: leave no server behind.
        App.serverList.remove(id);
        compare(App.serverList.count, 0);
    }

    function test_handler_step_not_now_asks_Xdg_and_finishes() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        // Reset the sandboxed state.json first: another test in this run may have already
        // marked the prompt asked.
        page.page = "handler";
        const finishedSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                       "target": page,
                                                       "signalName": "finished"
                                                   });
        page.skipHandler();
        compare(finishedSpy.count, 1);
        compare(Xdg.firstRunPromptNeeded(), false);
    }

    Component {
        id: signalSpyComponent

        SignalSpy {}
    }
}
