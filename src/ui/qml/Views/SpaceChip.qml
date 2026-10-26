pragma ComponentBehavior: Bound

import QtQuick
import Omaseed.Kit

// One chip of space's "under 1 %" line (board 51 D12 a, `.small span`; DECISIONS C32): a folder
// or a torrent too small for a block of its own, its words in a hairline. A torrent's chip is
// its block: the cursor, the selection and the clicks as a block's. A folder's holds `count`
// rows from `first`: the cursor on any of them lights it, and a click puts the cursor on its
// first, so the line over the map names the folder and "move this folder…" moves it.
Item {
    id: chip

    required property TorrentTable table
    /// The first proxy row it stands for, and how many (1 for a torrent).
    required property int first
    required property int count
    required property bool folder
    required property string text
    /// LibrarySpace's count of changes to the rows.
    required property int revision
    /// Whether it is drawn: the line has room for it (LibrarySpace's "… n more" takes the rest).
    property bool fits: true

    readonly property bool isCursor: chip.table.cursorIndex >= chip.first && chip.table.cursorIndex
                                     < chip.first + chip.count
    readonly property bool isSelected: !chip.folder && chip.table.selectionRevision >= 0
                                       && chip.table.isRowSelected(chip.first)
    readonly property ThemeInk ground: Theme.ground(chip.isSelected, chip.isCursor,
                                                    pointer.containsMouse, false)

    function click(modifiers: int): void {
        chip.table.forceActiveFocus();
        if (chip.folder) {
            chip.table.moveCursorTo(chip.first);
            return;
        }
        if (modifiers & Qt.ShiftModifier) {
            chip.table.selectRangeTo(chip.first, (modifiers & Qt.ControlModifier) !== 0);
            return;
        }
        if (modifiers & Qt.ControlModifier) {
            chip.table.cursorIndex = chip.first;
            chip.table.toggleSelection(chip.first);
            return;
        }
        const had = chip.table.selectedCount > 0;
        chip.table.cursorIndex = chip.first;
        chip.table.selectOnly(chip.first);
        chip.table.plainClicked(had);
    }

    Accessible.name: chip.text
    Accessible.role: chip.folder ? Accessible.Button : Accessible.ListItem
    Accessible.selectable: !chip.folder
    Accessible.selected: chip.isSelected
    Accessible.onPressAction: chip.click(Qt.NoModifier)
    enabled: chip.fits
    height: label.implicitHeight + 2 * Theme.spaceChipPadY
    objectName: (chip.folder ? "spaceFolderChip_" : "spaceChip_") + chip.first
    opacity: chip.fits ? 1 : 0
    width: Math.min(label.implicitWidth, Theme.spaceChipMaxWidth) + 2 * Theme.spaceChipPadX

    Plate {
        anchors.fill: parent
        edgeColor: chip.isCursor && chip.table.keysHere ? Theme.accent : Theme.hairColor
        edgeWidth: chip.isCursor && chip.table.keysHere ? Theme.spaceCursorEdge : Theme.hairWidth
        fill: chip.ground === Theme.on.window ? "transparent" : chip.ground.fill
        key: true
        objectName: "spaceChipPlate"
        radius: 0
    }

    Text {
        id: label

        color: chip.ground.foreground
        // A long name elides, so one chip never takes the whole line.
        elide: Text.ElideMiddle
        font.family: Theme.valueFamily
        font.pixelSize: Theme.spaceLineSize
        objectName: "spaceChipText"
        text: chip.text
        textFormat: Text.PlainText
        width: Math.min(label.implicitWidth, Theme.spaceChipMaxWidth)
        x: Theme.spaceChipPadX
        y: Theme.spaceChipPadY
    }

    SelectionMark {
        anchors.fill: parent
        cut: Theme.plateKeyCut
        visible: chip.isSelected
    }

    MouseArea {
        id: pointer

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton && !chip.folder) {
                chip.table.forceActiveFocus();
                const point = pointer.mapToItem(chip.table, mouse.x, mouse.y);
                chip.table.contextClick(chip.first, point.x, point.y);
                return;
            }
            chip.click(mouse.modifiers);
        }
    }
}
