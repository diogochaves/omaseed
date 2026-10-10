import QtQuick
import QtTest
import Omaseed

// T3.5. The footer's teaching line, one case per figure: §1 Fig. 1 for the table, §2
// Fig. 2-4 for the details pane's three list tabs, §7 Fig. 22-24 for the three states.
TestCase {
    id: testCase

    name: "HintModel"

    /// "j k move · space select · …", the way the footer reads.
    function line(hints) {
        return hints.map(hint => hint.key + " " + hint.label).join(" · ");
    }

    HintModel {
        id: model

        filteredCount: 142
        torrentCount: 142
    }

    function init() {
        model.overlayOpen = false;
        model.connected = true;
        model.detailsTab = 0;
        model.filterText = "";
        model.filteredCount = 142;
        model.focusArea = "table";
        model.renaming = false;
        model.torrentCount = 142;
    }

    // T4.8, batch-2 D3: an overlay owns the keyboard and teaches its keys in its own hint
    // row, so the window's footer stops teaching anything under it.
    function test_an_overlay_leaves_the_footer_without_hints() {
        model.overlayOpen = true;

        compare(model.area, "overlay");
        compare(model.hints.length, 0);
        compare(model.pinned, null, "`? help` cannot work under an overlay");

        model.overlayOpen = false;
        verify(model.hints.length > 0, "the table's hints did not come back");
        verify(model.pinned !== null);
    }

    function test_an_overlay_outranks_every_window_state() {
        model.connected = false; // Fig. 23 would otherwise win
        model.overlayOpen = true;
        compare(model.area, "overlay");
        model.overlayOpen = false;
        compare(model.area, "disconnected");
    }

    function test_the_table_teaches_figure_1() {
        compare(model.area, "table");
        compare(testCase.line(model.hints),
                "j k move · space select · enter actions · p stop/start · d delete · o folder · / filter");
        compare(model.pinned.key, "?");
        compare(model.pinned.label, "help");
    }

    function test_an_inline_rename_teaches_its_two_keys() {
        model.renaming = true;
        compare(testCase.line(model.hints), "enter apply · esc cancel");
    }

    function test_the_rail_teaches_its_own_five() {
        model.focusArea = "rail";
        compare(model.area, "rail");
        compare(testCase.line(model.hints),
                "j k move · enter select · 0–8 status · z fold · l table");
        compare(model.pinned.key, "?");
    }

    function test_the_search_field_teaches_two() {
        model.focusArea = "search";
        compare(model.area, "search");
        compare(testCase.line(model.hints), "enter apply · esc clear");
    }

    // docs/12 G5: General's rows have a cursor of their own.
    function test_the_details_general_tab_teaches_its_rows() {
        model.focusArea = "details";
        model.detailsTab = 0;
        compare(model.area, "details");
        compare(testCase.line(model.hints), "j k move · y copy value · enter change");
    }

    function test_the_details_tabs_teach_figures_2_to_4() {
        model.focusArea = "details";

        model.detailsTab = 1;
        // docs/27 (#42): the ticks' key first, then marking, the menu and the filter.
        compare(testCase.line(model.hints),
                "x skip / keep · space mark · ctrl+a mark all · p priority · enter menu · / filter");

        model.detailsTab = 2;
        compare(testCase.line(model.hints), "a add · d remove · R reannounce · y copy");

        model.detailsTab = 3;
        compare(testCase.line(model.hints), "b ban · a add peer · y copy");
    }

    function test_the_empty_state_teaches_figure_22() {
        model.torrentCount = 0;
        model.filteredCount = 0;
        compare(model.area, "empty");
        compare(testCase.line(model.hints), "a add");
        compare(model.pinned.key, "?");
    }

    function test_a_lost_server_teaches_figure_23() {
        model.connected = false;
        compare(model.area, "disconnected");
        compare(testCase.line(model.hints), "R retry · , settings");
        compare(model.pinned, null);
    }

    function test_a_filter_with_no_result_teaches_figure_24() {
        model.filterText = "fedora 45";
        model.filteredCount = 0;
        compare(model.area, "filteredEmpty");
        compare(testCase.line(model.hints), "esc clear");
        compare(model.pinned, null);
    }

    function test_a_lost_server_outranks_everything_else() {
        model.connected = false;
        model.torrentCount = 0;
        model.filteredCount = 0;
        model.focusArea = "rail";
        compare(model.area, "disconnected");
    }
}
