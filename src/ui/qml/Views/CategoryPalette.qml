pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `C` (spec v2 §4 Fig. 13): set the category of one or more torrents. `none` is always the
// first row, then every category App knows about (filtered by the query), each carrying its
// torrent count and save path; typing a name nothing matches adds a `create "<query>"` row
// that both creates the category (torrents/createCategory) and applies it.
//
// Opened by `openFor(hashes)` rather than the base `open()` so the hashes travel with it;
// `MainView` answers `App.categoryPickerRequested`.
Palette {
    id: root

    /// The torrents `enter` applies the chosen category to.
    property var hashes: []
    /// `openForChoice()`: the palette answers with `chosen(name)` instead of applying the
    /// category to torrents. That is what the add sheet's `…` chip needs — the torrent it is
    /// about does not exist yet.
    property bool choiceMode: false

    /// The name picked (or created) in choice mode. A `create` row still creates it on the
    /// server first, so the next sheet and the rail see it.
    signal chosen(string name)

    footerRight: qsTr("new name creates it")
    glyph: Glyphs.category
    hints: [
        {
            "key": "",
            "label": qsTr("type to filter")
        },
        {
            "key": "enter",
            "label": qsTr("set")
        }
    ]
    items: root._rows
    title: root.choiceMode ? qsTr("category for the new torrent") : root.hashes.length
                             === 1 ? qsTr("category for 1 torrent") : qsTr(
                                         "category for %1 torrents").arg(root.hashes.length)

    readonly property var _rows: {
        // `App.categories.count` is a real NOTIFY-backed dependency, unlike the invokable
        // `names()` call itself (see JumpPalette's `items` for why that matters here).
        void App.categories.count;
        const q = root.query;
        const rows = [];
        const names = App.categories.names();
        if (Fuzzy.match(qsTr("none"), q))
            rows.push({
                "title": qsTr("none"),
                "key": ""
            });
        for (const name of names) {
            if (!Fuzzy.match(name, q))
                continue;
            rows.push({
                "title": name,
                "meta": String(App.counts.categoryCount(name)),
                "detail": App.categories.savePathOf(name),
                "key": name
            });
        }
        if (q.length > 0 && names.indexOf(q) < 0)
            rows.push({
                "title": qsTr("create \"%1\"").arg(q),
                "key": q,
                "isCreate": true
            });
        return rows;
    }

    /// Opens the palette for `newHashes` (the seam `App.categoryPickerRequested` drives).
    /// The cursor starts on the category those torrents already share (Fig. 13/14's rule:
    /// a picker opens on the current value), not on row 0.
    function openFor(newHashes: var): void {
        root.hashes = newHashes;
        root.choiceMode = false;
        root.query = "";
        root.openOn(root._sharedCategory());
    }

    /// The category every hash already has, or `undefined` when they disagree.
    function _sharedCategory(): var {
        let shared;
        for (let i = 0; i < root.hashes.length; ++i) {
            const value = App.torrentRow(root.hashes[i]).category;
            if (i === 0)
                shared = value;
            else if (shared !== value)
                return undefined;
        }
        return shared;
    }

    /// Opens the palette to answer one caller (the add sheet) with `chosen(name)`.
    function openForChoice(): void {
        root.hashes = [];
        root.choiceMode = true;
        root.query = "";
        root.open();
    }

    onActivated: (index, item) => {
        // On torrents, a new category is set only once the server has created it.
        if (item.isCreate === true && !root.choiceMode) {
            App.createAndApplyCategory(root.hashes, item.key);
            root.close();
            return;
        }
        if (item.isCreate === true)
            App.createCategory(item.key);
        if (root.choiceMode) {
            // close() first: it gives the keyboard back to the caller before the caller
            // hears the answer, so a sheet underneath keeps its own focus order.
            root.close();
            root.chosen(item.key);
            return;
        }
        App.applyCategory(root.hashes, item.key);
        root.close();
    }
    onRejected: root.close()
}
