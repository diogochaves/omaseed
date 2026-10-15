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
    /// #28 (docs/26 §4): applying `name` would move the files of `moved` from under torrents
    /// outside `hashes` that use the same data. The palette has closed without applying; the
    /// caller asks (SharedMoveConfirm) and applies.
    signal sharedMoveFound(var hashes, string name, bool create, var moved)

    /// #35: category changes waiting for the file lists the shared-data check needs (a
    /// torrent saved without a folder of its own: `App.sharedData`'s `pending`), in the order
    /// they were picked: `[{hashes, name, create, moved}]`. The palette has closed; each is
    /// applied, or asked about, once its lists are in.
    property var _held: []

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

    /// Sets the category as picked. On torrents, a new category is set only once the server
    /// has created it (`App.createAndApplyCategory`).
    function _apply(change: var): void {
        if (change.create)
            App.createAndApplyCategory(change.hashes, change.name);
        else
            App.applyCategory(change.hashes, change.name);
    }

    /// A change whose shared-data check has its answer: applied, or, when it would move files
    /// from under torrents outside it, handed on through `sharedMoveFound`.
    function _settle(change: var, shared: var): void {
        if ((shared.outside ?? []).length > 0)
            root.sharedMoveFound(change.hashes, change.name, change.create, change.moved);
        else
            root._apply(change);
    }

    onActivated: (index, item) => {
        if (root.choiceMode) {
            if (item.isCreate === true)
                App.createCategory(item.key);
            // close() first: it gives the keyboard back to the caller before the caller
            // hears the answer, so a sheet underneath keeps its own focus order.
            root.close();
            root.chosen(item.key);
            return;
        }
        const change = {
            "hashes": root.hashes,
            "name": item.key,
            "create": item.isCreate === true,
            "moved": App.movedByCategory(root.hashes, item.key)
        };
        root.close();
        if (change.moved.length === 0) {
            root._apply(change);
            return;
        }
        // #28 (docs/26 §4): a move that takes files from under another torrent asks first.
        // #35: a torrent saved without a folder of its own is found to share only once the
        // file lists are in, so the change waits for them rather than missing it.
        const shared = App.sharedData(change.moved, true);
        if (shared.pending === true)
            root._held = root._held.concat([change]);
        else
            root._settle(change, shared);
    }
    onRejected: root.close()

    Connections {
        function onServerChanged(): void {
            // The held hashes were the last server's.
            root._held = [];
        }

        function onSharedFilesLoaded(): void {
            // In the order they were picked: one still waiting holds back the ones after it.
            const still = [];
            for (const change of root._held) {
                const shared = still.length > 0 ? null : App.sharedData(change.moved);
                if (shared === null || shared.pending === true)
                    still.push(change);
                else
                    root._settle(change, shared);
            }
            root._held = still;
        }

        target: App
    }
}
