import QtQuick
import Omaseed.Kit

// Omarchy's Plate (Kit/Plate.qml): the drawing sheets, menus and notices had before the
// seam, moved as it was. `fill` over `base` at `radius`; all four `edges` are its border,
// fewer are rules on those sides drawn after the leading `rule`, as the notice drew them.
Item {
    id: face

    required property Plate seam

    readonly property bool whole: face.seam.edges === (Qt.TopEdge | Qt.RightEdge | Qt.BottomEdge | Qt.LeftEdge)

    objectName: "plateFaceOmarchy"

    Rectangle {
        anchors.fill: parent
        color: face.seam.base
        radius: face.seam.radius
        visible: face.seam.base.a > 0
    }

    Rectangle {
        anchors.fill: parent
        border.color: face.seam.edgeColor
        border.width: face.whole ? face.seam.edgeWidth : 0
        color: face.seam.fill
        radius: face.seam.radius
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: face.seam.rule
        visible: face.seam.rule.a > 0
        width: face.seam.ruleWidth
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: face.seam.edgeColor
        height: face.seam.edgeWidth
        visible: !face.whole && (face.seam.edges & Qt.BottomEdge)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: face.seam.edgeColor
        height: face.seam.edgeWidth
        visible: !face.whole && (face.seam.edges & Qt.TopEdge)
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: face.seam.edgeColor
        visible: !face.whole && (face.seam.edges & Qt.LeftEdge)
        width: face.seam.edgeWidth
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        color: face.seam.edgeColor
        visible: !face.whole && (face.seam.edges & Qt.RightEdge)
        width: face.seam.edgeWidth
    }
}
