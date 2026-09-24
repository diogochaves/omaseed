pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `|`: a multi-select over the table's columns, listed in the order the table draws them.
// `space` (or `enter`, or a click) toggles `table.visibleColumns` live, so the header changes
// under the palette as you go; Name can never be hidden (there would be nothing left to
// click). N2: `alt+↑`/`alt+↓` move the cursor's column through `table.columnOrder` and the
// cursor follows it; `ctrl+r`, or the last row, puts back the default order. Name is pinned
// first, so it neither moves nor is passed. `MainView`'s own block persists both lists to
// `state.json` whenever they change, so this component stays unaware of persistence.
Palette {
    id: root

    /// The table whose columns `space` shows or hides and `alt+↑`/`alt+↓` move.
    required property TorrentTable table

    /// The key of the trailing "default order" row.
    readonly property string resetKey: "__resetOrder"

    // From the Keymap's `columns` area, so the row here, the footer and `?` say the same.
    // While a query narrows the list the order cannot be moved (review RUN-11), so the
    // `alt+↑↓` hint gives way to how to get it back.
    hints: [
        {
            "key": "space",
            "label": Keymap.labelOf("columns", "space")
        },
        root.query.length > 0 ? {
                                    "key": "esc",
                                    "label": qsTr("clear to move")
                                } : {
                                    "key": "alt+↑↓",
                                    "label": Keymap.labelOf("columns", "alt+↑")
                                },
        {
            "key": "ctrl+r",
            "label": Keymap.labelOf("columns", "ctrl+r")
        }
    ]
    items: root._rows
    multi: true
    reorderable: true
    title: qsTr("columns")

    readonly property var _rows: {
        const q = root.query;
        const rows = [];
        if (root.table === null)
            return rows;
        for (const column of root.table.orderedColumns) {
            // A header that is only a glyph (↓, ↑) says more in the list under its `name`.
            const title = column.name !== undefined ? column.name : column.title;
            if (!Fuzzy.match(title, q) && !Fuzzy.match(column.key, q))
                continue;
            rows.push({
                "title": title,
                "meta": column.key === "name" ? qsTr("always shown · first") : "",
                "checked": root.table.visibleColumns.indexOf(column.key) >= 0,
                "key": column.key
            });
        }
        // Only while the whole list shows and there is an order to undo.
        if (q.length === 0 && !root.table.columnOrderIsDefault)
            rows.push({
                "title": qsTr("default order"),
                "meta": "ctrl+r",
                "detail": qsTr("put the columns back in the order they came in"),
                "checked": false,
                "key": root.resetKey
            });
        return rows;
    }

    function _toggle(item: var): void {
        if (root.table === null || item.key === "name")
            return;
        if (item.key === root.resetKey) {
            root.table.resetColumnOrder();
            return;
        }
        const list = root.table.visibleColumns.slice();
        const at = list.indexOf(item.key);
        if (at >= 0)
            list.splice(at, 1);
        else
            list.push(item.key);
        root.table.visibleColumns = list;
    }

    onActivated: (index, item) => root._toggle(item)
    onMoveRequested: (index, item, delta) => {
        if (root.table === null || item.key === root.resetKey)
            return;
        // Past the neighbour this list shows, so a filtered list moves the way it looks.
        const shown = root.items.map(row => row.key).filter(key => key !== root.resetKey);
        if (root.table.moveColumn(item.key, delta, shown))
            root.cursorToKey(item.key);
    }
    onRejected: {
        root.close();
        // The next `|` opens on the whole list, not on the last filter.
        root.query = "";
    }
    onResetRequested: {
        if (root.table === null)
            return;
        const key = root.currentItem !== null ? root.currentItem.key : "";
        root.table.resetColumnOrder();
        root.cursorToKey(key);
    }
    onToggled: (index, item) => root._toggle(item)
}
