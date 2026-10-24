pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The add sheet's batch (#40, docs/27 B1/B2): every torrent that arrives together, one row
// each, added in one go under the sheet's one set of options. A row reads the name, a flag
// ("113 files", "magnet · size after metadata", "in the library · left out", "twice in this
// list · left out", "reading…"), the size and a `✕` that drops it. A row left out is dim.
//
// One bordered box like the sheet's other lists, and one stop in its `tab` cycle. With the
// keyboard on it (Keymap area "addBatch"):
//   j k / ↑ ↓   move            d  delete   drop the row
// Past eight rows the list scrolls.
//
//     AddBatchList { rows: sheet.batchRows; onRemoveRequested: index => sheet.removeFromBatch(index) }
Rectangle {
    id: root

    /// `AddSheet.batchRows`: `name`, `flag`, `warn` (the flag in yellow), `sizeText`, `out`.
    required property var rows
    property int cursorIndex: 0

    readonly property int count: root.rows ? root.rows.length : 0
    readonly property int sizeWidth: Theme.space(70)
    readonly property alias view: list

    /// A row's `✕`, or `d` on the cursor row.
    signal removeRequested(int index)

    /// `j`/`k`: one row, clamped and kept on screen.
    function moveCursor(delta: int): void {
        if (root.count === 0)
            return;
        root.cursorIndex = Math.max(0, Math.min(root.count - 1, root.cursorIndex + delta));
        list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
    }

    activeFocusOnTab: true
    border.color: root.activeFocus ? Theme.accent : hover.hovered ? Theme.fill(Theme.borderHover)
                                                                  : Theme.fill(Theme.borderIdle)
    border.width: 1
    clip: true
    color: Theme.on.idle.fill
    implicitHeight: Math.min(root.count, 8) * Theme.railRowHeight + 2
    implicitWidth: Theme.space(400)
    radius: Theme.cornerRadius

    // A row dropped at the end leaves the cursor past it.
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
        case Qt.Key_D:
        case Qt.Key_Delete:
            if (root.count > 0)
                root.removeRequested(root.cursorIndex);
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

        // #77: the batch's list, for a screen reader; each row is a list item.
        Accessible.name: qsTr("torrents to add")
        Accessible.role: Accessible.List
        anchors.fill: parent
        anchors.margins: 1
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: root.rows

        delegate: TableRow {
            id: row

            required property int index
            required property var modelData

            readonly property bool out: row.modelData.out === true
            /// docs/DECISIONS.md C30 F1: the row's words on the list's 4 % block, and under the
            /// cursor on the hover fill over it (the band's own `ink` is over the window).
            readonly property Ink blockInk: Ink {
                ground: Theme.groundIn(Theme.on.idle, false, row.hasCursor, false, false)
            }

            // #77: a screen reader finds the torrent by its name; the flag says the rest.
            Accessible.description: row.modelData.flag ?? ""
            Accessible.name: row.modelData.name
            hasCursor: root.activeFocus && row.index === root.cursorIndex
            height: Theme.railRowHeight
            objectName: "batchRow" + row.index
            width: ListView.view.width

            onClicked: {
                root.cursorIndex = row.index;
                root.forceActiveFocus();
            }

            // Above the row's own MouseArea (`parent: row`, z 1), so the `✕` takes its click.
            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(10)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                parent: row
                spacing: Theme.space(10)
                z: 1

                Text {
                    Layout.fillWidth: true
                    color: row.out ? row.blockInk.dimForeground : row.blockInk.foreground
                    elide: Text.ElideMiddle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "batchName"
                    text: row.modelData.name
                }

                Text {
                    color: row.modelData.warn === true ? row.blockInk.yellow
                                                       : row.blockInk.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.caption
                    objectName: "batchFlag"
                    text: row.modelData.flag
                    visible: text.length > 0
                }

                Text {
                    Layout.preferredWidth: root.sizeWidth
                    color: row.out ? row.blockInk.dimForeground : row.blockInk.foreground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.sizeText
                }

                Item {
                    id: removeButton

                    // #77: the ✕ is a "remove" button; its press is the click, and does
                    // nothing while disabled.
                    Accessible.name: qsTr("remove")
                    Accessible.role: Accessible.Button
                    Accessible.onPressAction: if (removeButton.enabled)
                                                  root.removeRequested(row.index)
                    Layout.preferredHeight: Theme.railRowHeight
                    Layout.preferredWidth: Theme.space(18)

                    Rectangle {
                        anchors.centerIn: parent
                        color: removeMouse.containsMouse ? Theme.on.blockHover.fill : "transparent"
                        height: Theme.railRowHeight - Theme.space(6)
                        radius: Theme.cornerRadius
                        width: parent.width
                    }

                    Text {
                        anchors.centerIn: parent
                        color: removeMouse.containsMouse ? Theme.on.blockHover.brightForeground
                                                         : row.blockInk.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: Glyphs.close
                    }

                    MouseArea {
                        id: removeMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        objectName: "batchRemove"

                        onClicked: root.removeRequested(row.index)
                    }
                }
            }
        }
    }

    ScrollBar {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.top: parent.top
        anchors.topMargin: 1
        flickable: list
        vertical: true
    }
}
