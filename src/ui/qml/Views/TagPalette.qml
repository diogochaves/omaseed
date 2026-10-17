pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `t` (spec v2 §4): a multi-select over every tag App knows about, `checked` when every
// selected torrent already carries it. `space` toggles a row locally; `enter` applies the
// diff against what the torrents started with (torrents/addTags + torrents/removeTags in one
// call each) and closes. A query nothing matches adds a `create "<query>"` row that creates
// the tag (torrents/createTags), selects it and applies immediately.
//
// The mouse: a click ticks a row, and a click on the create row creates the tag and ticks
// it. The footer's `enter apply` is a button too, and runs what `enter` runs, so a mouse user
// can save the ticks (the selection bar's `tags` opens this palette).
Palette {
    id: root

    /// The torrents `enter` applies the tag diff to.
    property var hashes: []
    /// `openForChoice()`: the palette answers with `chosen(tags)` instead of applying a diff
    /// to torrents — the add sheet's `…` chip, whose torrent does not exist yet.
    property bool choiceMode: false

    /// The tags checked when `enter` was pressed, as a list of names.
    signal chosen(var tags)

    footerRight: root._commaInQuery ? qsTr("\",\" cannot be part of a tag") : qsTr(
                                          "new name creates it")
    glyph: Glyphs.tag
    hints: [
        {
            "key": "space",
            "label": qsTr("toggle")
        },
        {
            "key": "enter",
            "label": qsTr("apply"),
            "clickable": true
        }
    ]
    items: root._rows
    // #79: a row's meta is its count.
    metaSpoken: true
    multi: true
    title: root.choiceMode ? qsTr("tags for the new torrent") : root.hashes.length
                             === 1 ? qsTr("tags for 1 torrent") : qsTr(
                                         "tags for %1 torrents").arg(root.hashes.length)

    /// tag name -> bool, the state as it was when the palette opened.
    property var _original: ({})
    /// tag name -> bool, the state the user has toggled to.
    property var _state: ({})
    /// Tags this palette has asked the server to create since it opened.
    property var _created: []
    /// Bumped by `_toggle()` so `_rows` (a plain read of `_state`) re-evaluates: QML cannot
    /// see through a mutation of a JS object held in a property.
    property int _rev: 0
    /// The Web API sends tags as one comma-separated list, so qBittorrent would split a
    /// name with a comma into two tags (review RUN-3): such a query offers no create row.
    readonly property bool _commaInQuery: root.query.indexOf(",") >= 0

    readonly property var _rows: {
        void root._rev; // dependency: re-run whenever a toggle bumps it
        // `App.tags.count` is a real NOTIFY-backed dependency, unlike the invokable
        // `tags()` call itself (see JumpPalette's `items` for why that matters here).
        void App.tags.count;
        const q = root.query;
        // qBittorrent trims a new tag's name; the create row names what it will get.
        const created = q.trim();
        const rows = [];
        const names = App.tags.tags();
        // In choice mode a tag the caller already carries may not be on the server yet (it
        // was created in this very palette a moment ago): it still needs a row, or it could
        // never be unchecked again.
        if (root.choiceMode) {
            for (const chosen of Object.keys(root._state)) {
                if (root._state[chosen] === true && names.indexOf(chosen) < 0)
                    names.push(chosen);
            }
        }
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
        if (created.length > 0 && names.indexOf(created) < 0 && !root._commaInQuery)
            rows.push({
                "title": qsTr("create \"%1\"").arg(created),
                "key": created,
                // Ticked by a click while the server has not answered with the tag yet.
                "checked": root._state[created] === true,
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
        root._setTicked(tag, !root._state[tag]);
    }

    function _setTicked(tag: string, on: bool): void {
        const next = Object.assign({}, root._state);
        next[tag] = on;
        root._state = next;
        root._rev += 1;
    }

    /// The create row: ask the server for the tag (once) and tick it. Ticking, not toggling:
    /// a click on the row and then `enter` on it must leave it ticked.
    function _createAndTick(tag: string): void {
        if (root._created.indexOf(tag) < 0) {
            root._created = root._created.concat([tag]);
            App.createTag(tag);
        }
        root._setTicked(tag, true);
    }

    /// `enter` on `item` (the cursor's row, or null), or a click on `enter apply`: apply the
    /// ticks (or hand them to the caller in choice mode) and close. On the create row, the tag
    /// is created and ticked first.
    function apply(item: var): void {
        if (item && item.isCreate === true)
            root._createAndTick(item.key);
        if (root.choiceMode) {
            const picked = root.checkedTags();
            // close() first: the keyboard goes back to the caller before it hears the answer.
            root.close();
            root.chosen(picked);
            return;
        }
        root._applyDiff();
        root.close();
    }

    /// Opens the palette for `newHashes` (the seam `App.tagPickerRequested` drives).
    function openFor(newHashes: var): void {
        root.hashes = newHashes;
        root.choiceMode = false;
        const seed = {};
        for (const name of App.tags.tags())
            seed[name] = root._everyHasTag(name);
        root._original = seed;
        root._state = Object.assign({}, seed);
        root._created = [];
        root._rev += 1;
        root.query = "";
        root.open();
    }

    /// Opens the palette to answer one caller (the add sheet) with `chosen(tags)`, starting
    /// from the tags it already carries.
    function openForChoice(current: var): void {
        root.hashes = [];
        root.choiceMode = true;
        const seed = {};
        for (const name of App.tags.tags())
            seed[name] = false;
        for (const name of current)
            seed[name] = true;
        root._original = seed;
        root._state = Object.assign({}, seed);
        root._created = [];
        root._rev += 1;
        root.query = "";
        root.open();
    }

    /// The names checked right now, in the palette's own row order.
    function checkedTags(): var {
        const out = [];
        for (const name of Object.keys(root._state)) {
            if (root._state[name] === true)
                out.push(name);
        }
        return out;
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

    onActivated: (index, item) => root.apply(item)
    onHintClicked: (index, hint) => {
        if (hint.key === "enter")
            root.apply(root.currentItem);
    }
    onRejected: root.close()
    // `space` or a click. On the create row the first one creates the tag and ticks it; the
    // tag exists from then on, so another only unticks it.
    onToggled: (index, item) => {
        if (item.isCreate === true && root._state[item.key] !== true)
            root._createAndTick(item.key);
        else
            root._toggle(item.key);
    }
}
