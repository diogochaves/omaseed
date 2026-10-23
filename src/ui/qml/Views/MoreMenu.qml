pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The hero's ⋯ menu (docs/08 §3): global, occasional actions that have no room in the hero
// of their own, each row teaching its key where it has one. Opened by the ⋯ button or
// `ctrl+.`; MainView runs `activated`. Rows for features the server has but omaseed does not
// implement yet (create torrent, search, export, RSS) are added when those features ship.
Menu {
    id: root

    /// The rail row reads "open rail" while it is folded (#167 assumed 4).
    property bool railHidden: false
    /// docs/10 §2: the hero is too narrow for its gear and `?`, so they lead the menu.
    property bool appRows: false
    /// docs/31: the alternative set's kind, "burst", "turtle" or "alt"; its row follows it.
    property string altKind: "alt"

    readonly property var _appRows: [
        {
            "text": qsTr("settings"),
            "glyph": Glyphs.settings,
            "key": ",",
            "action": "settings"
        },
        {
            "text": qsTr("help"),
            "glyph": Glyphs.help,
            "key": "?",
            "action": "help"
        },
        {
            "separator": true
        }
    ]

    /// docs/30 D10: the command palette, first, for the mouse.
    readonly property var _commandRows: [
        {
            "text": qsTr("commands…"),
            "glyph": Glyphs.commands,
            "key": ":",
            "action": "commands"
        },
        {
            "separator": true
        }
    ]

    menuWidth: Theme.space(280)
    items: root._commandRows.concat(root.appRows ? root._appRows : [], root._rows)

    readonly property var _rows: [
        {
            "text": qsTr("speed limits…"),
            "glyph": Glyphs.limits,
            "action": "globalLimits"
        },
        {
            "text": root.altKind === "burst" ? qsTr("burst limits…") : root.altKind === "turtle"
                                                                         ? qsTr("turtle limits…")
                                                                         : qsTr("alt-speed limits…"),
            "glyph": root.altKind === "burst" ? Glyphs.burst : Glyphs.altSpeed,
            "action": "altLimits"
        },
        {
            "separator": true
        },
        {
            "text": qsTr("columns…"),
            "glyph": Glyphs.columns,
            "key": "|",
            "action": "columns"
        },
        {
            "text": qsTr("select all visible"),
            "glyph": Glyphs.selectAll,
            "key": "ctrl+a",
            "action": "selectAll"
        },
        {
            "text": root.railHidden ? qsTr("open rail") : qsTr("fold rail"),
            "glyph": Glyphs.sidebar,
            "key": "ctrl+b",
            "action": "toggleRail"
        }
    ]
}
