pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The hero's ⋯ menu (docs/08 §3): global, occasional actions that have no room in the hero
// of their own, each row teaching its key where it has one. Opened by the ⋯ button or
// `ctrl+.`; MainView runs `activated`. Rows for features the server has but omaseed does not
// implement yet (create torrent, search, export, RSS) are added when those features ship.
Menu {
    id: root

    /// The rail row reads "show rail" while it is hidden.
    property bool railHidden: false

    menuWidth: Theme.space(280)
    items: [
        {
            "text": qsTr("speed limits…"),
            "glyph": Glyphs.limits,
            "action": "globalLimits"
        },
        {
            "text": qsTr("alt-speed limits…"),
            "glyph": Glyphs.altSpeed,
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
            "text": root.railHidden ? qsTr("show rail") : qsTr("hide rail"),
            "glyph": Glyphs.sidebar,
            "key": "ctrl+b",
            "action": "toggleRail"
        }
    ]
}
