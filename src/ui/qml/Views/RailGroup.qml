import QtQuick
import Omaseed.Kit

// One tracked-label group of the rail (spec v2 §1 `.rail .grp`/`.rail .grp .lbl`): the
// group's heading, `Theme.space(16)` from the left with `Theme.space(4)` below it, then
// whatever rows the caller declares as ordinary children — they land in this `Column`'s
// `data` the same way any other child of a positioner does, right after the heading.
//
// docs/08 §5: the heading folds the group. A chevron says which way it is (▾ open, › folded),
// a click on the heading toggles it, and a folded group prints how many entries it hides.
// The rail owns the folded state; this only draws it and reports the click.
Column {
    id: root

    property string label: ""
    property bool collapsed: false
    /// The entries a folded group hides; -1 prints none.
    property int count: -1
    /// The rail's keyboard cursor is on this heading (only a folded group's heading is a stop).
    property bool hasCursor: false
    /// What a screen reader calls the heading when `label` carries more than the group's
    /// name (the servers' "servers · 3"); that more goes in `accessibleDetail` (#78).
    property string accessibleName: root.label
    /// Said before "folded"/"unfolded" in the heading's description; "" says nothing more.
    property string accessibleDetail: ""
    /// The folded rail's groups go without their heading (board 46's narrow `rail()`).
    property bool headed: true
    /// What the heading sits on while it paints nothing: the window, or the rail's servers
    /// block (`Theme.on.idle`, docs/DECISIONS.md C30 F1).
    property ThemeInk block: Theme.on.window

    readonly property alias header: header
    /// The heading's words on its ground, the hover fill under the pointer or the cursor (over
    /// the block, in one); the fill comes and goes at once, so do they.
    readonly property Ink ink: Ink {
        animated: false
        ground: Theme.groundIn(root.block, false, false, root.hasCursor || headerMouse.containsMouse,
                               false)
    }

    signal toggled

    objectName: root.label
    spacing: 0

    Rectangle {
        id: header

        // #77: the heading is a button that folds the group, named by it; whether it is
        // folded is the description (Qt 6.11 has no `Accessible.expanded`). Its press is the
        // click, and does nothing while disabled.
        Accessible.description: [root.accessibleDetail, root.collapsed ? qsTr("folded")
                                                                       : qsTr("unfolded")].filter(
                                    part => part.length > 0).join(" · ")
        Accessible.name: root.accessibleName
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (header.enabled)
                                      root.toggled()
        color: root.ink.ground === root.block ? "transparent" : root.ink.ground.fill
        implicitHeight: headerText.implicitHeight + Theme.space(4)
        visible: root.headed
        width: parent.width

        Label {
            id: chevron

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(4)
            anchors.top: parent.top
            dim: true
            ink: root.ink
            size: "caption"
            text: root.collapsed ? Glyphs.chevronRight : Glyphs.chevronDown
            visible: headerMouse.containsMouse || root.collapsed || root.hasCursor
        }

        Label {
            id: headerText

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.top: parent.top
            ink: root.ink
            tracked: true
            text: root.label
        }

        Label {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.top: parent.top
            dim: true
            ink: root.ink
            size: "caption"
            text: String(root.count)
            visible: root.collapsed && root.count >= 0
        }

        MouseArea {
            id: headerMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: root.toggled()
        }
    }
}
