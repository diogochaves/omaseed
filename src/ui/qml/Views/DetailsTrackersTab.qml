pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Trackers tab (spec v2 §2 Fig. 3 `.list.trk`): a health dot, then
// `Tracker · Seeds · Peers · Status`. Protocol prefixes are already dropped by the model;
// the full URL travels in the `y` copy action and shows on hover. DHT/PeX/LSD are dimmed rows
// with a dim dot. Hosts elide in the middle so the TLD survives (docs/11 J1), and each header
// sits over its column's text, the dot column included (J2).
Item {
    id: root

    required property TorrentTrackersModel model
    property int cursorIndex: 0

    signal rowClicked(int row)

    readonly property int count: root.model ? root.model.count : 0

    // The spec's `.trk { grid-template-columns: 14px minmax(0,1fr) 50px 44px 150px }`,
    // trimmed to fit the docked column: Fig. 3 is a 400 px mock, the real pane is 340, and
    // the host is the column that has to stay readable.
    readonly property int dotWidth: Theme.space(10)
    readonly property int seedsWidth: Theme.space(40)
    readonly property int peersWidth: Theme.space(34)
    readonly property int statusWidth: Theme.space(118)

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
            Layout.preferredWidth: root.dotWidth
            text: ""
        }

        DetailsHeader {
            Layout.fillWidth: true
            text: qsTr("Tracker")
        }

        DetailsHeader {
            Layout.preferredWidth: root.seedsWidth
            alignRight: true
            text: qsTr("Seeds")
        }

        DetailsHeader {
            Layout.preferredWidth: root.peersWidth
            alignRight: true
            text: qsTr("Peers")
        }

        DetailsHeader {
            Layout.preferredWidth: root.statusWidth
            text: qsTr("Status")
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
            required property string host
            required property string url
            required property string statusText
            required property string dotRole
            required property int status
            required property string seedsText
            required property string leechesText
            required property bool isPseudo

            readonly property color rowColor: row.isPseudo ? Theme.dimForeground : row.textColor
            readonly property color statusColor: row.isPseudo ? Theme.dimForeground : row.status
                                                                === 4 ? Theme.yellow : row.textColor

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

                // The dot's column is as wide as its (empty) header, so the host starts
                // where "Tracker" does.
                Item {
                    Layout.preferredHeight: Theme.space(6)
                    Layout.preferredWidth: root.dotWidth

                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme[row.dotRole]
                        height: Theme.space(6)
                        radius: width / 2
                        width: Theme.space(6)
                    }
                }

                DetailsCell {
                    Layout.fillWidth: true
                    alwaysTip: !row.isPseudo
                    color: row.rowColor
                    elide: Text.ElideMiddle
                    objectName: "trackerHost_" + row.index
                    text: row.host
                    tip: tip
                    tipText: row.url
                }

                DetailsCell {
                    Layout.preferredWidth: root.seedsWidth
                    color: row.rowColor
                    horizontalAlignment: Text.AlignRight
                    text: row.seedsText
                }

                DetailsCell {
                    Layout.preferredWidth: root.peersWidth
                    color: row.rowColor
                    horizontalAlignment: Text.AlignRight
                    text: row.leechesText
                }

                DetailsCell {
                    Layout.preferredWidth: root.statusWidth
                    color: row.statusColor
                    text: row.statusText
                    tip: tip
                }
            }
        }
    }

    // The whole announce URL on a host, the whole status on a cut one.
    Tooltip {
        id: tip
    }
}
