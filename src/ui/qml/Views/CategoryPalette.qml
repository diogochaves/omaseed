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
    title: root.hashes.length === 1 ? qsTr("category for 1 torrent") : qsTr(
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
    function openFor(newHashes: var): void {
        root.hashes = newHashes;
        root.query = "";
        root.open();
    }

    onActivated: (index, item) => {
        if (item.isCreate === true)
            App.createCategory(item.key);
        App.applyCategory(root.hashes, item.key);
        root.close();
    }
    onRejected: root.close()
}
