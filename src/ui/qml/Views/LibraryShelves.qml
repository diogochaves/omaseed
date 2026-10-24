pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// The overview in the Omarchy skin (board 46 `overview()`, "shelves", DECISIONS C24 H3–H4,
// #168 assumed 6): one shelf per category, by name, "no category" last, each a head (its name,
// its count and size) over its covers (LibraryCover), wrapping onto more lines. The console's
// station waits for its skin (#171).
//
// A face of the table (TorrentTable `face`), as the rows display is: the same proxy, grouped by
// category (App sets TorrentFilterModel::groupBy for the display), the table's cursor,
// selection and keys (j/k go cover by cover, shelf after shelf).
Item {
    id: root

    required property TorrentTable table
    readonly property TorrentFilterModel filtered: root.table.filtered
    property real bottomRoom: 0

    /// Moves whenever the rows' words may have: a poll, a sort, rows come or go.
    property int revision: 0

    // ---- the face the table asks (TorrentTable `face`) ----------------------------------
    readonly property real scrollY: list.contentY
    readonly property bool hovered: shelvesHover.hovered
    readonly property bool inlineRename: false

    /// The shelf holding `row`, as an index in `filtered.groups`, or -1.
    function shelfOf(row: int): int {
        const groups = root.filtered.groups;
        for (let i = 0; i < groups.length; ++i) {
            if (row >= groups[i].first && row < groups[i].first + groups[i].count)
                return i;
        }
        return -1;
    }

    /// The cover drawing `row` when its shelf is built, else null.
    function coverOf(row: int): Item {
        const shelf = list.itemAtIndex(root.shelfOf(row));
        return shelf !== null ? shelf.coverAt(row) : null;
    }

    function positionAt(row: int): void {
        const index = root.shelfOf(row);
        if (index < 0)
            return;
        if (list.itemAtIndex(index) === null)
            list.positionViewAtIndex(index, ListView.Beginning);
        const item = root.coverOf(row);
        if (item === null)
            return;
        const top = item.mapToItem(list.contentItem, 0, 0).y;
        // The first line of a shelf brings its head with it.
        const shelf = list.itemAtIndex(index);
        const from = top - shelf.y < shelf.firstLine ? shelf.y : top;
        if (from < list.contentY)
            list.contentY = from;
        else if (top + item.height > list.contentY + list.height)
            list.contentY = top + item.height - list.height;
    }

    function rowInView(row: int): bool {
        const item = root.coverOf(row);
        if (item === null)
            return false;
        const top = item.mapToItem(list.contentItem, 0, 0).y;
        return top >= list.contentY - 1 && top + item.height <= list.contentY + list.height + 1;
    }

    function rowAnchor(row: int): point {
        const item = root.coverOf(row);
        if (item === null)
            return Qt.point(Theme.libraryMenuInset, list.height / 2);
        return item.mapToItem(root, 0, item.height);
    }

    function rowAt(x: real, y: real): int {
        if (x < list.x || x >= list.x + list.width || y < list.y || y >= list.y + list.height)
            return -1;
        const at = root.mapToItem(list.contentItem, x, y);
        const shelf = list.itemAt(at.x, at.y);
        if (shelf === null)
            return -1;
        const inFlow = list.contentItem.mapToItem(shelf.coverFlow, at.x, at.y);
        const item = shelf.coverFlow.childAt(inFlow.x, inFlow.y);
        return item !== null && item.coverRow !== undefined ? item.coverRow : -1;
    }

    Connections {
        function onDataChanged(): void {
            root.revision += 1;
        }

        function onLayoutChanged(): void {
            root.revision += 1;
        }

        function onModelReset(): void {
            root.revision += 1;
        }

        function onRowsInserted(): void {
            root.revision += 1;
        }

        function onRowsRemoved(): void {
            root.revision += 1;
        }

        enabled: root.visible
        target: root.filtered
    }

    ListView {
        id: list

        anchors.fill: parent
        anchors.topMargin: Theme.shelfInsetTop
        bottomMargin: root.bottomRoom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        keyNavigationEnabled: false
        model: root.visible ? root.filtered.groups : []
        objectName: "libraryShelves"
        spacing: Theme.shelfGap

        Accessible.name: qsTr("torrents")
        Accessible.role: Accessible.List

        HoverHandler {
            id: shelvesHover
        }

        delegate: Item {
            id: shelf

            required property var modelData
            required property int index
            readonly property alias coverFlow: flow
            /// Where the covers' first line starts, under the head.
            readonly property real firstLine: flow.y + Theme.coverArtHeight

            function coverAt(row: int): Item {
                return covers.itemAt(row - shelf.modelData.first);
            }

            height: flow.y + flow.height
            objectName: "shelf_" + shelf.index
            width: ListView.view.width

            // The head (`.shelf h5`): the name, then its count and size.
            Row {
                id: head

                spacing: Theme.libraryRowGap
                x: Theme.shelfInset

                Text {
                    id: shelfName

                    Accessible.name: shelfName.text
                    Accessible.role: Accessible.Heading
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.shelfNameSize
                    font.weight: Theme.shelfNameWeight
                    objectName: "shelfName"
                    text: shelf.modelData.title
                    textFormat: Text.PlainText
                }

                Text {
                    anchors.baseline: shelfName.baseline
                    color: Theme.dimForeground
                    font.capitalization: Font.AllUppercase
                    font.family: Theme.fontFamily
                    font.letterSpacing: Theme.shelfLineTracking
                    font.pixelSize: Theme.shelfLineSize
                    objectName: "shelfLine"
                    text: shelf.modelData.line
                    textFormat: Text.PlainText
                }
            }

            Flow {
                id: flow

                anchors.left: parent.left
                anchors.leftMargin: Theme.shelfInset
                anchors.right: parent.right
                anchors.rightMargin: Theme.shelfInset
                spacing: Theme.coverGap
                y: head.height + Theme.shelfHeadGap

                Repeater {
                    id: covers

                    model: shelf.modelData.count

                    LibraryCover {
                        required property int index

                        coverRow: shelf.modelData.first + index
                        revision: root.revision
                        table: root.table
                    }
                }
            }
        }
    }

    // Below the last shelf: a click clears the selection.
    // a11y-ignore A2: empty space, not a control; esc clears the selection too (#77)
    MouseArea {
        id: emptySpace

        readonly property real shelvesEnd: list.y + list.originY + list.contentHeight - list.contentY

        height: Math.max(0, root.height - emptySpace.y)
        objectName: "libraryShelvesEmpty"
        width: root.width
        y: Math.max(0, Math.min(root.height, emptySpace.shelvesEnd))
        z: 1

        onClicked: {
            root.table.forceActiveFocus();
            root.table.clearSelection();
        }
    }

    WheelScroll {
        anchors.fill: list
        flickable: list
        rowHeight: Theme.coverArtHeight / 3
        z: 2
    }

    ScrollHint {
        anchors.fill: list
        flickable: list
        revealOnHover: false
        style: "scrollbar"
        z: 3
    }
}
