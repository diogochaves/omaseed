pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// The rows display (board 46 `rows()`, DECISIONS C24 H3): the library grouped under the five
// questions, each torrent two lines in words before numbers (LibraryRow, core/RowWords), each
// group under its head (LibraryGroupHead: the question, its count, a short line).
//
// A face of the table (TorrentTable's `face`): the table keeps the cursor, the selection, the
// keys and the rename, and this draws the same proxy, grouped by question
// (TorrentFilterModel::groupBy, which App sets for the display), and answers the table's
// questions about where its rows are. Clicks follow "The click rules every list follows"
// through the table's own functions, so the selection, the action bar, the outcome strip and
// its "show" hold here as they do in the table.
Item {
    id: root

    /// The table whose cursor and selection these rows draw.
    required property TorrentTable table
    readonly property TorrentFilterModel filtered: root.table.filtered

    /// Room past the last row for what floats over the bottom (the outcome strip).
    property real bottomRoom: 0
    /// The narrow step, from the display's own width (`Theme.libraryRowsSteps`).
    readonly property int level: {
        const steps = Theme.libraryRowsSteps;
        for (let i = 0; i < steps.length; ++i) {
            if (root.width >= steps[i])
                return i;
        }
        return steps.length;
    }

    // ---- the face the table asks (TorrentTable `face`) ----------------------------------
    readonly property real scrollY: list.contentY
    readonly property bool hovered: rowsHover.hovered
    readonly property bool inlineRename: true

    /// A needs-you row's fix was clicked: `action` is RowWords::fixAction
    /// ("problem:start", "failure:location"); the cursor is on `hash` already.
    signal fixRequested(string hash, string action)

    function positionAt(row: int): void {
        if (row < 0 || row >= root.filtered.count)
            return;
        // ListView builds the row; the containing is done here, where a group's head counts:
        // the first row of a group brings its head into view with it.
        list.positionViewAtIndex(row, ListView.Contain);
        const item = list.itemAtIndex(row);
        if (item === null)
            return;
        const group = root.filtered.data(root.filtered.index(row, 0), TorrentFilterModel.GroupRole);
        const first = row === 0 || root.filtered.data(root.filtered.index(row - 1, 0), TorrentFilterModel.GroupRole)
                    !== group;
        const top = item.y - (first ? Theme.libraryGroupHeadHeight : 0);
        if (top < list.contentY)
            list.contentY = Math.max(list.originY, top);
        else if (item.y + item.height > list.contentY + list.height)
            list.contentY = item.y + item.height - list.height;
    }

    function rowInView(row: int): bool {
        const item = list.itemAtIndex(row);
        if (item === null)
            return false;
        return item.y >= list.contentY - 1 && item.y + item.height <= list.contentY + list.height + 1;
    }

    function rowAnchor(row: int): point {
        const item = list.itemAtIndex(row);
        if (item === null)
            return Qt.point(Theme.libraryMenuInset, list.height / 2);
        return Qt.point(Theme.libraryMenuInset, list.y + item.y - list.contentY + item.height);
    }

    function rowAt(x: real, y: real): int {
        if (x < list.x || x >= list.x + list.width || y < list.y || y >= list.y + list.height)
            return -1;
        const at = root.mapToItem(list.contentItem, x, y);
        const row = list.indexAt(at.x, at.y);
        if (row < 0)
            return -1;
        // indexAt() counts a group's head as its first row's.
        const item = list.itemAtIndex(row);
        return item !== null && at.y < item.y ? -1 : row;
    }

    /// A group's entry in `filtered.groups` by its key, or an empty one.
    function groupOf(key: string): var {
        for (const group of root.filtered.groups) {
            if (group.key === key)
                return group;
        }
        return ({});
    }

    ListView {
        id: list

        anchors.fill: parent
        bottomMargin: root.bottomRoom
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: Theme.libraryRowHeight * 6
        clip: true
        keyNavigationEnabled: false
        model: root.filtered
        objectName: "libraryRows"
        reuseItems: true
        section.criteria: ViewSection.FullString
        section.property: "group"

        // #77: a list for a screen reader, each row a list item in it.
        Accessible.name: qsTr("torrents")
        Accessible.role: Accessible.List

        // #51 D9: the rows hold still under the pointer, as the table's do.
        HoverHandler {
            id: rowsHover
        }

        section.delegate: LibraryGroupHead {
            required property string section

            readonly property var group: root.groupOf(section)

            count: group.count ?? 0
            line: root.level >= 1 ? "" : (group.line ?? "")
            needsYou: (group.question ?? 0) === TorrentFilterModel.NeedsYou
            title: group.title ?? ""
            width: ListView.view.width
        }

        delegate: LibraryRow {
            table: root.table
            narrow: root.level
            width: ListView.view.width

            onFixClicked: (hash, action) => root.fixRequested(hash, action)
        }
    }

    // The empty space below the last row: a click clears the selection, as in the table.
    // a11y-ignore A2: empty space, not a control; esc clears the selection too (#77)
    MouseArea {
        id: emptySpace

        readonly property real rowsEnd: list.originY + list.contentHeight - list.contentY

        height: Math.max(0, list.height - emptySpace.y)
        objectName: "libraryRowsEmpty"
        width: list.width
        y: Math.max(0, Math.min(list.height, emptySpace.rowsEnd))
        z: 1

        onClicked: {
            root.table.forceActiveFocus();
            root.table.clearSelection();
        }
    }

    // A wheel notch moves three rows, as everywhere (Kit/WheelScroll).
    WheelScroll {
        anchors.fill: list
        flickable: list
        objectName: "libraryRowsWheel"
        rowHeight: Theme.libraryRowHeight
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
