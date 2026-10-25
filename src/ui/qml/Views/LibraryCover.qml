pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// One cover on a shelf of the overview (board 46 `overview()`, `.cover`; board 39 L3): drawn,
// never fetched. The art is the torrent's grid, filled in reading order as it arrives (#168
// assumed 7: sync/maindata has no piece states), in its state colour, under its two letters
// (core/RowWords::monogram); under the art its name and its one fact ("3 min", "2.4×", "no
// access", red when it needs you).
//
// The table owns the cursor and the selection (TorrentTable `face`): a click goes through the
// table's own functions, as a row's does. Its form is `Theme.cover*`.
Item {
    id: cover

    required property TorrentTable table
    /// The proxy row this cover draws.
    required property int coverRow
    /// LibraryShelves' count of changes to the rows, so the words follow a poll.
    required property int revision

    readonly property TorrentFilterModel filtered: cover.table.filtered
    readonly property var at: cover.revision >= 0 ? cover.filtered.index(cover.coverRow, 0) : null
    readonly property string hash: cover.revision >= 0 ? cover.filtered.hashAt(cover.coverRow) : ""
    readonly property string name: cover.filtered.data(cover.at, TorrentModel.NameRole) ?? ""
    readonly property string fact: cover.filtered.data(cover.at, TorrentFilterModel.RowFactRole) ?? ""
    readonly property string monogram: cover.filtered.data(cover.at, TorrentFilterModel.MonogramRole) ?? ""
    readonly property real progress: cover.filtered.data(cover.at, TorrentModel.ProgressRole) ?? 0
    readonly property string stateRole: cover.filtered.data(cover.at, TorrentModel.StateColorRoleRole) ?? "stateStopped"
    readonly property bool needsYou: (cover.filtered.data(cover.at, TorrentFilterModel.QuestionRole) ?? 0)
                                     === TorrentFilterModel.NeedsYou
    /// It needs you and the skin marks that with the accent edge (board 46 `.card.needs`).
    readonly property bool needsEdge: cover.needsYou && Theme.coverNeedsEdge

    readonly property bool isCursor: cover.coverRow === cover.table.cursorIndex
    readonly property bool isSelected: cover.table.selectionRevision >= 0 && cover.table.isRowSelected(
                                           cover.coverRow)
    readonly property Ink ink: Ink {
        ground: Theme.ground(cover.isSelected, cover.isCursor, pointer.containsMouse, false)
    }

    function click(modifiers: int): void {
        cover.table.forceActiveFocus();
        if (modifiers & Qt.ShiftModifier) {
            cover.table.selectRangeTo(cover.coverRow, (modifiers & Qt.ControlModifier) !== 0);
            return;
        }
        if (modifiers & Qt.ControlModifier) {
            cover.table.cursorIndex = cover.coverRow;
            cover.table.toggleSelection(cover.coverRow);
            return;
        }
        const had = cover.table.selectedCount > 0;
        cover.table.cursorIndex = cover.coverRow;
        cover.table.selectOnly(cover.coverRow);
        cover.table.plainClicked(had);
    }

    Accessible.description: cover.fact
    Accessible.name: cover.name
    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: cover.isSelected
    Accessible.onPressAction: cover.click(Qt.NoModifier)
    height: art.height + Theme.coverLineGap + nameText.height + factText.height
    objectName: "cover_" + cover.coverRow
    width: Theme.coverWidth

    Item {
        id: art

        height: Theme.coverArtHeight
        width: parent.width

        // The art's ground and edge (the skin's Plate face; Console's is a key plate, board 46
        // `.card`): the accent's lit edge when selected, and when it needs you where the skin
        // says (`.card.needs`).
        Plate {
            anchors.fill: parent
            // #201 F16: the cursor's edge only while the list has the keys.
            edgeColor: cover.isSelected || cover.needsEdge ? Theme.accent
                                                          : cover.isCursor && cover.table.keysHere ? Theme.fill(Theme.borderHover)
                                                                                                   : Theme.fill(Theme.hairlineAlpha)
            edgeWidth: cover.isSelected || cover.needsEdge ? Theme.coverSelectedBorder : Theme.hairWidth
            fill: cover.ink.ground === Theme.on.window ? Theme.fill(Theme.fillIdle) : cover.ink.ground.fill
            key: true
            objectName: "coverPlate"
            radius: 0
        }

        // The grid: a mark, so it keeps the window's colours (C30 lifts words, not marks).
        Meter {
            anchors.left: parent.left
            anchors.leftMargin: Theme.coverGridInset
            anchors.right: parent.right
            anchors.rightMargin: Theme.coverGridInset
            anchors.top: parent.top
            anchors.topMargin: Theme.coverGridInset
            cellGap: Theme.coverCellGap
            cellHeight: Theme.coverCellHeight
            color: Theme[cover.stateRole] ?? Theme.foreground
            columns: Theme.coverGridColumns
            fillAlpha: Theme.coverCellAlpha
            height: Theme.coverGridRows * (Theme.coverCellHeight + Theme.coverCellGap)
            objectName: "coverGrid"
            rows: Theme.coverGridRows
            trackColor: Theme.fill(Theme.coverCellTrack)
            value: cover.progress
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.coverMonogramInset
            anchors.left: parent.left
            anchors.leftMargin: Theme.coverMonogramInset
            color: cover.ink.brightForeground
            // Board 46 `.card .two`: the label family, tracked.
            font.family: Theme.labelFamily
            font.letterSpacing: Theme.coverMonogramSize * Theme.nameTrack
            font.pixelSize: Theme.coverMonogramSize
            font.weight: Theme.coverMonogramWeight
            objectName: "coverMonogram"
            text: cover.monogram
            textFormat: Text.PlainText
        }

        // The selected cover's mark: none in Omarchy's face (the lit edge says it), Console's
        // brackets (board 46 `.card.sel::after`).
        SelectionMark {
            anchors.fill: parent
            cut: Theme.plateKeyCut
            objectName: "coverMark"
            visible: cover.isSelected
        }
    }

    Text {
        id: nameText

        anchors.top: art.bottom
        anchors.topMargin: Theme.coverLineGap
        color: Theme.foreground
        elide: Text.ElideRight
        font.family: Theme.valueFamily
        font.pixelSize: Theme.coverNameSize
        objectName: "coverName"
        text: cover.name
        textFormat: Text.PlainText
        width: parent.width
    }

    Text {
        id: factText

        anchors.top: nameText.bottom
        color: cover.needsEdge ? Theme.accent : cover.needsYou ? Theme.red : Theme.dimForeground
        elide: Text.ElideRight
        font.family: Theme.valueFamily
        font.pixelSize: Theme.coverFactSize
        objectName: "coverFact"
        text: cover.fact
        textFormat: Text.PlainText
        width: parent.width
    }

    MouseArea {
        id: pointer

        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        anchors.fill: parent
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                cover.table.forceActiveFocus();
                const point = pointer.mapToItem(cover.table, mouse.x, mouse.y);
                cover.table.contextClick(cover.coverRow, point.x, point.y);
                return;
            }
            if (mouse.button === Qt.MiddleButton) {
                cover.table.forceActiveFocus();
                cover.table.cursorIndex = cover.coverRow;
                cover.table.toggleSelection(cover.coverRow);
                return;
            }
            cover.click(mouse.modifiers);
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                cover.table.activated(cover.hash);
        }
    }
}
