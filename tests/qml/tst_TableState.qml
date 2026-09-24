import QtQuick
import QtTest
import Omaseed
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
}
