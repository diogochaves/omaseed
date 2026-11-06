pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The add sheet's "in Downloads" (manual box M7): the newest `.torrent` files in the user's
// Downloads folder (`App.downloadsTorrents`, at most five), so the one the browser just saved
// is one click away. Each row reads name, then "in library" when the library already holds
// that torrent (it can still be chosen: the add then leads to Fig. 10's duplicate sheet),
// then its size and when the file arrived.
//
// One bordered box, like the file tree under it, and one stop in the sheet's `tab` cycle.
// With the keyboard on it (Keymap area "addDownloads"):
//   j k / ↑ ↓   move            enter   use this file (`chosen(path)`)
//   space       tick the row (#40, docs/27 B3); with ticks, `enter` hands over the ticked
//               files together (`chosenMany(paths)`), for the sheet's batch
// A click on a row chooses it too, or, with ticks, adds it to them and hands them all over;
// ctrl+click ticks or unticks a row, shift+click ticks the rows from the last one clicked (or
// ticked) to this one instead of what was ticked, ctrl+shift+click adds them to it. `tab` is
// left to the sheet.
//
//     AddDownloadsList { torrents: App.downloadsTorrents; onChosen: path => … }
Rectangle {
    id: root

    /// `App.downloadsTorrents`' entries: `path`, `name`, `sizeText`, `ageText`, `inLibrary`.
    required property var torrents
    /// Which row the keyboard is on.
    property int cursorIndex: 0
    /// The ticked rows' paths (#40).
    property var ticked: []
    /// Where a shift+click range starts: the row last clicked or ticked; -1 for the cursor.
    property int anchorIndex: -1

    readonly property int count: root.torrents ? root.torrents.length : 0
    readonly property int sizeWidth: Theme.space(64)
    readonly property int ageWidth: Theme.space(110)

    /// A row was chosen: the `.torrent` file's path.
    signal chosen(string path)
    /// Several were, through the ticks: their paths, in the list's order.
    signal chosenMany(var paths)

    /// `space`, or ctrl+click: tick the row, or untick it.
    function toggleTick(index: int): void {
        if (index < 0 || index >= root.count)
            return;
        root.cursorIndex = index;
        root.anchorIndex = index;
        const path = root.torrents[index].path;
        root.ticked = root.ticked.indexOf(path) >= 0 ? root.ticked.filter(p => p !== path)
                                                     : root.ticked.concat([path]);
    }

    /// shift+click: tick the rows from the anchor to `index`, instead of the ticks there were
    /// or, with `add` (ctrl+shift+click), as well as them. The anchor stays where it was.
    function tickRange(index: int, add: bool): void {
        if (index < 0 || index >= root.count)
            return;
        const anchor = root.anchorIndex >= 0 && root.anchorIndex < root.count ? root.anchorIndex
                                                                               : root.cursorIndex;
        const from = Math.min(anchor, index);
        const to = Math.max(anchor, index);
        const range = root.torrents.slice(from, to + 1).map(t => t.path);
        const kept = add ? root.ticked.filter(p => range.indexOf(p) < 0) : [];
        root.ticked = kept.concat(range);
        root.cursorIndex = index;
    }

    /// The ticked files plus `extra` (a clicked row), in the list's order.
    function _tickedWith(extra: string): var {
        return root.torrents.map(t => t.path).filter(p => p === extra || root.ticked.indexOf(p) >= 0);
    }

    /// The sheet's `add` with ticks: the ticked files, in the list's order, and no ticks left.
    function takeTicked(): var {
        const paths = root._tickedWith("");
        root.ticked = [];
        return paths;
    }

    /// `j`/`k`: one row, clamped to the list.
    function moveCursor(delta: int): void {
        if (root.count === 0)
            return;
        root.cursorIndex = Math.max(0, Math.min(root.count - 1, root.cursorIndex + delta));
    }

    /// `enter`, or a click: hand the row's file to the sheet; with ticks, hand over the
    /// ticked ones (a click adds its own row to them).
    function choose(index: int, clicked = false): void {
        if (index < 0 || index >= root.count)
            return;
        root.cursorIndex = index;
        const path = root.torrents[index].path;
        if (root.ticked.length > 0) {
            const paths = root._tickedWith(clicked === true ? path : "");
            root.ticked = [];
            if (paths.length > 1) {
                root.chosenMany(paths);
                return;
            }
            if (paths.length === 1) {
                root.chosen(paths[0]);
                return;
            }
        }
        root.chosen(path);
    }

    activeFocusOnTab: true
    border.color: root.activeFocus ? Theme.accent : hover.hovered ? Theme.fill(Theme.borderHover)
                                                                  : Theme.fill(Theme.borderIdle)
    border.width: Theme.hairWidth
    clip: true
    color: Theme.on.idle.fill
    implicitHeight: root.count * Theme.railRowHeight + 2
    implicitWidth: Theme.space(400)
    radius: Theme.cornerRadius

    // A rescan with fewer files leaves the cursor past the end.
    onCountChanged: {
        root.cursorIndex = Math.max(0, Math.min(root.cursorIndex, root.count - 1));
        if (root.anchorIndex >= root.count)
            root.anchorIndex = -1;
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_J:
        case Qt.Key_Down:
            root.moveCursor(1);
            break;
        case Qt.Key_K:
        case Qt.Key_Up:
            root.moveCursor(-1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            // Taken here, or the sheet would run `add` on an empty field.
            root.choose(root.cursorIndex);
            break;
        case Qt.Key_Space:
            root.toggleTick(root.cursorIndex);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    HoverHandler {
        id: hover
    }

    Column {
        // #77: the files' list, for a screen reader; each row is a list item, selected while
        // ticked.
        Accessible.name: qsTr("in Downloads")
        Accessible.role: Accessible.List
        anchors.fill: parent
        anchors.margins: 1

        Repeater {
            model: root.torrents

            delegate: TableRow {
                id: row

                required property int index
                required property var modelData

                readonly property bool inLibrary: row.modelData.inLibrary === true
                readonly property bool isTicked: root.ticked.indexOf(row.modelData.path) >= 0
                /// docs/DECISIONS.md C30 F1: the row's words on the list's 4 % block, the hover
                /// fill over it under the cursor, the selected fills while ticked (the band's
                /// own `ink` is over the window).
                readonly property Ink blockInk: Ink {
                    ground: Theme.groundIn(Theme.on.idle, row.selected, row.hasCursor, false, false)
                }
                readonly property color wordColor: row.selected ? row.blockInk.brightForeground
                                                                : row.blockInk.foreground

                /// A click on the row with `modifiers`: the pointer's, and a screen reader's
                /// press (#77), which is a plain one.
                function click(modifiers: int): void {
                    const ctrl = (modifiers & Qt.ControlModifier) !== 0;
                    if ((modifiers & Qt.ShiftModifier) !== 0) {
                        root.tickRange(row.index, ctrl);
                    } else if (ctrl) {
                        root.toggleTick(row.index);
                    } else {
                        root.anchorIndex = row.index;
                        root.choose(row.index, true);
                    }
                }

                // #77: a screen reader finds the file by its torrent's name.
                Accessible.name: row.modelData.name
                hasCursor: root.activeFocus && row.index === root.cursorIndex
                height: Theme.railRowHeight
                objectName: "downloadsRow" + row.index
                selected: row.isTicked
                width: parent ? parent.width : 0

                onClicked: modifiers => row.click(modifiers)

                // The row's own click carries no modifiers: this one, above it, reads ctrl.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    parent: row
                    z: 2

                    onClicked: mouse => row.click(mouse.modifiers)
                }

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(8)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(8)

                    Text {
                        Layout.preferredWidth: Theme.space(10)
                        color: row.blockInk.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        objectName: "downloadsTick"
                        text: row.isTicked ? Glyphs.check : ""
                    }

                    Text {
                        Layout.fillWidth: true
                        color: row.inLibrary ? row.blockInk.dimForeground : row.wordColor
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.modelData.name
                    }

                    Text {
                        color: row.blockInk.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        objectName: "downloadsInLibrary"
                        text: qsTr("in library")
                        visible: row.inLibrary
                    }

                    Text {
                        Layout.preferredWidth: root.sizeWidth
                        color: row.inLibrary ? row.blockInk.dimForeground : row.wordColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignRight
                        text: row.modelData.sizeText
                    }

                    Text {
                        Layout.preferredWidth: root.ageWidth
                        color: row.blockInk.dimForeground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignRight
                        text: row.modelData.ageText
                    }
                }
            }
        }
    }
}
