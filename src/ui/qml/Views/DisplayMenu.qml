pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The display tool's menu (board 46 `.tool.disp`, DECISIONS C24 H3–H4): rows, table, space
// (board 51 D12 a, C32), the scope's own checked. `activated` carries "rows", "table" or
// "space"; the window sets it for the scope shown (App.setDisplay). No keys (#168 assumed 8).
// Its form is the kit Menu's.
//
// Board 51 D13 a (DECISIONS C32): under them a sort block, "sort, inside each group" (the
// table's "sort"), a radio per order with the picked one's direction in words ("newest
// first"), then "reverse"; the footer names `s`, the sort palette, which stays. A sort the
// block does not list (the table's default download speed, a header click) joins it, picked,
// at its top, so the block always shows the one in use. Those rows carry "sort:<role>" and
// "sort:reverse"; `sortWord` is the order's short name, for the tool's word ("rows · added").
// Space has no sort block: size lays its map out, the largest first (board 51 D12 a), so the
// footer says that, and the tool's word is "space" alone.
Menu {
    id: root

    /// The display the scope shows now.
    property string current: ""
    /// The list's sort: a Torrent role name and a Qt.SortOrder (App.filtered's).
    property string sortRole: ""
    property int sortOrder: Qt.DescendingOrder

    /// The short name of the sort in use ("added"), "" for none and in space.
    readonly property string sortWord: root.sortRole.length > 0 && root.current !== "space"
                                       ? root.sortLabel(root.sortRole) : ""

    /// The block's orders, as board 51 `dispMenu()` lists them; `up` and `down` say each
    /// direction in words.
    readonly property var _sorts: [
        {
            "role": "addedOn",
            "text": qsTr("added"),
            "up": qsTr("oldest first"),
            "down": qsTr("newest first")
        },
        {
            "role": "name",
            "text": qsTr("name"),
            "up": qsTr("a to z"),
            "down": qsTr("z to a")
        },
        {
            "role": "size",
            "text": qsTr("size"),
            "up": qsTr("smallest first"),
            "down": qsTr("largest first")
        },
        {
            "role": "progress",
            "text": qsTr("progress"),
            "up": qsTr("least done first"),
            "down": qsTr("most done first")
        },
        {
            "role": "eta",
            "text": qsTr("time to ready"),
            "up": qsTr("soonest first"),
            "down": qsTr("latest first")
        },
        {
            "role": "ratio",
            "text": qsTr("ratio"),
            "up": qsTr("lowest first"),
            "down": qsTr("highest first")
        }
    ]
    /// Names for the orders the block lists only while in use (the table's other columns).
    readonly property var _otherWords: ({
            "dlspeed": qsTr("download speed"),
            "upspeed": qsTr("upload speed"),
            "stateText": qsTr("state"),
            "numSeeds": qsTr("seeds"),
            "numLeechs": qsTr("peers"),
            "numComplete": qsTr("swarm"),
            "completionOn": qsTr("completed"),
            "lastActivity": qsTr("last active"),
            "category": qsTr("category"),
            "tagsText": qsTr("tags"),
            "trackerHost": qsTr("tracker"),
            "totalSize": qsTr("total size"),
            "downloaded": qsTr("downloaded"),
            "uploaded": qsTr("uploaded")
        })

    function sortLabel(role: string): string {
        for (const sort of root._sorts) {
            if (sort.role === role)
                return sort.text;
        }
        return root._otherWords[role] ?? role;
    }

    function _directionWords(role: string, order: int): string {
        for (const sort of root._sorts) {
            if (sort.role === role)
                return order === Qt.AscendingOrder ? sort.up : sort.down;
        }
        return order === Qt.AscendingOrder ? qsTr("lowest first") : qsTr("highest first");
    }

    function _sortRow(role: string, text: string): var {
        const on = root.sortRole === role;
        return {
            "text": text,
            "glyph": on ? Glyphs.radioOn : Glyphs.radioOff,
            "glyphColor": on ? Theme.accent : Theme.dimForeground,
            "action": "sort:" + role,
            "note": on ? root._directionWords(role, root.sortOrder) : "",
            "checked": on,
            "radio": true
        };
    }

    dimmed: false
    footer: root.current === "space" ? qsTr("size orders the map, the largest first")
                                     : qsTr("also s, the sort palette")
    items: {
        const rows = [
            {
                "text": qsTr("rows"),
                "glyph": Glyphs.displayRows,
                "action": "rows",
                "note": qsTr("by question"),
                "checked": root.current === "rows"
            },
            {
                "text": qsTr("table"),
                "glyph": Glyphs.displayTable,
                "action": "table",
                "note": qsTr("one sorted list"),
                "checked": root.current === "table"
            },
            {
                "text": qsTr("space"),
                "glyph": Glyphs.displaySpace,
                "action": "space",
                "note": qsTr("by size"),
                "checked": root.current === "space"
            },
        ];
        if (root.current === "space")
            return rows;
        rows.push({
            "separator": true
        });
        rows.push({
            "caption": true,
            "text": root.current === "table" ? qsTr("sort") : qsTr("sort, inside each group")
        });
        const listed = root._sorts.some(sort => sort.role === root.sortRole);
        if (!listed && root.sortRole.length > 0)
            rows.push(root._sortRow(root.sortRole, root.sortWord));
        for (const sort of root._sorts)
            rows.push(root._sortRow(sort.role, sort.text));
        rows.push({
            "separator": true
        });
        rows.push({
            "text": qsTr("reverse"),
            "glyph": Glyphs.sort,
            "action": "sort:reverse"
        });
        return rows;
    }
    menuWidth: Theme.displayMenuWidth
}
