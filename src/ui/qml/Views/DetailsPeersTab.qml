pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Peers tab (spec v2 §2 Fig. 4 `.list.peers`):
// `Address · CC · Client · ↓ · ↑ · Done`. Speeds are in the table's units ("1.02 MB/s",
// docs/11 J2). The address column shows the IP only — the port travels with the `b` ban and
// `y` copy actions, and shows on hover with the peer's country and connection.
//
// J1: docked on the right there is not room for six columns, so the address takes a fixed
// width that holds a whole IPv4 address (an IPv6 one elides in the middle), CC steps aside
// (the country is in the address's hover label) and the client takes what is left. The
// client is the one flexible column, with a preferred width of 1 on both the header and the
// row, so the header's caption starts where the row text does whatever their lengths (J2).
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
    readonly property int speedWidth: Theme.space(64)
    readonly property int addressWidth: root.width >= Theme.space(560) ? Theme.space(160)
                                                                       : Theme.space(100)
    readonly property bool showCode: root.width >= Theme.space(480)
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
        spacing: Theme.space(8)

        DetailsHeader {
            Layout.preferredWidth: root.addressWidth
            text: qsTr("Address")
        }

        DetailsHeader {
            Layout.preferredWidth: root.codeWidth
            text: qsTr("CC")
            visible: root.showCode
        }

        DetailsHeader {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            text: qsTr("Client")
        }

        DetailsHeader {
            Layout.preferredWidth: root.speedWidth
            alignRight: true
            text: Glyphs.arrowDown
        }

        DetailsHeader {
            Layout.preferredWidth: root.speedWidth
            alignRight: true
            text: Glyphs.arrowUp
        }

        DetailsHeader {
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
            required property string address
            required property string country
            required property string connection
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
                spacing: Theme.space(8)

                DetailsCell {
                    Layout.preferredWidth: root.addressWidth
                    alwaysTip: true
                    color: row.textColor
                    elide: Text.ElideMiddle
                    objectName: "peerAddress_" + row.index
                    text: row.ip
                    tip: tip
                    tipText: [row.address, row.country, row.connection].filter(part => part.length
                                                                                   > 0).join(" · ")
                }

                DetailsCell {
                    Layout.preferredWidth: root.codeWidth
                    color: row.textColor
                    text: row.countryCode
                    tip: tip
                    tipText: row.country
                    visible: root.showCode
                }

                DetailsCell {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    color: row.textColor
                    // The version is the end worth keeping: "qBitt…4.6.2".
                    elide: Text.ElideMiddle
                    text: row.client
                    tip: tip
                }

                DetailsCell {
                    Layout.preferredWidth: root.speedWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.dlSpeedText
                }

                DetailsCell {
                    Layout.preferredWidth: root.speedWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.upSpeedText
                }

                DetailsCell {
                    Layout.preferredWidth: root.doneWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.progressText
                }
            }
        }
    }

    Tooltip {
        id: tip
    }
}
