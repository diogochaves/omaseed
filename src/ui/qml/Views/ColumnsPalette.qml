pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// `|`: a multi-select over the table's columns. `space` toggles `table.visibleColumns`
// live, so the header changes under the palette as you go; Name can never be hidden (there
// would be nothing left to click). `MainView`'s own block persists the result to
// `state.json` whenever `table.visibleColumns` changes, so this component stays unaware of
// persistence.
Palette {
    id: root

    /// The table whose columns `space` shows or hides.
    required property TorrentTable table

    hints: [
        {
            "key": "space",
            "label": qsTr("toggle")
        }
    ]
    items: root._rows
    multi: true
    title: qsTr("columns")

    readonly property var _rows: {
        const q = root.query;
        const rows = [];
        if (root.table === null)
            return rows;
        for (const column of root.table.columns) {
            if (!Fuzzy.match(column.title, q) && !Fuzzy.match(column.key, q))
                continue;
            rows.push({
                "title": column.title,
                "meta": column.key === "name" ? qsTr("always shown") : "",
                "checked": root.table.visibleColumns.indexOf(column.key) >= 0,
                "key": column.key
            });
        }
        return rows;
    }

    onRejected: root.close()
    onToggled: (index, item) => {
        if (item.key === "name" || root.table === null)
            return;
        const list = root.table.visibleColumns.slice();
        const at = list.indexOf(item.key);
        if (at >= 0)
            list.splice(at, 1);
        else
            list.push(item.key);
        root.table.visibleColumns = list;
    }
}
