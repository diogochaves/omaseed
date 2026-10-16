import QtQuick

// Stand-in for the installed shell's (Omarchy 4.0.4) Button: the properties the widget sets,
// and no accessibility, as there.
Rectangle {
    property string text: ""
    property string iconText: ""
    property string tooltipText: ""
    property bool selected: false
    property bool active: false
    property bool hasCursor: false
    property bool focusable: false
    property bool bordered: false
    property color foreground: "white"
    property color background: "transparent"
    property color accent: "steelblue"
    property string fontFamily: "sans-serif"
    property real fontSize: 14
    property real horizontalPadding: 8
    property real verticalPadding: 4

    signal clicked
    signal rightClicked
    signal hovered(bool isHovered)

    implicitWidth: 60
    implicitHeight: 24
    color: background
}
