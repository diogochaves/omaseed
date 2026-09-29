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
        compare(page._rows.length, 3);
        compare(page._rows[0].kind, "local");
        compare(page._rows[0].detail, localCandidate.detail);
        compare(page._rows[1].kind, "remote");
        // Q20: the install row stays, dimmed and captioned, under the two others.
        compare(page._rows[2].kind, "install");
        compare(page._rows[2].dimmed, true);
        compare(page._rows[2].detail, "a local server was already found");
        compare(page._rows[0].dimmed, false);
        compare(page._rows[1].dimmed, false);
    }

    function test_rows_with_no_candidate_offer_install() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        compare(page._rows.length, 2);
        compare(page._rows[0].kind, "remote");
        compare(page._rows[1].kind, "install");
        compare(page._rows[1].dimmed, false);
        verify(page._rows[1].detail.indexOf("Adds the package") === 0);
    }

    function test_j_k_move_the_cursor_and_clamp() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        compare(page.cursorIndex, 0);
        page.moveCursor(1);
        compare(page.cursorIndex, 1);
        page.moveCursor(1); // the dimmed install row takes the cursor like any other
        compare(page.cursorIndex, 2);
        page.moveCursor(1); // clamps at the last row
        compare(page.cursorIndex, 2);
        page.moveCursor(-2);
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

    // Cookie login needs a user as well as a password, as Settings' server sheet asks; the
    // secret sent is always the visible field's, never one left in a hidden field.
    function test_cookie_asks_for_a_user_and_a_password() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.chooseCursor(); // "connect to a remote server"
        compare(page.page, "apikey");
        const key = findChild(page, "keyField");
        const user = findChild(page, "userField");
        const pass = findChild(page, "passField");

        page.editAuthMode = "apiKey";
        verify(key.visible);
        verify(!user.visible);
        key.text = "a-key";
        compare(page.editSecret, "a-key");

        page.editAuthMode = "cookie";
        verify(!key.visible);
        verify(user.visible);
        verify(pass.visible);
        compare(page.editSecret, ""); // the key does not go out as the password
        user.text = "omaseed";
        pass.text = "hunter2";
        compare(page._fieldMap().authMode, "cookie");
        compare(page._fieldMap().username, "omaseed");
        compare(page.editSecret, "hunter2");

        page.editAuthMode = "apiKey";
        compare(page.editSecret, "a-key");
        compare(page._fieldMap().username, "");
        page.editAuthMode = "none";
        compare(page.editSecret, "");
    }

    // A turned-down credential reads in words, in red, with the raw error dim underneath.
    function test_a_refused_login_says_so_with_the_raw_error_under_it() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.chooseCursor();
        const detail = findChild(page, "testErrorDetail");
        verify(!detail.visible);

        page.testing = true; // as runTest() leaves it: only an answer to a running test counts
        App.serverTested("", false, "", "", -1, 4, "wrong username or password",
                         "unauthorized (401): POST auth/login: Host requires authentication");
        compare(page.testResultText, "wrong username or password");
        verify(detail.visible);
        compare(detail.text, "unauthorized (401): POST auth/login: Host requires authentication");

        // Any other failure has only the raw error, and no second line.
        page.testing = true;
        App.serverTested("", false, "", "", -1, 4, "network: Connection refused", "");
        compare(page.testResultText, "network: Connection refused");
        verify(!detail.visible);
    }

    // The heading asks the step's own question, not step 1's on every page.
    function test_each_step_has_its_own_heading() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        const heading = findChild(page, "heading");
        compare(heading.text, "Which qBittorrent should omaseed talk to?");
        page.chooseCursor(); // remote
        compare(heading.text, "Where is it, and how does omaseed sign in?");
        page.page = "install";
        compare(heading.text, "Install qBittorrent on this machine?");
        page.page = "handler";
        compare(heading.text, "Open magnet links and .torrent files with omaseed?");
    }

    function test_enter_on_the_install_row_opens_the_install_step() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.cursorIndex = 1; // remote, install
        page.chooseCursor();
        compare(page.page, "install");
    }

    function test_dimmed_install_row_opens_the_install_step_with_the_port_warning() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.cursorIndex = 2; // local, remote, install (dimmed)
        page.chooseCursor();
        compare(page.page, "install");
        compare(page.chosenKind, "install");
        const warning = findChild(page, "portWarning");
        verify(warning !== null);
        compare(warning.visible, true);
    }

    function test_install_step_has_no_port_warning_without_a_local_server() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.cursorIndex = 1;
        page.chooseCursor();
        compare(page.page, "install");
        const warning = findChild(page, "portWarning");
        verify(warning !== null);
        compare(warning.visible, false);
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

    // Manual box M2: until a test passes, `test` is the highlighted action and `continue` is
    // dimmed with "test first"; after a pass the two swap.
    function test_test_leads_until_a_pass_then_continue_does() {
        App.loadDemo(16);
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.chooseCursor();
        const testButton = findChild(page, "testButton");
        const continueButton = findChild(page, "continueButton");
        const hint = findChild(page, "continueHint");
        verify(testButton.primary);
        verify(!continueButton.primary);
        verify(!continueButton.enabled);
        verify(hint.visible);
        compare(hint.text, "test first");

        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": App,
                                              "signalName": "serverTested"
                                          });
        page.runTest();
        spy.wait(2000);
        verify(!testButton.primary);
        verify(continueButton.primary);
        verify(continueButton.enabled);
        verify(!hint.visible);
    }

    // M2: `enter` does whichever is next: test first, then continue.
    function test_enter_tests_first_then_continues() {
        App.loadDemo(16);
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.chooseCursor();
        page.forceActiveFocus();
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": App,
                                              "signalName": "serverTested"
                                          });
        keyClick(Qt.Key_Return);
        compare(page.page, "apikey", "the first enter tests, it does not save");
        spy.wait(2000);
        verify(page.testOk);

        keyClick(Qt.Key_Return);
        compare(page.page, "handler");
        compare(App.serverList.count, 1);
        App.serverList.remove(App.serverList.idAt(0));
        compare(App.serverList.count, 0);
    }

    // M2: a pass is for what it tested. A new address or login asks for a new test; a new
    // name, only a label, keeps it. An answer for fields changed since is dropped.
    function test_changing_the_address_or_login_asks_for_a_new_test() {
        App.loadDemo(16);
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.chooseCursor();
        const spy = createTemporaryObject(signalSpyComponent, testCase, {
                                              "target": App,
                                              "signalName": "serverTested"
                                          });
        page.runTest();
        spy.wait(2000);
        verify(page.testOk);
        page.editName = "home";
        verify(page.testOk, "the name is only a label");
        page.editUrl = "http://127.0.0.1:8081";
        verify(!page.tested);
        verify(!findChild(page, "continueButton").enabled);

        page.runTest();
        page.editAuthMode = "cookie"; // changed while the test runs: its answer is stale
        spy.wait(2000);
        verify(!page.tested, "an answer for the fields before the change does not count");
    }

    // M1: the cookie choice says what it asks for.
    function test_the_cookie_choice_reads_username_and_password() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.chooseCursor();
        const texts = [];
        const walk = item => {
            for (const child of item.children) {
                if (child.text !== undefined && child.selected !== undefined)
                    texts.push(child.text);
                walk(child);
            }
        };
        walk(page); // chips and buttons alike: the auth choices sit together among them
        const at = texts.indexOf("API key");
        verify(at >= 0);
        compare(texts.slice(at, at + 3), ["API key", "username and password", "none"]);
        verify(texts.indexOf("cookie") < 0);
    }

    // M3: the footer no longer counts steps; it keeps the version until the done screen.
    function test_the_footer_does_not_count_steps() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        const facts = findChild(page, "setupFacts");
        compare(facts.text, App.versionText);
        page.chooseCursor();
        compare(facts.text, App.versionText);
        verify(facts.text.indexOf("step") < 0);
        page.page = "done";
        compare(facts.text, "setup done");
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

    // docs/14 E1: "not now" leads to the done screen; only "open the library" finishes.
    function test_handler_step_not_now_asks_Xdg_then_the_done_screen_finishes() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page.page = "handler";
        const finishedSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                       "target": page,
                                                       "signalName": "finished"
                                                   });
        page.skipHandler();
        compare(Xdg.firstRunPromptNeeded(), false);
        compare(page.page, "done");
        compare(page.registered, false);
        compare(finishedSpy.count, 0);
        verify(findChild(page, "doneColumn").visible);
        compare(findChild(page, "setupFacts").text, "setup done");

        page.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(finishedSpy.count, 1);
    }

    // When omaseed already answers for magnet: links (often only because nothing else claims
    // them), the step offers to keep it instead of asking to switch, and a skip still ends on
    // "open here". Only the answer is faked; the real handler is never registered here.
    function test_handler_step_when_omaseed_already_opens_them() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page._openHandlerStep();
        Xdg.isDefaultMagnetHandlerResult(true, "io.github.diogochaves.omaseed.desktop");
        compare(page.alreadyHandler, true);
        compare(page.heading, "Magnet links and .torrent files already open with omaseed.");
        compare(findChild(page, "registerButton").text, "keep omaseed");
        verify(findChild(page, "handlerText").text.startsWith("Keep it that way"));

        page.skipHandler();
        compare(page.registered, false);
        compare(findChild(page, "doneHandler").text, "Magnet links and .torrent files open here.");
    }

    // Another app answers: the step asks, as docs/14 drew it.
    function test_handler_step_when_another_app_opens_them() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [localCandidate]
                                           });
        page._openHandlerStep();
        Xdg.isDefaultMagnetHandlerResult(false, "org.qbittorrent.qBittorrent.desktop");
        compare(page.alreadyHandler, false);
        compare(page.heading, "Open magnet links and .torrent files with omaseed?");
        compare(findChild(page, "registerButton").text, "open them with omaseed");
        page.skipHandler();
        compare(findChild(page, "doneHandler").text,
                "Magnet links still open elsewhere; Settings can change that.");
    }

    // docs/14 B1: the welcome comes first on a real first run; enter moves on to step 1 and
    // esc quits, as its footer says. The search's answer is listed as it arrives.
    function test_welcome_enter_sets_up_and_esc_quits() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [],
                                               "page": "welcome"
                                           });
        const quitSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                   "target": page,
                                                   "signalName": "quitRequested"
                                               });
        verify(findChild(page, "detectLines").visible);
        verify(!findChild(page, "heading").visible);
        verify(!findChild(page, "setupSteps").visible);
        page.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        compare(quitSpy.count, 1);
        keyClick(Qt.Key_Return);
        compare(page.page, "choose");
        verify(findChild(page, "setupSteps").visible);
    }

    // esc backs out of a half-filled step, quits from step 1, and does nothing once the server
    // is saved (there is no un-saving it from here).
    function test_esc_goes_back_then_quits() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        const quitSpy = createTemporaryObject(signalSpyComponent, testCase, {
                                                   "target": page,
                                                   "signalName": "quitRequested"
                                               });
        page.chooseCursor();
        compare(page.page, "apikey");
        page.goBack();
        compare(page.page, "choose");
        compare(quitSpy.count, 0);
        page.page = "install";
        page.goBack();
        compare(page.page, "choose");
        page.goBack();
        compare(quitSpy.count, 1);
        page.page = "handler";
        page.goBack();
        compare(page.page, "handler");
        compare(quitSpy.count, 1);
    }

    // docs/14 D1: the named steps above the question, done ones ticked.
    function test_steps_name_where_you_are() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        const steps = findChild(page, "setupSteps");
        const labels = () => {
            const out = [];
            for (const child of steps.children)
                if (child.text !== undefined)
                    out.push(child.text);
            return out;
        };
        // Manual box M3: names only, no numbers.
        compare(labels(), ["server", "sign in", "links"]);
        page.page = "apikey";
        compare(labels(), ["✓ server", "sign in", "links"]);
        page.page = "done";
        compare(labels(), ["✓ server", "✓ sign in", "✓ links"]);
        compare(findChild(page, "heading").text.indexOf("You're connected to"), 0);
    }

    // docs/14 C1: the column sits in the middle of the space above the footer.
    function test_the_column_is_centred() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [],
                                               "width": 900,
                                               "height": 700
                                           });
        const column = findChild(page, "setupColumn");
        const footer = findChild(page, "setupFacts").parent.parent;
        tryVerify(() => column.height > 0);
        fuzzyCompare(column.x + column.width / 2, 450, 1);
        fuzzyCompare(column.y + column.height / 2, (700 - footer.height) / 2, 1);
    }

    // Too tall for the window (a big font on a small screen): the column starts at the top,
    // scrolls, and the footer keeps its own strip below it.
    function test_a_column_that_does_not_fit_scrolls_above_the_footer() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [],
                                               "width": 900,
                                               "height": 260
                                           });
        page.chooseCursor(); // step 2, the tallest
        const column = findChild(page, "setupColumn");
        const scroller = findChild(page, "setupScroller");
        const footer = findChild(page, "setupFacts").parent.parent;
        tryVerify(() => column.height > scroller.height);
        verify(scroller.interactive);
        compare(column.y, scroller.margin);
        compare(scroller.y + scroller.height, footer.y);
        verify(scroller.clip);

        // Tab to a field below the fold brings it into view.
        page.editAuthMode = "apiKey";
        findChild(page, "keyField").focusInput();
        page._cycleApiKeyFocus(1); // the test button, further down
        verify(scroller.contentY > 0);
    }

    // A server that is down right now can still be saved, but only after a test said so.
    function test_save_anyway_only_after_a_failed_test() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": []
                                           });
        page.chooseCursor();
        const saveAnyway = findChild(page, "saveAnywayButton");
        verify(!saveAnyway.visible);
        page.testing = true;
        App.serverTested("", false, "", "", -1, 4, "network: Connection refused", "");
        verify(saveAnyway.visible);
        verify(!findChild(page, "continueButton").enabled);
    }

    // Review Q9: an unreadable servers.json is named where it was kept, on the welcome and
    // step 1 only; nothing is said when it loaded.
    function test_an_unreadable_servers_file_is_named() {
        const page = createTemporaryObject(pageComponent, testCase, {
                                               "candidates": [],
                                               "page": "welcome"
                                           });
        const line = findChild(page, "serversKeptLine");
        verify(line);
        compare(App.serversKeptPath, "", "the test App loaded no servers.json");
        verify(!line.visible);

        const kept = "/home/someone/.config/omaseed/servers.json.unreadable-20260924-101500";
        page.serversKeptPath = kept;
        verify(line.visible);
        compare(line.text, "Your servers.json could not be read. It was kept as " + kept + ".");
        page.page = "choose";
        verify(line.visible);
        page.chooseCursor(); // on to the server details: the line has said its piece
        verify(page.page !== "choose");
        verify(!line.visible);
    }

    Component {
        id: signalSpyComponent

        SignalSpy {}
    }
}
