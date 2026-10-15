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
    /// docs/DECISIONS.md S1: the figure prints `0–9`, the app `0–8` (batch-2 S1).
    readonly property list<string> figureKeys: ["j", "k", "g", "G", "h", "l", "/", "0–8", "i", "1–4",
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
        compare(navigate.map(row => row.helpKeys), ["j k", "g G", "h l", "/", "0–8", "ctrl+k",
                                                  "ctrl+h ctrl+l"]);

        const select = Keymap.helpRows("select");
        compare(select.map(row => row.helpKeys), ["space", "v", "ctrl+a", "esc"]);

        const act = Keymap.helpRows("act");
        // Then the Files tab's two (N1) and the details pane's copy (docs/12 G5).
        compare(act.map(row => row.helpKeys), ["a", "enter", "p P", "d D", "c R", "C t m L r", "[ ]",
                                               "o", "y Y n", "p + - x", "O o r", "y enter"]);
        compare(act.map(row => row.help), ["add", "actions", "stop/start · force",
                                           "delete · with files", "recheck · reannounce",
                                           "category · tags · location · limits · rename", "queue",
                                           "open folder", "copy magnet · hash · name",
                                           "file priority", "open · folder · rename",
                                           "copy value · change option"]);

        const view = Keymap.helpRows("view");
        // #24 puts the seed console's ` first.
        compare(view.map(row => row.helpKeys), ["`", "i", "1–4", "s", "|", "alt+↑ alt+↓", "ctrl+b", "I",
                                                "< >", "H L", "X"]);

        const application = Keymap.helpRows("app");
        compare(application.map(row => row.helpKeys), [": ctrl+shift+p", "A", ", ctrl+,", "ctrl+shift+,", "ctrl+.",
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

    function test_the_palette_has_a_key_and_a_help_row() {
        compare(Keymap.find("global", ":").action, "commands");
        compare(Keymap.find("global", "ctrl+shift+p").action, "commands");
        const row = Keymap.helpRows("app")[0];
        compare(row.helpKeys, ": ctrl+shift+p");
    }

    // §5: 43 rows from the table, plus the focused area's own: Files 15, Trackers 4, Peers 3,
    // HTTP sources 3, the seed console 3.
    function test_the_rows_from_each_area() {
        compare(Keymap.commandRows(testCase._context({})).length, 43);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "rail"
                                                     })).length, 43);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "files"
                                                     })).length, 58);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "trackers"
                                                     })).length, 47);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "peers"
                                                     })).length, 46);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "webseeds"
                                                     })).length, 46);
        compare(Keymap.commandRows(testCase._context({
                                                         "area": "console"
                                                     })).length, 46);
        // Every action id is listed once, and movements never.
        const actions = Keymap.commandRows(testCase._context({})).map(row => row.action);
        for (const action of actions)
            compare(actions.filter(other => other === action).length, 1, "listed twice: " + action);
        for (const movement of ["moveDown", "first", "focusRail", "toggleSelection", "selectRange",
                                "statusFilter", "escape", "actions", "more", "stopStart", "commands",
                                "hyprlandBind"])
            verify(actions.indexOf(movement) < 0, "not a command: " + movement);
    }

    // P1: nothing typed, the last commands run first, then the `?` sheet's order, then what
    // can't run. Each row: what enter does, its key, and what it acts on.
    function test_two_selected_both_running() {
        const rows = Keymap.commandRows(testCase._context({
                                                              "recent": ["stop", "location", "copyMagnet"]
                                                          }));
        compare(rows.slice(0, 8).map(row => row.title), ["stop", "set location", "copy magnet",
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
            compare(row.disabled, true, action + " can run with nothing to act on");
            verify(row.detail.endsWith("nothing selected, no torrent under the cursor")
                   || row.detail.endsWith("no torrent under the cursor"), row.detail);
        }
        compare(testCase._row(rows, "add").disabled, false);
        compare(rows[0].action, "selectAll", "a row that can run did not come first");
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
        compare(peers[1].detail, "peers · 2 marked");
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
        compare(testCase._row(rows, "toggleRail").title, "show the rail");
        // The `details` need: smaller and larger only while the pane shows.
        compare(testCase._row(rows, "detailsSmaller").detail, "view · details hidden · i shows them");
    }

    function test_the_limit_sheets_show_their_path() {
        const rows = Keymap.commandRows(testCase._context({}));
        compare(testCase._row(rows, "globalLimits").via, "ctrl+.");
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
    function test_the_groups_and_the_entries_agree() {
        for (const group of Keymap.commandGroups) {
            for (const action of group.actions)
                verify(Keymap.commandEntry(group.group, action) !== null, group.group + ": " + action);
        }
        for (const entry of Keymap.entries) {
            if (entry.title === undefined && entry.titles === undefined)
                continue;
            verify(["", "torrents", "cursor", "stopped", "running", "selection", "details", "marked",
                    "row", "rows", "file"].indexOf(entry.needs) >= 0, "unknown needs: " + entry.needs);
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
        id: moreMenuComponent

        MoreMenu {
            appRows: true
        }
    }

    function test_the_menus_rows_are_in_the_keymap() {
        const actions = createTemporaryObject(actionsMenuComponent, testCase);
        for (const item of actions.items) {
            if (item.separator === true)
                continue;
            const entry = Keymap.entries.find(e => e.action === item.action && e.area === "table");
            verify(entry !== undefined, "an actions menu row not in the Keymap: " + item.action);
            if (entry.key.length > 0)
                compare(entry.key, item.key, item.action);
            else
                compare(entry.via, "enter " + item.key, item.action);
        }
        const more = createTemporaryObject(moreMenuComponent, testCase);
        for (const item of more.items) {
            if (item.separator === true)
                continue;
            const entry = Keymap.entries.find(e => e.action === item.action && (e.area === "global"
                                                                                 || e.area === "table"));
            verify(entry !== undefined, "a ⋯ menu row not in the Keymap: " + item.action);
            if (entry.key.length > 0)
                compare(entry.key, item.key, item.action);
            else
                compare(entry.via, "ctrl+.", item.action);
        }
    }
}
