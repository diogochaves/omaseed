pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The everything palette (board 63, docs/DECISIONS.md C42): the command, jump and server
// palettes as one palette over three kinds of row. `:` and `ctrl+shift+p` open it on every
// kind, `ctrl+k` with the "torrents" filter on, `ctrl+s` with "servers" (Z5); MainView calls
// `openFor(kind)`. Settings will be a fourth kind, second in the order (Z2, #266).
//
// Each kind keeps the rows, ranking and `enter` its own palette had:
//  - commands (docs/30, C1): `Keymap.commandRows(context)`; MainView takes the context as it
//    opens the palette, which then holds the keyboard. Nothing typed lists them in
//    commandRows' order (P1); typing filters by title and area word with `Fuzzy.match`,
//    sorted by `Fuzzy.score`, then rows that can run, then recent ones (P2). `enter` closes
//    the palette, which gives the focus back, then asks MainView to run the row.
//  - servers (docs/33 S1, C12): `App.serverRows` in Settings' order, filtered by name or
//    address. `enter` switches (App.switchServer); `tab` asks for the server's menu, the one
//    the rail opens (Z8); `,` opens Settings › Servers on the cursor's row while the filter
//    is on servers (elsewhere a comma is typed text).
//  - torrents (spec v2 §4 Fig. 12): `App.jumpMatches()`, over name, hash and tracker host of
//    every torrent of the server on show. `enter` moves the table's cursor to the row,
//    clearing a filter that hides it; `tab` also asks for its actions menu.
//
// What is listed (Z1–Z3, Z6, Z11):
//  - a filter on (`filter`, drawn as the kit's chip): that kind's list, as its palette was;
//  - no filter, nothing typed: the commands (Z6);
//  - no filter, text typed: when several kinds match, a section per kind in `kinds`' order: a
//    heading with its count, up to `sectionRows` rows, then an "N more" row whose `enter`
//    sets that kind's filter and keeps the text. When one kind matches, its whole list with
//    no heading and no cap. When none does, a dim line and, where the server has search, the
//    row that searches its plugins (C26 SR14).
//
// The filter (Z4): a query that starts with `/` lists the kinds (and `/all` while a filter is
// on), narrowed by the letters after it; `enter` or `space` on one sets the filter and clears
// the text. `backspace` in an empty query removes the filter and `shift+tab` steps it.
//
// `openOnly(kind)` is the palette held to one kind with no chip and no way out of it: the
// add sheet's server chip, where commands and torrents have no place (docs/33 D14).
Palette {
    id: root

    /// The table whose cursor a torrent's `enter`/`tab` move; null where no torrent is listed.
    property TorrentTable table: null
    /// `Keymap.commandRows()`'s plain context.
    property var context: ({})
    /// The server kind's rows: `{key, title, meta, detail, address}`.
    property var serverRows: App.serverRows
    /// C42 Z4: the kind the palette is narrowed to, "" for every kind.
    property string filter: ""
    /// The one kind this opening is held to (`openOnly`), "" when it is free.
    property string only: ""

    /// `enter` on a command: MainView runs `action` through `runAction`.
    signal runRequested(string action)
    /// `enter` on SR14's row: the search face, asking for `text`.
    signal searchRequested(string text)
    /// `,` on a server: Settings › Servers on server `id`.
    signal manageRequested(string id)
    /// `enter` switched (or stayed) to server `id`.
    signal switched(string id)
    /// `tab` on a server (C42 Z8): its menu, at the palette's corner (`x`, `y` in the
    /// palette's own coordinates, which are its host's).
    signal serverMenuRequested(string id, real x, real y)

    /// C42 Z2: the sections' order.
    readonly property var kinds: ["commands", "servers", "torrents"]
    /// C42 Z3: the rows a section shows before its "N more".
    readonly property int sectionRows: 4
    readonly property string scope: root.only.length > 0 ? root.only : root.filter
    readonly property string typed: root.query.trim()
    /// C42 Z4: the query is naming a kind.
    readonly property bool slash: root.only.length === 0 && root.query.startsWith("/")
    readonly property var allCommands: Keymap.commandRows(root.context)
    /// `{mode, items, total, none}`: `mode` is "kinds", a kind's name or "all" (sections, or
    /// the one kind that matched); `none` when nothing matched.
    readonly property var view: root._build()
    /// The kind of the cursor's row; "" on none, "more" on an "N more" row.
    readonly property string cursorKind: {
        const item = root.currentItem;
        return item === null ? "" : item.more === true ? "more" : (item.kind ?? "");
    }

    function kindName(kind: string): string {
        switch (kind) {
        case "commands":
            return qsTr("commands");
        case "servers":
            return qsTr("servers");
        case "torrents":
            return qsTr("torrents");
        default:
            return qsTr("all");
        }
    }

    /// Opens on `kind` ("" for every kind) with nothing typed. On servers the cursor starts
    /// on the next one, the row after the one the app is on (docs/33 D2).
    function openFor(kind: string): void {
        root.only = "";
        root.query = "";
        root.open();
        root.setFilter(kind);
    }

    /// Opens held to `kind`: no chip, no other kind.
    function openOnly(kind: string): void {
        root.filter = "";
        root.only = kind;
        root.query = "";
        root.open();
        root._cursorToStart();
    }

    /// Sets the filter, keeping what is typed, and puts the cursor on the list's first row.
    function setFilter(kind: string): void {
        root.filter = root.kinds.indexOf(kind) >= 0 ? kind : "";
        root._cursorToStart();
    }

    function _cursorToStart(): void {
        root.cursorIndex = root.scope === "servers" && root.typed.length === 0 && root.items.length > 1 ? 1 : 0;
        root._settleCursor();
    }

    /// Moves `table`'s cursor to `hash`, clearing every filter first when the row is not
    /// currently visible (the same helper MainView's `showTorrent` uses).
    function jumpTo(hash: string): void {
        if (root.table !== null)
            root.table.revealHash(hash);
    }

    function _manage(): void {
        const item = root.currentItem;
        root.close();
        root.manageRequested(item ? item.key : "");
    }

    function _ranked(rows: var, text: string): var {
        const hits = [];
        for (let i = 0; i < rows.length; ++i) {
            // C16 L2: a row's old name (`also`) still finds it. Lower is better, -1 none.
            const byTitle = Fuzzy.score(rows[i].title + " " + rows[i].word, text);
            const byAlso = (rows[i].also ?? "").length > 0 ? Fuzzy.score(rows[i].also + " " + rows[i].word,
                                                                         text) : -1;
            const score = byTitle < 0 ? byAlso : byAlso < 0 ? byTitle : Math.min(byTitle, byAlso);
            if (score >= 0)
                hits.push({
                              "row": rows[i],
                              "score": score,
                              "index": i
                          });
        }
        // A recent row (0 = newest) before one that is not (-1).
        const recency = row => row.recent < 0 ? 1000 : row.recent;
        hits.sort((a, b) => a.score - b.score || Number(a.row.disabled) - Number(b.row.disabled)
                  || recency(a.row) - recency(b.row) || a.index - b.index);
        return hits.map(hit => hit.row);
    }

    function _commands(text: string): var {
        return text.length === 0 ? root.allCommands : root._ranked(root.allCommands, text);
    }

    function _servers(text: string): var {
        const rows = root.serverRows || [];
        return text.length === 0 ? rows : rows.filter(row => Fuzzy.match(row.title + " " + row.address, text));
    }

    /// `rows` as rows of `kind`. `spoken` adds the kind to what a screen reader hears, for
    /// the list that mixes kinds.
    function _as(kind: string, rows: var, spoken: bool): var {
        const out = [];
        for (let i = 0; i < rows.length; ++i) {
            const row = {};
            for (const field in rows[i])
                row[field] = rows[i][field];
            row.kind = kind;
            if (spoken)
                row.spokenTitle = kind === "commands" ? qsTr("%1, command").arg(row.title)
                                                      : kind === "servers" ? qsTr("%1, server").arg(row.title)
                                                                           : qsTr("%1, torrent").arg(row.title);
            out.push(row);
        }
        return out;
    }

    /// C26 SR14, C42 Z11: the row that hands the text to search, where the server has it.
    function _searchRow(text: string): var {
        if ((root.context || {}).searchOffered === false)
            return [];
        return [
                    {
                        "key": "searchFor",
                        "kind": "commands",
                        "searchFor": true,
                        "title": qsTr("search the plugins for “%1”").arg(text),
                        "meta": "ctrl+f",
                        "detail": qsTr("search")
                    }];
    }

    /// C42 Z4: the kinds a `/` query names, and `/all` while a filter is on.
    function _kindRows(): var {
        const word = root.query.substring(1).trim().toLowerCase();
        const details = {
            "commands": qsTr("%n that make sense now", "", root.allCommands.length),
            "servers": qsTr("%n saved", "", (root.serverRows || []).length),
            "torrents": App.serverName.length > 0 ? qsTr("%1 on %2").arg(App.torrents.count).arg(App.serverName)
                                                  : String(App.torrents.count),
            "all": qsTr("every kind")
        };
        const names = root.filter.length > 0 ? root.kinds.concat(["all"]) : root.kinds;
        const rows = [];
        for (const kind of names) {
            const name = root.kindName(kind);
            if (name.toLowerCase().startsWith(word))
                rows.push({
                              "key": kind,
                              "kind": kind,
                              "kindRow": true,
                              "title": name,
                              "meta": "/" + name,
                              "detail": details[kind]
                          });
        }
        return rows;
    }

    function _build(): var {
        // `App.torrents.count` is a real NOTIFY-backed dependency, unlike the invokable
        // `jumpMatches()` call itself: without it this binding would keep whatever it saw
        // the moment the palette was created if `query` happened not to change afterwards
        // (every qmltestrunner file's tree is built before any test function runs).
        void App.torrents.count;
        const text = root.typed;
        const one = (mode, rows, total) => ({
                                                "mode": mode,
                                                "items": rows,
                                                "total": total,
                                                "none": total === 0
                                            });
        if (root.slash) {
            const rows = root._kindRows();
            return one("kinds", rows, rows.length);
        }
        // C42 Z6: with no filter and nothing typed the list is the commands'.
        if (root.scope === "commands" || (root.scope.length === 0 && text.length === 0)) {
            const rows = root._commands(text);
            return one("commands", rows.length > 0 ? root._as("commands", rows, false) : root._searchRow(text),
                       rows.length);
        }
        if (root.scope === "servers") {
            const rows = root._servers(text);
            return one("servers", root._as("servers", rows, false), rows.length);
        }
        // The jump palette handed App the query as typed, untrimmed.
        if (root.scope === "torrents")
            return one("torrents", root._as("torrents", App.jumpMatches(root.query, 50), false),
                       App.jumpMatchCount(root.query));
        const found = {
            "commands": root._commands(text),
            "servers": root._servers(text)
        };
        const counts = {
            "commands": found.commands.length,
            "servers": found.servers.length,
            "torrents": App.jumpMatchCount(text)
        };
        const matched = root.kinds.filter(kind => counts[kind] > 0);
        const total = counts.commands + counts.servers + counts.torrents;
        const rowsOf = (kind, limit) => root._as(kind, kind === "torrents" ? App.jumpMatches(text, limit)
                                                                           : found[kind].slice(0, limit), true);
        if (matched.length === 0)
            return one("all", root._searchRow(text), 0);
        // C42 Z3: one kind matched, so nothing is kept apart: no heading, no cap.
        if (matched.length === 1)
            return one("all", rowsOf(matched[0], 50), total);
        const more = {
            "commands": n => qsTr("%n more commands", "", n),
            "servers": n => qsTr("%n more servers", "", n),
            "torrents": n => qsTr("%n more torrents", "", n)
        };
        let rows = [];
        for (const kind of matched) {
            rows.push({
                          "heading": true,
                          "title": root.kindName(kind),
                          "meta": String(counts[kind])
                      });
            rows = rows.concat(rowsOf(kind, root.sectionRows));
            if (counts[kind] > root.sectionRows)
                rows.push({
                              "key": "more:" + kind,
                              "kind": kind,
                              "more": true,
                              "title": more[kind](counts[kind] - root.sectionRows),
                              "meta": qsTr("enter shows them")
                          });
        }
        return one("all", rows, total);
    }

    /// The query row's words while nothing is typed: what the commands act on (P1, P5, P6).
    function _title(): string {
        const ctx = root.context || {};
        if (ctx.serverLost === true)
            return qsTr("commands · the server is lost");
        // docs/32 §6: the log face's commands act on its lines.
        if (ctx.area === "log")
            return (ctx.marked || 0) === 1 ? qsTr("commands for 1 marked line")
                                           : (ctx.marked || 0) > 1 ? qsTr("commands for %1 marked lines").arg(ctx.marked)
                                                                   : qsTr("commands for the log");
        if (ctx.area === "files" && (ctx.marked || 0) > 0)
            return qsTr("commands for %n marked files", "", ctx.marked);
        if (["trackers", "peers", "webseeds"].indexOf(ctx.area) >= 0 && (ctx.marked || 0) > 0)
            return qsTr("commands for %n marked", "", ctx.marked);
        if ((ctx.selected || 0) > 0)
            return qsTr("commands for %n selected", "", ctx.selected);
        if ((ctx.cursor || "").length > 0)
            return qsTr("commands for %1").arg(ctx.cursor);
        return qsTr("commands");
    }

    chip: root.only.length === 0 && root.filter.length > 0 ? root.kindName(root.filter) : ""
    counter: {
        switch (root.view.mode) {
        case "kinds":
            return qsTr("%n kinds", "", root.view.total);
        case "commands":
            return root.typed.length === 0 ? qsTr("%n commands", "", root.allCommands.length) : qsTr("%1 of %2").arg(
                                                 root.view.total).arg(root.allCommands.length);
        case "servers":
            return qsTr("%n servers", "", (root.serverRows || []).length);
        case "torrents":
            return qsTr("%1 of %2").arg(root.view.total).arg(App.torrents.count);
        default:
            return qsTr("%n matches", "", root.view.total);
        }
    }
    emptyText: {
        if (!root.view.none)
            return "";
        switch (root.view.mode) {
        case "kinds":
            return qsTr("No kind matches “%1”.").arg(root.typed);
        case "commands":
            return qsTr("No command matches “%1”.").arg(root.typed);
        case "servers":
            return qsTr("No server matches “%1”.").arg(root.typed);
        case "torrents": // Fig. 12 has no line: the counter says 0
            return "";
        default:
            return qsTr("Nothing here matches “%1”.").arg(root.typed);
        }
    }
    filterable: root.only.length === 0
    footerRight: {
        if (root.slash || root.scope === "servers")
            return "";
        if (root.scope === "torrents")
            return qsTr("also matches hash and tracker");
        // C42 Z6: what typing adds to the commands.
        if (root.scope.length === 0 && root.typed.length === 0)
            return qsTr("type to find torrents, servers");
        return qsTr("? lists every key");
    }
    glyph: root.scope === "torrents" ? Glyphs.jump : root.scope === "servers" ? Glyphs.server : Glyphs.commands
    // A torrent's or a server's `tab`, and a right-click on its row.
    hasSecondary: root.only.length === 0
    hints: {
        const esc = {
            "key": "esc",
            "label": root.query.length > 0 ? qsTr("clear") : qsTr("close")
        };
        if (root.view.mode === "kinds")
            return [
                        {
                            "key": "↑↓",
                            "label": qsTr("move")
                        },
                        {
                            "key": "enter",
                            "label": qsTr("filter")
                        },
                        {
                            "key": "space",
                            "label": qsTr("filter and go on typing")
                        },
                        esc];
        // C42 Z11: nothing found, and at most the row that searches.
        if (root.view.none && (root.view.mode === "commands" || root.view.mode === "all"))
            return (root.view.items.length > 0 ? [
                                                     {
                                                         "key": "enter",
                                                         "label": qsTr("search")
                                                     }] : []).concat([esc]);
        const hints = [
                    {
                        "key": "↑↓",
                        "label": qsTr("move")
                    }];
        // C42 Z1: the `enter` word follows the cursor's kind. With no row able to run (C1
        // P3) there is nothing for `enter` to do.
        const words = {
            "commands": qsTr("run"),
            "servers": qsTr("switch"),
            "torrents": qsTr("jump"),
            "more": qsTr("show")
        };
        if (words[root.cursorKind] !== undefined)
            hints.push({
                           "key": "enter",
                           "label": words[root.cursorKind]
                       });
        // C42 Z8: the row's second action.
        if (root.cursorKind === "torrents")
            hints.push({
                           "key": "tab",
                           "label": qsTr("actions")
                       });
        else if (root.cursorKind === "servers" && root.hasSecondary)
            hints.push({
                           "key": "tab",
                           "label": qsTr("menu")
                       });
        if (root.scope === "servers")
            hints.push({
                           "key": ",",
                           "label": qsTr("manage servers"),
                           "clickable": true
                       });
        if (root.filterable)
            hints.push({
                           "key": "shift+tab",
                           "label": qsTr("kind")
                       });
        hints.push(esc);
        return hints;
    }
    items: root.view.items
    keyHandler: event => {
        if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) !== 0)
            return;
        // docs/33 S1: `,` in the server list.
        if (event.key === Qt.Key_Comma && root.scope === "servers" && !root.slash) {
            root._manage();
            event.accepted = true;
        }
        // C42 Z4: `space` after `/kind` sets the filter, and what is typed next is the query.
        if (event.key === Qt.Key_Space && root.slash && root.cursorKind.length > 0 && root.query.indexOf(" ") < 0) {
            root.activate();
            event.accepted = true;
        }
    }
    // #79: a torrent's or a server's meta is words (its state, "4 torrents"); a command's is
    // its key. In sections the kit's one switch stays off, for the commands.
    metaSpoken: root.scope === "torrents" || root.scope === "servers"
    objectName: "everythingPalette"
    // Board 63 Z3 draws it 560 wide; held to the servers it is docs/33's 520.
    paletteWidth: root.only.length > 0 ? Theme.space(520) : Theme.space(560)
    title: root.scope === "torrents" ? qsTr("jump to a torrent") : root.scope === "servers" ? qsTr("switch server")
                                                                                         : root._title()

    // Every opening starts from an empty query: the last one's text would otherwise stay in
    // the row and the next name be typed after it.
    onOpenedChanged: {
        if (root.opened)
            root.query = "";
    }
    onActivated: (index, item) => {
        // C42 Z4: a kind's row sets the filter and clears the `/…` that named it.
        if (item.kindRow === true) {
            root.query = "";
            root.setFilter(item.kind);
            return;
        }
        // C42 Z3: "N more" narrows to its kind, the typed text kept.
        if (item.more === true) {
            root.setFilter(item.kind);
            return;
        }
        const text = root.typed;
        switch (item.kind) {
        case "torrents":
            root.jumpTo(item.key);
            root.close();
            break;
        case "servers":
            root.close();
            App.switchServer(item.key);
            root.switched(item.key);
            break;
        default:
            root.close();
            if (item.searchFor === true)
                root.searchRequested(text);
            else
                root.runRequested(item.action);
        }
    }
    onFilterRemoveRequested: root.setFilter("")
    // C42 Z4: all, then each kind in the sections' order, then all again.
    onFilterStepRequested: root.setFilter(root.kinds[root.kinds.indexOf(root.filter) + 1] ?? "")
    onHintClicked: (index, hint) => {
        if (hint.key === ",")
            root._manage();
    }
    onRejected: root.close()
    // C42 Z8: nothing on a command.
    onSecondary: (index, item) => {
        if (item.more === true || item.kindRow === true)
            return;
        if (item.kind === "torrents") {
            const hash = item.key;
            root.jumpTo(hash);
            root.close();
            // MainView answers it with Fig. 11 on that row.
            App.showActionsRequested(hash);
        } else if (item.kind === "servers") {
            const corner = root.panel.mapToItem(root, 0, 0);
            root.close();
            root.serverMenuRequested(item.key, corner.x, corner.y);
        }
    }
}
