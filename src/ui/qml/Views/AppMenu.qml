pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The omaseed menu (board 46 `menuHtml("app")`, DECISIONS C24 H9): the rail's mark and
// `ctrl+.` open it under the mark. C13's rows, in C13's order, from the one list the tray and
// the widget's ⋯ read (`App.appMenuRows`, integration/BarMenu: the servers with two or more,
// alternative limits, stop all, start all, add, settings, quit), so the window is C13's third
// place; then, before quit, a block for this window's own rows (seed console, log, commands,
// keys and help, hide window), each teaching its key. It grows out of the lit mark in one
// outline (board 51 D14 b, DECISIONS C32): no head of its own, since the mark says omaseed;
// its first line the version and the server, as the status line's lane says it.
//
// It runs nothing itself: `activated(id)` carries a C13 id ("alt-speed", "server:<id>",
// "quit"…) or a window row's action (the Keymap's: "toggleConsole", "toggleLog", "commands",
// "help", "hide"), and MainView answers (`appMenuAction`). Its form is the kit Menu's and
// `Theme.appMenuWidth` (frame plan, "form in tokens").
Menu {
    id: root

    /// `App.appMenuRows`: `[{id, label, kind, checked, enabled, detail, urgent, state}]`.
    property var sharedRows: []
    /// The head's words after "omaseed": the version, and the status line's server line
    /// ("public · qBittorrent 5.3.0").
    property string version: ""
    property string serverLine: ""
    /// docs/31: the alternative set's kind, "burst", "turtle" or "alt"; the row's glyph.
    property string altKind: "alt"

    /// The window's own rows, in board 46's order; their actions are the Keymap's, and each
    /// key is that action's (tst_Keymap checks).
    readonly property var windowRows: [
        {
            "text": qsTr("seed console"),
            "glyph": Glyphs.viewSeed,
            "key": "`",
            "action": "toggleConsole"
        },
        {
            "text": qsTr("log"),
            "glyph": Glyphs.viewLog,
            "key": "~",
            "action": "toggleLog"
        },
        {
            "text": qsTr("commands"),
            "glyph": Glyphs.commands,
            "key": ":",
            "action": "commands"
        },
        {
            "text": qsTr("keys and help"),
            "glyph": Glyphs.help,
            "key": "?",
            "action": "help"
        },
        {
            "text": qsTr("hide window"),
            "glyph": Glyphs.hideWindow,
            "key": "q",
            "action": "hide"
        }
    ]
    readonly property var windowActions: root.windowRows.map(row => row.action)

    /// A C13 row's key in the window, and the Keymap action it shares it with (tst_Keymap).
    readonly property var sharedKeys: ({
                                           "alt-speed": "A",
                                           "add": "a",
                                           "settings": ",",
                                           "quit": "ctrl+q"
                                       })
    readonly property var sharedKeymapActions: ({
                                                    "alt-speed": "altSpeed",
                                                    "add": "add",
                                                    "settings": "settings",
                                                    "quit": "quit"
                                                })

    /// A server row's dot: C12's colours (docs/35 N3: yellow while omaseed retries, red when
    /// the server needs you), the current one green while it answers.
    function serverColor(row: var): color {
        switch (row.state) {
        case "ok":
            return row.checked ? Theme.green : Theme.dimForeground;
        case "first-sync":
        case "down":
            return Theme.yellow;
        case "refused":
            return Theme.red;
        default:
            return Theme.dimForeground;
        }
    }

    function _glyph(id: string): string {
        switch (id) {
        case "alt-speed":
            return root.altKind === "burst" ? Glyphs.burst : Glyphs.altSpeed;
        case "stop-all":
            return Glyphs.stop;
        case "start-all":
            return Glyphs.start;
        case "add":
            return Glyphs.add;
        case "settings":
            return Glyphs.settings;
        case "quit":
            return Glyphs.quit;
        default:
            return "";
        }
    }

    /// A C13 row as the kit Menu draws it.
    function _item(row: var): var {
        if (row.kind === "separator")
            return {
                "separator": true
            };
        if (row.kind === "caption")
            return {
                "caption": true,
                "text": row.label
            };
        if (row.kind === "radio")
            return {
                "text": row.label,
                "glyph": Glyphs.dot,
                "glyphColor": root.serverColor(row),
                "note": row.detail,
                "noteColor": row.state === "refused" ? Theme.red : row.urgent ? Theme.yellow :
                                                                                Theme.dimForeground,
                "checked": row.checked,
                "disabled": !row.enabled,
                "action": row.id
            };
        const item = {
            // Board 46: the window's last row says whose quit it is, beside "hide window".
            "text": row.id === "quit" ? qsTr("quit omaseed") : row.label,
            "glyph": root._glyph(row.id),
            "key": root.sharedKeys[row.id],
            "disabled": !row.enabled,
            "action": row.id
        };
        if (row.kind === "check")
            item.checked = row.checked;
        return item;
    }

    headingOneLine: true
    headingTitle: root.version
    heading: root.serverLine
    menuWidth: Theme.appMenuWidth
    items: {
        const out = [];
        for (const row of root.sharedRows) {
            // The window's block goes before quit's rule (board 46).
            if (row.id === "separator:quit") {
                out.push({
                             "separator": true
                         });
                out.push({
                             "caption": true,
                             "text": qsTr("this window only")
                         });
                for (const windowRow of root.windowRows)
                    out.push(windowRow);
            }
            out.push(root._item(row));
        }
        return out;
    }
}
