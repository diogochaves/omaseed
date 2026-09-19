import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One server in the settings modal's Servers pane (spec v2 §5 Fig. 16 `.srv .it`): a health
// dot, the name in bright over a dim `url · auth · N torrents` line, and a right-hand status
// word — `default` for the one the engine is on, `retrying` in dim red for one that stopped
// answering. `dashed` draws the figure's "+ add a server" row, which has no dot and a dim
// title.
Rectangle {
    id: root

    property string title: ""
    property string description: ""
    property string status: ""
    /// Red status word and a red dot: the server's last test failed.
    property bool failing: false
    /// No dot, dim title, dashed outline — Fig. 16's "add a server" row.
    property bool dashed: false
    property string glyph: ""
    property bool hasCursor: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse

    border.color: root.hasCursor ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
    border.width: 1
    color: root.hot ? Theme.fill(Theme.fillHover) : "transparent"
    implicitHeight: Math.max(Theme.space(40), layout.implicitHeight + Theme.space(16))
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(10)

        // `.srv .it .dot` — green when the last test answered, red when it did not.
        Rectangle {
            Layout.preferredHeight: Theme.space(7)
            Layout.preferredWidth: Theme.space(7)
            color: root.failing ? Theme.red : Theme.green
            radius: width / 2
            visible: !root.dashed
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: root.glyph
            visible: root.dashed && root.glyph.length > 0
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(1)

            Label {
                Layout.fillWidth: true
                bright: !root.dashed
                dim: root.dashed
                elide: Text.ElideRight
                font.weight: root.dashed ? Font.Normal : Font.Medium
                text: root.title
            }

            Label {
                Layout.fillWidth: true
                dim: true
                elide: Text.ElideRight
                size: "small"
                text: root.description
                visible: root.description.length > 0
            }
        }

        Text {
            color: root.failing ? Theme.red : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.status
            visible: root.status.length > 0
        }
    }
}
