import QtQuick

// Stand-in for the installed shell's (Omarchy 4.0.4) CursorSurface: the properties the widget
// sets, and no accessibility, as there.
Rectangle {
    property bool hasCursor: false
    property bool current: false
    property bool outline: false
    property bool bordered: false
    property color foreground: "white"
    property color accent: "steelblue"
    color: "transparent"
}
