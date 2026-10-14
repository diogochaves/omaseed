import QtQuick

// The selection of one of the details pane's flat lists — Trackers, Peers, HTTP sources
// (#51 D15) — by the click rules every list follows (docs/DECISIONS.md, "The click rules").
//
// It is kept by key (a tracker's or web seed's URL, a peer's `ip:port`), not by row: the
// peers move on every poll as their speeds change, and a tracker list comes back whole. So
// is the range anchor. A row `selectable` turns down (DHT, PeX and LSD, which are rows of
// the Trackers list but not trackers of the torrent) never joins the selection, though it
// can still be the anchor of a range.
QtObject {
    id: root

    /// The list's length, and its key at a row ("" out of range).
    property int rowCount: 0
    property var keyAt: function (row) {
        return "";
    }
    /// Whether a row may join the selection.
    property var selectable: function (row) {
        return true;
    }

    /// The selected keys, in the order they were added.
    property var keys: []
    /// The range anchor: the last row a plain click, a ctrl+click or `space` left it on.
    property string anchorKey: ""

    readonly property int count: root.keys.length

    /// The key of row `row` (keyAt, as a method the pane can call).
    function keyOf(row: int): string {
        return root.keyAt(row);
    }

    function has(key: string): bool {
        return key.length > 0 && root.keys.indexOf(key) >= 0;
    }

    function rowOf(key: string): int {
        if (key.length === 0)
            return -1;
        for (let row = 0; row < root.rowCount; ++row) {
            if (root.keyAt(row) === key)
                return row;
        }
        return -1;
    }

    /// The anchor's row now, or `fallback` (the cursor) when it has none on show.
    function anchorRow(fallback: int): int {
        const row = root.rowOf(root.anchorKey);
        return row >= 0 ? row : fallback;
    }

    function clear(): void {
        root.keys = [];
        root.anchorKey = "";
    }

    /// A plain click: that row alone, and the anchor there.
    function only(row: int): void {
        const key = root.keyAt(row);
        root.keys = key.length > 0 && root.selectable(row) ? [key] : [];
        root.anchorKey = key;
    }

    /// ctrl+click, a middle click or `space`: the row in or out, and the anchor there.
    function toggle(row: int): void {
        const key = root.keyAt(row);
        root.anchorKey = key;
        if (key.length === 0 || !root.selectable(row))
            return;
        const at = root.keys.indexOf(key);
        const next = root.keys.slice();
        if (at >= 0)
            next.splice(at, 1);
        else
            next.push(key);
        root.keys = next;
    }

    /// shift+click (`add` false: the selection becomes the range), ctrl+shift+click and `v`
    /// (`add` true: the range joins it). The anchor stays where it was.
    function range(from: int, to: int, add: bool): void {
        const next = add ? root.keys.slice() : [];
        for (let row = Math.min(from, to); row <= Math.max(from, to); ++row) {
            const key = root.keyAt(row);
            if (key.length > 0 && root.selectable(row) && next.indexOf(key) < 0)
                next.push(key);
        }
        root.keys = next;
    }

    /// `ctrl+a`: every row that can be selected.
    function all(): void {
        root.range(0, root.rowCount - 1, false);
    }

    /// The list changed: keys whose row is gone leave the selection.
    function prune(): void {
        if (root.keys.length === 0)
            return;
        const present = root.keys.filter(key => root.rowOf(key) >= 0);
        if (present.length !== root.keys.length)
            root.keys = present;
    }

    /// What an action acts on: the selection when there is one, in the list's order;
    /// otherwise the cursor's row, when it can be selected.
    function targets(cursor: int): var {
        if (root.keys.length > 0) {
            const rows = root.keys.map(key => root.rowOf(key)).filter(row => row >= 0);
            rows.sort((a, b) => a - b);
            return rows.map(row => root.keyAt(row));
        }
        const key = root.keyAt(cursor);
        return key.length > 0 && root.selectable(cursor) ? [key] : [];
    }
}
