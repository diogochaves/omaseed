pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// One torrent's block on space's map (board 51 D12 a, `.blk2`; DECISIONS C32): an area as big
// as its share of the scope's size, its ground the state colour at the selected fill under a
// line of it, its name and then its size and fact ("716 MB · 7 min") where they fit. LibrarySpace
// lays the blocks out; this draws one at the place it is given.
//
// The table owns the cursor and the selection (TorrentTable `face`), as in the table: the
// cursor's block has the accent's edge (#201 F16: only while the list has the keys) and the
// fill the cursor's row has, a selected block the selection's fill (Console adds its brackets,
// SelectionMark). A click goes through the table's own functions, as a row's does.
Item {
    id: tile

    required property TorrentTable table
    /// The proxy row this block draws.
    required property int tileRow
    /// LibrarySpace's count of changes to the rows, so the words follow a poll.
    required property int revision

    readonly property TorrentFilterModel filtered: tile.table.filtered
    readonly property var at: tile.revision >= 0 ? tile.filtered.index(tile.tileRow, 0) : null
    readonly property string hash: tile.revision >= 0 ? tile.filtered.hashAt(tile.tileRow) : ""
    readonly property string name: tile.filtered.data(tile.at, TorrentModel.NameRole) ?? ""
    readonly property string fact: tile.filtered.data(tile.at, TorrentFilterModel.RowFactRole) ?? ""
    readonly property real bytes: tile.filtered.data(tile.at, TorrentModel.SizeRole) ?? 0
    readonly property string stateRole: tile.filtered.data(tile.at,
                                                           TorrentModel.StateColorRoleRole)
                                        ?? "stateStopped"
    readonly property color stateColor: Theme[tile.stateRole] ?? Theme.foreground
    readonly property bool needsYou: (tile.filtered.data(tile.at, TorrentFilterModel.QuestionRole)
                                      ?? 0) === TorrentFilterModel.NeedsYou
    /// It needs you and the skin marks that with the accent edge (board 46 `.card.needs`).
    readonly property bool needsEdge: tile.needsYou && Theme.spaceNeedsEdge
    /// "716 MB · 7 min": the size, and the fact while the block is tall enough for it.
    readonly property string line: {
        const size = tile.bytes > 0 ? App.sizeText(tile.bytes) : "";
        if (tile.height < Theme.spaceTileFactHeight || tile.fact.length === 0)
            return size;
        return size.length > 0 ? qsTr("%1 · %2").arg(size).arg(tile.fact) : tile.fact;
    }

    readonly property bool isCursor: tile.tileRow === tile.table.cursorIndex
    readonly property bool isSelected: tile.table.selectionRevision >= 0 && tile.table.isRowSelected(
                                           tile.tileRow)
    readonly property ThemeInk ground: Theme.ground(tile.isSelected, tile.isCursor,
                                                    pointer.containsMouse, false)

    function click(modifiers: int): void {
        tile.table.forceActiveFocus();
        if (modifiers & Qt.ShiftModifier) {
            tile.table.selectRangeTo(tile.tileRow, (modifiers & Qt.ControlModifier) !== 0);
            return;
        }
        if (modifiers & Qt.ControlModifier) {
            tile.table.cursorIndex = tile.tileRow;
            tile.table.toggleSelection(tile.tileRow);
            return;
        }
        const had = tile.table.selectedCount > 0;
        tile.table.cursorIndex = tile.tileRow;
        tile.table.selectOnly(tile.tileRow);
        tile.table.plainClicked(had);
    }

    Accessible.description: tile.line
    Accessible.name: tile.name
    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: tile.isSelected
    Accessible.onPressAction: tile.click(Qt.NoModifier)
    clip: true
    objectName: "spaceTile_" + tile.tileRow

    // The ground: the state colour at the selected fill (`.blk2`), the skin's plate.
    Plate {
        anchors.fill: parent
        anchors.margins: Theme.spaceTileGap
        edgeWidth: 0
        fill: Qt.alpha(tile.stateColor, Theme.fillSelected)
        key: true
        objectName: "spaceTileGround"
        radius: 0
    }

    // The cursor's, the selection's or the pointer's fill over it, as on a row.
    Plate {
        anchors.fill: parent
        anchors.margins: Theme.spaceTileGap
        edgeWidth: 0
        fill: tile.ground === Theme.on.window ? "transparent" : tile.ground.fill
        key: true
        objectName: "spaceTileFill"
        radius: 0
    }

    // The state colour's line along the top (`box-shadow: inset 0 2px 0 var(--sc)`).
    Rectangle {
        color: tile.stateColor
        height: Theme.spaceTileLine
        objectName: "spaceTileLine"
        width: parent.width - 2 * Theme.spaceTileGap
        x: Theme.spaceTileGap
        y: Theme.spaceTileGap
    }

    // The edge: the cursor's in the accent while the list has the keys (`.blk2.sel`), the
    // accent's when it needs you where the skin says so.
    Plate {
        anchors.fill: parent
        anchors.margins: Theme.spaceTileGap
        edgeColor: Theme.accent
        edgeWidth: (tile.isCursor && tile.table.keysHere) || tile.needsEdge ? Theme.spaceCursorEdge :
                                                                              0
        fill: "transparent"
        key: true
        objectName: "spaceTileEdge"
        radius: 0
    }

    Column {
        id: words

        // A line shows only whole: the name from 70 px wide and where it fits in height, the
        // size and fact under it where they fit too.
        spacing: 1
        visible: tile.width >= Theme.spaceTileWordsWidth && tile.height >= words.y
                 + nameText.implicitHeight + Theme.spaceTileGap
        width: tile.width - 2 * (Theme.spaceTileGap + Theme.spaceTilePadX)
        x: Theme.spaceTileGap + Theme.spaceTilePadX
        y: Theme.spaceTileGap + Theme.spaceTilePadY

        Text {
            id: nameText

            color: tile.ground.brightForeground
            elide: Text.ElideRight
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceTileTextSize
            font.weight: Theme.spaceTileNameWeight
            objectName: "spaceTileName"
            text: tile.name
            textFormat: Text.PlainText
            width: parent.width
        }

        Text {
            color: tile.needsEdge ? Theme.accent : tile.needsYou ? Theme.red :
                                                                   tile.ground.foreground
            elide: Text.ElideRight
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceTileTextSize
            objectName: "spaceTileWords"
            text: tile.line
            textFormat: Text.PlainText
            visible: text.length > 0 && tile.height >= words.y + nameText.implicitHeight
                     + words.spacing + implicitHeight + Theme.spaceTileGap
            width: parent.width
        }
    }

    // The selected block's mark: none in Omarchy's face (the fill says it), Console's brackets.
    SelectionMark {
        anchors.fill: parent
        anchors.margins: Theme.spaceTileGap
        cut: Theme.plateKeyCut
        objectName: "spaceTileMark"
        visible: tile.isSelected
    }

    MouseArea {
        id: pointer

        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        anchors.fill: parent
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                tile.table.forceActiveFocus();
                const point = pointer.mapToItem(tile.table, mouse.x, mouse.y);
                tile.table.contextClick(tile.tileRow, point.x, point.y);
                return;
            }
            if (mouse.button === Qt.MiddleButton) {
                tile.table.forceActiveFocus();
                tile.table.cursorIndex = tile.tileRow;
                tile.table.toggleSelection(tile.tileRow);
                return;
            }
            tile.click(mouse.modifiers);
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ShiftModifier
                                                                       | Qt.ControlModifier)))
                tile.table.activated(tile.hash);
        }
    }
}
