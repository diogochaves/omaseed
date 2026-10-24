pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed.Views

// docs/30 (DECISIONS C1): the command palette on a context of its own, as MainView hands it
// one. The window's keys that open it, and what its rows run, are tst_Main's.
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

    function _open(overrides) {
        const palette = createTemporaryObject(paletteComponent, testCase, {
                                                  "context": testCase._context(overrides)
                                              });
        palette.open();
        return palette;
    }

    function _type(text) {
        for (const c of text)
            keyClick(c);
    }

    height: 700
    name: "CommandPalette"
    visible: true
    when: windowShown
    width: 900

    Component {
        id: paletteComponent

        CommandPalette {
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
        id: jumpSpy

        SignalSpy {
            signalName: "jumpRequested"
        }
    }

    Component {
        id: searchSpy

        SignalSpy {
            signalName: "searchRequested"
        }
    }

    // P1: nothing typed, the query row says what the commands act on.
    function test_nothing_typed() {
        const palette = testCase._open({
                                           "recent": ["stop", "location"]
                                       });
        compare(palette.title, "commands for 2 selected");
        compare(palette.counter, "57 commands");
        compare(palette.items.length, 57);
        compare(palette.items[0].title, "stop");
        compare(palette.items[1].title, "move…");
        compare(palette.cursorIndex, 0);
        compare(palette.hints.map(hint => hint.key), ["↑↓", "enter", "esc"]);
        compare(palette.hints[2].label, "close");
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
        compare(palette.counter, "72 commands");
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
        compare(palette.counter, palette.items.length + " of 57");
        compare(palette.hints[2].label, "clear");
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
        compare(palette.hints.map(hint => hint.key), ["↑↓", "esc"]);
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

    // P4: no match. A dim line, one row for the jump palette, and `enter` hands it the text.
    function test_no_match_hands_the_text_to_the_jump_palette() {
        const palette = testCase._open({});
        const jumps = createTemporaryObject(jumpSpy, testCase, {
                                                "target": palette
                                            });
        testCase._type("ubuntu");
        compare(palette.counter, "0 of 57");
        compare(palette.emptyText, "No command matches “ubuntu”.");
        compare(palette.items.length, 2);
        compare(palette.items[0].title, "find torrents matching “ubuntu”");
        compare(palette.items[0].meta, "ctrl+k");
        // DECISIONS C26 SR14: a second row searches the plugins for it.
        compare(palette.items[1].title, "search plugins for “ubuntu”");
        compare(palette.items[1].meta, "ctrl+f");
        compare(palette.hints.map(hint => hint.key + " " + hint.label), ["enter jump palette",
                                                                         "esc clear"]);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(jumps.count, 1);
        compare(jumps.signalArguments[0][0], "ubuntu");
    }

    // C26 SR14: the second no-match row hands the text to search; never on a server without it
    // (C25 K30).
    function test_no_match_can_search_the_plugins() {
        const palette = testCase._open({});
        const searches = createTemporaryObject(searchSpy, testCase, {
                                                   "target": palette
                                               });
        testCase._type("open movie");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(palette.opened, false);
        compare(searches.count, 1);
        compare(searches.signalArguments[0][0], "open movie");
        const without = testCase._open({
                                           "searchOffered": false
                                       });
        testCase._type("ubuntu");
        compare(without.items.length, 1);
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
}
