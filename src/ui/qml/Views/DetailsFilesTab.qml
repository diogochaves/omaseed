pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Files tab (spec v2 §2 Fig. 2 `.list.files`, UX-NOTES N1): an optional
// filter field, a header row that sorts on a click, then the visible rows of the torrent's
// file tree. Folders carry a `▾`/`›` fold glyph and their subtree's totals; every level
// indents by `Theme.space(14)`, matching the figure's 8 / 22 / 38 px left padding.
//
// The model is already flat — TorrentFilesModel drops a folded folder's subtree, a filtered
// out file and sorts siblings — so this is a plain ListView. `cursorIndex` is owned by
// Details.qml, because the keys that move it belong to the pane, not to the tab; so do the
// menus, which cover the whole pane.
//
// The columns fit the room (docs/11 J1). Docked on the right the pane shows the figure's
// four — Name, Size, Done (a progress line and its percent), Priority — and at the bottom,
// or dragged wider, it adds Remaining, Availability and the piece range, in that order.
Item {
    id: root

    required property TorrentFilesModel model
    property int cursorIndex: 0
    /// The row being renamed (`r`), or -1; the text lives in `renameText`.
    property int renamingRow: -1
    property string renameText: ""
    /// `/` was pressed: show the filter field even while it is still empty.
    property bool filterOpen: false

    signal rowClicked(int row)
    signal rowActivated(int row)
    /// A right click on a row: the pane opens its menu there (in this item's coordinates).
    signal rowMenuRequested(int row, real x, real y)
    signal renameCommitted(string text)
    signal renameCancelled
    /// The filter field let go of the keyboard (`enter`, `esc`, `↓`).
    signal filterDone

    readonly property int count: root.model ? root.model.count : 0
    readonly property bool filterShown: root.filterOpen || (root.model !== null
                                                           && root.model.filterText.length > 0)
    /// The caret is in the filter field.
    readonly property bool filterEditing: filterField.editing

    // The spec's `.files { grid-template-columns: 1fr 70px 56px 72px }`, trimmed to fit the
    // real docked column (spec §2's Fig. 2 is a 400 px mock). Each fixed column holds its
    // widest value ("81.4 GB", "100 %", "maximal", "3860–3911").
    readonly property int sizeWidth: Theme.space(60)
    readonly property int doneWidth: root.compact ? Theme.space(46) : Theme.space(86)
    readonly property int priorityWidth: Theme.space(58)
    readonly property int remainingWidth: Theme.space(60)
    readonly property int availabilityWidth: Theme.space(50)
    readonly property int piecesWidth: Theme.space(78)

    /// Dragged narrower than the default: the progress line goes, its percent stays.
    readonly property bool compact: root.width < Theme.space(360)
    readonly property bool showRemaining: root.width >= Theme.space(480)
    readonly property bool showAvailability: root.width >= Theme.space(560)
    readonly property bool showPieces: root.width >= Theme.space(660)

    /// Keeps the cursor row on screen when `j`/`k` walk past the edge.
    function ensureVisible() {
        list.positionViewAtIndex(root.cursorIndex, ListView.Contain);
    }

    /// Where a menu for `row` should open, in `target`'s coordinates: under the row's name.
    function menuPoint(row: int, target: Item): point {
        const item = list.itemAtIndex(row);
        if (item === null)
            return root.mapToItem(target, Theme.space(16), list.y);
        return item.mapToItem(target, Theme.space(24), item.height);
    }

    function focusFilter(): void {
        root.filterOpen = true;
        filterField.focusInput();
    }

    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    // A rename opened on a row that then scrolls or refreshes away is dropped.
    onCountChanged: {
        if (root.renamingRow >= root.count)
            root.renameCancelled();
    }

    Field {
        id: filterField

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.top: parent.top
        anchors.topMargin: root.filterShown ? Theme.space(6) : 0
        height: root.filterShown ? Theme.controlHeight : 0
        hint: Glyphs.search
        placeholder: qsTr("filter files")
        trailing: root.model && root.model.filterText.length > 0 ? qsTr("%1 of %2").arg(
                                                                        root.model.matchCount).arg(
                                                                        root.model.fileCount) : ""
        visible: root.filterShown

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                root.filterDone();
                event.accepted = true;
            }
        }
        onAccepted: root.filterDone()
        onCleared: {
            root.filterOpen = false;
            root.filterDone();
        }
        onEditingChanged: {
            if (!filterField.editing && filterField.text.length === 0)
                root.filterOpen = false;
        }
        onTextChanged: {
            if (root.model && root.model.filterText !== filterField.text)
                root.model.filterText = filterField.text;
        }

        // A new torrent clears the model's filter; the field follows.
        Connections {
            function onFilterTextChanged(): void {
                if (filterField.text !== root.model.filterText)
                    filterField.text = root.model.filterText;
            }

            target: root.model
        }
    }

    RowLayout {
        id: header

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.top: filterField.bottom
        anchors.topMargin: root.filterShown ? Theme.space(4) : 0
        spacing: Theme.space(8)

        DetailsHeader {
            Layout.fillWidth: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_name"
            sorted: root.model !== null && root.model.sortKey === "name"
            text: qsTr("Name")

            onClicked: root.model.sortBy("name")
        }

        DetailsHeader {
            Layout.preferredWidth: root.sizeWidth
            alignRight: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_size"
            sorted: root.model !== null && root.model.sortKey === "size"
            text: qsTr("Size")

            onClicked: root.model.sortBy("size")
        }

        DetailsHeader {
            Layout.preferredWidth: root.doneWidth
            alignRight: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_progress"
            sorted: root.model !== null && root.model.sortKey === "progress"
            text: qsTr("Done")

            onClicked: root.model.sortBy("progress")
        }

        DetailsHeader {
            Layout.preferredWidth: root.remainingWidth
            alignRight: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_remaining"
            sorted: root.model !== null && root.model.sortKey === "remaining"
            text: qsTr("Remaining")
            visible: root.showRemaining

            onClicked: root.model.sortBy("remaining")
        }

        DetailsHeader {
            Layout.preferredWidth: root.priorityWidth
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_priority"
            sorted: root.model !== null && root.model.sortKey === "priority"
            text: qsTr("Priority")

            onClicked: root.model.sortBy("priority")
        }

        DetailsHeader {
            Layout.preferredWidth: root.availabilityWidth
            alignRight: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_availability"
            sorted: root.model !== null && root.model.sortKey === "availability"
            text: qsTr("Avail.")
            visible: root.showAvailability

            onClicked: root.model.sortBy("availability")
        }

        DetailsHeader {
            Layout.preferredWidth: root.piecesWidth
            alignRight: true
            descending: root.model !== null && root.model.sortDescending
            objectName: "fileHeader_pieces"
            sorted: root.model !== null && root.model.sortKey === "pieces"
            text: qsTr("Pieces")
            visible: root.showPieces

            onClicked: root.model.sortBy("pieces")
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
            required property string path
            required property string sizeText
            required property real progress
            required property string progressText
            required property string remainingText
            required property int priority
            required property string priorityText
            required property string availabilityText
            required property string piecesText
            required property bool isFolder
            required property int depth
            required property bool expanded
            required property bool rowSelected

            readonly property color priorityColor: row.priority === 6 || row.priority === 7
                                                   ? Theme.yellow : row.selected
                                                     ? row.textColor : Theme.dimForeground
            // A delegate on its way out of a reset reads index -1 for a moment; without the
            // first test it would open the editor, which takes the keyboard as it is built.
            readonly property bool editing: root.renamingRow >= 0 && root.renamingRow === row.index

            hasCursor: row.index === root.cursorIndex
            height: Theme.railRowHeight
            selected: row.rowSelected
            width: ListView.view.width

            onClicked: root.rowClicked(row.index)
            onDoubleClicked: root.rowActivated(row.index)
            onRightClicked: {
                const at = row.mapToItem(root, Theme.space(24), row.height);
                root.rowMenuRequested(row.index, at.x, at.y);
            }

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(8)

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

                    DetailsCell {
                        anchors.left: row.isFolder ? glyph.right : parent.left
                        anchors.leftMargin: row.isFolder ? Theme.space(6) : 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.textColor
                        // J1: both ends survive, so two long names that share a start
                        // still read differently, and a file keeps its extension.
                        elide: Text.ElideMiddle
                        objectName: "fileName_" + row.index
                        text: row.name
                        tip: nameTip
                        tipText: row.path
                        visible: !row.editing
                    }

                    // `r`: the inline editor, as the table's (`.tbl .rn .ren`).
                    Loader {
                        active: row.editing
                        anchors.left: row.isFolder ? glyph.right : parent.left
                        anchors.leftMargin: row.isFolder ? Theme.space(6) : 0
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.railRowHeight - Theme.space(6)
                        sourceComponent: renameEditor
                    }
                }

                DetailsCell {
                    Layout.preferredWidth: root.sizeWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.sizeText
                }

                Item {
                    Layout.preferredHeight: doneText.implicitHeight
                    Layout.preferredWidth: root.doneWidth

                    ProgressLine {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: doneText.width + Theme.space(6)
                        anchors.verticalCenter: parent.verticalCenter
                        // Complete in the seeding colour, from where the percent reads
                        // "100 %" (a folder of 3.9 GB is never exactly 1 with 18 KB skipped).
                        color: row.progress >= 0.99995 ? Theme.stateSeeding : Theme.accent
                        showText: false
                        value: row.progress
                        visible: !root.compact
                    }

                    Text {
                        id: doneText

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignRight
                        text: row.progressText
                        width: Theme.space(40)
                    }
                }

                DetailsCell {
                    Layout.preferredWidth: root.remainingWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.remainingText
                    visible: root.showRemaining
                }

                Text {
                    Layout.preferredWidth: root.priorityWidth
                    color: row.priorityColor
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    // `.pri.skip { text-decoration: line-through }`.
                    font.strikeout: row.priority === 0
                    text: row.priorityText
                }

                DetailsCell {
                    Layout.preferredWidth: root.availabilityWidth
                    color: row.textColor
                    horizontalAlignment: Text.AlignRight
                    text: row.availabilityText
                    visible: root.showAvailability
                }

                DetailsCell {
                    Layout.preferredWidth: root.piecesWidth
                    color: Theme.dimForeground
                    horizontalAlignment: Text.AlignRight
                    text: row.piecesText
                    tip: nameTip
                    visible: root.showPieces
                }
            }
        }
    }

    ScrollBar {
        anchors.bottom: list.bottom
        anchors.right: parent.right
        anchors.top: list.top
        flickable: list
        vertical: true
        width: Theme.space(4)
    }

    // A filter that hides everything says so, rather than leaving an empty list.
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: header.bottom
        anchors.topMargin: Theme.space(16)
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: qsTr("no file matches · esc clears")
        visible: root.count === 0 && root.model !== null && root.model.filterText.length > 0
    }

    // The full name (the whole path inside the torrent) of a row whose name was cut.
    Tooltip {
        id: nameTip
    }

    Component {
        id: renameEditor

        Rectangle {
            border.color: Theme.accent
            border.width: 1
            color: Theme.fill(Theme.fillIdle)

            TextInput {
                id: editor

                readonly property color accent: Theme.accent
                readonly property color highlight: Qt.rgba(editor.accent.r, editor.accent.g,
                                                           editor.accent.b, 0.35)

                anchors.fill: parent
                anchors.leftMargin: Theme.space(4)
                anchors.rightMargin: Theme.space(4)
                clip: true
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "fileRenameEditor"
                selectByMouse: true
                selectedTextColor: Theme.brightForeground
                selectionColor: editor.highlight
                text: root.renameText
                verticalAlignment: TextInput.AlignVCenter

                Keys.onEscapePressed: root.renameCancelled()
                onAccepted: root.renameCommitted(editor.text)
                onTextChanged: root.renameText = editor.text

                Component.onCompleted: {
                    // A file opens with its name selected and its extension left alone, as
                    // a file manager's rename does; a folder with all of it.
                    const dot = editor.text.lastIndexOf(".");
                    if (dot > 0 && !root.model.isFolder(root.renamingRow))
                        editor.select(0, dot);
                    else
                        editor.selectAll();
                    editor.forceActiveFocus();
                }
            }
        }
    }
}
