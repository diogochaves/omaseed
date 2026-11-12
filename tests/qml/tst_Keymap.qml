import QtQuick
import QtTest
import Omaseed
import Omaseed.Views

// T3.5. `Keymap` is the contract of spec v2 §7 Fig. 26, so these cases check the data, not a
// drawing: every key the figure prints exists, no area binds one key twice, and the help
// sheet's four columns come back in the figure's order with the figure's words.
TestCase {
    id: testCase

    name: "Keymap"

    /// Every key of Fig. 26, in the figure's reading order. One deviation, logged in
    /// DECISIONS C24 H2 (supersedes S1): the figure's `0–9` became the five questions, `1–5`.
    readonly property list<string> figureKeys: ["j", "k", "g", "G", "h", "l", "/", "1–5", "i", "1–4",
        "ctrl+k", "space", "v", "ctrl+a", "esc", "a", "enter", "p", "P", "d", "D", "c", "R", "C",
        "t", "m", "L", "r", "[", "]", "o", "y", "Y", "s", "A", ",", "ctrl+,", "|", "q", "ctrl+q", "?"]

    function test_every_figure_key_is_bound() {
        const bound = Keymap.entries.map(entry => entry.key);
        for (const key of testCase.figureKeys)
            verify(bound.indexOf(key) >= 0, "Fig. 26 key not in the keymap: " + key);
    }

    function test_every_area_binds_a_key_at_most_once() {
        for (const area of Keymap.areas) {
            // A menu-only action has no key (docs/30 D3): it binds nothing.
            const keys = Keymap.forArea(area).map(entry => entry.key).filter(key => key.length > 0);
            for (const key of keys) {
                compare(keys.filter(other => other === key).length, 1, "key bound twice in " + area
                        + ": " + key);
            }
        }
    }

    function test_every_entry_has_a_known_area() {
        for (const entry of Keymap.entries)
            verify(Keymap.areas.indexOf(entry.area) >= 0, "unknown area: " + entry.area);
    }

    function test_help_and_escape_are_global() {
        const help = Keymap.find("global", "?");
        verify(help !== null);
        compare(help.action, "help");
        compare(help.label, "help");

        const escape = Keymap.find("global", "esc");
        verify(escape !== null);
        compare(escape.action, "escape");
    }

    function test_f1_is_an_alias_for_the_help_sheet() {
        const f1 = Keymap.find("global", "f1");
        verify(f1 !== null, "f1 is not in the keymap");
        compare(f1.action, Keymap.find("global", "?").action);
        // It leads no help-sheet row: `?` is the key the footer and the sheet teach.
        compare(f1.column, "");
        compare(f1.helpKeys, "");
    }

    // docs/08 §4: `ctrl+,` joins `,` on Settings, and Columns moves to `|`.
    function test_ctrl_comma_is_settings_and_columns_is_the_pipe() {
        compare(Keymap.find("global", "ctrl+,").action, "settings");
        compare(Keymap.find("global", ",").action, "settings");
        compare(Keymap.find("global", "|").action, "columns");
    }

    function test_case_separates_the_stronger_variant() {
        compare(Keymap.find("table", "p").action, "stopStart");
        compare(Keymap.find("table", "P").action, "forceStart");
        compare(Keymap.find("table", "d").action, "delete");
        compare(Keymap.find("table", "D").action, "deleteWithFiles");
        compare(Keymap.find("table", "y").action, "copyMagnet");
        compare(Keymap.find("table", "Y").action, "copyHash");
        compare(Keymap.find("table", "n").action, "copyName");
    }

    // N2: the columns palette's own keys. `j`/`k` are typed text there, so the moves take alt.
    function test_the_columns_palette_moves_with_alt_and_resets_with_ctrl_r() {
        compare(Keymap.find("columns", "alt+↑").action, "moveColumnUp");
        compare(Keymap.find("columns", "alt+↓").action, "moveColumnDown");
        compare(Keymap.find("columns", "alt+k").action, "moveColumnUp");
        compare(Keymap.find("columns", "alt+j").action, "moveColumnDown");
        compare(Keymap.find("columns", "ctrl+r").action, "resetColumnOrder");
        compare(Keymap.find("columns", "space").action, "toggleColumn");
        compare(Keymap.find("columns", "j"), null);
    }

    function test_find_returns_null_for_an_unbound_key() {
        compare(Keymap.find("table", "§"), null);
        compare(Keymap.find("search", "j"), null);
    }

    function test_label_falls_back_to_the_global_entry() {
        // `/` is not a table key; the footer still prints "filter" beside it.
        compare(Keymap.labelOf("table", "/"), "filter");
        compare(Keymap.labelOf("table", "j"), "move");
        compare(Keymap.labelOf("details", "a"), "add");
        compare(Keymap.labelOf("global", "a"), "add");
    }

    // Fig. 26's rows, regrouped by docs/08 §4 (layout A): i, 1–4, s and | form "view".
    function test_the_help_sheet_is_figure_26() {
        compare(Keymap.columns, ["navigate", "select", "view", "act", "app"]);

        const navigate = Keymap.helpRows("navigate");
        compare(navigate.map(row => row.helpKeys), ["j k", "g G", "h l", "/", "1–5 0", "ctrl+k",
                                                  "ctrl+h ctrl+l"]);

        const select = Keymap.helpRows("select");
        compare(select.map(row => row.helpKeys), ["space", "v", "ctrl+a", "esc"]);

        const act = Keymap.helpRows("act");
        // Then the Files tab's two (N1) and the details pane's copy (docs/12 G5).
        // #160: shift+f, free the space, after them (C30 F13: "or add it again").
        // #146: ctrl+n, create a torrent, after add.
        compare(act.map(row => row.helpKeys), ["a", "ctrl+n", "enter", "p P e", "d D", "c R", "C t m L r", "[ ]",
                                               "o", "y Y n", "p + - x", "O o r", "F", "y enter"]);
        compare(act.map(row => row.help), ["add", "create a torrent", "actions", "stop/start · force · retry",
                                           "delete · with files", "recheck · reannounce",
                                           "category · tags · location · limits · rename", "queue",
                                           "open folder", "copy magnet · hash · name",
                                           "file priority", "open · folder · rename",
                                           "free the space, or add it again",
                                           "copy value · change option"]);

        const view = Keymap.helpRows("view");
        // #24 puts the seed console's ` first, docs/32 the log's ~ after it, and the log
        // face's own keys last.
        compare(view.map(row => row.helpKeys), ["`", "~", "ctrl+f", "i", "1–5", "s", "|", "alt+↑ alt+↓", "ctrl+b", "I",
                                                "< >", "H L", "X", "1–4 / w f t"]);

        const application = Keymap.helpRows("app");
        compare(application.map(row => row.helpKeys), [": ctrl+shift+p", "A", ", ctrl+,", "ctrl+shift+,", "ctrl+s", "ctrl+.",
                                                       "q", "ctrl+q",
                                                       "super+t"]);
    }

    // docs/08 §4: every row carries a sentence, and every section a heading.
    function test_every_sheet_row_has_a_sentence() {
        for (const column of Keymap.columns) {
            verify(Keymap.columnTitles[column].length > 0, "no heading for " + column);
            for (const row of Keymap.helpRows(column))
                verify(row.about !== undefined && row.about.length > 0, "no sentence for "
                       + row.helpKeys);
        }
    }

    function test_every_sheet_row_names_a_real_entry() {
        for (const column of Keymap.columns) {
            for (const row of Keymap.helpRows(column)) {
                verify(row.helpKeys.length > 0);
                verify(row.help.length > 0);
                // The row's chip always starts with the key the entry itself binds.
                compare(row.helpKeys.split(" ")[0], row.key);
            }
        }
    }

    // ---- the command palette (docs/30, DECISIONS C1) -----------------------------------
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
                "consoleShown": false,
                "consoleScoped": false,
                "altSpeed": false,
                "placement": "right",
                "hideSkipped": false,
                "seqDl": false,
                "firstLast": false,
                "superSeeding": false,
                "autoTmm": false
            }
        };
        for (const key in overrides)
            context[key] = overrides[key];
        return context;
    }

    function _row(rows, action) {
        return rows.find(row => row.action === action) || null;
    }

    // docs/31 S1: A's line follows the word; mixed, the same or not known keep today's, and
    // every one still says "alternative", so a search for "alt" finds it.
    function test_the_alt_speed_rows_follow_the_set() {
        const line = kind => Keymap.helpRows("app", kind).find(row => row.action === "altSpeed");
        compare(line("burst").about, "Burst: switch to your alternative limits, the faster ones");
        compare(line("burst").help, "burst");
        compare(line("turtle").about, "Turtle: switch to your alternative limits, the slower ones");
        compare(line("alt").about, "Toggle the alternative speed limits");
        compare(line(undefined).help, "alt-speed");
        for (const kind of ["burst", "turtle", "alt"])
            verify(line(kind).about.includes("alternative"), kind);
        // The table itself is not touched.
        compare(Keymap.find("global", "A").help, "alt-speed");

        const rows = kind => Keymap.commandRows(testCase._context({
                                                                       "altKind": kind
                                                                   }));
        compare(testCase._row(rows("burst"), "altSpeed").title, "burst on");
        compare(testCase._row(rows("burst"), "altLimits").title, "burst limits…");
        compare(testCase._row(rows("turtle"), "altSpeed").title, "turtle on");
        compare(testCase._row(rows("alt"), "altSpeed").title, "alt-speed on");
        compare(testCase._row(rows("alt"), "altLimits").title, "alt-speed limits…");
        const on = testCase._context({
                                         "altKind": "burst"
                                     });
        on.states.altSpeed = true;
        compare(testCase._row(Keymap.commandRows(on), "altSpeed").title, "burst off");
    }

    function test_the_palette_has_a_key_and_a_help_row() {
        compare(Keymap.find("global", ":").action, "commands");
        compare(Keymap.find("global", "ctrl+shift+p").action, "commands");
        const row = Keymap.helpRows("app")[0];
        compare(row.helpKeys, ": ctrl+shift+p");
    }

    // §5: 58 rows from the table (docs/32 added `~`, board 42 "move folder…", C24 H2 a "show
    // STATE…" per `is:` word, C24 H3 three displays, C26 "search…", #146 "create torrent…"), plus the focused area's own: Files 15,
    // Trackers 4, Peers 3, HTTP sources 3, the seed console 3, the log 14.
    function test_the_rows_from_each_area() {
        compare(Keymap.commandRows(testCase._context({})).length, 58);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "rail"
                                                     })).length, 58);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "files"
                                                     })).length, 73);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "trackers"
                                                     })).length, 62);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "peers"
                                                     })).length, 61);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "webseeds"
                                                     })).length, 61);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "console"
                                                     })).length, 61);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "log"
                                                     })).length, 73); // board 51 D5 a: export log…
        // Every action id is listed once, and movements never.
        const actions = Keymap.commandRows(testCase._context({})).map(row => row.action);
        for (const action of actions)
            compare(actions.filter(other => other === action).length, 1, "listed twice: " + action);
        for (const movement of ["moveDown", "first", "focusRail", "toggleSelection", "selectRange",
                                "question", "escape", "actions", "appMenu", "stopStart", "commands",
                                "hyprlandBind"])
            verify(actions.indexOf(movement) < 0, "not a command: " + movement);
    }

    // P1: nothing typed, the last commands run first, then the `?` sheet's order, then what
    // can't run. Each row: what enter does, its key, and what it acts on.
    function test_two_selected_both_running() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "recent": ["stop", "location", "copyMagnet"]
                                                          }));
        compare(rows.slice(0, 8).map(row => row.title), ["stop", "move…", "copy magnet",
                                                         "force start", "recheck", "reannounce",
                                                         "delete", "delete with files"]);
        const stop = rows[0];
        compare(stop.meta, "p");
        compare(stop.detail, "torrents · 2 selected");
        compare(stop.disabled, false);
        const start = testCase._row(rows, "start");
        compare(start.disabled, true);
        compare(start.meta, "", "p shows on stop, which p runs now");
        compare(start.detail, "torrents · both already running");
        compare(rows.indexOf(start), rows.length - 1, "start did not sink to the end");
        compare(testCase._row(rows, "rename").detail, "torrents · Field Recordings");
        compare(testCase._row(rows, "add").detail, "library");
        compare(testCase._row(rows, "cycleDetailsPlacement").detail, "view · now right");
        compare(testCase._row(rows, "clearSelection").disabled, false);
    }

    // DECISIONS C20 (#107): `p` stops an errored torrent, so "retry" is a command of its own,
    // `e`, listed only while a target is in error.
    function test_retry_only_while_a_target_is_in_error() {
        compare(Keymap.find("table", "e").action, "retry");
        verify(testCase._row(Keymap.commandRows(testCase._context({})), "retry") === null,
               "retry listed with nothing in error");
        const rows = Keymap.commandRows(testCase._context({
                                                              "errored": 1
                                                          }));
        const retry = testCase._row(rows, "retry");
        compare(retry.title, "retry");
        compare(retry.meta, "e");
        compare(retry.disabled, false);
        compare(retry.detail, "torrents · 2 selected");
        compare(rows.length, Keymap.commandRows(testCase._context({})).length + 1);
    }

    function test_one_stopped_one_running_runs_both_and_p_starts() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "stopped": 1,
                                                              "running": 1,
                                                              "firstStopped": true
                                                          }));
        compare(testCase._row(rows, "start").disabled, false);
        compare(testCase._row(rows, "stop").disabled, false);
        compare(testCase._row(rows, "start").meta, "p");
        compare(testCase._row(rows, "stop").meta, "");
        compare(rows[0].action, "start");
    }

    // The cursor row alone: one target, named.
    function test_the_cursor_row_alone() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "selected": 0,
                                                              "stopped": 1,
                                                              "running": 0,
                                                              "firstStopped": true
                                                          }));
        compare(testCase._row(rows, "start").detail, "torrents · Field Recordings");
        compare(testCase._row(rows, "stop").detail, "torrents · already stopped");
        compare(testCase._row(rows, "clearSelection").detail, "selection · nothing selected");
        compare(testCase._row(rows, "clearSelection").disabled, true);
    }

    // P3: no selection and no cursor row: every torrent command is dim, with the reason.
    function test_nothing_to_act_on() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "selected": 0,
                                                              "cursor": "",
                                                              "running": 0
                                                          }));
        for (const action of Keymap.commandGroups[0].actions) {
            const row = testCase._row(rows, action);
            if (action === "retry") { // C20: listed only while a target is in error
                compare(row, null);
                continue;
            }
            compare(row.disabled, true, action + " can run with nothing to act on");
            verify(row.detail.endsWith("nothing selected, no torrent under the cursor")
                   || row.detail.endsWith("no torrent under the cursor")
                   || row.detail.endsWith("pick a folder in the rail"), row.detail);
        }
        compare(testCase._row(rows, "add").disabled, false);
        compare(rows[0].action, "selectAll", "a row that can run did not come first");
    }

    // DECISIONS C24 H2: the states the rail no longer lists, one "show STATE…" row each after
    // "filter", which types `is:STATE`; none while the filter can't run.
    function test_the_states_have_palette_rows() {
        const rows = Keymap.commandRows(testCase._context({}));
        const at = rows.findIndex(row => row.action === "filter");
        verify(at >= 0);
        const states = rows.slice(at + 1, at + 9);
        compare(states.map(row => row.title), ["show downloading…", "show seeding…", "show completed…",
                                               "show stopped…", "show active…", "show stalled…",
                                               "show checking…", "show errored…"]);
        compare(states[5].action, Keymap.showStatePrefix + "stalled");
        compare(states[5].detail, "library · is:stalled");
        verify(states.every(row => !row.disabled));
        const lost = Keymap.commandRows(testCase._context({
                                                              "serverLost": true
                                                          }));
        compare(lost.filter(row => row.action.startsWith(Keymap.showStatePrefix)).length, 0);
    }

    // P6, Fig. 23: only settings, shortcuts, hide and quit run; the rest is read-only.
    function test_a_lost_server() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "serverLost": true
                                                          }));
        compare(rows.slice(0, 4).map(row => row.title), ["settings", "shortcuts", "hide the window",
                                                         "quit"]);
        compare(rows.filter(row => !row.disabled).length, 4);
        compare(testCase._row(rows, "add").detail, "library · read-only until the server is back");
        compare(testCase._row(rows, "qbtSettings").disabled, true);
    }

    // P5: from the Files tab with 3 files marked, its own commands lead.
    function test_opened_from_the_files_tab() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "area": "files",
                                                              "marked": 3,
                                                              "row": "03 - Dawn Chorus.flac",
                                                              "recent": ["stop"]
                                                          }));
        compare(rows.slice(0, 7).map(row => row.title), ["file priority…", "raise priority",
                                                         "lower priority", "skip or keep", "open file",
                                                         "show in folder", "rename files"]);
        compare(rows[0].meta, "p");
        compare(rows[0].detail, "files · 3 marked");
        compare(testCase._row(rows, "openFile").detail, "files · 03 - Dawn Chorus.flac");
        compare(testCase._row(rows, "hideSkipped").title, "hide skipped files");
        compare(testCase._row(rows, "hideSkipped").detail, "files");
        compare(rows[15].action, "stop", "the recent command did not follow the tab's own");
        // A folder under the cursor can't be opened; an empty tab has nothing to act on.
        const folder = Keymap.commandRows(testCase._context({
                                                                "area": "files",
                                                                "row": "Disc 1",
                                                                "rowIsFolder": true
                                                            }));
        compare(testCase._row(folder, "openFile").disabled, true);
        compare(testCase._row(folder, "revealFile").disabled, false);
        const empty = Keymap.commandRows(testCase._context({
                                                               "area": "files"
                                                           }));
        compare(testCase._row(empty, "priorityMenu").detail, "files · nothing under the cursor");
        compare(testCase._row(empty, "sortFiles").disabled, false);
    }

    // #160 (C17 D3): "free the space…" only on a torrent whose skipped files hold space here.
    function test_free_the_space_only_when_skipped_files_hold_space() {
        const none = Keymap.commandRows(testCase._context({
                                                              "area": "files",
                                                              "row": "a.bin"
                                                          }));
        compare(testCase._row(none, "freeSpace"), null);
        const rows = Keymap.commandRows(testCase._context({
                                                              "area": "files",
                                                              "row": "a.bin",
                                                              "freeable": true
                                                          }));
        const row = testCase._row(rows, "freeSpace");
        verify(row !== null);
        compare(row.title, "free the space…");
        compare(row.meta, "F");
        compare(row.disabled, false);
        compare(Keymap.whyNot(Keymap.find("details", "F"), {"freeable": false}),
                "no skipped file holds space here");
        // Not a command of the library's own palette.
        const library = Keymap.commandRows(testCase._context({"freeable": true}));
        compare(testCase._row(library, "freeSpace"), null);

        // DECISIONS C30 F13: when only adding it again frees them, the row says so.
        const readd = Keymap.commandRows(testCase._context({
                                                               "area": "files",
                                                               "row": "a.bin",
                                                               "freeable": true,
                                                               "readdText": "1.03 GB"
                                                           }));
        compare(testCase._row(readd, "freeSpace").title, "add it again to free 1.03 GB…");
        compare(testCase._row(readd, "freeSpace").meta, "F");
    }

    function test_the_trackers_and_peers_tabs() {
        const trackers = Keymap.commandRows(testCase._context({
                                                                  "area": "trackers",
                                                                  "row": "udp://tracker.example:1337"
                                                              }));
        compare(trackers.slice(0, 4).map(row => row.title), ["add trackers", "remove trackers",
                                                             "reannounce to every tracker",
                                                             "copy tracker URLs"]);
        compare(trackers[1].detail, "trackers · udp://tracker.example:1337");
        const peers = Keymap.commandRows(testCase._context({
                                                               "area": "peers",
                                                               "marked": 2,
                                                               "row": "10.0.0.2:6881"
                                                           }));
        compare(peers.slice(0, 3).map(row => row.title), ["add peers", "ban peers",
                                                          "copy peer addresses"]);
        compare(peers[1].meta, "b");
        // DECISIONS C24 H6: the peers are the details' swarm tab.
        compare(peers[1].detail, "swarm · 2 marked");
    }

    // An option row reads like the actions menu's: the cursor torrent decides on or off.
    function test_option_rows_follow_the_cursor_torrent() {
        let rows = Keymap.commandRows(testCase._context({}));
        const seq = testCase._row(rows, "sequential");
        compare(seq.title, "sequential download on");
        compare(seq.meta, "");
        compare(seq.via, "enter s");
        compare(seq.detail, "torrents · 2 selected · now off on Field Recordings");
        rows = Keymap.commandRows(testCase._context({
                                                        "selected": 0,
                                                        "states": {
                                                            "seqDl": true,
                                                            "detailsShown": false,
                                                            "altSpeed": true,
                                                            "railShown": false
                                                        }
                                                    }));
        compare(testCase._row(rows, "sequential").title, "sequential download off");
        compare(testCase._row(rows, "sequential").detail, "torrents · Field Recordings · now on");
        compare(testCase._row(rows, "toggleDetails").title, "show details");
        compare(testCase._row(rows, "altSpeed").title, "alt-speed off");
        compare(testCase._row(rows, "toggleRail").title, "open the rail");
        // The `details` need: smaller and larger only while the pane shows.
        compare(testCase._row(rows, "detailsSmaller").detail, "view · details hidden · i shows them");
    }

    function test_the_limit_sheets_show_their_path() {
        const rows = Keymap.commandRows(testCase._context({}));
        compare(testCase._row(rows, "globalLimits").via, ":");
        compare(testCase._row(rows, "altLimits").title, "alt-speed limits…");
    }

    // D3: every entry with no key says how to reach it, and is found by no key.
    function test_every_key_less_entry_has_a_path() {
        for (const entry of Keymap.entries) {
            if (entry.key.length > 0)
                continue;
            verify((entry.via ?? "").length > 0 || (entry.sharesKey ?? "").length > 0,
                   "no via for " + entry.action);
            verify(entry.title !== undefined, "a key-less entry that is no command: " + entry.action);
            compare(entry.column, "", "a key-less entry in the ? sheet: " + entry.action);
        }
        compare(Keymap.find("table", ""), null);
        compare(Keymap.labelOf("table", ""), "");
    }

    // Every command in the groups has an entry, and every entry with a title is in a group.
    // docs/32 §6 (#56): `~` anywhere, and the log face's keys an area of their own, which the
    // palette lists first on that face.
    function test_the_log_face_has_its_own_area() {
        compare(Keymap.find("global", "~").action, "toggleLog");
        verify(Keymap.areas.indexOf("log") >= 0);
        for (const key of ["1–4", "/", "j", "k", "g", "G", "w", "W", "f", "space", "v", "ctrl+a", "y", "enter",
                           "t", "esc"])
            verify(Keymap.find("log", key) !== null, "the log face does not bind " + key);
        const rows = Keymap.commandRows(testCase._context({
                                                              "area": "log",
                                                              "row": "Added new torrent. Torrent: \"x\""
                                                          }));
        compare(rows[0].group, "log");
        compare(rows[0].title, "filter the log");
        const follow = rows.find(row => row.action === "logFollow");
        compare(follow.title, "follow new lines");
        const copy = rows.find(row => row.action === "logCopy");
        compare(copy.meta, "y");
        verify(!copy.disabled);
        // With no line under the cursor, the line's commands can't run.
        const empty = Keymap.commandRows(testCase._context({
                                                               "area": "log"
                                                           }));
        verify(empty.find(row => row.action === "logShowTorrent").disabled);
        // The face itself is a view command everywhere.
        const toggle = Keymap.commandRows(testCase._context({})).find(row => row.action === "toggleLog");
        compare(toggle.title, "qBittorrent's log");
        compare(toggle.meta, "~");
    }

    // Board 45 B3 (C18 D7): "undo last move" while the history keeps a move, the newest only.
    function test_undo_last_move_follows_the_history() {
        const none = Keymap.commandRows(testCase._context({}));
        verify(none.find(row => row.action === "undoLastMove") === undefined,
               "listed with no move to undo");
        const move = {
            "id": "m1",
            "title": "games",
            "when": "16:24",
            "where": "/tmp/fast/games",
            "count": 48,
            "canBack": true
        };
        const rows = Keymap.commandRows(testCase._context({
                                                             "lastMove": move
                                                         }));
        const row = testCase._row(rows, "undoLastMove");
        compare(row.disabled, false);
        compare(row.title, "undo last move");
        compare(row.detail, "library · games · 16:24 · /tmp/fast/games");
        // Everything moved again since: listed, dim, with why.
        move.canBack = false;
        const dim = testCase._row(Keymap.commandRows(testCase._context({
                                                                          "lastMove": move
                                                                      })), "undoLastMove");
        compare(dim.disabled, true);
        verify(dim.detail.endsWith("moved again since: nothing to move back"), dim.detail);
    }

    function test_the_groups_and_the_entries_agree() {
        for (const group of Keymap.commandGroups) {
            for (const action of group.actions)
                verify(Keymap.commandEntry(group.group, action) !== null, group.group + ": " + action);
        }
        for (const entry of Keymap.entries) {
            if (entry.title === undefined && entry.titles === undefined)
                continue;
            verify(["", "torrents", "cursor", "stopped", "running", "errored", "selection", "details", "marked",
                    "row", "rows", "file", "servers", "folder", "lastMove", "freeable", "search", "create",
                    "createFolder"].indexOf(entry.needs) >= 0, "unknown needs: " + entry.needs);
            const listed = Keymap.commandGroups.some(group => group.actions.indexOf(entry.action) >= 0
                                                     && Keymap.commandEntry(group.group, entry.action) === entry);
            verify(listed, "a titled entry in no group: " + entry.area + " " + entry.action);
        }
    }

    // §7: the actions menu's and the ⋯ menu's rows are all in the Keymap, with the same key,
    // or (a menu-only action) a path that ends in it, so the menus and the palette can't drift.
    Component {
        id: actionsMenuComponent

        ActionsMenu {}
    }

    Component {
        id: appMenuComponent

        AppMenu {}
    }

    function test_the_menus_rows_are_in_the_keymap() {
        const actions = createTemporaryObject(actionsMenuComponent, testCase);
        // A submenu's row (#201 F15) only opens it: its rows are the ones with keys.
        const rows = [];
        for (const item of actions.items)
            rows.push(...(item.submenu ?? [item]));
        for (const item of rows) {
            if (item.separator === true)
                continue;
            const entry = Keymap.entries.find(e => e.action === item.action && e.area === "table");
            verify(entry !== undefined, "an actions menu row not in the Keymap: " + item.action);
            if (entry.key.length > 0)
                compare(entry.key, item.key, item.action);
            else
                compare(entry.via, "enter " + item.key, item.action);
        }
        // C24 H9: the omaseed menu's window rows are Keymap actions with their keys, and each
        // C13 row it gives a key has that key in the Keymap.
        const app = createTemporaryObject(appMenuComponent, testCase);
        for (const row of app.windowRows) {
            const entry = Keymap.entries.find(e => e.action === row.action && e.area === "global"
                                              && e.key === row.key);
            verify(entry !== undefined, "an omaseed menu row not in the Keymap: " + row.action);
        }
        for (const id of Object.keys(app.sharedKeys)) {
            const action = app.sharedKeymapActions[id];
            const entry = Keymap.entries.find(e => e.action === action && e.area === "global"
                                              && e.key === app.sharedKeys[id]);
            verify(entry !== undefined, "an omaseed menu key not in the Keymap: " + id);
        }
    }

    // ---- docs/33 D2, D4, D19 (DECISIONS C12) ---------------------------------------------
    readonly property var threeServers: [
        {
            "id": "a",
            "name": "public",
            "current": true,
            "last": ""
        },
        {
            "id": "b",
            "name": "private",
            "current": false,
            "last": "4 torrents"
        },
        {
            "id": "c",
            "name": "seedbox",
            "current": false,
            "last": "sign-in refused"
        }
    ]

    function test_ctrl_s_switches_server() {
        const entry = Keymap.find("global", "ctrl+s");
        compare(entry.action, "switchServer");
        compare(entry.title, "switch server…");
        verify(entry.aboutOne.indexOf("two servers or more") >= 0);
    }

    function test_the_palette_switches_server_with_two_or_more() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "servers": testCase.threeServers
                                                          }));
        const actions = rows.map(row => row.action);
        const at = actions.indexOf("switchServer");
        verify(at >= 0);
        // "switch server…" first, then one row per other server.
        compare(actions.slice(at, at + 3), ["switchServer", "switchTo:b", "switchTo:c"]);
        compare(rows[at].detail, "app · 3 servers");
        compare(rows[at + 1].title, "switch to private");
        compare(rows[at + 1].detail, "app · 4 torrents last time");
        compare(rows[at + 2].detail, "app · sign-in refused last time");
        verify(actions.indexOf("switchTo:a") < 0);
    }

    function test_the_switch_rows_run_while_the_server_is_lost() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "servers": testCase.threeServers,
                                                              "serverLost": true
                                                          }));
        verify(!rows.find(row => row.action === "switchServer").disabled);
        verify(!rows.find(row => row.action === "switchTo:b").disabled);
        verify(rows.find(row => row.action === "add").disabled);
        verify(Keymap.isOffline("switchTo:b"));
        verify(Keymap.isOffline("switchServer"));
        verify(!Keymap.isOffline("stop"));
    }

    function test_one_server_offers_no_switch() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "servers": [testCase.threeServers[0]]
                                                          }));
        verify(!rows.some(row => row.action === "switchServer" || row.action.startsWith("switchTo:")));
    }

    // ---- docs/36 §4.3 (DECISIONS C14 E4): what the server can't do is not offered --------
    readonly property var lessCapable: ({
                                            "forceStart": false,
                                            "queue": false,
                                            "tags": false,
                                            "superSeeding": false,
                                            "engineLog": false,
                                            "qbtSettings": false,
                                            "peerControl": false,
                                            "peerBan": false,
                                            "editWebSeeds": false
                                        })

    // Transmission 4.1's record (TransmissionTranslate::features; board 47 §1, C25).
    readonly property var transmission: ({
                                             "categories": false,
                                             "tags": true,
                                             "queue": true,
                                             "forceStart": true,
                                             "sequentialDownload": true,
                                             "firstLastPiece": false,
                                             "superSeeding": false,
                                             "autoManagement": false,
                                             "peerControl": false,
                                             "editWebSeeds": false,
                                             "engineLog": false,
                                             "addAgain": false,
                                             "qbtSettings": false,
                                             "torrentsBackup": true,
                                             "torrentsExport": true,
                                             "torrentsImport": false,
                                             "rss": false,
                                             "altSpeeds": true,
                                             "search": false,
                                             "createTorrent": false,
                                             "tagsAreLabels": true,
                                             "tagWord": "label",
                                             "tagsWord": "labels",
                                             "untaggedWord": "no label"
                                         })

    // Board 47's Deluge column (C25): its Label plugin's labels in the categories place, no
    // tags, no turtle; it adds a peer but bans none, and reports no web seeds.
    readonly property var deluge: ({
                                       "categories": true,
                                       "categoriesAreLabels": true,
                                       "categoryWord": "label",
                                       "categoriesWord": "labels",
                                       "uncategorizedWord": "no label",
                                       "labelPluginOff": false,
                                       "tags": false,
                                       "queue": true,
                                       "forceStart": true,
                                       "sequentialDownload": true,
                                       "firstLastPiece": true,
                                       "superSeeding": true,
                                       "autoManagement": false,
                                       "peerControl": true,
                                       "peerBan": false,
                                       "webSeeds": false,
                                       "editWebSeeds": false,
                                       "engineLog": false,
                                       "addAgain": false,
                                       "qbtSettings": false,
                                       "torrentsBackup": false,
                                       "rss": false,
                                       "altSpeeds": false,
                                       "search": false,
                                       "createTorrent": false
                                   })

    // Board 47 §B (C25 K13, K15): Deluge's labels take C's words, its act row has no t, and
    // with the Label plugin off the row is led by m and C says why in the status line.
    function test_delugesLabels_are_Cs_and_the_act_row_has_no_t() {
        const act = Keymap.helpRows("act", "alt", testCase.deluge);
        const row = act.find(r => r.action === "category");
        verify(row, "the act row went");
        compare(row.helpKeys, "C m L r");
        compare(row.help, "label · location · limits · rename");
        compare(row.about, "Set label · location · limits; rename the cursor row");
        verify(act.every(r => r.action !== "tags"), "t is offered on Deluge");
        const nav = Keymap.helpRows("navigate", "alt", testCase.deluge);
        compare(nav.find(r => r.action === "filter").about, "Filter the table by name, label or hash");
        compare(nav.find(r => r.action === "jump").about, "Jump to any torrent or label by name");
        const set = Keymap.commandRows(testCase._context({
                                                             "features": testCase.deluge
                                                         })).filter(r => r.action === "category");
        compare(set.length, 1, "C is offered once");
        compare(set[0].title, "set label");

        const off = Object.assign({}, testCase.deluge, {
                                      "categories": false,
                                      "labelPluginOff": true
                                  });
        const plain = Keymap.helpRows("act", "alt", off).find(r => r.helpKeys === "m L r");
        verify(plain, "the act row went with the Label plugin");
        compare(plain.action, "location");
        compare(plain.help, "location · limits · rename");
        // E6: both point at Settings › Deluge › Plugins, which says it in full.
        compare(Keymap.missingNote("category", off, "", "nas", "Deluge"),
                "nas (Deluge) has no labels: its Label plugin is off, see Settings › Deluge › Plugins");
        verify(Keymap.missingLine(off, "nas", "Deluge", "2.2.0").indexOf(
                   "labels (its Label plugin is off: Settings › Deluge › Plugins)") >= 0);
    }

    // #237: ctrl+shift+, takes its words from the server's engine, in the palette and the ?
    // sheet, wherever the engine has a group in Settings (its own sections: sessionSettings).
    function test_the_settings_command_is_named_for_the_engine() {
        const titleOf = features => Keymap.commandRows(testCase._context({
                                                                             "features": features
                                                                         })).find(r => r.action === "qbtSettings");
        compare(titleOf({}).title, "qBittorrent settings");
        compare(titleOf({
                            "engineName": "qBittorrent"
                        }).title, "qBittorrent settings");
        for (const engine of ["Transmission", "Deluge"]) {
            const features = Object.assign({}, engine === "Deluge" ? testCase.deluge : testCase.transmission, {
                                               "sessionSettings": true,
                                               "engineName": engine
                                           });
            verify(Keymap.offered("qbtSettings", features, ""), engine);
            const row = titleOf(features);
            verify(row, engine + ": no settings command");
            compare(row.title, engine + " settings");
            compare(row.meta, "ctrl+shift+,");
            const help = Keymap.helpRows("app", "alt", features).find(r => r.action === "qbtSettings");
            compare(help.help, engine + " settings");
            compare(Keymap.missingNote("qbtSettings", features, "", "nas", engine), "");
        }
        // Without a group of its own the command goes, and its key says so.
        verify(titleOf(testCase.deluge) === undefined);
        compare(Keymap.missingNote("qbtSettings", testCase.deluge, "", "nas", "Deluge"),
                "nas (Deluge) has no settings omaseed can change");
    }

    // Board 47 §H–§I (C25 K29, K30): what a Deluge lacks, said once and in the ? sheet's foot.
    function test_what_a_deluge_lacks_is_said_in_words() {
        compare(Keymap.missingNote("altSpeed", testCase.deluge, "", "nas", "Deluge"), "nas (Deluge) has no turtle");
        compare(Keymap.missingNote("tags", testCase.deluge, "", "nas", "Deluge"), "nas (Deluge) has no tags");
        compare(Keymap.missingNote("ban", testCase.deluge, "peers", "nas", "Deluge"),
                "nas (Deluge) has no banning peers");
        compare(Keymap.missingNote("addRow", testCase.deluge, "peers", "nas", "Deluge"), "");
        verify(Keymap.offered("addRow", testCase.deluge, "peers"));
        verify(!Keymap.offered("ban", testCase.deluge, "peers"));
        compare(Keymap.missingLine(testCase.deluge, "nas", "Deluge", "2.2.0"),
                "Not on nas (Deluge 2.2.0): the log, search, creating a torrent, RSS, tags, turtle and its schedule, automatic management, banning peers, web seeds, settings export and import, the torrents backup.");
    }

    // Board 47 §B (C25 K13–K14): Transmission's labels are called labels in the palette and
    // the ? sheet, and without categories the act row (C t m L r) is led by t, not dropped.
    function test_labels_are_called_labels_and_t_leads_the_act_row() {
        const act = Keymap.helpRows("act", "alt", testCase.transmission);
        const row = act.find(r => r.helpKeys === "t m L r");
        verify(row, "the act row went with categories");
        compare(row.action, "tags");
        compare(row.help, "labels · location · limits · rename");
        compare(row.about, "Set labels · location · limits; rename the cursor row");
        verify(act.every(r => r.action !== "category"));
        // C14 E4: adding it again is qBittorrent's; the F row only frees the space.
        const free = act.find(r => r.action === "freeSpace");
        compare(free.help, "free the space");
        verify(free.about.indexOf("add it again") < 0, free.about);
        const nav = Keymap.helpRows("navigate", "alt", testCase.transmission);
        compare(nav.find(r => r.action === "filter").about, "Filter the table by name, label or hash");
        compare(nav.find(r => r.action === "jump").about, "Jump to any torrent or label by name");
        const tags = Keymap.commandRows(testCase._context({
                                                              "features": testCase.transmission
                                                          })).filter(r => r.action === "tags");
        compare(tags.length, 1, "t is offered once");
        compare(tags[0].title, "set labels");
        // qBittorrent keeps its words.
        const qbt = Keymap.helpRows("act", "alt", undefined).find(r => r.action === "category");
        compare(qbt.help, "category · tags · location · limits · rename");
        compare(Keymap.helpRows("navigate", "alt", {}).find(r => r.action === "filter").about,
                "Filter the table by name, category, tag or hash");
    }

    // C14 E4 on a Transmission: the palette and the ? sheet keep what it does and lose what
    // it lacks; with no turtle (Deluge's K20) the alternative limits go too.
    // Board 47 §I (C25 K30): a key the server lacks says so once in the status line, and
    // the ? sheet's foot line names everything it lacks, in the board's order.
    function test_what_a_transmission_lacks_is_said_in_words() {
        compare(Keymap.missingNote("superSeeding", testCase.transmission, "", "seedbox", "Transmission"),
                "seedbox (Transmission) has no super seeding");
        compare(Keymap.missingNote("category", testCase.transmission, "", "seedbox", "Transmission"),
                "seedbox (Transmission) has no categories");
        compare(Keymap.missingNote("toggleLog", testCase.transmission, "", "seedbox", "Transmission"),
                "seedbox (Transmission) has no log");
        compare(Keymap.missingNote("addRow", testCase.transmission, "peers", "seedbox", "Transmission"),
                "seedbox (Transmission) has no adding or banning peers");
        compare(Keymap.missingNote("stop", testCase.transmission, "", "seedbox", "Transmission"), "");
        compare(Keymap.missingNote("altSpeed", testCase.transmission, "", "seedbox", "Transmission"), "");
        compare(Keymap.keyOf("toggleLog"), "~");
        compare(Keymap.missingLine(testCase.transmission, "seedbox", "Transmission", "4.1.3"),
                "Not on seedbox (Transmission 4.1.3): the log, search, creating a torrent, RSS, categories, first and last pieces first, super seeding, automatic management, adding and banning peers, editing web seeds, settings export and import, importing torrents.");
        // qBittorrent lacks none of them: no line.
        compare(Keymap.missingLine({}, "public", "qBittorrent", "5.3.0"), "");
    }

    function test_a_transmission_is_offered_what_it_does() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "features": testCase.transmission
                                                          })).map(row => row.action);
        for (const action of ["stop", "forceStart", "queueUp", "tags", "rename", "recheck",
                              "reannounce", "altSpeed"])
            verify(rows.indexOf(action) >= 0, action);
        for (const action of ["category", "superSeeding", "firstLast", "autoManagement",
                              "toggleLog", "qbtSettings", "search", "create"])
            verify(rows.indexOf(action) < 0, action);
        verify(!Keymap.offered("search:ask", testCase.transmission, ""));
        verify(!Keymap.offered("createFromFolder", testCase.transmission, ""));
        const help = ["app", "view", "torrent", "global"].reduce((out, column) => out.concat(
                                                                     Keymap.helpRows(column, "alt", testCase.transmission).map(
                                                                         row => row.action)), []);
        for (const action of ["toggleLog", "search", "create", "qbtSettings"])
            verify(help.indexOf(action) < 0, action);
        verify(help.indexOf("altSpeed") >= 0);
        const noTurtle = Object.assign({}, testCase.transmission, {
                                           "altSpeeds": false
                                       });
        verify(!Keymap.offered("altSpeed", noTurtle, ""));
        verify(!Keymap.offered("altLimits", noTurtle, ""));
    }

    function test_offered_reads_the_feature_record() {
        verify(Keymap.offered("forceStart", undefined, ""));
        verify(Keymap.offered("forceStart", {
                                  "forceStart": true
                              }, ""));
        verify(!Keymap.offered("forceStart", testCase.lessCapable, ""));
        verify(!Keymap.offered("queueUp", testCase.lessCapable, ""));
        verify(Keymap.offered("recheck", testCase.lessCapable, ""));
        // A details tab's shared key: trackers keep `a`, peers and HTTP sources lose it.
        verify(Keymap.offered("addRow", testCase.lessCapable, "trackers"));
        verify(!Keymap.offered("addRow", testCase.lessCapable, "peers"));
        verify(!Keymap.offered("ban", testCase.lessCapable, "peers"));
        verify(!Keymap.offered("removeRow", testCase.lessCapable, "webseeds"));
        // The log face's own keys go with the engine's log.
        verify(!Keymap.offered("logFilter", testCase.lessCapable, "log"));
    }

    function test_the_palette_lists_only_what_the_server_can_do() {
        const all = Keymap.commandRows(testCase._context({})).map(row => row.action);
        for (const action of ["forceStart", "queueUp", "queueDown", "tags", "superSeeding", "toggleLog",
                              "qbtSettings"])
            verify(all.indexOf(action) >= 0, action);
        const fewer = Keymap.commandRows(testCase._context({
                                                               "features": testCase.lessCapable
                                                           })).map(row => row.action);
        for (const action of ["forceStart", "queueUp", "queueDown", "tags", "superSeeding", "toggleLog",
                              "qbtSettings"])
            verify(fewer.indexOf(action) < 0, action);
        verify(fewer.indexOf("recheck") >= 0);
        verify(fewer.indexOf("category") >= 0);
        const peers = Keymap.commandRows(testCase._context({
                                                               "area": "peers",
                                                               "features": testCase.lessCapable
                                                           })).map(row => row.action);
        verify(peers.indexOf("addRow") < 0);
        verify(peers.indexOf("ban") < 0);
        verify(peers.indexOf("copy") >= 0);
    }

    function test_the_help_sheet_leaves_out_what_the_server_cant_do() {
        const view = Keymap.helpRows("view", "alt").map(row => row.action);
        verify(view.indexOf("toggleLog") >= 0);
        verify(view.indexOf("logLevels") >= 0);
        const fewer = Keymap.helpRows("view", "alt", testCase.lessCapable).map(row => row.action);
        // The log face goes, its own row with it.
        verify(fewer.indexOf("toggleLog") < 0);
        verify(fewer.indexOf("logLevels") < 0);
        compare(fewer.length, view.length - 2);
    }

    function test_the_actions_menu_hides_rows_and_tidies_separators() {
        const menu = createTemporaryObject(actionsMenuComponent, testCase);
        // Board 46's menu keeps force start and the options under "transfer ›" and the queue
        // under "queue ›" (C24 H9): the actions are read out of the submenus too.
        const flat = items => items.reduce((out, item) => out.concat(item.submenu !== undefined ? item.submenu.map(child => child.action) : [item.action]), []);
        verify(flat(menu.items).indexOf("forceStart") >= 0);
        const fewer = menu._offered(menu.items, {
                                        "forceStart": false,
                                        "queue": false,
                                        "sequentialDownload": false,
                                        "firstLastPiece": false,
                                        "superSeeding": false,
                                        "autoManagement": false
                                    });
        const actions = flat(fewer);
        for (const action of ["forceStart", "queueUp", "queueDown", "sequential", "firstLast",
                              "superSeeding", "autoManagement"])
            verify(actions.indexOf(action) < 0, action);
        // A submenu left empty goes ("queue ›").
        verify(fewer.every(item => item.submenu === undefined || item.submenu.length > 0));
        verify(actions.indexOf("stopStart") >= 0);
        verify(actions.indexOf("delete") >= 0);
        // The options' block is gone whole: no two separators in a row, none first or last.
        for (let i = 1; i < fewer.length; ++i)
            verify(!(fewer[i].separator === true && fewer[i - 1].separator === true), "two separators at " + i);
        verify(fewer[0].separator !== true);
        verify(fewer[fewer.length - 1].separator !== true);
    }
}
