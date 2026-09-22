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

    readonly property alias header: header

    signal toggled

    objectName: root.label
    spacing: 0

    Rectangle {
        id: header

        color: root.hasCursor || headerMouse.containsMouse ? Theme.fill(Theme.fillHover) :
                                                             "transparent"
        implicitHeight: headerText.implicitHeight + Theme.space(4)
        width: parent.width

        Label {
            id: chevron

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(4)
            anchors.top: parent.top
            dim: true
            size: "caption"
            text: root.collapsed ? Glyphs.chevronRight : Glyphs.chevronDown
            visible: headerMouse.containsMouse || root.collapsed || root.hasCursor
        }

        Label {
            id: headerText

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.top: parent.top
            tracked: true
            text: root.label
        }

        Label {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.top: parent.top
            dim: true
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
