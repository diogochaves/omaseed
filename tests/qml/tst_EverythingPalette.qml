pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Board 63 (DECISIONS C42): the everything palette on a context and a server list of its own,
// over the demo library's torrents. Its commands are docs/30's (DECISIONS C1), its torrents
// Fig. 12's, its servers docs/33's; the jump cases on a real table are tst_Palettes', and the
// window's keys that open it, and what its rows run, are tst_Main's.
TestCase {
    id: testCase

    /// Board 30 P1: two torrents selected, both running, the cursor on Field Recordings.
    function _context(overrides) {
        const context = {
            "area": "table",
            "selected": 2,
            "cursor": "Field Recordings",
            "stopped": 0,
            "running": 2,
            "firstStopped": false,
            "marked": 0,
            "row": "",
            "rowIsFolder": false,
            "serverLost": false,
            "recent": [],
            "states": {
                "detailsShown": true,
                "railShown": true,
                "placement": "right"
            }
        };
        for (const key in overrides)
            context[key] = overrides[key];
        return context;
    }

    /// The palette on `kind` ("" for every kind), as MainView opens it.
    function _openOn(kind, overrides) {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "context": testCase._context(overrides),
                                                  "serverRows": testCase.servers
                                              });
        palette.openFor(kind);
        return palette;
    }

    /// Narrowed to its commands: the command palette as it was.
    function _open(overrides) {
        return testCase._openOn("commands", overrides);
    }

    function _headings(palette) {
        return palette.items.filter(row => row.heading === true).map(row => row.title + " " + row.meta);
    }

    function _hints(palette) {
        return palette.hints.map(hint => hint.key + " " + hint.label);
    }

    function init() {
        if (App.torrents.count !== 16)
            App.loadDemo(16);
    }

    /// Two saved servers, as `App.serverRows` gives them.
    readonly property var servers: [
        {
            "key": "one",
            "title": "public",
            "meta": "on it now",
            "detail": "10.99.0.2:8080 · 16 torrents",
            "address": "10.99.0.2:8080"
        },
        {
            "key": "two",
            "title": "private",
            "meta": "3 torrents",
            "detail": "10.99.0.3:8080 · yesterday",
            "address": "10.99.0.3:8080"
        }
    ]

    function _type(text) {
        for (const c of text)
            keyClick(c);
    }

    height: 700
    name: "EverythingPalette"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: paletteComponent

        EverythingPalette {
            anchors.fill: parent
        }
    }

    Component {
        id: tableComponent

        SettingsTable {}
    }

    // A server that has every key the schema names, each one on (tst_SettingsTable's).
    Component {
        id: fullServerComponent

        QtObject {
            id: fullServer

            property var applied: []
            property bool loaded: true
            /// The keys that are not plain on.
            property var overrides: ({})
            property var values: new Proxy({}, {
                                               "get": (target, key) => key === "enabled_plugins" ? ["Label", "Scheduler"]
                                                                                                   : key in fullServer.overrides
                                                                                                   ? fullServer.overrides[key] : true
                                           })

            signal applyFailed(var keys, string error)

            function apply(changes) {
                applied.push(changes);
            }

            function has(key) {
                return true;
            }

            function owns(changes) {
                return false;
            }
        }
    }

    Component {
        id: settingsSpy

        SignalSpy {
            signalName: "settingsRequested"
        }
    }

    /// A table with omaseed's own rows only: no server has answered.
    function _ownTable() {
        return createTemporaryObject(tableComponent, testCase, {
                                         "connected": false
                                     });
    }

    /// A table on a server with everything, speaking `engine` ("" for qBittorrent).
    function _serverTable(engine, overrides) {
        const server = createTemporaryObject(fullServerComponent, testCase, {
                                                 "overrides": overrides ?? ({})
                                             });
        return createTemporaryObject(tableComponent, testCase, {
                                         "connected": true,
                                         "prefs": server,
                                         "presets": server,
                                         "serverName": "public",
                                         "features": engine.length === 0 ? ({}) : ({
                                                                                      "qbtSettings": false,
                                                                                      "sessionSettings": true
                                                                                  }),
                                         "engineName": engine
                                     });
    }

    /// The palette on `kind` over the settings of `table`.
    function _openWith(table, kind) {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "context": testCase._context({}),
                                                  "serverRows": testCase.servers,
                                                  "settings": table
                                              });
        palette.openFor(kind);
        return palette;
    }

    Component {
        id: runSpy

        SignalSpy {
            signalName: "runRequested"
        }
    }

    Component {
        id: anySpy

        SignalSpy {}
    }

    Component {
        id: searchSpy

        SignalSpy {
            signalName: "searchRequested"
        }
    }

    // ---- narrowed to the commands: docs/30's palette, row for row (C42 Z5) ----------------
    // P1: nothing typed, the query row says what the commands act on.
    function test_nothing_typed() {
        const palette = testCase._open({
                                           "recent": ["stop", "location"]
                                       });
        compare(palette.title, "commands for 2 selected");
        compare(palette.counter, "58 commands");
        compare(palette.items.length, 58);
        compare(palette.items[0].title, "stop");
        compare(palette.items[1].title, "move…");
        compare(palette.cursorIndex, 0);
        compare(palette.chip, "commands");
        compare(testCase._hints(palette), ["↑↓ move", "enter run", "shift+tab kind", "esc close"]);
        compare(palette.footerRight, "? lists every key");
        palette.close();
    }

    function test_the_title_follows_what_the_commands_act_on() {
        let palette = testCase._open({
                                         "selected": 0
                                     });
        compare(palette.title, "commands for Field Recordings");
        palette.close();
        palette = testCase._open({
                                     "area": "files",
                                     "marked": 3,
                                     "row": "03 - Dawn Chorus.flac"
                                 });
        compare(palette.title, "commands for 3 marked files");
        compare(palette.counter, "73 commands");
        palette.close();
        palette = testCase._open({
                                     "serverLost": true
                                 });
        compare(palette.title, "commands · the server is lost");
        palette.close();
    }

    // P2: whole-word hits before letters in order, then rows that can run, then recent ones.
    function test_typing_filters_and_ranks() {
        const palette = testCase._open({});
        testCase._type("del");
        compare(palette.items[0].title, "delete");
        compare(palette.items[1].title, "delete with files");
        verify(palette.items.length > 2);
        compare(palette.counter, palette.items.length + " of 58");
        compare(palette.hints[palette.hints.length - 1].label, "clear");
        verify(palette.items.every(row => row.heading !== true && row.more !== true),
               "the commands' own list is cut into sections");
        palette.close();
    }

    function test_recent_commands_break_ties() {
        const palette = testCase._open({
                                           "recent": ["copyName"]
                                       });
        testCase._type("copy");
        compare(palette.items[0].action, "copyName");
        palette.close();
    }

    // The area word matches too: `files` lists the Files tab's commands.
    function test_the_area_word_matches() {
        const palette = testCase._open({
                                           "area": "files",
                                           "row": "notes.txt"
                                       });
        testCase._type("files");
        verify(palette.items.some(row => row.action === "skipFile"), "the Files tab's skip is not listed");
        palette.close();
    }

    // P3: every match can't run: no cursor, no `enter` hint, and `enter` does nothing.
    function test_nothing_to_act_on() {
        const palette = testCase._open({
                                           "selected": 0,
                                           "cursor": "",
                                           "running": 0
                                       });
        const runs = createTemporaryObject(runSpy, testCase, {
                                               "target": palette
                                           });
        testCase._type("recheck"); // "stop" would find "show stopped…" too (C24 H2), which can
        verify(palette.items.length > 0);
        verify(palette.items.every(row => row.disabled), "a row can run with nothing to act on");
        compare(palette.currentItem, null);
        compare(palette.hints.map(hint => hint.key), ["↑↓", "shift+tab", "esc"]);
        keyClick(Qt.Key_Return);
        compare(runs.count, 0);
        compare(palette.opened, true);
        palette.close();
    }

    // `enter` closes, then asks for the row's action.
    function test_enter_closes_and_runs() {
        const palette = testCase._open({});
        const runs = createTemporaryObject(runSpy, testCase, {
                                               "target": palette
                                           });
        testCase._type("force");
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(runs.count, 1);
        compare(runs.signalArguments[0][0], "forceStart");
    }

    // P4 as C42 Z11 leaves it: no match is a dim line and, on a server with search, the row
    // that searches its plugins (C26 SR14; never on a server without it, C25 K30). C1 D8's
    // row to the jump palette is gone.
    function test_no_command_matches() {
        const palette = testCase._open({});
        const searches = createTemporaryObject(searchSpy, testCase, {
                                                   "target": palette
                                               });
        testCase._type("open movie");
        compare(palette.counter, "0 of 58");
        compare(palette.emptyText, "No command matches “open movie”.");
        compare(palette.items.length, 1);
        compare(palette.items[0].title, "search the plugins for “open movie”");
        compare(palette.items[0].meta, "ctrl+f");
        compare(testCase._hints(palette), ["enter search", "esc clear"]);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(searches.count, 1);
        compare(searches.signalArguments[0][0], "open movie");
        const without = testCase._open({
                                           "searchOffered": false
                                       });
        testCase._type("ubuntu");
        compare(without.items.length, 0);
        compare(testCase._hints(without), ["esc clear"]);
        without.close();
    }

    // Found in the box: after a run from a query, the next opening put the cursor where that
    // row had been, not on the first row.
    function test_every_opening_starts_on_the_first_row() {
        const palette = testCase._open({});
        testCase._type("start");
        compare(palette.currentItem.action, "forceStart");
        verify(palette.cursorIndex > 0);
        keyClick(Qt.Key_Return);
        palette.context = testCase._context({
                                                "recent": ["forceStart"]
                                            });
        palette.open();
        compare(palette.query, "");
        compare(palette.cursorIndex, 0);
        compare(palette.currentItem.action, "forceStart");
        palette.close();
    }

    // The escape ladder: clear, then close; every opening starts empty.
    function test_escape_clears_then_closes() {
        const palette = testCase._open({});
        testCase._type("sort");
        keyClick(Qt.Key_Escape);
        compare(palette.query, "");
        compare(palette.opened, true);
        keyClick(Qt.Key_Escape);
        compare(palette.opened, false);
        palette.open();
        compare(palette.query, "");
        palette.close();
    }

    // ---- every kind (C42 Z1–Z3, Z6, Z11) ---------------------------------------------------
    // Z6: no filter, nothing typed: the commands, and no chip. The footer says what typing adds.
    function test_unfiltered_and_empty_is_the_command_list() {
        const palette = testCase._openOn("", {});
        compare(palette.chip, "");
        compare(palette.title, "commands for 2 selected");
        compare(palette.counter, "58 commands");
        compare(palette.items.length, 58);
        compare(testCase._headings(palette), []);
        compare(testCase._hints(palette), ["↑↓ move", "enter run", "shift+tab kind", "esc close"]);
        compare(palette.footerRight, "type to find torrents, servers");
        palette.close();
    }

    // Z1, Z2, Z3: several kinds match: a section each in the fixed order, its count in the
    // heading, four rows, then "N more". The cursor starts on the first command.
    function test_several_kinds_make_sections_of_four() {
        const palette = testCase._openOn("", {});
        testCase._type("p");
        const torrents = App.jumpMatchCount("p");
        const commands = palette._commands("p").length;
        verify(torrents > 4 && commands > 4, "the fixture: p finds " + commands + " commands, " + torrents + " torrents");
        compare(testCase._headings(palette), ["commands " + commands, "servers 2", "torrents " + torrents]);
        compare(palette.counter, (commands + 2 + torrents) + " matches");
        const rows = palette.items;
        compare(rows[0].heading, true);
        compare(rows.slice(1, 5).map(row => row.kind), ["commands", "commands", "commands", "commands"]);
        compare(rows[5].more, true);
        compare(rows[5].title, (commands - 4) + " more commands");
        compare(rows[5].meta, "enter shows them");
        // Two servers: under the cap, so no "more" row.
        compare(rows.slice(6, 9).map(row => row.heading === true ? "heading" : row.title), ["heading", "public",
                                                                                           "private"]);
        compare(rows.filter(row => row.kind === "torrents" && row.more !== true).length, 4);
        compare(rows[rows.length - 1].title, (torrents - 4) + " more torrents");
        // Each kind's rows are its palette's: the same ranking, the same words.
        compare(rows[1].title, palette._commands("p")[0].title);
        compare(rows[10].title, App.jumpMatches("p", 1)[0].title);
        compare(palette.cursorIndex, 1);
        compare(palette.footerRight, "? lists every key");
        // A screen reader hears the row's kind where kinds are mixed.
        compare(rows[1].spokenTitle, rows[1].title + ", command");
        compare(rows[7].spokenTitle, "public, server");
        compare(rows[10].spokenTitle, rows[10].title + ", torrent");
        palette.close();
    }

    // Z1: the footer's enter word, and tab, follow the cursor's kind.
    function test_the_footer_follows_the_cursors_kind() {
        const palette = testCase._openOn("", {});
        testCase._type("p");
        compare(testCase._hints(palette), ["↑↓ move", "enter run", "shift+tab kind", "esc clear"]);
        palette.cursorToKey("more:commands");
        compare(testCase._hints(palette), ["↑↓ move", "enter show", "shift+tab kind", "esc clear"]);
        palette.cursorToKey("two");
        compare(testCase._hints(palette), ["↑↓ move", "enter switch", "tab menu", "shift+tab kind", "esc clear"]);
        palette.cursorToKey(App.jumpMatches("p", 1)[0].key);
        compare(testCase._hints(palette), ["↑↓ move", "enter jump", "tab actions", "shift+tab kind", "esc clear"]);
        palette.close();
    }

    // Z3: enter on "N more" narrows to that kind and keeps the text.
    function test_n_more_narrows_with_the_text_kept() {
        const palette = testCase._openOn("", {});
        testCase._type("p");
        palette.cursorToKey("more:torrents");
        keyClick(Qt.Key_Return);
        compare(palette.opened, true);
        compare(palette.filter, "torrents");
        compare(palette.chip, "torrents");
        compare(palette.query, "p");
        compare(palette.items.length, App.jumpMatchCount("p"));
        compare(testCase._headings(palette), []);
        verify(palette.items.every(row => row.more !== true));
        compare(palette.cursorIndex, 0);
        compare(palette.counter, App.jumpMatchCount("p") + " of 16");
        palette.close();
    }

    // Z3: one kind matches: its whole list, no heading and no cap.
    function test_one_kind_matching_has_no_heading_and_no_cap() {
        const palette = testCase._openOn("", {});
        testCase._type("bunny");
        compare(palette.items.length, 1);
        compare(palette.items[0].kind, "torrents");
        verify(palette.items[0].title.indexOf("Bunny") >= 0);
        compare(palette.counter, "1 match");
        compare(testCase._hints(palette), ["↑↓ move", "enter jump", "tab actions", "shift+tab kind", "esc clear"]);
        keyClick(Qt.Key_Escape);
        testCase._type("example");
        const torrents = App.jumpMatchCount("example");
        verify(torrents > 4 && palette._commands("example").length === 0, "the fixture: example finds only torrents");
        compare(palette.items.length, torrents);
        compare(testCase._headings(palette), []);
        palette.close();
    }

    // Z11: nothing found: the dim line and the row that searches, where the server has search.
    function test_nothing_found() {
        const palette = testCase._openOn("", {});
        const searches = createTemporaryObject(searchSpy, testCase, {
                                                   "target": palette
                                               });
        testCase._type("zzqq");
        compare(palette.counter, "0 matches");
        compare(palette.emptyText, "Nothing here matches “zzqq”.");
        compare(palette.items.map(row => row.title), ["search the plugins for “zzqq”"]);
        compare(testCase._hints(palette), ["enter search", "esc clear"]);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(searches.signalArguments[0][0], "zzqq");
        const without = testCase._openOn("", {
                                             "searchOffered": false
                                         });
        testCase._type("zzqq");
        compare(without.items.length, 0);
        compare(without.emptyText, "Nothing here matches “zzqq”.");
        without.close();
    }

    // ---- the filter (C42 Z4) --------------------------------------------------------------
    // `/` lists the kinds, each with its count; letters narrow them.
    function test_slash_lists_the_kinds() {
        const palette = testCase._openOn("", {});
        testCase._type("/");
        compare(palette.items.map(row => row.title + " " + row.meta), ["commands /commands", "settings /settings",
                                                                       "servers /servers", "torrents /torrents"]);
        compare(palette.items[0].detail, "58 that make sense now");
        compare(palette.items[2].detail, "2 saved");
        verify(palette.items[3].detail.startsWith("16"), palette.items[3].detail);
        compare(palette.counter, "4 kinds");
        compare(testCase._hints(palette), ["↑↓ move", "enter filter", "space filter and go on typing", "esc clear"]);
        compare(palette.footerRight, "");
        // The plan's call 10: `/s` names two kinds, the settings first (Z2's order).
        testCase._type("s");
        compare(palette.items.map(row => row.title), ["settings", "servers"]);
        testCase._type("er");
        compare(palette.items.map(row => row.title), ["servers"]);
        testCase._type("x");
        compare(palette.items.length, 0);
        compare(palette.emptyText, "No kind matches “/serx”.");
        palette.close();
    }

    // `/t` and space set the chip and clear the text: what is typed next is the query.
    function test_slash_and_space_set_the_chip() {
        const palette = testCase._openOn("", {});
        testCase._type("/t bunny");
        compare(palette.filter, "torrents");
        compare(palette.chip, "torrents");
        compare(palette.query, "bunny");
        compare(palette.items.length, 1);
        compare(palette.items[0].kind, "torrents");
        palette.close();
    }

    // enter does the same, and replaces a chip that is on.
    function test_slash_and_enter_set_the_chip() {
        const palette = testCase._openOn("torrents", {});
        testCase._type("/ser");
        keyClick(Qt.Key_Return);
        compare(palette.opened, true);
        compare(palette.chip, "servers");
        compare(palette.query, "");
        compare(palette.items.map(row => row.title), ["public", "private"]);
        palette.close();
    }

    // With a chip on the list also has `/all`, which clears it.
    function test_slash_all_clears_a_chip() {
        const palette = testCase._openOn("servers", {});
        testCase._type("/");
        compare(palette.items.map(row => row.meta), ["/commands", "/settings", "/servers", "/torrents", "/all"]);
        testCase._type("a");
        keyClick(Qt.Key_Return);
        compare(palette.filter, "");
        compare(palette.chip, "");
        compare(palette.query, "");
        compare(palette.items.length, 58);
        palette.close();
    }

    // backspace in an empty query removes the chip; with text it erases the text.
    function test_backspace_removes_the_chip() {
        const palette = testCase._openOn("torrents", {});
        testCase._type("b");
        keyClick(Qt.Key_Backspace);
        compare(palette.chip, "torrents");
        keyClick(Qt.Key_Backspace);
        compare(palette.chip, "");
        compare(palette.opened, true);
        compare(palette.items.length, 58);
        palette.close();
    }

    // shift+tab steps all → commands → settings → servers → torrents → all, the text kept.
    function test_shift_tab_steps_the_kinds() {
        const palette = testCase._openOn("", {});
        testCase._type("p");
        const seen = [];
        for (let i = 0; i < 5; ++i) {
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            seen.push(palette.chip);
        }
        compare(seen, ["commands", "settings", "servers", "torrents", ""]);
        compare(palette.query, "p");
        palette.close();
    }

    // ---- narrowed to the torrents: Fig. 12's palette (C42 Z5) -----------------------------
    function test_on_torrents_it_is_the_jump_palette() {
        const palette = testCase._openOn("torrents", {});
        compare(palette.chip, "torrents");
        compare(palette.title, "jump to a torrent");
        compare(palette.counter, "16 of 16");
        compare(palette.items.length, 16);
        compare(palette.metaSpoken, true);
        compare(palette.hasSecondary, true);
        compare(testCase._hints(palette), ["↑↓ move", "enter jump", "tab actions", "shift+tab kind", "esc close"]);
        compare(palette.footerRight, "also matches hash and tracker");
        testCase._type("bunny");
        compare(palette.counter, "1 of 16");
        // A command's name finds no command here.
        keyClick(Qt.Key_Escape);
        testCase._type("delete");
        verify(palette.items.every(row => row.kind === "torrents"));
        palette.close();
    }

    // ---- narrowed to the servers: docs/33 S1's palette (C42 Z5, Z8) ------------------------
    function test_on_servers_it_is_the_server_palette() {
        const palette = testCase._openOn("servers", {});
        compare(palette.chip, "servers");
        compare(palette.title, "switch server");
        compare(palette.counter, "2 servers");
        compare(palette.items.map(row => row.title), ["public", "private"]);
        // The cursor starts on the next one.
        compare(palette.cursorIndex, 1);
        compare(testCase._hints(palette), ["↑↓ move", "enter switch", "tab menu", ", manage servers", "shift+tab kind",
                                           "esc close"]);
        testCase._type("10.99.0.2");
        compare(palette.items.map(row => row.title), ["public"]);
        compare(palette.counter, "2 servers");
        testCase._type("x");
        compare(palette.emptyText, "No server matches “10.99.0.2x”.");
        palette.close();
    }

    function test_a_servers_enter_comma_and_tab() {
        let palette = testCase._openOn("servers", {});
        const switched = createTemporaryObject(anySpy, testCase, {
                                                   "target": palette,
                                                   "signalName": "switched"
                                               });
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(switched.count, 1);
        compare(switched.signalArguments[0][0], "two");

        palette = testCase._openOn("servers", {});
        const managed = createTemporaryObject(anySpy, testCase, {
                                                  "target": palette,
                                                  "signalName": "manageRequested"
                                              });
        keyClick(Qt.Key_Comma);
        compare(palette.opened, false);
        compare(managed.count, 1);
        compare(managed.signalArguments[0][0], "two");

        // Z8: tab asks for the server's menu.
        palette = testCase._openOn("servers", {});
        const menus = createTemporaryObject(anySpy, testCase, {
                                                "target": palette,
                                                "signalName": "serverMenuRequested"
                                            });
        keyClick(Qt.Key_Tab);
        compare(palette.opened, false);
        compare(menus.count, 1);
        compare(menus.signalArguments[0][0], "two");
    }

    // Z8: tab does nothing on a command; a comma outside the servers is typed text.
    function test_a_command_has_no_tab() {
        const palette = testCase._openOn("", {});
        const runs = createTemporaryObject(runSpy, testCase, {
                                               "target": palette
                                           });
        keyClick(Qt.Key_Tab);
        compare(palette.opened, true);
        compare(runs.count, 0);
        testCase._type(",");
        compare(palette.query, ",");
        compare(palette.opened, true);
        palette.close();
    }

    // The add sheet's chip: held to the servers, with no chip and no way to another kind.
    function test_held_to_the_servers() {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "serverRows": testCase.servers
                                              });
        palette.openOnly("servers");
        compare(palette.chip, "");
        compare(palette.filterable, false);
        compare(palette.cursorIndex, 1);
        compare(testCase._hints(palette), ["↑↓ move", "enter switch", ", manage servers", "esc close"]);
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        keyClick(Qt.Key_Backspace);
        compare(palette.items.map(row => row.title), ["public", "private"]);
        testCase._type("/");
        compare(palette.items.length, 0, "a / listed the kinds in a palette held to one");
        palette.close();
    }

    // ---- settings as rows (board 63 §6, C42 Z2, Z7, Z8; #266) ------------------------------
    // Z2: the settings are the second section; four rows, then "N more settings".
    function test_settings_are_the_second_section() {
        const palette = testCase._openWith(testCase._ownTable(), "");
        testCase._type("p");
        const settings = palette._settings("p").length;
        verify(settings > 4, "the fixture: p finds " + settings + " settings");
        compare(testCase._headings(palette).map(heading => heading.split(" ")[0]), ["commands", "settings", "servers",
                                                                                    "torrents"]);
        compare(testCase._headings(palette)[1], "settings " + settings);
        const rows = palette.items;
        const at = rows.findIndex(row => row.heading === true && row.title === "settings");
        compare(rows.slice(at + 1, at + 5).map(row => row.kind), ["settings", "settings", "settings", "settings"]);
        compare(rows[at + 5].more, true);
        compare(rows[at + 5].title, (settings - 4) + " more settings");
        compare(rows[at + 6].heading, true);
        compare(rows[at + 1].spokenTitle, rows[at + 1].title + ", setting");
        // "N more" narrows to the settings, the text kept.
        palette.cursorToKey("more:settings");
        compare(testCase._hints(palette)[1], "enter show");
        keyClick(Qt.Key_Return);
        compare(palette.chip, "settings");
        compare(palette.query, "p");
        compare(palette.items.length, settings);
        compare(palette.counter, settings + " of " + palette.allSettings.length);
        palette.close();
    }

    // Z6: nothing typed and no filter is still the commands alone.
    function test_no_settings_with_nothing_typed() {
        const palette = testCase._openWith(testCase._ownTable(), "");
        compare(palette.items.length, 58);
        verify(palette.items.every(row => row.kind === "commands"));
        compare(palette.footerRight, "type to find settings, servers, torrents");
        palette.close();
    }

    // Z4: `/se` and space set the chip; nothing typed lists every setting in the table's
    // order, less the rows that are no setting to find (the plan's calls 1 and 13).
    function test_slash_se_and_space_set_the_settings_chip() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "");
        testCase._type("/");
        compare(palette.items[1].detail, "26 on omaseed");
        testCase._type("se ");
        compare(palette.filter, "settings");
        compare(palette.chip, "settings");
        compare(palette.query, "");
        compare(palette.title, "find a setting");
        compare(palette.counter, "26 settings");
        const listed = table.rows().map(row => row.id).filter(id => ["backup.backUp", "backup.restore",
                                                                     "about.widgetLine"].indexOf(id) < 0);
        // 30, less those three and Window transparency, which an opaque ground has no row for.
        compare(listed.length, 26);
        compare(palette.items.map(row => row.key), listed);
        compare(testCase._headings(palette), []);
        // The first is a switch (Z7): `enter` says which way it goes.
        compare(testCase._hints(palette), ["↑↓ move", palette.currentItem.toggle ? "enter turn off" : "enter turn on",
                                           "tab show in Settings", "shift+tab kind", "esc close"]);
        testCase._type("zzzz");
        compare(palette.items.length, 0);
        compare(palette.emptyText, "No setting matches “zzzz”.");
        palette.close();
    }

    // Board 63 S1: the pane's name matches too, so `notif` lists the Notifications pane, as
    // Settings orders it, with no heading (one kind matched, Z3).
    function test_a_panes_name_lists_its_rows() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "");
        testCase._type("notif");
        compare(testCase._headings(palette), []);
        const pane = table.rows().filter(row => row.pane === "notifications").map(row => row.id);
        compare(pane.length, 7);
        compare(palette.items.map(row => row.key), pane);
        compare(palette.counter, "7 matches");
        verify(palette.items.every(row => row.kind === "settings" && row.detail === "Settings › Notifications"));
        // The six switches show theirs before "on" or "off"; the threshold says its number.
        compare(palette.items.filter(row => typeof row.toggle === "boolean").length, 6);
        const completed = palette.items[0];
        compare(completed.title, table.row(completed.key).title);
        compare(completed.toggle, table.row(completed.key).value);
        compare(completed.meta, completed.toggle ? "on" : "off");
        const threshold = palette.items.find(row => typeof row.toggle !== "boolean");
        compare(threshold.toggle, undefined);
        compare(threshold.meta, table.row(threshold.key).valueText);
        // A title's hit goes before a pane's.
        palette.query = "weekly";
        compare(palette.items[0].key, "notifications.weekly");
        // Words over both find it; letters scattered through a title do not (a torrent's
        // name would find settings by chance).
        palette.query = "notif weekly";
        compare(palette.items.map(row => row.key), ["notifications.weekly"]);
        palette.query = "wkly";
        verify(palette.items.every(row => row.kind !== "settings"));
        palette.close();
    }

    // A setting's row: its value as the meta, a screen reader hears it, and where it lives
    // as the second line; a choice says its option and draws no switch.
    function test_a_settings_row() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "settings");
        const was = App.pulseRows;
        App.pulseRows = true;
        let pulse = palette.items.find(row => row.key === "appearance.pulseRows");
        compare(pulse.title, "Pulse sending rows");
        compare(pulse.detail, "Settings › Appearance");
        compare(pulse.meta, "on");
        compare(pulse.toggle, true);
        compare(pulse.metaSpoken, true);
        waitForRendering(palette.listView);
        palette.cursorToKey("appearance.pulseRows");
        const drawn = palette.listView.itemAtIndex(palette.cursorIndex);
        compare(drawn.Accessible.description, "on · Settings › Appearance");
        verify(findChild(drawn, "paletteRowToggle").visible);
        // The list follows the value.
        App.pulseRows = false;
        pulse = palette.items.find(row => row.key === "appearance.pulseRows");
        compare(pulse.meta, "off");
        compare(pulse.toggle, false);
        App.pulseRows = was;
        const skin = palette.items.find(row => row.key === "appearance.skin");
        compare(skin.toggle, undefined);
        compare(skin.meta, table.row("appearance.skin").valueText);
        verify(skin.meta.length > 0);
        palette.close();
    }

    // Z7, Z8 (board 63 S3): `enter` on a setting that is changed in its pane, and `tab` on
    // any setting, close the palette and ask for Settings on that row.
    function test_enter_on_a_go_row_and_tab_ask_for_settings_on_the_row() {
        const palette = testCase._openWith(testCase._ownTable(), "");
        const asked = createTemporaryObject(settingsSpy, testCase, {
                                                "target": palette
                                            });
        const run = createTemporaryObject(runSpy, testCase, {
                                              "target": palette
                                          });
        testCase._type("low disk");
        palette.cursorToKey("notifications.lowDiskThreshold");
        compare(palette.currentItem.key, "notifications.lowDiskThreshold");
        compare(palette.currentItem.settingKind, "go");
        verify(testCase._hints(palette).indexOf("enter open in Settings") >= 0);
        verify(testCase._hints(palette).indexOf("tab show in Settings") >= 0);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(asked.count, 1);
        compare(asked.signalArguments[0][0], "notifications.lowDiskThreshold");
        // `tab` on a switch and on a choice shows them in Settings, unchanged.
        const was = App.pulseRows;
        const skin = App.skinChoice;
        palette.openFor("settings");
        testCase._type("pulse sending");
        compare(palette.currentItem.key, "appearance.pulseRows");
        keyClick(Qt.Key_Tab);
        compare(palette.opened, false);
        compare(asked.count, 2);
        compare(asked.signalArguments[1][0], "appearance.pulseRows");
        compare(App.pulseRows, was, "tab flipped the switch");
        palette.openFor("");
        // Commands match "skin" too, and theirs is the first section.
        testCase._type("skin");
        palette.cursorToKey("appearance.skin");
        compare(palette.currentItem.key, "appearance.skin");
        keyClick(Qt.Key_Tab);
        compare(palette.opened, false);
        compare(palette.choosing, "");
        compare(asked.count, 3);
        compare(asked.signalArguments[2][0], "appearance.skin");
        compare(App.skinChoice, skin);
        compare(run.count, 0);
    }

    // Board 63 S1 (Z7): `enter` flips a switch in its row; the palette stays, the cursor on
    // that row, so several can be set in one visit.
    function test_enter_flips_a_switch_and_the_palette_stays() {
        const palette = testCase._openWith(testCase._ownTable(), "");
        const asked = createTemporaryObject(settingsSpy, testCase, {
                                                "target": palette
                                            });
        const was = App.pulseRows;
        App.pulseRows = true;
        // The pane's name lists its rows; the cursor is taken off the first.
        testCase._type("appearance");
        palette.cursorToKey("appearance.pulseRows");
        const at = palette.cursorIndex;
        verify(at > 0);
        compare(palette.currentItem.toggle, true);
        compare(testCase._hints(palette), ["↑↓ move", "enter turn off", "tab show in Settings", "shift+tab kind",
                                           "esc clear"]);
        keyClick(Qt.Key_Return);
        compare(App.pulseRows, false);
        compare(palette.opened, true);
        compare(palette.query, "appearance");
        compare(palette.cursorIndex, at);
        compare(palette.currentItem.key, "appearance.pulseRows");
        compare(palette.currentItem.toggle, false);
        compare(palette.currentItem.meta, "off");
        compare(testCase._hints(palette)[1], "enter turn on");
        waitForRendering(palette.listView);
        const drawn = palette.listView.itemAtIndex(palette.cursorIndex);
        compare(drawn.Accessible.description, "off · Settings › Appearance");
        // And back, in the same visit.
        keyClick(Qt.Key_Return);
        compare(App.pulseRows, true);
        compare(palette.currentItem.key, "appearance.pulseRows");
        // The cursor is the hand's again once it moves: a later change leaves it alone.
        keyClick(Qt.Key_Up);
        const moved = palette.currentItem.key;
        verify(moved !== "appearance.pulseRows");
        App.pulseRows = false;
        compare(palette.currentItem.key, moved);
        compare(asked.count, 0);
        App.pulseRows = was;
        palette.close();
    }

    // A server's switch posts what its pane's switch posts (the row's `enable` extra
    // included), and a refusal is said in the footer.
    function test_enter_flips_a_servers_switch() {
        const table = testCase._serverTable("", {
                                                "max_ratio_enabled": false,
                                                "max_ratio": -1
                                            });
        const palette = testCase._openWith(table, "settings");
        testCase._type("share ratio");
        compare(palette.currentItem.title, "Share ratio");
        compare(palette.currentItem.toggle, false);
        compare(testCase._hints(palette)[1], "enter turn on");
        const key = palette.currentItem.key;
        keyClick(Qt.Key_Return);
        compare(palette.opened, true);
        compare(palette.currentItem.key, key);
        compare(table.prefs.applied.length, 1);
        compare(table.prefs.applied[0], {
                    "max_ratio_enabled": true,
                    "max_ratio": 1
                });
        compare(palette.footerRight, "");
        table.prefs.applyFailed(["max_ratio_enabled"], "the server said no");
        compare(palette.footerRight, "not changed: the server said no");
        compare(palette.currentItem.key, key);
        // The next key takes the line away.
        keyClick(Qt.Key_Down);
        compare(palette.footerRight, "");
        palette.close();
    }

    // A row its pane would ask about first never changes from the palette: `enter` goes
    // to the pane (board 63 S3; the plan's calls 4 and 5).
    function test_a_confirmed_row_never_changes_here() {
        const table = testCase._serverTable("");
        const palette = testCase._openWith(table, "settings");
        const asked = createTemporaryObject(settingsSpy, testCase, {
                                                "target": palette
                                            });
        const confirmed = ["server.connection.listen_port"].concat(
                            palette.items.filter(row => row.key.startsWith("server.webui.")
                                                 || row.key.endsWith(".anonymous_mode")).map(row => row.key));
        verify(confirmed.length >= 3, confirmed.join(" "));
        verify(confirmed.some(key => key.endsWith(".anonymous_mode")));
        for (const key of confirmed) {
            palette.openFor("settings");
            palette.cursorToKey(key);
            compare(palette.currentItem.key, key);
            compare(palette.currentItem.settingKind, "go", key);
            compare(palette.currentItem.toggle, undefined, key);
            compare(testCase._hints(palette)[1], "enter open in Settings", key);
            keyClick(Qt.Key_Return);
            compare(palette.opened, false, key);
            compare(asked.signalArguments[asked.count - 1][0], key);
        }
        compare(asked.count, confirmed.length);
        compare(table.prefs.applied.length, 0);
    }

    // An own row its pane dims is the pane's too: no switch, and `enter` opens Settings.
    function test_a_dimmed_row_goes_to_its_pane() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "settings");
        const dimmed = table.rows().filter(row => !row.enabled && row.kind !== "go");
        if (dimmed.length === 0)
            skip("every own switch and choice can be set here");
        const asked = createTemporaryObject(settingsSpy, testCase, {
                                                "target": palette
                                            });
        const before = dimmed[0].value;
        palette.cursorToKey(dimmed[0].id);
        compare(palette.currentItem.key, dimmed[0].id);
        compare(palette.currentItem.settingKind, "go");
        compare(palette.currentItem.toggle, undefined);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(asked.count, 1);
        compare(asked.signalArguments[0][0], dimmed[0].id);
        compare(table.row(dimmed[0].id).value, before);
    }

    // Board 63 S2 (Z7): `enter` on a choice lists its options in place of the matches;
    // `enter` sets one and the matches are back, `esc` goes back with nothing changed.
    function test_enter_on_a_choice_lists_its_options() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "settings");
        const asked = createTemporaryObject(settingsSpy, testCase, {
                                                "target": palette
                                            });
        const was = App.skinChoice;
        App.skinChoice = "omarchy";
        testCase._type("skin");
        compare(palette.currentItem.key, "appearance.skin");
        compare(palette.chip, "settings");
        compare(testCase._hints(palette), ["↑↓ move", "enter choose", "tab show in Settings", "shift+tab kind",
                                           "esc clear"]);
        keyClick(Qt.Key_Return);
        compare(palette.opened, true);
        compare(palette.choosing, "appearance.skin");
        compare(palette.query, "");
        compare(palette.title, "Skin");
        compare(palette.chip, "");
        const options = table.row("appearance.skin").options;
        compare(palette.items.map(row => row.title), options.map(option => option[1]));
        compare(palette.counter, options.length + " options");
        compare(palette.footerRight, "Settings › Appearance");
        compare(testCase._hints(palette), ["↑↓ move", "enter use", "esc back"]);
        // The one in use is checked, and the cursor starts on it.
        compare(palette.items.filter(row => row.meta === Glyphs.check).map(row => row.value), ["omarchy"]);
        compare(palette.currentItem.value, "omarchy");
        waitForRendering(palette.listView);
        compare(palette.listView.itemAtIndex(palette.cursorIndex).Accessible.name, "Omarchy, in use");
        // No kind, no filter step, no second action while choosing.
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        keyClick(Qt.Key_Backspace);
        compare(palette.opened, true);
        compare(palette.choosing, "appearance.skin");
        compare(palette.filter, "settings");
        compare(asked.count, 0);
        testCase._type("/");
        compare(palette.items.length, 0);
        compare(palette.emptyText, "No option matches “/”.");
        compare(testCase._hints(palette), ["esc clear"]);
        keyClick(Qt.Key_Escape);
        compare(palette.choosing, "appearance.skin");
        // Typing narrows the options.
        testCase._type("cons");
        compare(palette.items.map(row => row.value), ["console"]);
        compare(palette.counter, "1 of " + options.length);
        keyClick(Qt.Key_Return);
        compare(App.skinChoice, "console");
        compare(palette.opened, true);
        compare(palette.choosing, "");
        compare(palette.query, "skin");
        compare(palette.chip, "settings");
        compare(palette.currentItem.key, "appearance.skin");
        compare(palette.currentItem.meta, "Console");
        // In again; `esc` goes back with nothing changed, the next clears, the third closes.
        keyClick(Qt.Key_Return);
        compare(palette.choosing, "appearance.skin");
        compare(palette.currentItem.value, "console");
        keyClick(Qt.Key_Up);
        compare(palette.currentItem.value, "omarchy");
        keyClick(Qt.Key_Escape);
        compare(palette.opened, true);
        compare(palette.choosing, "");
        compare(App.skinChoice, "console");
        compare(palette.query, "skin");
        compare(palette.currentItem.key, "appearance.skin");
        keyClick(Qt.Key_Escape);
        compare(palette.opened, true);
        compare(palette.query, "");
        keyClick(Qt.Key_Escape);
        compare(palette.opened, false);
        // A new opening is on the matches.
        palette.openFor("settings");
        compare(palette.choosing, "");
        palette.close();
        App.skinChoice = was;
    }

    // In the bar lists only what can be had now, as its pane does.
    function test_a_choice_lists_only_the_offered_options() {
        const table = testCase._ownTable();
        const palette = testCase._openWith(table, "settings");
        palette.cursorToKey("appearance.inTheBar");
        compare(palette.currentItem.key, "appearance.inTheBar");
        keyClick(Qt.Key_Return);
        compare(palette.choosing, "appearance.inTheBar");
        const offered = table.options("appearance.inTheBar");
        verify(offered.length > 0);
        compare(palette.items.map(row => row.value), offered.map(option => option[0]));
        verify(palette.items.every(row => table.offers("appearance.inTheBar", row.value)));
        keyClick(Qt.Key_Escape);
        compare(palette.choosing, "");
        palette.close();
    }

    // A server's choice posts its key, and the cursor waits on the row for the answer.
    function test_a_servers_choice() {
        const table = testCase._serverTable("", {
                                                "encryption": 0
                                            });
        const palette = testCase._openWith(table, "");
        testCase._type("encryption");
        palette.cursorToKey("server.discovery.encryption");
        compare(palette.currentItem.key, "server.discovery.encryption");
        compare(palette.currentItem.meta, "prefer");
        keyClick(Qt.Key_Return);
        compare(palette.choosing, "server.discovery.encryption");
        compare(palette.items.map(row => row.title), ["prefer", "require", "off"]);
        compare(palette.currentItem.title, "prefer");
        compare(palette.footerRight, table.row("server.discovery.encryption").paneText);
        verify(palette.footerRight.startsWith("Settings › qBittorrent › "));
        // The one in use again posts nothing.
        keyClick(Qt.Key_Return);
        compare(table.prefs.applied.length, 0);
        compare(palette.choosing, "");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(table.prefs.applied.length, 1);
        compare(table.prefs.applied[0], {
                    "encryption": 1
                });
        compare(palette.opened, true);
        compare(palette.choosing, "");
        compare(palette.query, "encryption");
        compare(palette.currentItem.key, "server.discovery.encryption");
        palette.close();
    }

    // The server's settings are rows only once it has answered, and only its engine's.
    function test_the_server_on_shows_settings() {
        const none = testCase._openWith(testCase._ownTable(), "settings");
        verify(none.items.every(row => !row.server));
        none.close();

        const qbt = testCase._serverTable("");
        const palette = testCase._openWith(qbt, "settings");
        const server = palette.items.filter(row => row.server);
        compare(server.length, qbt.serverRows().length);
        compare(palette.items.length, 26 + server.length);
        // After omaseed's own, section by section.
        compare(palette.items.findIndex(row => row.server), 26);
        const port = palette.items.find(row => row.key === "server.connection.listen_port");
        verify(port !== undefined);
        compare(port.detail, "Settings › qBittorrent › Connection");
        compare(port.toggle, undefined);
        testCase._type("/");
        compare(palette.items[1].detail, (26 + server.length) + " on omaseed and public");
        palette.query = "";
        // A row the pane asks about first is no switch here: it says its value in words.
        const webui = palette.items.filter(row => row.key.startsWith("server.webui."));
        verify(webui.length > 0);
        verify(webui.every(row => row.toggle === undefined));
        // The section's name finds its rows.
        palette.query = "connection";
        verify(palette.items.some(row => row.key === "server.connection.listen_port"));
        palette.close();

        const transmission = testCase._serverTable("Transmission");
        const other = testCase._openWith(transmission, "settings");
        const theirs = other.items.filter(row => row.server);
        compare(theirs.length, transmission.serverRows().length);
        verify(theirs.length > 0 && theirs.length < server.length);
        verify(theirs.every(row => row.detail.startsWith("Settings › Transmission › ")), theirs[0].detail);
        other.close();

        // Deluge's plugin rows are the pane's to show (the plan's call 6).
        const deluge = testCase._serverTable("Deluge");
        const readOnly = deluge.serverRows().filter(row => row.readOnly).map(row => row.id);
        compare(readOnly.length, 3);
        const third = testCase._openWith(deluge, "settings");
        compare(third.items.filter(row => row.server).length, deluge.serverRows().length - 3);
        verify(third.items.every(row => readOnly.indexOf(row.key) < 0));
        third.close();
    }

    // No table, no settings: the palette as it was (the add sheet's, a test's).
    function test_without_a_table_there_are_no_settings() {
        const palette = testCase._openOn("settings", {});
        compare(palette.items.length, 0);
        compare(palette.counter, "0 settings");
        palette.close();
    }
}
