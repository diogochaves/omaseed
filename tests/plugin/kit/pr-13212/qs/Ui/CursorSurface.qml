import QtQuick

// Stand-in for CursorSurface with omacom/omarchy PR #13212 (at 30ed618f3): the properties the
// widget sets, and the PR's accessibility lines as it writes them. A row it has no name for
// stays out of the tree, and it never presses a row: the row's owner does.
Rectangle {
    id: root

    property bool hasCursor: false
    property bool current: false
    property bool outline: false
    property bool bordered: false
    property color foreground: "white"
    property color accent: "steelblue"

    property string accessibleName: ""

    Accessible.role: Accessible.ListItem
    Accessible.name: accessibleName
    Accessible.selectable: true
    Accessible.selected: current
    Accessible.ignored: accessibleName === ""

    color: "transparent"
}
