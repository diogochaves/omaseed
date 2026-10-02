pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's HTTP sources tab (#10, docs/12 L1): the torrent's web seeds, one URL a
// row, in the Trackers tab's look. The model drops `https://` (a plain `http://` source keeps
// its scheme); the whole URL shows on hover and travels in `y` and `d`. URLs elide in the
// middle, so the host and the file or folder at the end both survive (docs/11 J1). The pane
// only shows this tab when the torrent has web seeds.
Item {
    id: root

    required property TorrentWebSeedsModel model
    property int cursorIndex: 0

    signal rowClicked(int row)

    readonly property int count: root.model ? root.model.count : 0

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
            Layout.fillWidth: true
            text: qsTr("URL")
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
            required property string text
            required property string url

            hasCursor: row.index === root.cursorIndex
            height: Theme.railRowHeight
            width: ListView.view.width

            onClicked: root.rowClicked(row.index)

            DetailsCell {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                alwaysTip: true
                color: row.textColor
                elide: Text.ElideMiddle
                objectName: "webSeedUrl_" + row.index
                text: row.text
                tip: tip
                tipText: row.url
            }
        }
    }

    // The whole URL, scheme and all.
    Tooltip {
        id: tip
    }
}
