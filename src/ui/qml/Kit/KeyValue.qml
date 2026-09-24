pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The details General tab's key/value block (spec v2 §3 Fig. 9 `.kv`): a fixed key column and
// a flexible value column, each row closed by a hairline. `items` is
// `[{key, value, color?}]`; `color` lets a caller pick a state colour for the value (e.g. the
// status row in `Theme.stateDownloading`), defaulting to `foreground`. Rows are
// `Theme.space(26)` tall, or taller when a value wraps to more than one line.
Item {
    id: root

    property var items: []

    height: implicitHeight
    implicitHeight: column.implicitHeight + Theme.space(20)

    Column {
        id: column

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(10)
        spacing: 0

        Repeater {
            model: root.items

            Item {
                id: rowItem

                required property var modelData

                height: Math.max(Theme.space(26), rowLayout.implicitHeight)
                width: column.width

                RowLayout {
                    id: rowLayout

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(10)

                    Label {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: Theme.space(92)
                        text: rowItem.modelData.key
                        tracked: true
                    }

                    Text {
                        Layout.fillWidth: true
                        color: rowItem.modelData.color ? rowItem.modelData.color : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: rowItem.modelData.value
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: Theme.fill(Theme.keyValueRuleAlpha)
                    height: 1
                }
            }
        }
    }
}
