import QtQuick
import Omaseed.Kit

// One of the console's six totals (docs/22 §4 `.tot`): a tracked caption, a big number with
// its unit after it, smaller and dim, and a note line under it.
Item {
    id: root

    property string caption: ""
    property string value: ""
    property string unit: ""
    property string note: ""
    property color noteColor: Theme.dimForeground
    property color valueColor: Theme.brightForeground

    implicitHeight: column.implicitHeight + Theme.space(21)

    Column {
        id: column

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(14)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(10)
        spacing: Theme.space(2)

        Label {
            text: root.caption
            tracked: true
        }

        Row {
            spacing: Theme.space(3)

            Text {
                color: root.valueColor
                font.family: Theme.fontFamily
                font.pixelSize: Math.round(Theme.font.heading * 1.4)
                font.weight: Font.Medium
                text: root.value
            }

            Text {
                anchors.baseline: parent.children[0].baseline
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                text: root.unit
                visible: root.unit.length > 0
            }
        }

        Text {
            color: root.noteColor
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: root.note
            width: parent.width
        }
    }
}
