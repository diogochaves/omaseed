import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One server in the settings modal's Servers pane (spec v2 §5 Fig. 16 `.srv .it`): a health
// dot, the name in bright over a dim `url · auth · N torrents` line, and a right-hand status
// word — `default` for the one the engine is on, `retrying` in dim red for one that stopped
// answering. `dashed` draws the figure's "+ add a server" row, which has no dot and a dim
// title.
//
// A right-click sends `menuRequested` with the point in the row's own coordinates; the
// Servers pane opens the row's menu there (#51 D4).
Rectangle {
    id: root

    property string title: ""
    property string description: ""
    property string status: ""
    /// Red status word and a red dot: the server's last test failed.
    property bool failing: false
    /// Nothing is known yet (never tested, not the one the app is synced with): a dim dot,
    /// not the green of a server that answered (review RUN-9).
    property bool untested: false
    /// No dot, dim title, dashed outline — Fig. 16's "add a server" row.
    property bool dashed: false
    property string glyph: ""
    property bool hasCursor: false

    signal clicked
    /// A right-click at `x`, `y` (the row's coordinates).
    signal menuRequested(real x, real y)

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

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                root.menuRequested(mouse.x, mouse.y);
            else
                root.clicked();
        }
    }

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(10)

        // `.srv .it .dot` — green when the last test answered, red when it did not, dim
        // while nothing is known.
        Rectangle {
            Layout.preferredHeight: Theme.space(7)
            Layout.preferredWidth: Theme.space(7)
            objectName: "serverDot"
            color: root.failing ? Theme.red : root.untested ? Theme.dimForeground : Theme.green
            radius: width / 2
            visible: !root.dashed
        }

        Glyph {
            color: Theme.dimForeground
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
                id: lineText

                Layout.fillWidth: true
                dim: true
                elide: Text.ElideRight
                objectName: "serverRowDescription"
                size: "small"
                text: root.description
                visible: root.description.length > 0

                HoverHandler {
                    id: lineHover
                }

                // The kit's one tooltip, as KeyValue's: the full text
                // while it is cut off and the pointer is on it.
                Tooltip {
                    id: lineTip

                    readonly property bool wanted: lineText.truncated && lineHover.hovered

                    objectName: "serverRowTip"
                    parent: lineText.Window.window ? lineText.Window.window.contentItem : lineText
                    z: 1000

                    onWantedChanged: lineTip.wanted ? lineTip.showFor(lineText, lineText.text, false)
                                                   : lineTip.hide()
                }
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
