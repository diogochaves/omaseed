pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `t` (spec v2 §4): a multi-select over every tag App knows about, `checked` when every
// selected torrent already carries it. `space` toggles a row locally; `enter` applies the
// diff against what the torrents started with (torrents/addTags + torrents/removeTags in one
// call each) and closes. A query nothing matches adds a `create "<query>"` row that creates
// the tag (torrents/createTags), selects it and applies immediately.
Palette {
    id: root

    /// The torrents `enter` applies the tag diff to.
    property var hashes: []

    footerRight: qsTr("new name creates it")
    glyph: Glyphs.tag
    hints: [
        {
            "key": "space",
            "label": qsTr("toggle")
        },
        {
            "key": "enter",
            "label": qsTr("apply")
        }
    ]
    items: root._rows
    multi: true
    title: root.hashes.length === 1 ? qsTr("tags for 1 torrent") : qsTr(
                                          "tags for %1 torrents").arg(root.hashes.length)

    /// tag name -> bool, the state as it was when the palette opened.
    property var _original: ({})
    /// tag name -> bool, the state the user has toggled to.
    property var _state: ({})
    /// Bumped by `_toggle()` so `_rows` (a plain read of `_state`) re-evaluates: QML cannot
    /// see through a mutation of a JS object held in a property.
    property int _rev: 0

    readonly property var _rows: {
        void root._rev; // dependency: re-run whenever a toggle bumps it
        // `App.tags.count` is a real NOTIFY-backed dependency, unlike the invokable
        // `tags()` call itself (see JumpPalette's `items` for why that matters here).
        void App.tags.count;
        const q = root.query;
        const rows = [];
        const names = App.tags.tags();
        for (const name of names) {
            if (!Fuzzy.match(name, q))
                continue;
            rows.push({
                "title": name,
                "meta": String(App.counts.tagCount(name)),
                "checked": root._state[name] === true,
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

    /// Whether every torrent in `hashes` already carries `tag`.
    function _everyHasTag(tag: string): bool {
        for (const hash of root.hashes) {
            if (App.tagsOf(hash).indexOf(tag) < 0)
                return false;
        }
        return root.hashes.length > 0;
    }

    function _toggle(tag: string): void {
        const next = Object.assign({}, root._state);
        next[tag] = !next[tag];
        root._state = next;
        root._rev += 1;
    }

    /// Opens the palette for `newHashes` (the seam `App.tagPickerRequested` drives).
    function openFor(newHashes: var): void {
        root.hashes = newHashes;
        const seed = {};
        for (const name of App.tags.tags())
            seed[name] = root._everyHasTag(name);
        root._original = seed;
        root._state = Object.assign({}, seed);
        root.query = "";
        root.open();
    }

    /// `enter`: the tags now checked that were not, and vice versa.
    function _applyDiff(): void {
        const added = [];
        const removed = [];
        for (const name of Object.keys(root._state)) {
            const now = root._state[name] === true;
            const was = root._original[name] === true;
            if (now && !was)
                added.push(name);
            else if (!now && was)
                removed.push(name);
        }
        if (added.length > 0 || removed.length > 0)
            App.applyTags(root.hashes, added, removed);
    }

    onActivated: (index, item) => {
        if (item.isCreate === true) {
            App.createTag(item.key);
            root._toggle(item.key);
        }
        root._applyDiff();
        root.close();
    }
    onRejected: root.close()
    onToggled: (index, item) => root._toggle(item.key)
}
