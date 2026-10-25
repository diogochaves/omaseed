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

    /// The pane's selection (#51 D15), as `ip:port` addresses: those rows fill.
    property var selectedKeys: []

    /// `modifiers`: ctrl and shift select by the pane's click rules.
    signal rowClicked(int row, int modifiers)
    /// A middle click on a row: the pane toggles it in the selection, as ctrl+click.
    signal rowMiddleClicked(int row)
    /// A click on the list's empty space, under its last row: the selection goes.
    signal emptyClicked
    /// A right click on a row: the pane moves the cursor there and opens the tab's menu at
    /// `x`, `y` (this item's coordinates).
    signal rowMenuRequested(int row, real x, real y)

    readonly property int count: root.model ? root.model.count : 0

    // The spec's `.peers { grid-template-columns: 1fr 40px 1fr 64px 64px 52px }`, trimmed to
    // fit the docked column (Fig. 4 is a 400 px mock, the pane is 340). Six columns in 340
    // means the two flexible ones elide; the address gets the larger share of what is left,
    // because it is what identifies the row for `b` ban and `y` copy.
    // The fixed columns are also at least their caption's whole width (DetailsHeader.fitWidth),
    // for a skin's wider caption voice (#171).
    readonly property int codeWidth: Math.max(Theme.space(26), codeHead.fitWidth)
    readonly property int speedWidth: Math.max(Theme.space(64), downHead.fitWidth, upHead.fitWidth)
    readonly property int addressWidth: root.width >= Theme.space(560) ? Theme.space(160)
                                                                       : Theme.space(100)
    readonly property bool showCode: root.width >= Theme.space(480)
    readonly property int doneWidth: Math.max(Theme.space(40), doneHead.fitWidth)

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
            clickable: false
            text: qsTr("Address")
        }

        DetailsHeader {
            id: codeHead

            Layout.preferredWidth: root.codeWidth
            clickable: false
            text: qsTr("CC")
            visible: root.showCode
        }

        DetailsHeader {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            clickable: false
            text: qsTr("Client")
        }

        DetailsHeader {
            id: downHead

            Layout.preferredWidth: root.speedWidth
            alignRight: true
            accessibleName: qsTr("Download speed")
            clickable: false
            text: Glyphs.arrowDown
        }

        DetailsHeader {
            id: upHead

            Layout.preferredWidth: root.speedWidth
            alignRight: true
            accessibleName: qsTr("Upload speed")
            clickable: false
            text: Glyphs.arrowUp
        }

        DetailsHeader {
            id: doneHead

            Layout.preferredWidth: root.doneWidth
            alignRight: true
            clickable: false
            text: qsTr("Done")
        }
    }

    ListView {
        id: list

        // #77: the peers' list, for a screen reader; each row is a list item.
        Accessible.name: qsTr("peers")
        Accessible.role: Accessible.List
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

            // #77: a screen reader finds the peer by its address; the speeds and progress
            // change too often to be in the name.
            Accessible.name: row.address
            hasCursor: row.index === root.cursorIndex
            height: Theme.railRowHeight
            objectName: "peerRow_" + row.index
            selected: root.selectedKeys.indexOf(row.address) >= 0
            width: ListView.view.width

            onClicked: modifiers => root.rowClicked(row.index, modifiers)
            onRightClicked: {
                const at = row.mapToItem(root, Theme.space(24), row.height);
                root.rowMenuRequested(row.index, at.x, at.y);
            }

            // The middle button, which TableRow leaves alone, toggles the row (#51 D8).
            MouseArea {
                acceptedButtons: Qt.MiddleButton
                anchors.fill: parent
                parent: row

                onClicked: root.rowMiddleClicked(row.index)
            }

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

    // The list's empty space, under its last row: a click there lets the selection go, as
    // in the table.
    // a11y-ignore A2: empty space, not a control; esc lets the selection go too (#77)
    MouseArea {
        id: emptySpace

        /// Where the rows end, in the list's coordinates.
        readonly property real rowsEnd: list.originY + list.contentHeight - list.contentY

        height: Math.max(0, list.y + list.height - emptySpace.y)
        objectName: "peersEmptySpace"
        width: list.width
        x: list.x
        y: list.y + Math.max(0, Math.min(list.height, emptySpace.rowsEnd))
        z: 1

        onClicked: root.emptyClicked()
    }

    ScrollBar {
        anchors.bottom: list.bottom
        anchors.right: parent.right
        anchors.top: list.top
        flickable: list
        vertical: true
        width: Theme.space(4)
    }

    // A wheel notch moves three rows, and quick notches add up (Kit/WheelScroll).
    WheelScroll {
        anchors.fill: list
        flickable: list
        rowHeight: Theme.railRowHeight
        z: 2
    }

    Tooltip {
        id: tip
    }
}
