pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// One-line notice bar (spec v2 §7 Fig. 23 `.banner.err`: "✕ connection refused · retrying in
// 7 s · retry now · settings"). `kind` is "error" | "warning" | "info", each with its own text
// colour and glyph; a 1 px left rule marks the kind. `actions` is a list of `{label, key}`
// pairs rendered as inline accent-text links; clicking one emits `actionTriggered(key)`.
Row {
    id: root

    property var actions: []
    property string kind: "info"
    property string text: ""

    signal actionTriggered(string key)

    readonly property string _kindGlyph: root.kind === "error" ? Glyphs.close : root.kind
                                                                 === "warning"
                                                                 ? Glyphs.statusStalled : Glyphs.dot
    readonly property color _kindColor: root.kind === "error" ? Theme.red : root.kind === "warning"
                                                                ? Theme.yellow : Theme.accent

    height: Theme.controlHeight
    leftPadding: Theme.space(10)
    spacing: Theme.space(8)

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        color: root._kindColor
        height: parent.height
        width: 1
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        color: root._kindColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root._kindGlyph
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root.text
    }

    Repeater {
        model: root.actions

        Text {
            id: actionText

            required property var modelData

            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: actionText.modelData.label

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor

                onClicked: root.actionTriggered(actionText.modelData.key)
            }
        }
    }
}
