pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Trackers tab (spec v2 §2 Fig. 3 `.list.trk`): a health dot, then
// `Tracker · Seeds · Peers · Status`. Protocol prefixes are already dropped by the model;
// the full URL travels in the `y` copy action. DHT/PeX/LSD are dimmed rows with a dim dot.
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
        spacing: Theme.space(4)

        TableHeader {
            Layout.preferredWidth: root.dotWidth
            text: ""
        }

        TableHeader {
            Layout.fillWidth: true
            text: qsTr("Tracker")
        }

        TableHeader {
            Layout.preferredWidth: root.seedsWidth
            alignRight: true
            text: qsTr("Seeds")
        }

        TableHeader {
            Layout.preferredWidth: root.peersWidth
            alignRight: true
            text: qsTr("Peers")
        }

        TableHeader {
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
                spacing: Theme.space(4)

                Item {
                    Layout.preferredHeight: Theme.space(6)
                    Layout.preferredWidth: Theme.space(6)

                    Rectangle {
                        anchors.fill: parent
                        color: Theme[row.dotRole]
                        radius: width / 2
                    }
                }

                Text {
                    Layout.fillWidth: true
                    color: row.rowColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: row.host
                }

                Text {
                    Layout.preferredWidth: root.seedsWidth
                    color: row.rowColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.seedsText
                }

                Text {
                    Layout.preferredWidth: root.peersWidth
                    color: row.rowColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.leechesText
                }

                Text {
                    Layout.preferredWidth: root.statusWidth
                    color: row.statusColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: row.statusText
                }
            }
        }
    }
}
