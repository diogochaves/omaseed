pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Files tab (spec v2 §2 Fig. 2 `.list.files`): a header row of
// `Name · Size · Done · Priority`, then the visible rows of the torrent's file tree.
// Folders carry a `▾`/`›` fold glyph and their subtree's totals; every level indents by
// `Theme.space(14)`, matching the figure's 8 / 22 / 38 px left padding.
//
// The model is already flat — TorrentFilesModel drops a folded folder's subtree — so this is
// a plain ListView. `cursorIndex` is owned by Details.qml, because the keys that move it
// belong to the pane, not to the tab.
Item {
    id: root

    required property TorrentFilesModel model
    property int cursorIndex: 0

    signal rowClicked(int row)
    signal rowActivated(int row)

    readonly property int count: root.model ? root.model.count : 0

    // The spec's `.files { grid-template-columns: 1fr 70px 56px 72px }`, trimmed to fit the
    // real docked column: spec §2's Fig. 2 is drawn in a 400 px mock, while §1 Fig. 1 docks
    // the pane at 340. The three fixed columns still hold their widest value ("81.4 GB",
    // "100 %", "maximal"); the rest goes to the name, which elides as the figure does.
    readonly property int sizeWidth: Theme.space(60)
    readonly property int doneWidth: Theme.space(48)
    readonly property int priorityWidth: Theme.space(62)

    /// Keeps the cursor row on screen when `j`/`k` walk past the edge.
    function ensureVisible() {
        list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
    }

    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    RowLayout {
        id: header

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.top: parent.top
        spacing: Theme.space(4)

        TableHeader {
            Layout.fillWidth: true
            text: qsTr("Name")
        }

        TableHeader {
            Layout.preferredWidth: root.sizeWidth
            alignRight: true
            text: qsTr("Size")
        }

        TableHeader {
            Layout.preferredWidth: root.doneWidth
            alignRight: true
            text: qsTr("Done")
        }

        TableHeader {
            Layout.preferredWidth: root.priorityWidth
            text: qsTr("Priority")
        }
    }

    ListView {
        id: list

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: root.model

        delegate: TableRow {
            id: row

            required property int index
            required property string name
            required property string sizeText
            required property string progressText
            required property int priority
            required property string priorityText
            required property bool isFolder
            required property int depth
            required property bool expanded

            readonly property color priorityColor: row.priority === 6 || row.priority === 7
                                                   ? Theme.yellow : Theme.dimForeground

            hasCursor: row.index === root.cursorIndex
            height: Theme.railRowHeight
            width: ListView.view.width

            onClicked: root.rowClicked(row.index)
            onDoubleClicked: root.rowActivated(row.index)

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(4)

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: glyph.implicitHeight
                    // Spec §2 Fig. 2 `.files .i1 { padding-left: 22px }` / `.i2 { 38px }`:
                    // one step of Theme.space(14) per level on top of the row's own padding.
                    Layout.leftMargin: row.depth * Theme.space(14)

                    Text {
                        id: glyph

                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.expanded ? Glyphs.chevronDown : Glyphs.chevronRight
                        visible: row.isFolder
                        width: row.isFolder ? implicitWidth : 0
                    }

                    Text {
                        anchors.left: row.isFolder ? glyph.right : parent.left
                        anchors.leftMargin: row.isFolder ? Theme.space(6) : 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.textColor
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: row.name
                    }
                }

                Text {
                    Layout.preferredWidth: root.sizeWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.sizeText
                }

                Text {
                    Layout.preferredWidth: root.doneWidth
                    color: row.textColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    horizontalAlignment: Text.AlignRight
                    text: row.progressText
                }

                Text {
                    Layout.preferredWidth: root.priorityWidth
                    color: row.priorityColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    // `.pri.skip { text-decoration: line-through }`.
                    font.pixelSize: Theme.font.small
                    font.strikeout: row.priority === 0
                    text: row.priorityText
                }
            }
        }
    }
}
