pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The display tool's menu (board 46 `.tool.disp`, DECISIONS C24 H3–H4): rows, table, shelves,
// the scope's own checked. `activated` carries "rows", "table" or "overview"; the window sets
// it for the scope shown (App.setDisplay). No keys (#168 assumed 8). Its form is the kit Menu's.
Menu {
    id: root

    /// The display the scope shows now.
    property string current: ""

    dimmed: false
    items: [
        {
            "text": qsTr("rows"),
            "glyph": Glyphs.displayRows,
            "action": "rows",
            "note": qsTr("by question"),
            "checked": root.current === "rows"
        },
        {
            "text": qsTr("table"),
            "glyph": Glyphs.displayTable,
            "action": "table",
            "note": qsTr("one sorted list"),
            "checked": root.current === "table"
        },
        {
            "text": qsTr("shelves"),
            "glyph": Glyphs.displayShelves,
            "action": "overview",
            "note": qsTr("by category"),
            "checked": root.current === "overview"
        }
    ]
    menuWidth: Theme.displayMenuWidth
}
