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
// `enter` and `tab` are left to the sheet. The rows scroll past `maximumHeight`.
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

    readonly property int count: root.model ? root.model.count : 0
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

            readonly property color priorityColor: row.priority === 6 || row.priority === 7
                                                   ? Theme.yellow : Theme.dimForeground

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

            // Above the row's own MouseArea (`parent: row`, z 1), so the check mark, the fold
            // arrow and the priority take their clicks; anywhere else a click falls through to
            // the row.
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
                        anchors.left: row.isFolder ? foldGlyph.right : parent.left
                        anchors.leftMargin: row.isFolder ? Theme.space(6) : 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.wanted === 0 ? Theme.dimForeground : row.textColor
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.name
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
