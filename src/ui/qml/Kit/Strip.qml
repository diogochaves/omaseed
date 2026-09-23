pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The details head's four-up fact strip (spec v2 §3 Fig. 8 `.strip`): four equal columns,
// each a tracked dim label over a bright value. `items` is `[{label, value}]`; the spec always
// shows four, but this component lays out however many it is given, at equal width
// (`grid-template-columns:repeat(4,1fr)`). Padding is the spec's `6px 16px 10px`; a hairline
// closes the bottom unless `rule` is off (docs/13: in the details band the band draws it).
Item {
    id: root

    property var items: []
    property bool rule: true

    readonly property int _labelSize: Math.max(1, Theme.font.caption - 1)

    height: implicitHeight
    implicitHeight: row.implicitHeight + Theme.space(6) + Theme.space(10)

    RowLayout {
        id: row

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(6)
        spacing: Theme.space(10)

        Repeater {
            model: root.items

            ColumnLayout {
                id: cell

                required property var modelData

                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    color: Theme.dimForeground
                    elide: Text.ElideRight
                    font.capitalization: Font.AllUppercase
                    font.family: Theme.fontFamily
                    font.letterSpacing: root._labelSize * 0.1
                    font.pixelSize: root._labelSize
                    text: cell.modelData.label
                }

                Text {
                    Layout.fillWidth: true
                    color: Theme.brightForeground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: cell.modelData.value
                }
            }
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.rule
    }
}
