import QtQuick
import QtTest
import Omaseed

// T3.5. `Keymap` is the contract of spec v2 §7 Fig. 26, so these cases check the data, not a
// drawing: every key the figure prints exists, no area binds one key twice, and the help
// sheet's four columns come back in the figure's order with the figure's words.
TestCase {
    id: testCase

    name: "Keymap"

    /// Every key of Fig. 26, in the figure's reading order. One deviation, logged in
    /// REVIEW-v0.1.0.md §9: the figure prints `0–9`, the app `0–8` (batch-2 S1).
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
            const keys = Keymap.forArea(area).map(entry => entry.key);
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
        compare(act.map(row => row.helpKeys), ["a", "enter", "p P", "d D", "c R", "C t m L r", "[ ]",
                                               "o", "y Y n"]);
        compare(act.map(row => row.help), ["add", "actions", "stop/start · force",
                                           "delete · with files", "recheck · reannounce",
                                           "category · tags · location · limits · rename", "queue",
                                           "open folder", "copy magnet · hash · name"]);

        const view = Keymap.helpRows("view");
        compare(view.map(row => row.helpKeys), ["i", "1–4", "s", "|", "alt+↑ alt+↓", "ctrl+b", "I",
                                                "< >"]);

        const application = Keymap.helpRows("app");
        compare(application.map(row => row.helpKeys), ["A", ", ctrl+,", "ctrl+.", "q", "ctrl+q",
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
}
