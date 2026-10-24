import QtQuick
import Omaseed.Kit

// Omarchy's SelectionMark (Kit/SelectionMark.qml): the rows' and the rail's leading rule, as
// they drew it; nothing when the seam asks for no rule.
Item {
    id: face

    required property SelectionMark seam

    objectName: "selectionMarkFaceOmarchy"

    Rectangle {
        color: face.seam.color
        height: parent.height
        visible: face.seam.rule
        width: face.seam.ruleWidth
    }
}
