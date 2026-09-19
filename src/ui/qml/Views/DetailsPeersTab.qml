pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Peers tab (spec v2 §2 Fig. 4 `.list.peers`):
// `Address · CC · Client · ↓ · ↑ · Done`. Speeds are already abbreviated by the model
// ("1.02 M"); the full figure lives in the tooltip. The address column shows the IP only —
// the port travels with the `b` ban and `y` copy actions.
Item {
    id: root

    required property TorrentPeersModel model
    property int cursorIndex: 0

    signal rowClicked(int row)

    readonly property int count: root.model ? root.model.count : 0

    // The spec's `.peers { grid-template-columns: 1fr 40px 1fr 64px 64px 52px }`, trimmed to
    // fit the docked column (Fig. 4 is a 400 px mock, the pane is 340). Six columns in 340
    // means the two flexible ones elide; the address gets the larger share of what is left,
    // because it is what identifies the row for `b` ban and `y` copy.
    readonly property int codeWidth: Theme.space(26)
    readonly property int speedWidth: Theme.space(48)
    readonly property int doneWidth: Theme.space(40)

    function ensureVisible() {
        list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
    }

    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    RowLayout {
        id: header

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.top: parent.top
        spacing: Theme.space(4)

        TableHeader {
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 3
            text: qsTr("Address")
        }

        TableHeader {
            Layout.preferredWidth: root.codeWidth
            text: qsTr("CC")
        }

        TableHeader {
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 2
            text: qsTr("Client")
        }

        TableHeader {
            Layout.preferredWidth: root.speedWidth
            alignRight: true
            text: Glyphs.arrowDown
        }

        TableHeader {
            Layout.preferredWidth: root.speedWidth
            alignRight: true
            text: Glyphs.arrowUp
        }

        TableHeader {
            Layout.preferredWidth: root.doneWidth
            alignRight: true
            text: qsTr("Done")
        }
    }

    ListView {
        id: list

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: root.model

        delegate: TableRow {
            id: row

            required property int index
            required property string ip
            required property string countryCode
            required property string client
            required property string dlSpeedText
            required property string upSpeedText
            required property string progressText

            hasCursor: row.index === root.cursorIndex
            height: Theme.railRowHeight
            width: ListView.view.width

            onClicked: root.rowClicked(row.index)

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(4)

                Text {
                    Layout.fillWidth: true
                    Layout.horizontalStretchFactor: 3
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: row.ip
                }

                Text {
                    Layout.preferredWidth: root.codeWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: row.countryCode
                }

                Text {
                    Layout.fillWidth: true
                    Layout.horizontalStretchFactor: 2
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: row.client
                }

                Text {
                    Layout.preferredWidth: root.speedWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.dlSpeedText
                }

                Text {
                    Layout.preferredWidth: root.speedWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.upSpeedText
                }

                Text {
                    Layout.preferredWidth: root.doneWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.progressText
                }
            }
        }
    }
}
