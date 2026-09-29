pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The key/value block of spec v2 §3 Fig. 9 (`.kv`): a fixed key column and a flexible value
// column, each row closed by a hairline. `items` is `[{key, value, color?}]`; `color` lets a
// caller pick a state colour for the value (e.g. the status row in `Theme.stateDownloading`),
// defaulting to `brightForeground`.
//
// The spec's own look (ruling Q7, review KIT-17): `.kv .k` is a tracked caption one pixel
// under `caption` (as Strip's labels are), `.kv .v` is fg-bright on a single line with an
// ellipsis. Every row is therefore `Theme.space(26)` tall; a value that had to elide shows in
// full in a tooltip (IconButton's look) while the pointer rests on it.
Item {
    id: root

    property var items: []

    /// `.kv .k`: one pixel under the caption size.
    readonly property int _keySize: Math.max(1, Theme.font.caption - 1)

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

                readonly property alias valueText: valueText

                height: Math.max(Theme.space(26), rowLayout.implicitHeight)
                objectName: "keyValueRow"
                width: column.width

                RowLayout {
                    id: rowLayout

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(10)

                    Text {
                        Layout.preferredWidth: Theme.space(92)
                        color: Theme.dimForeground
                        elide: Text.ElideRight
                        font.capitalization: Font.AllUppercase
                        font.family: Theme.fontFamily
                        font.letterSpacing: Math.max(1, Math.round(root._keySize * 0.08))
                        font.pixelSize: root._keySize
                        objectName: "keyValueKey"
                        text: rowItem.modelData.key
                        textFormat: Text.PlainText
                    }

                    Text {
                        id: valueText

                        Layout.fillWidth: true
                        color: rowItem.modelData.color ? rowItem.modelData.color :
                                                         Theme.brightForeground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        maximumLineCount: 1
                        text: rowItem.modelData.value
                        textFormat: Text.PlainText
                        wrapMode: Text.NoWrap

                        HoverHandler {
                            id: valueHover
                        }

                        // The kit's one tooltip (ruling 8), on the window's content item like
                        // IconButton's, so a row one line tall does not clamp it.
                        Tooltip {
                            id: valueTip

                            readonly property bool wanted: valueText.truncated && valueHover.hovered

                            objectName: "keyValueTip"
                            parent: valueText.Window.window ? valueText.Window.window.contentItem : valueText
                            z: 1000

                            onWantedChanged: valueTip.wanted ? valueTip.showFor(valueText, valueText.text, false)
                                                             : valueTip.hide()
                        }
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
