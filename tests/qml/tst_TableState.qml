import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Review RUN-1. A fresh window reads the saved columns, order and sort and shows them. The
// table's own initial sort (TorrentTable's Component.onCompleted) must not write the
// defaults over `state.json` before the startup restore has read it.
TestCase {
    id: testCase

    height: 800
    name: "TableState"
    visible: true
    when: windowShown
    width: 1360

    Component {
        id: viewComponent

        MainView {
            anchors.fill: parent
        }
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function test_a_new_window_shows_the_saved_columns_and_sort_and_keeps_them() {
        const original = App.tableState("demo");
        const columns = ["name", "size", "progress", "addedOn"];
        const order = ["name", "addedOn", "size", "progress"];
        App.saveTableState("demo", {
                               "columns": columns,
                               "columnOrder": order,
                               "sortRole": "size",
                               "sortOrder": Qt.AscendingOrder
                           });

        const view = createTemporaryObject(viewComponent, testCase);
        verify(view !== null);
        tryCompare(view, "_stateKey", "demo");
        wait(50);

        const shown = view.table.visibleColumns;
        verify(shown.indexOf("addedOn") >= 0, "the saved Added column is not shown: " + shown);
        compare(App.filtered.sortRoleName, "size");
        compare(App.filtered.sortOrder, Qt.AscendingOrder);
        compare(view.table.columnOrder.join(","), view.table.normalizedColumnOrder(order).join(","));
        compare(view.table.visibleColumns.join(","), columns.join(","));

        const after = App.tableState("demo");
        compare(JSON.stringify(after.columns), JSON.stringify(columns),
                "state.json lost the saved columns at startup");
        compare(after.sortRole, "size", "state.json lost the saved sort at startup");

        App.saveTableState("demo", original);
    }

    // docs/17 M10.5: a folded pane reopens folded, with the size it had kept for unfolding.
    function test_a_new_window_reopens_the_details_folded_as_left() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {
                               "detailsHeight": 300,
                               "detailsFolded": true
                           });

        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");
        compare(view.split.folded, true);
        compare(view.details.collapsed, true);
        compare(view.split.sideHeight, Theme.space(300));

        view.toggleDetails();
        compare(view.split.folded, false);
        compare(App.tableState("demo").detailsFolded, false, "unfolding was not saved");
        compare(App.tableState("demo").detailsHeight, 300, "the kept size changed on unfolding");

        App.saveTableState("demo", original);
    }

    // #34: after a restore writes the settings, the open window shows the restored columns,
    // order and sort at once, for the server it already shows, and keeps them.
    function test_a_restore_shows_the_restored_layout_at_once() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {
                               "columns": ["name", "size", "progress", "status"],
                               "sortRole": "name",
                               "sortOrder": Qt.DescendingOrder
                           });
        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");
        compare(App.filtered.sortRoleName, "name");

        // What a restore leaves in state.json for this server.
        const columns = ["name", "size", "progress", "addedOn"];
        const order = ["name", "addedOn", "size", "progress"];
        App.saveTableState("demo", {
                               "columns": columns,
                               "columnOrder": order,
                               "sortRole": "size",
                               "sortOrder": Qt.AscendingOrder
                           });
        compare(App.filtered.sortRoleName, "name", "the layout changed before the restore said so");
        App.tableStatesRestored();

        compare(view.table.visibleColumns.join(","), columns.join(","));
        compare(view.table.columnOrder.join(","), view.table.normalizedColumnOrder(order).join(","));
        compare(App.filtered.sortRoleName, "size");
        compare(App.filtered.sortOrder, Qt.AscendingOrder);
        compare(view._stateKey, "demo");
        // Applying it saved nothing over it.
        const after = App.tableState("demo");
        compare(JSON.stringify(after.columns), JSON.stringify(columns));
        compare(after.sortRole, "size");

        App.saveTableState("demo", original);
    }

    // #52: dragged widths come back with the table's layout, in base-12 units, and a state
    // file without them (before #52) gives the defaults.
    function test_a_new_window_shows_the_saved_widths() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {
                               "columnWidths": {
                                   "size": 140,
                                   "name": 300
                               }
                           });
        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");
        compare(view.table.columnWidths.size, 140);
        compare(view.table.columnWidths.name, 300);
        compare(view.table.columnLayout.size.w, Theme.space(140));
        verify(view.table.columnLayout.name.w >= Theme.space(300));
        compare(App.tableState("demo").columnWidths.size, 140, "startup wrote over the widths");

        App.saveTableState("demo", {
                               "columns": ["name", "size", "progress", "status"]
                           });
        view.reloadTableState();
        verify(view.table.columnWidthsAreDefault, "an older state file kept the last widths");

        App.saveTableState("demo", original);
    }

    // A width set in the table (a grip's release, a fit) lands in state.json, and the
    // columns palette's "default order" takes it out again.
    function test_a_width_change_is_saved_and_reset_with_the_order() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {});
        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");

        view.table.setColumnWidth("eta", 90);
        compare(App.tableState("demo").columnWidths.eta, 90);
        view.table.resetColumns();
        compare(JSON.stringify(App.tableState("demo").columnWidths), "{}");

        App.saveTableState("demo", original);
    }

    // #34: a restore's widths show at once, as its order and sort do.
    function test_a_restore_shows_the_restored_widths_at_once() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {});
        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");
        verify(view.table.columnWidthsAreDefault);

        App.saveTableState("demo", {
                               "columnWidths": {
                                   "progress": 200
                               }
                           });
        App.tableStatesRestored();
        compare(view.table.columnWidths.progress, 200);
        compare(view.table.columnLayout.progress.w, Theme.space(200));
        compare(App.tableState("demo").columnWidths.progress, 200);

        App.saveTableState("demo", original);
    }

    // A state file from before docs/17 has no `detailsFolded`: the pane opens, as it always did.
    function test_an_older_state_file_opens_the_details() {
        const original = App.tableState("demo");
        App.saveTableState("demo", {
                               "detailsHeight": 300
                           });

        const view = createTemporaryObject(viewComponent, testCase);
        tryCompare(view, "_stateKey", "demo");
        compare(view.split.folded, false);
        compare(view.details.collapsed, false);

        App.saveTableState("demo", original);
    }
}
