pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// Space, the library's third display (board 51 D12 a, `spaceView()`; DECISIONS C32, in the
// place of C24 H3–H4's shelves): where the space goes, as a size map by folder, then torrent.
// Over the map, its line ("1.36 GB in 117 torrents · by folder, then torrent · the cursor on
// …") and the cursor's folder with "move this folder…"; the map fills the rest, each folder an
// area as big as its share (outlined and named when there is more than one), each torrent a
// block of it (SpaceTile); under it, what is under 1 % of the scope gathered in a line of chips
// (SpaceChip), folders first, "… n more" past the room. The map is laid out by board 51's
// `treemap()`: halves by size, split along the longer side. It never scrolls.
//
// A face of the table (TorrentTable `face`), as the rows display is: the same proxy, grouped by
// folder the largest first, the torrents the largest first inside each (App sets
// TorrentFilterModel::BySpace for the display), the table's cursor, selection and keys (j/k go
// block by block in that order, which is the map's). `r` opens the rename sheet.
Item {
    id: root

    required property TorrentTable table
    readonly property TorrentFilterModel filtered: root.table.filtered
    /// The table's room for what floats over the list's foot. Space never scrolls, so it keeps
    /// `reserve` free there instead, whether the bar shows or not: a click that selects never
    /// moves the map under the pointer.
    property real bottomRoom: 0
    property real reserve: 0

    /// Moves whenever the rows' words may have: a poll, a sort, rows come or go.
    property int revision: 0

    /// "move this folder…" on the cursor's folder.
    signal moveFolderRequested(string folder)

    // ---- the face the table asks (TorrentTable `face`) ----------------------------------
    readonly property real scrollY: 0
    readonly property bool hovered: spaceHover.hovered
    readonly property bool inlineRename: false

    /// What the map draws and what it gathers, whatever its size (gather()): `{total, count,
    /// big: [{group, weight, drawn: [{row, weight}]}], chips: [{first, count, folder, text}]}`.
    readonly property var gathered: root.visible ? root.gather(root.revision, root.filtered.groups) :
                                                   root.gather(0, [])
    /// The map, laid out (layOut()): the gathered, plus `folders: [{key, title, line, first,
    /// count, bytes, x, y, w, h, head, framed}]` and `tiles: [{row, x, y, w, h}]`, in `map`'s
    /// coordinates.
    readonly property var layout: root.layOut(root.gathered, map.width, map.height)

    /// The group holding the cursor, or null.
    readonly property var cursorFolder: {
        const row = root.table.cursorIndex;
        for (const group of root.filtered.groups) {
            if (row >= group.first && row < group.first + group.count)
                return group;
        }
        return null;
    }
    readonly property string cursorName: root.revision >= 0 && root.table.cursorIndex >= 0
                                         && root.table.cursorIndex < root.filtered.count
                                         ? root.filtered.data(root.filtered.index(
                                                                  root.table.cursorIndex, 0),
                                                              TorrentModel.NameRole) ?? "" : ""

    /// Board 51 `treemap()`: `items` ({weight}) into the rectangle, halves by weight along the
    /// longer side, in their order; each laid out item is pushed onto `out` with its x, y, w, h.
    function treemap(items: var, x: real, y: real, w: real, h: real, out: var): void {
        if (items.length === 0)
            return;
        if (items.length === 1) {
            out.push({
                         "item": items[0],
                         "x": x,
                         "y": y,
                         "w": w,
                         "h": h
                     });
            return;
        }
        const total = items.reduce((sum, item) => sum + item.weight, 0);
        let acc = 0;
        let i = 0;
        for (; i < items.length - 1; ++i) {
            if (acc + items[i].weight > total / 2 && i > 0)
                break;
            acc += items[i].weight;
        }
        const f = total > 0 ? acc / total : 0.5;
        const a = items.slice(0, i);
        const b = items.slice(i);
        if (w >= h) {
            root.treemap(a, x, y, w * f, h, out);
            root.treemap(b, x + w * f, y, w * (1 - f), h, out);
        } else {
            root.treemap(a, x, y, w, h * f, out);
            root.treemap(b, x, y + h * f, w, h * (1 - f), out);
        }
    }

    /// Which folders and torrents get an area (the "big"), and the chips of the rest: a folder
    /// under `Theme.spaceSmallShare` of the scope's size, a torrent under that share of its
    /// folder's (a folder of many like-sized torrents keeps them all as blocks; a speck beside
    /// a big one goes to the line). `revision` is only read, for a binding.
    function gather(revision: int, groups: var): var {
        const total = groups.reduce((sum, group) => sum + Math.max(0, group.bytes), 0);
        const floor = total * Theme.spaceSmallShare;
        const big = [];
        const smallFolders = [];
        const smallTorrents = [];
        let count = 0;
        for (const group of groups) {
            count += group.count;
            const bytes = Math.max(0, group.bytes);
            if (total <= 0 || bytes < floor) {
                smallFolders.push(group);
                continue;
            }
            const drawn = [];
            let weight = 0;
            for (let row = group.first; row < group.first + group.count; ++row) {
                const size = Math.max(0, root.filtered.data(root.filtered.index(row, 0),
                                                            TorrentModel.SizeRole) ?? 0);
                if (size < bytes * Theme.spaceSmallShare) {
                    smallTorrents.push(row);
                } else {
                    drawn.push({
                                   "row": row,
                                   "weight": size
                               });
                    weight += size;
                }
            }
            big.push({
                         "group": group,
                         "weight": weight,
                         "drawn": drawn
                     });
        }

        // Under 1 %: the folders (a lone torrent's folder as that torrent), then the torrents.
        const chips = [];
        for (const group of smallFolders) {
            if (group.count === 1) {
                chips.push({
                               "first": group.first,
                               "count": 1,
                               "folder": false,
                               "text": root.torrentWords(group.first)
                           });
                continue;
            }
            const parts = [group.title, String(group.count)];
            if (group.bytes > 0)
                parts.push(App.sizeText(group.bytes));
            chips.push({
                           "first": group.first,
                           "count": group.count,
                           "folder": true,
                           "text": parts.join(" · ")
                       });
        }
        for (const row of smallTorrents)
            chips.push({
                           "first": row,
                           "count": 1,
                           "folder": false,
                           "text": root.torrentWords(row)
                       });
        return {
            "total": total,
            "count": count,
            "big": big,
            "chips": chips
        };
    }

    /// `gathered`'s areas in a map `width` by `height`.
    function layOut(gathered: var, width: real, height: real): var {
        const big = gathered.big;
        const placed = [];
        if (width > 0 && height > 0)
            root.treemap(big, 0, 0, width, height, placed);
        const framed = placed.length > 1;
        const folders = [];
        const tiles = [];
        for (const area of placed) {
            const group = area.item.group;
            // More than one folder: each is outlined, its words in a head over its blocks.
            const gap = framed ? Theme.spaceFolderGap / 2 : 0;
            const fx = area.x + gap;
            const fy = area.y + gap;
            const fw = Math.max(0, area.w - 2 * gap);
            const fh = Math.max(0, area.h - 2 * gap);
            const head = framed && fh >= 2 * Theme.spaceFolderHead ? Theme.spaceFolderHead : 0;
            folders.push({
                             "key": group.key,
                             "title": group.title,
                             "line": group.line,
                             "first": group.first,
                             "count": group.count,
                             "bytes": group.bytes,
                             "x": fx,
                             "y": fy,
                             "w": fw,
                             "h": fh,
                             "head": head,
                             "framed": framed
                         });
            const inset = framed ? Theme.hairWidth : 0;
            const out = [];
            root.treemap(area.item.drawn, fx + inset, fy + head + inset, fw - 2 * inset, fh - head
                         - 2 * inset, out);
            for (const tile of out)
                tiles.push({
                               "row": tile.item.row,
                               "x": tile.x,
                               "y": tile.y,
                               "w": tile.w,
                               "h": tile.h
                           });
        }
        return {
            "total": gathered.total,
            "count": gathered.count,
            "folders": folders,
            "tiles": tiles,
            "chips": gathered.chips
        };
    }

    /// A torrent chip's words: "tiny-notes.txt · 4 KB".
    function torrentWords(row: int): string {
        const at = root.filtered.index(row, 0);
        const name = root.filtered.data(at, TorrentModel.NameRole) ?? "";
        const size = root.filtered.data(at, TorrentModel.SizeRole) ?? 0;
        return size > 0 ? qsTr("%1 · %2").arg(name).arg(App.sizeText(size)) : name;
    }

    function tileOf(row: int): var {
        for (const tile of root.layout.tiles) {
            if (tile.row === row)
                return tile;
        }
        return null;
    }

    /// The chip standing for `row` (its own, or its folder's), when drawn, else null.
    function chipOf(row: int): Item {
        for (let i = 0; i < chipRepeater.count; ++i) {
            const chip = chipRepeater.itemAt(i) as SpaceChip;
            if (chip !== null && row >= chip.first && row < chip.first + chip.count)
                return chip.fits ? chip : null;
        }
        return null;
    }

    function positionAt(row: int): void {
        // Nothing to scroll: every block is in view, and every chip that fits.
    }

    function rowInView(row: int): bool {
        return root.tileOf(row) !== null || root.chipOf(row) !== null;
    }

    function rowAnchor(row: int): point {
        const tile = root.tileOf(row);
        if (tile !== null)
            return map.mapToItem(root, tile.x, tile.y + tile.h);
        const chip = root.chipOf(row);
        if (chip !== null)
            return chip.mapToItem(root, 0, chip.height);
        return Qt.point(Theme.libraryMenuInset, root.height / 2);
    }

    function rowAt(x: real, y: real): int {
        const at = root.mapToItem(map, x, y);
        for (const tile of root.layout.tiles) {
            if (at.x >= tile.x && at.x < tile.x + tile.w && at.y >= tile.y && at.y < tile.y
                    + tile.h)
                return tile.row;
        }
        for (let i = 0; i < chipRepeater.count; ++i) {
            const chip = chipRepeater.itemAt(i) as SpaceChip;
            if (chip === null || chip.folder || !chip.fits)
                continue;
            const inChip = root.mapToItem(chip, x, y);
            if (inChip.x >= 0 && inChip.x < chip.width && inChip.y >= 0 && inChip.y < chip.height)
                return chip.first;
        }
        return -1;
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

    HoverHandler {
        id: spaceHover
    }

    // Off the blocks: a click clears the selection.
    // a11y-ignore A2: empty space, not a control; esc clears the selection too (#77)
    MouseArea {
        anchors.fill: parent
        objectName: "librarySpaceEmpty"

        onClicked: {
            root.table.forceActiveFocus();
            root.table.clearSelection();
        }
    }

    // The line over the map (`.sh`): what it shows, and where the cursor is.
    Row {
        id: headLine

        objectName: "spaceHead"
        spacing: Theme.spaceLineGap
        width: parent.width - 2 * Theme.spaceInset
        x: Theme.spaceInset
        y: Theme.spaceInsetTop

        Text {
            id: headWord

            color: Theme.brightForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceHeadSize
            font.weight: Theme.spaceHeadWeight
            objectName: "spaceHeadWord"
            text: qsTr("Where the space goes")
            textFormat: Text.PlainText

            Accessible.name: headWord.text
            Accessible.role: Accessible.Heading
        }

        Text {
            anchors.baseline: headWord.baseline
            color: Theme.dimForeground
            elide: Text.ElideRight
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceLineSize
            objectName: "spaceHeadLine"
            text: {
                const parts = [root.layout.count === 1 ? qsTr("%1 in 1 torrent").arg(App.sizeText(
                                                                                         root.layout.total)) :
                                                         qsTr("%1 in %2 torrents").arg(App.sizeText(
                                                                                           root.layout.total)).arg(
                                                             root.layout.count), qsTr(
                                   "by folder, then torrent")];
                if (root.cursorName.length > 0)
                    parts.push(qsTr("the cursor on %1").arg(root.cursorName));
                return parts.join(" · ");
            }
            textFormat: Text.PlainText
            width: Math.max(0, headLine.width - headWord.width - headLine.spacing)
        }
    }

    // The cursor's folder, and "move this folder…" (board 51's second `.sh`).
    Item {
        id: folderLine

        height: folderWords.implicitHeight
        objectName: "spaceFolderLine"
        width: parent.width - 2 * Theme.spaceInset
        x: Theme.spaceInset
        y: headLine.y + headLine.height + Theme.spaceGap

        Text {
            id: folderWords

            color: Theme.dimForeground
            elide: Text.ElideMiddle
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceLineSize
            objectName: "spaceFolderWords"
            text: root.cursorFolder !== null ? qsTr("%1 · %2").arg(root.cursorFolder.title).arg(
                                                   root.cursorFolder.line) : ""
            textFormat: Text.PlainText
            width: Math.max(0, folderLine.width - moveFolder.width - Theme.spaceLineGap)
        }

        Text {
            id: moveFolder

            Accessible.name: moveFolder.text
            Accessible.role: Accessible.Button
            Accessible.onPressAction: root.moveFolderRequested(root.cursorFolder.key)
            anchors.right: parent.right
            color: Theme.accent
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceLineSize
            font.underline: moveFolderPointer.containsMouse
            objectName: "spaceMoveFolder"
            text: qsTr("move this folder…")
            textFormat: Text.PlainText
            visible: root.cursorFolder !== null && root.cursorFolder.key.length > 0

            MouseArea {
                id: moveFolderPointer

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: root.moveFolderRequested(root.cursorFolder.key)
            }
        }
    }

    // The map: the folders' frames, then the blocks.
    Item {
        id: map

        height: Math.max(0, smallLine.y - Theme.spaceGap - map.y)
        objectName: "spaceMap"
        width: parent.width - 2 * Theme.spaceInset
        x: Theme.spaceInset
        y: folderLine.y + folderLine.height + Theme.spaceGap

        Accessible.name: qsTr("torrents by size")
        Accessible.role: Accessible.List

        Repeater {
            model: root.layout.folders

            Item {
                id: frame

                required property var modelData
                required property int index

                height: frame.modelData.h
                objectName: "spaceFolder_" + frame.index
                visible: frame.modelData.framed
                width: frame.modelData.w
                x: frame.modelData.x
                y: frame.modelData.y

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: frame.modelData.head
                    border.color: Theme.hairColor
                    border.width: Theme.hairWidth
                    color: "transparent"
                }

                // Its words over its blocks; a click puts the cursor on its first.
                Text {
                    color: Theme.dimForeground
                    elide: Text.ElideMiddle
                    font.family: Theme.valueFamily
                    font.letterSpacing: Theme.spaceFolderLabelTracking
                    font.pixelSize: Theme.spaceFolderLabelSize
                    height: frame.modelData.head
                    objectName: "spaceFolderLabel"
                    text: qsTr("%1 · %2").arg(frame.modelData.title).arg(App.sizeText(
                                                                             frame.modelData.bytes))
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                    visible: frame.modelData.head > 0
                    width: parent.width

                    Accessible.name: text
                    Accessible.role: Accessible.Button
                    Accessible.onPressAction: root.table.moveCursorTo(frame.modelData.first)

                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            root.table.forceActiveFocus();
                            root.table.moveCursorTo(frame.modelData.first);
                        }
                    }
                }
            }
        }

        Repeater {
            model: root.layout.tiles

            SpaceTile {
                required property var modelData

                height: modelData.h
                revision: root.revision
                table: root.table
                tileRow: modelData.row
                width: modelData.w
                x: modelData.x
                y: modelData.y
            }
        }
    }

    // Under 1 % (`.small`): the label, the chips that fit, "… n more".
    Item {
        id: smallLine

        /// How many chips the line has no room for.
        readonly property int hidden: {
            let count = 0;
            for (let i = 0; i < chipRepeater.count; ++i) {
                const chip = chipRepeater.itemAt(i) as SpaceChip;
                if (chip !== null && !chip.fits)
                    ++count;
            }
            return count;
        }
        /// The right end of the last chip that fits.
        readonly property real fitEnd: {
            let end = 0;
            for (let i = 0; i < chipRepeater.count; ++i) {
                const chip = chipRepeater.itemAt(i) as SpaceChip;
                if (chip !== null && chip.fits)
                    end = chip.x + chip.width;
            }
            return end;
        }

        height: root.gathered.chips.length > 0 ? chips.height : 0
        objectName: "spaceSmall"
        visible: root.gathered.chips.length > 0
        width: parent.width - 2 * Theme.spaceInset
        x: Theme.spaceInset
        y: root.height - Math.max(root.reserve, Theme.spaceInsetBottom) - smallLine.height

        Text {
            id: smallLabel

            anchors.verticalCenter: chips.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceLineSize
            objectName: "spaceSmallLabel"
            text: qsTr("under 1 %:")
            textFormat: Text.PlainText
        }

        Row {
            id: chips

            readonly property real room: smallLine.width - chips.x - moreWords.implicitWidth - 2
                                         * Theme.spaceChipPadX - Theme.spaceChipGap

            spacing: Theme.spaceChipGap
            x: smallLabel.width + Theme.spaceChipGap

            Repeater {
                id: chipRepeater

                model: root.gathered.chips

                SpaceChip {
                    required property var modelData

                    count: modelData.count
                    first: modelData.first
                    fits: x + width <= chips.room
                    folder: modelData.folder
                    revision: root.revision
                    table: root.table
                    text: modelData.text
                }
            }
        }

        Text {
            id: moreWords

            anchors.verticalCenter: chips.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.valueFamily
            font.pixelSize: Theme.spaceLineSize
            objectName: "spaceMore"
            text: qsTr("… %1 more").arg(smallLine.hidden)
            textFormat: Text.PlainText
            visible: smallLine.hidden > 0
            x: chips.x + smallLine.fitEnd + Theme.spaceChipGap + Theme.spaceChipPadX
        }
    }
}
