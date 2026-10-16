import QtQuick

// Stand-in for the shell's ButtonGroup: the properties the widget sets (no accessibility of
// its own on either kit).
Row {
    property var options: []
    property string value: ""
    property color foreground: "white"
    property color background: "black"
    property color accent: "steelblue"
    property string fontFamily: "sans-serif"
    property real fontSize: 14
    property bool focusable: true
    property int cursorIndex: -1

    signal changed(string value)
    signal hovered(int index, bool isHovered)
}
