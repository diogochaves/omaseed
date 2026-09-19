import QtQuick
import Omaseed.Kit

// One tracked-label group of the rail (spec v2 §1 `.rail .grp`/`.rail .grp .lbl`): the
// group's heading, `Theme.space(16)` from the left with `Theme.space(4)` below it, then
// whatever rows the caller declares as ordinary children — they land in this `Column`'s
// `data` the same way any other child of a positioner does, right after the heading.
Column {
    id: root

    property string label: ""

    objectName: root.label
    spacing: 0

    Item {
        id: header

        implicitHeight: headerText.implicitHeight + Theme.space(4)
        width: parent.width

        Label {
            id: headerText

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.top: parent.top
            tracked: true
            text: root.label
        }
    }
}
