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
        compare(palette.items.map(row => row.title + " " + row.meta), ["commands /commands", "servers /servers",
                                                                       "torrents /torrents"]);
        compare(palette.items[0].detail, "58 that make sense now");
        compare(palette.items[1].detail, "2 saved");
        verify(palette.items[2].detail.startsWith("16"), palette.items[2].detail);
        compare(palette.counter, "3 kinds");
        compare(testCase._hints(palette), ["↑↓ move", "enter filter", "space filter and go on typing", "esc clear"]);
        compare(palette.footerRight, "");
        testCase._type("s");
        compare(palette.items.map(row => row.title), ["servers"]);
        testCase._type("x");
        compare(palette.items.length, 0);
        compare(palette.emptyText, "No kind matches “/sx”.");
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
        compare(palette.items.map(row => row.meta), ["/commands", "/servers", "/torrents", "/all"]);
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

    // shift+tab steps all → commands → servers → torrents → all, the text kept.
    function test_shift_tab_steps_the_kinds() {
        const palette = testCase._openOn("", {});
        testCase._type("p");
        const seen = [];
        for (let i = 0; i < 4; ++i) {
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            seen.push(palette.chip);
        }
        compare(seen, ["commands", "servers", "torrents", ""]);
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
}
