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

    /// The pane's selection (#51 D15), as URLs: those rows fill.
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
            clickable: false
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
            objectName: "webSeedRow_" + row.index
            selected: root.selectedKeys.indexOf(row.url) >= 0
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

    // The list's empty space, under its last row: a click there lets the selection go, as
    // in the table.
    MouseArea {
        id: emptySpace

        /// Where the rows end, in the list's coordinates.
        readonly property real rowsEnd: list.originY + list.contentHeight - list.contentY

        height: Math.max(0, list.y + list.height - emptySpace.y)
        objectName: "webSeedsEmptySpace"
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

    // The whole URL, scheme and all.
    Tooltip {
        id: tip
    }
}
