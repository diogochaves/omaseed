pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The add sheet's file picker (docs/11 D1): the files of a `.torrent` before it is added, as
// the details pane's Files tab draws them (spec v2 §2 Fig. 2 `.list.files`), with a check
// mark in front of every row and no Done column, since nothing has been downloaded yet.
//
// It is one bordered box, like a Kit `Field`, and one stop in the sheet's `tab` cycle. With
// the keyboard on it, the keys are the Files tab's (Keymap area "addFiles"):
//   j k / ↑ ↓   move            space   check or uncheck the row (a folder: all of it)
//   + -         priority        x       skip
//   h l / ← →   fold, unfold (h on a file or a closed folder goes to its folder)
//   r  F2       rename the row (M18)
// `enter` and `tab` are left to the sheet. The rows scroll past `maximumHeight`.
//
// Renaming (manual box M18): `r`, F2 or the pencil on a hovered row opens a field over the
// name. `enter` keeps the name (a refused one turns the field red and says why), `esc` puts
// it back — the field takes both keys, so the sheet's own `esc` never sees them — and leaving
// the field any other way (a click elsewhere, `tab`) drops the edit too. A renamed row shows
// its original name dim after the new one. The names go to the server after the add
// (`AddFilesModel::renameList`, App::applyAddRenames).
//
// The pointer (manual box M17): a click on the check mark checks or unchecks the row (a
// folder: all of it, as `space`), on a folder's arrow folds or unfolds it, on the priority
// cycles normal → high → maximum → normal (`AddFilesModel::cyclePriority`: a folder's skipped
// files stay skipped, a skipped row comes back at normal). Each of those parts shows the
// pointing hand and the hover fill; a click anywhere else on the row only moves the cursor,
// and a double click still folds a folder or checks a file. Every click puts the cursor on
// its row and the keyboard in the tree, as a row click always did.
//
//     AddFilesTree { model: App.addFiles; maximumHeight: 300 }
Rectangle {
    id: root

    required property AddFilesModel model
    /// Which row the keyboard is on.
    property int cursorIndex: 0
    /// The tallest the box grows; past it the rows scroll behind a Kit ScrollBar.
    property real maximumHeight: Theme.space(240)

    /// The row whose name is being edited, or -1.
    property int editingRow: -1
    /// Why the name in the field was refused ("" while it is fine).
    property string renameError: ""

    readonly property int count: root.model ? root.model.count : 0
    readonly property bool editing: root.editingRow >= 0
    readonly property alias view: list
    readonly property alias scrollBar: scrollBar

    // The Files tab's columns without Done (`.files { grid-template-columns: 1fr 70px 56px
    // 72px }`), plus the check mark in front.
    readonly property int checkWidth: Theme.space(14)
    readonly property int sizeWidth: Theme.space(64)
    readonly property int priorityWidth: Theme.space(62)

    /// `j`/`k`: one row, kept on screen.
    function moveCursor(delta: int): void {
        if (root.count === 0)
            return;
        root.cursorIndex = Math.max(0, Math.min(root.count - 1, root.cursorIndex + delta));
        list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
    }

    /// `h`: close an open folder, otherwise go up to the folder the row is in.
    function fold(): void {
        const row = root.cursorIndex;
        if (root.model.isFolder(row) && root.model.isExpanded(row)) {
            root.model.setExpanded(row, false);
            return;
        }
        const depth = root.model.depthAt(row);
        for (let above = row - 1; above >= 0; --above) {
            if (root.model.depthAt(above) < depth) {
                root.cursorIndex = above;
                list.positionViewAtIndex(above, ListView.Contain);
                return;
            }
        }
    }

    /// `l`: open a closed folder, or step into an open one.
    function unfold(): void {
        const row = root.cursorIndex;
        if (!root.model.isFolder(row))
            return;
        if (!root.model.isExpanded(row))
            root.model.setExpanded(row, true);
        else
            root.moveCursor(1);
    }

    /// A click on a row's check mark, fold arrow or priority: the cursor goes to the row and
    /// the keyboard to the tree (what any row click does), then the part acts.
    function _clickPart(row: int, part: string): void {
        root.cursorIndex = row;
        root.forceActiveFocus();
        if (part === "mark")
            root.model.toggleWanted(row);
        else if (part === "fold")
            root.model.setExpanded(row, !root.model.isExpanded(row));
        else if (part === "priority")
            root.model.cyclePriority(row);
        else if (part === "rename")
            root.startRename(row);
    }

    /// `r`, F2 or the pencil: the field over `row`'s name (it focuses itself when shown).
    function startRename(row: int): void {
        if (row < 0 || row >= root.count)
            return;
        root.cursorIndex = row;
        list.positionViewAtIndex(row, ListView.Contain);
        root.renameError = "";
        root.editingRow = row;
    }

    /// `enter` in the field: keep the name, or say why not and stay in the field.
    function commitRename(name: string): bool {
        const row = root.editingRow;
        if (row < 0)
            return false;
        const refused = root.model.renameError(row, name);
        if (refused.length > 0) {
            root.renameError = refused;
            return false;
        }
        const parent = root.model.pathOf(row).split("/").slice(0, -1);
        root.model.rename(row, name);
        root.editingRow = -1;
        root.renameError = "";
        // The rename may have moved the row (the tree sorts by name): follow it.
        const moved = root.model.rowOfPath(parent.concat([name]).join("/"));
        if (moved >= 0) {
            root.cursorIndex = moved;
            list.positionViewAtIndex(moved, ListView.Contain);
        }
        root.forceActiveFocus();
        return true;
    }

    /// `esc` in the field (the keyboard comes back to the tree), or the field losing the
    /// keyboard to something else (it stays there): nothing changes.
    function cancelRename(backToTree: bool): void {
        if (root.editingRow < 0)
            return;
        root.editingRow = -1;
        root.renameError = "";
        if (backToTree)
            root.forceActiveFocus();
    }

    activeFocusOnTab: true
    border.color: root.activeFocus ? Theme.accent : hover.hovered ? Theme.fill(Theme.borderHover)
                                                                  : Theme.fill(Theme.borderIdle)
    border.width: 1
    clip: true
    color: Theme.fill(Theme.fillIdle)
    implicitHeight: Math.min(root.count * Theme.railRowHeight + 2, root.maximumHeight)
    implicitWidth: Theme.space(400)
    radius: Theme.cornerRadius

    // A folder that folds away under the cursor leaves it past the end.
    onCountChanged: root.cursorIndex = Math.max(0, Math.min(root.cursorIndex, root.count - 1))

    Keys.onPressed: event => {
        // The rename field's keys that it did not take itself (arrows and the like) climb up
        // to here: they move nothing while it is open. `tab` goes on to the sheet.
        if (root.editing) {
            event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
            return;
        }
        switch (event.key) {
        case Qt.Key_J:
        case Qt.Key_Down:
            root.moveCursor(1);
            break;
        case Qt.Key_K:
        case Qt.Key_Up:
            root.moveCursor(-1);
            break;
        case Qt.Key_Space:
            root.model.toggleWanted(root.cursorIndex);
            break;
        case Qt.Key_Plus:
        case Qt.Key_Equal:
            root.model.raise(root.cursorIndex);
            break;
        case Qt.Key_Minus:
            root.model.lower(root.cursorIndex);
            break;
        case Qt.Key_X:
            root.model.setPriority(root.cursorIndex, 0);
            break;
        case Qt.Key_H:
        case Qt.Key_Left:
            root.fold();
            break;
        case Qt.Key_L:
        case Qt.Key_Right:
            root.unfold();
            break;
        case Qt.Key_R:
        case Qt.Key_F2:
            root.startRename(root.cursorIndex);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    HoverHandler {
        id: hover
    }

    ListView {
        id: list

        anchors.fill: parent
        anchors.margins: 1
        anchors.rightMargin: scrollBar.visible ? scrollBar.width + 1 : 1
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: root.model

        delegate: TableRow {
            id: row

            required property int index
            required property string name
            required property string sizeText
            required property int priority
            required property string priorityText
            required property bool isFolder
            required property int depth
            required property bool expanded
            required property int wanted
            required property bool renamed
            required property string originalName

            readonly property color priorityColor: row.priority === 6 || row.priority === 7
                                                   ? Theme.yellow : Theme.dimForeground
            /// The pointer is anywhere on the row: the pencil shows.
            readonly property bool hovered: rowHover.containsMouse || markMouse.containsMouse
                                            || foldMouse.containsMouse
                                            || priorityMouse.containsMouse
                                            || pencilMouse.containsMouse
            readonly property bool editing: root.editingRow === row.index

            hasCursor: root.activeFocus && row.index === root.cursorIndex
            height: Theme.railRowHeight
            width: ListView.view.width

            onClicked: {
                root.cursorIndex = row.index;
                root.forceActiveFocus();
            }
            // The row's own MouseArea takes every press, so a double click is the pointer's
            // way to fold a folder or check a file.
            onDoubleClicked: {
                if (row.isFolder)
                    root.model.setExpanded(row.index, !row.expanded);
                else
                    root.model.toggleWanted(row.index);
            }

            // Hover only, for the pencil: no buttons, so every click goes on to the row's own
            // MouseArea under it. Between that and the parts (z 1), which hear the pointer
            // over themselves.
            MouseArea {
                id: rowHover

                acceptedButtons: Qt.NoButton
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                parent: row
                z: 0.5
            }

            // Above the row's own MouseArea (`parent: row`, z 1), so the check mark, the fold
            // arrow, the priority and the pencil take their clicks; anywhere else a click
            // falls through to the row.
            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                parent: row
                spacing: Theme.space(8)
                z: 1

                // ✓ nothing skipped, – some, · all (the palettes' checked/unchecked marks).
                Item {
                    Layout.preferredHeight: Theme.railRowHeight
                    Layout.preferredWidth: root.checkWidth

                    Rectangle {
                        anchors.centerIn: parent
                        color: markMouse.containsMouse ? Theme.fill(Theme.fillHover)
                                                       : "transparent"
                        height: Theme.railRowHeight - Theme.space(6)
                        radius: Theme.cornerRadius
                        width: root.checkWidth + Theme.space(6)
                    }

                    Text {
                        anchors.centerIn: parent
                        color: markMouse.containsMouse ? Theme.brightForeground
                                                       : row.wanted === 0 ? Theme.dimForeground
                                                                          : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.wanted === 2 ? Glyphs.check : row.wanted === 1 ? "–"
                                                                                 : Glyphs.middot
                    }

                    MouseArea {
                        id: markMouse

                        anchors.centerIn: parent
                        cursorShape: Qt.PointingHandCursor
                        height: Theme.railRowHeight
                        hoverEnabled: true
                        objectName: "addFilesMark"
                        width: root.checkWidth + Theme.space(6)

                        // Two quick clicks are two toggles, not the row's double click.
                        onClicked: root._clickPart(row.index, "mark")
                        onDoubleClicked: root._clickPart(row.index, "mark")
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.leftMargin: row.depth * Theme.space(14)
                    Layout.preferredHeight: foldGlyph.implicitHeight

                    Rectangle {
                        anchors.centerIn: foldGlyph
                        color: foldMouse.containsMouse ? Theme.fill(Theme.fillHover)
                                                       : "transparent"
                        height: Theme.railRowHeight - Theme.space(6)
                        radius: Theme.cornerRadius
                        visible: row.isFolder
                        width: foldGlyph.implicitWidth + Theme.space(8)
                    }

                    Text {
                        id: foldGlyph

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        color: foldMouse.containsMouse ? Theme.brightForeground
                                                       : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.expanded ? Glyphs.chevronDown : Glyphs.chevronRight
                        visible: row.isFolder
                        width: row.isFolder ? implicitWidth : 0
                    }

                    MouseArea {
                        id: foldMouse

                        anchors.horizontalCenter: foldGlyph.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        cursorShape: Qt.PointingHandCursor
                        enabled: row.isFolder
                        height: Theme.railRowHeight
                        hoverEnabled: true
                        objectName: "addFilesFold"
                        width: foldGlyph.implicitWidth + Theme.space(8)

                        onClicked: root._clickPart(row.index, "fold")
                        onDoubleClicked: root._clickPart(row.index, "fold")
                    }

                    Text {
                        id: nameText

                        anchors.left: row.isFolder ? foldGlyph.right : parent.left
                        anchors.leftMargin: row.isFolder ? Theme.space(6) : 0
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.wanted === 0 ? Theme.dimForeground : row.textColor
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.name
                        visible: !row.editing
                        // Room for the original name after a rename, never less than half.
                        width: Math.min(implicitWidth, parent.width - x
                                        - (wasText.visible ? Math.min(wasText.implicitWidth,
                                                                      (parent.width - x) / 2)
                                                             + Theme.space(8) : 0))
                    }

                    // M18: what the row was called in the .torrent.
                    Text {
                        id: wasText

                        anchors.left: nameText.right
                        anchors.leftMargin: Theme.space(8)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        objectName: "addFilesWas"
                        text: qsTr("was %1").arg(row.originalName)
                        visible: row.renamed && !row.editing
                    }

                    // The rename field, over the name while this row is being renamed.
                    Rectangle {
                        anchors.left: nameText.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        border.color: root.renameError.length > 0 ? Theme.red : Theme.accent
                        border.width: 1
                        color: Theme.background
                        height: Theme.railRowHeight - Theme.space(4)
                        radius: Theme.cornerRadius
                        visible: row.editing

                        onVisibleChanged: {
                            if (!visible)
                                return;
                            const name = root.model.plainName(row.index);
                            renameInput.text = name;
                            // A file's stem is what gets renamed most: its extension stays out
                            // of the selection, as in a file manager.
                            const dot = row.isFolder ? -1 : name.lastIndexOf(".");
                            renameInput.select(0, dot > 0 ? dot : name.length);
                            renameInput.forceActiveFocus();
                        }

                        TextInput {
                            id: renameInput

                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(6)
                            anchors.right: renameErrorText.visible ? renameErrorText.left
                                                                   : parent.right
                            anchors.rightMargin: Theme.space(6)
                            anchors.verticalCenter: parent.verticalCenter
                            clip: true
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            objectName: "addFilesRenameInput"
                            selectedTextColor: Theme.background
                            selectionColor: Theme.accent
                            selectByMouse: true

                            // Both keys stop here: the guarded sheet's `esc` and `enter`
                            // (add) never see them while a name is being typed.
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Escape) {
                                    root.cancelRename(true);
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Return
                                           || event.key === Qt.Key_Enter) {
                                    root.commitRename(renameInput.text);
                                    event.accepted = true;
                                }
                            }
                            onActiveFocusChanged: {
                                if (!activeFocus && row.editing)
                                    root.cancelRename(false);
                            }
                            onTextEdited: root.renameError = ""
                        }

                        Text {
                            id: renameErrorText

                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(6)
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.red
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.caption
                            objectName: "addFilesRenameError"
                            text: root.renameError
                            visible: root.renameError.length > 0
                            width: Math.min(implicitWidth, parent.width / 2)
                        }
                    }
                }

                // M18: the pencil on the hovered row.
                Item {
                    Layout.preferredHeight: Theme.railRowHeight
                    Layout.preferredWidth: Theme.space(18)

                    Rectangle {
                        anchors.centerIn: parent
                        color: pencilMouse.containsMouse ? Theme.fill(Theme.fillHover)
                                                         : "transparent"
                        height: Theme.railRowHeight - Theme.space(6)
                        radius: Theme.cornerRadius
                        visible: pencilText.visible
                        width: parent.width
                    }

                    Text {
                        id: pencilText

                        anchors.centerIn: parent
                        color: pencilMouse.containsMouse ? Theme.brightForeground
                                                         : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: Glyphs.rename
                        visible: row.hovered && !row.editing
                    }

                    MouseArea {
                        id: pencilMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: !row.editing
                        hoverEnabled: true
                        objectName: "addFilesPencil"

                        onClicked: root._clickPart(row.index, "rename")
                    }
                }

                Text {
                    Layout.preferredWidth: root.sizeWidth
                    color: row.wanted === 0 ? Theme.dimForeground : row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.sizeText
                }

                Item {
                    Layout.preferredHeight: Theme.railRowHeight
                    Layout.preferredWidth: root.priorityWidth

                    Rectangle {
                        anchors.fill: parent
                        anchors.bottomMargin: Theme.space(3)
                        anchors.leftMargin: -Theme.space(4)
                        anchors.topMargin: Theme.space(3)
                        color: priorityMouse.containsMouse ? Theme.fill(Theme.fillHover)
                                                           : "transparent"
                        radius: Theme.cornerRadius
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: priorityMouse.containsMouse ? Theme.brightForeground
                                                           : row.priorityColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        // `.pri.skip { text-decoration: line-through }`, as in the Files tab.
                        font.strikeout: row.priority === 0
                        text: row.priorityText
                    }

                    MouseArea {
                        id: priorityMouse

                        anchors.fill: parent
                        anchors.leftMargin: -Theme.space(4)
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        objectName: "addFilesPriority"

                        onClicked: root._clickPart(row.index, "priority")
                        onDoubleClicked: root._clickPart(row.index, "priority")
                    }
                }
            }
        }
    }

    ScrollBar {
        id: scrollBar

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.top: parent.top
        anchors.topMargin: 1
        flickable: list
        objectName: "addFilesScrollBar"
        vertical: true
    }
}
