pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `s` (spec v2 §4 Fig. 14): pick the table's sort. `enter` on the already-current role
// reverses it — `table.sortBy()` already does that, so this palette only ever calls it.
Palette {
    id: root

    /// The table `enter` sorts.
    required property TorrentTable table

    readonly property var _options: [
        {
            "label": qsTr("name"),
            "role": "name"
        },
        {
            "label": qsTr("size"),
            "role": "size"
        },
        {
            "label": qsTr("progress"),
            "role": "progress"
        },
        {
            "label": qsTr("download speed"),
            "role": "dlspeed"
        },
        {
            "label": qsTr("upload speed"),
            "role": "upspeed"
        },
        {
            "label": qsTr("ratio"),
            "role": "ratio"
        },
        {
            "label": qsTr("eta"),
            "role": "eta"
        },
        {
            "label": qsTr("added"),
            "role": "addedOn"
        }
    ]

    glyph: "⇅"
    hints: [
        {
            "key": "enter",
            "label": qsTr("sort")
        },
        {
            "key": "enter",
            "label": qsTr("again reverses")
        },
        {
            "key": "",
            "label": qsTr("or click a header")
        }
    ]
    items: root._rows
    title: qsTr("sort by")

    readonly property var _rows: {
        const q = root.query;
        const rows = [];
        for (const option of root._options) {
            if (!Fuzzy.match(option.label, q))
                continue;
            const current = root.table !== null && root.table.sortRoleName === option.role;
            let meta = "";
            if (current)
                meta = (App.filtered.sortOrder === Qt.AscendingOrder ? Glyphs.chevronUp
                                                                     : Glyphs.chevronDown) + " "
                        + qsTr("current");
            rows.push({
                "title": option.label,
                "meta": meta,
                "key": option.role
            });
        }
        return rows;
    }

    /// Opens with the cursor on the role the table is already sorted by — Fig. 14 draws it
    /// on "download speed · ▾ current" — instead of on row 0.
    function openOnCurrent(): void {
        root.query = "";
        root.openOn(root.table !== null ? root.table.sortRoleName : "");
    }

    onActivated: (index, item) => {
        if (root.table !== null)
            root.table.sortBy(item.key);
        root.close();
    }
    onRejected: root.close()
}
