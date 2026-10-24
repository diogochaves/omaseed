pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// `:` and `ctrl+shift+p` (docs/30, DECISIONS C1): every command that makes sense now, found
// by name and run as its key would, each row teaching that key. The rows are
// `Keymap.commandRows(context)`; MainView takes the context as it opens the palette (the
// palette then holds the keyboard, so the focus can no longer say where it came from).
//
// With nothing typed the rows come in commandRows' order (P1). Typing filters by title and
// area word with `Fuzzy.match`, sorted by `Fuzzy.score`, then rows that can run before rows
// that can't, then recent ones (P2). No match leaves one row that hands the text to the
// jump palette (D8, P4) and, on a server with search, a second that searches its plugins for
// it (DECISIONS C26 SR14). `enter` closes the palette, which gives the focus back, then asks
// MainView to run the row (`runRequested`), to jump (`jumpRequested`) or to search
// (`searchRequested`).
Palette {
    id: root

    /// `Keymap.commandRows()`'s plain context.
    property var context: ({})

    /// `enter` on a command: MainView runs `action` through `runAction`.
    signal runRequested(string action)
    /// `enter` on P4's row: the jump palette, with `text` typed.
    signal jumpRequested(string text)
    /// `enter` on SR14's row: the search face, asking for `text`.
    signal searchRequested(string text)

    readonly property var allRows: Keymap.commandRows(root.context)
    readonly property string typed: root.query.trim()
    /// The rows that match what is typed, best first; every row with nothing typed.
    readonly property var matches: root.typed.length === 0 ? root.allRows : root._ranked(root.allRows,
                                                                                             root.typed)
    readonly property bool noMatch: root.typed.length > 0 && root.matches.length === 0

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

    counter: root.typed.length === 0 ? qsTr("%n commands", "", root.allRows.length) : qsTr("%1 of %2").arg(
                                           root.matches.length).arg(root.allRows.length)
    emptyText: root.noMatch ? qsTr("No command matches “%1”.").arg(root.typed) : ""
    footerRight: qsTr("? lists every key")
    glyph: Glyphs.commands
    hints: {
        if (root.noMatch)
            return [
                        {
                            "key": "enter",
                            "label": qsTr("jump palette")
                        },
                        {
                            "key": "esc",
                            "label": qsTr("clear")
                        }];
        const hints = [
                    {
                        "key": "↑↓",
                        "label": qsTr("move")
                    }];
        // P3: with no row able to run there is nothing for `enter` to do.
        if (root.currentItem !== null)
            hints.push({
                           "key": "enter",
                           "label": qsTr("run")
                       });
        hints.push({
                       "key": "esc",
                       "label": root.query.length > 0 ? qsTr("clear") : qsTr("close")
                   });
        return hints;
    }
    items: root.noMatch ? [
                              {
                                  "key": "jump",
                                  "jump": true,
                                  "title": qsTr("find torrents matching “%1”").arg(root.typed),
                                  "meta": "ctrl+k",
                                  "detail": qsTr("library · opens the jump palette with your text")
                              }].concat((root.context || {}).searchOffered === false ? [] : [
                                                                                          {
                                                                                              "key": "searchFor",
                                                                                              "searchFor": true,
                                                                                              "title": qsTr("search plugins for “%1”").arg(root.typed),
                                                                                              "meta": "ctrl+f",
                                                                                              "detail": qsTr("search · asks the plugins that are on")
                                                                                          }]) : root.matches
    title: root._title()

    // Every opening starts from an empty query, like the other palettes.
    onOpenedChanged: {
        if (root.opened)
            root.query = "";
    }
    onActivated: (index, item) => {
        const text = root.typed;
        root.close();
        if (item.jump === true)
            root.jumpRequested(text);
        else if (item.searchFor === true)
            root.searchRequested(text);
        else
            root.runRequested(item.action);
    }
    onRejected: root.close()
}
