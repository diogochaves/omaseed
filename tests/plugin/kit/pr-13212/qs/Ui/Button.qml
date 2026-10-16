import QtQuick

// Stand-in for Button with omacom/omarchy PR #13212 (at 30ed618f3): the properties the widget
// sets, and the PR's accessibility lines as it writes them, its own press action included.
Rectangle {
    id: root

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

    property string accessibleName: text !== "" ? text : tooltipText

    signal clicked
    signal rightClicked
    signal hovered(bool isHovered)

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.description: tooltipText !== accessibleName ? tooltipText : ""
    Accessible.focusable: focusable
    Accessible.selectable: selected || active
    Accessible.selected: selected || active
    Accessible.onPressAction: if (root.enabled)
        root.clicked()

    implicitWidth: 60
    implicitHeight: 24
    color: background
}
