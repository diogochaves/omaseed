import QtQuick

// Stand-in for the installed shell's (Omarchy 4.0.4) ToggleSwitch: the properties the widget
// sets, and no accessibility, as there.
Item {
    property bool checked: false
    property bool busy: false
    property bool interactive: true
    property bool hasCursor: false
    property color foreground: "white"
    property color accent: "steelblue"

    signal toggled
    signal hovered(bool isHovered)

    implicitWidth: 40
    implicitHeight: 20
}
