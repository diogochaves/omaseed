pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One key/value row of the details pane's facts tab (General's until DECISIONS C24 H6; spec v2
// §3 Fig. 9 `.kv`, grouped by docs/12 G1): a fixed key column and a flexible value column,
// closed by a row hairline, the same measures as Kit's KeyValue. `entry` is one of
// DetailsFactsTab's rows,
// `{key, value, color?, dim?, halves?, tail?}`; `tail` follows the value in the dim
// colour (docs/28: the Status row's "error · no access"). A `halves` value that does not fit on one line breaks
// at its middle (20 + 20 for a hash v1) instead of leaving two characters on a line of their
// own. `hasCursor` draws the details lists' cursor (a hover fill and
// a 1 px border, as Kit's TableRow does), so the tabs read as one family.
Rectangle {
    id: root

    required property var entry
    property bool hasCursor: false
    property int keyWidth: Theme.space(100)

    /// A click anywhere on the row.
    signal clicked

    readonly property bool hasTail: (root.entry.tail || "").length > 0
    /// docs/DECISIONS.md C30 F1: the row's words on its ground, the cursor's fill or the
    /// pointer's 4 %; they change at once, as the fill does. An entry's `color` is a window
    /// role: the ground's of the same (C30 F3).
    readonly property Ink ink: Ink {
        animated: false
        ground: root.hasCursor ? Theme.on.cursor : mouse.containsMouse ? Theme.on.idle
                                                                         : Theme.on.window
    }
    readonly property color valueColor: root.entry.color ? root.ink.of(root.entry.color)
                                                         : root.entry.dim ? root.ink.dimForeground
                                                                          : root.ink.foreground

    // #77: a list item named by its key, the value (live, some of them) the description; its
    // press is the click, and does nothing while disabled.
    Accessible.description: root.entry.value + (root.hasTail ? " · " + root.entry.tail : "")
    Accessible.name: root.entry.key
    Accessible.role: Accessible.ListItem
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

    border.color: root.hasCursor ? Theme.fill(Theme.borderHover) : "transparent"
    border.width: root.hasCursor ? 1 : 0
    color: root.hasCursor || mouse.containsMouse ? root.ink.fill : "transparent"
    implicitHeight: Math.max(Theme.space(26), rowLayout.implicitHeight + Theme.space(8))

    RowLayout {
        id: rowLayout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(10)

        Label {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: root.keyWidth
            elide: Text.ElideRight
            ink: root.ink
            text: root.entry.key
            tracked: true
        }

        Text {
            id: valueText

            readonly property string value: root.entry.value
            readonly property bool split: root.entry.halves === true
                                          && oneLine.advanceWidth > valueText.width
            readonly property int middle: Math.ceil(valueText.value.length / 2)

            Layout.fillWidth: !root.hasTail
            color: root.valueColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "factsValue"
            text: valueText.split ? valueText.value.slice(0, valueText.middle) + "\n"
                                    + valueText.value.slice(valueText.middle) : valueText.value
            textFormat: Text.PlainText
            wrapMode: Text.Wrap

            TextMetrics {
                id: oneLine

                font: valueText.font
                text: root.entry.halves === true ? valueText.value : ""
            }
        }

        Text {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            color: root.ink.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "factsTail"
            text: "· " + (root.entry.tail || "")
            textFormat: Text.PlainText
            visible: root.hasTail
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

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }
}
