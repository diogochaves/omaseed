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
// A click on a row chooses it too. `tab` is left to the sheet.
//
//     AddDownloadsList { torrents: App.downloadsTorrents; onChosen: path => … }
Rectangle {
    id: root

    /// `App.downloadsTorrents`' entries: `path`, `name`, `sizeText`, `ageText`, `inLibrary`.
    required property var torrents
    /// Which row the keyboard is on.
    property int cursorIndex: 0

    readonly property int count: root.torrents ? root.torrents.length : 0
    readonly property int sizeWidth: Theme.space(64)
    readonly property int ageWidth: Theme.space(110)

    /// A row was chosen: the `.torrent` file's path.
    signal chosen(string path)

    /// `j`/`k`: one row, clamped to the list.
    function moveCursor(delta: int): void {
        if (root.count === 0)
            return;
        root.cursorIndex = Math.max(0, Math.min(root.count - 1, root.cursorIndex + delta));
    }

    /// `enter`, or a click: hand the row's file to the sheet.
    function choose(index: int): void {
        if (index < 0 || index >= root.count)
            return;
        root.cursorIndex = index;
        root.chosen(root.torrents[index].path);
    }

    activeFocusOnTab: true
    border.color: root.activeFocus ? Theme.accent : hover.hovered ? Theme.fill(Theme.borderHover)
                                                                  : Theme.fill(Theme.borderIdle)
    border.width: 1
    clip: true
    color: Theme.fill(Theme.fillIdle)
    implicitHeight: root.count * Theme.railRowHeight + 2
    implicitWidth: Theme.space(400)
    radius: Theme.cornerRadius

    // A rescan with fewer files leaves the cursor past the end.
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
        case Qt.Key_Return:
        case Qt.Key_Enter:
            // Taken here, or the sheet would run `add` on an empty field.
            root.choose(root.cursorIndex);
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
        anchors.fill: parent
        anchors.margins: 1

        Repeater {
            model: root.torrents

            delegate: TableRow {
                id: row

                required property int index
                required property var modelData

                readonly property bool inLibrary: row.modelData.inLibrary === true

                hasCursor: root.activeFocus && row.index === root.cursorIndex
                height: Theme.railRowHeight
                objectName: "downloadsRow" + row.index
                width: parent ? parent.width : 0

                onClicked: root.choose(row.index)

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space(8)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space(8)

                    Text {
                        Layout.fillWidth: true
                        color: row.inLibrary ? Theme.dimForeground : row.textColor
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.modelData.name
                    }

                    Text {
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.caption
                        objectName: "downloadsInLibrary"
                        text: qsTr("in library")
                        visible: row.inLibrary
                    }

                    Text {
                        Layout.preferredWidth: root.sizeWidth
                        color: row.inLibrary ? Theme.dimForeground : row.textColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignRight
                        text: row.modelData.sizeText
                    }

                    Text {
                        Layout.preferredWidth: root.ageWidth
                        color: Theme.dimForeground
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
