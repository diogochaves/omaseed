import QtQuick

// Stand-in for ToggleSwitch with omacom/omarchy PR #13212 (at 30ed618f3): the properties the
// widget sets, and the PR's accessibility lines as it writes them. Hidden when the row around
// it owns the click (`interactive: false`).
Item {
    id: root

    property bool checked: false
    property bool busy: false
    property bool interactive: true
    property bool hasCursor: false
    property color foreground: "white"
    property color accent: "steelblue"

    property string accessibleName: ""

    signal toggled
    signal hovered(bool isHovered)

    Accessible.role: Accessible.CheckBox
    Accessible.name: accessibleName
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.ignored: !interactive
    Accessible.onToggleAction: if (root.enabled && !root.busy)
        root.toggled()
    Accessible.onPressAction: if (root.enabled && !root.busy)
        root.toggled()

    implicitWidth: 40
    implicitHeight: 20
}
