pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The details pane's Files tab (spec v2 §2 Fig. 2 `.list.files`, UX-NOTES N1): a summary line
// that turns into the filter field (docs/12 F1), a header row that sorts on a click, then the
// visible rows of the torrent's file tree. Folders carry a `▾`/`›` fold glyph and their
// subtree's totals; every level indents by `Theme.space(14)`, matching the figure's
// 8 / 22 / 38 px left padding.
//
// The model is already flat — TorrentFilesModel drops a folded folder's subtree, a filtered
// out file and sorts siblings — so this is a plain ListView. `cursorIndex` is owned by
// Details.qml, because the keys that move it belong to the pane, not to the tab; so do the
// menus, which cover the whole pane.
//
// #42 (docs/27): a tick column comes first, the add sheet's (FileTick), and the header's tick
// stands for every file the filter shows. A click asks the pane, which sends the priority.
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
    /// True until the editor has opened once: it selects the name then. An editor rebuilt
    /// because the rows moved under it puts the caret back at `renameCaret` instead.
    property bool renameFresh: false
    property int renameCaret: 0
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
    /// A click on the summary line's "N skipped" (#9): the pane turns hideSkipped over, as `X`.
    signal hideSkippedRequested
    /// A click on a row's tick (#42, docs/27): `wanted` is what the tick showed (2 ticked,
    /// 1 mixed, 0 skipped); the pane skips a full one and ticks the others.
    signal tickClicked(int row, int wanted)
    /// A click on the header's tick, for every file the filter shows (docs/27 D1/D4).
    signal headTickClicked

    readonly property int count: root.model ? root.model.count : 0
    readonly property bool filterShown: root.filterOpen || (root.model !== null
                                                           && root.model.filterText.length > 0)
    /// The caret is in the filter field.
    readonly property bool filterEditing: filterField.editing

    readonly property int fileCount: root.model ? root.model.fileCount : 0
    /// docs/12 F2: a single-file torrent has nothing to filter, so it offers no filter.
    readonly property bool canFilter: root.fileCount > 1
    /// docs/12 F1's summary line, in three parts: the file count, in the foreground, the
    /// rest, dim (" · 2 folders · 3.91 GB"; folders only when there are), then the skipped
    /// files as their own toggle (#9, `skippedText`).
    readonly property string summaryCountText: root.fileCount === 0 ? qsTr("no files")
                                                                    : root.fileCount === 1
                                                                      ? qsTr("1 file")
                                                                      : qsTr("%1 files").arg(
                                                                            root.fileCount)
    readonly property string summaryFactsText: {
        if (root.model === null || root.fileCount === 0)
            return "";
        const parts = [""];
        const folders = root.model.folderCount;
        if (folders > 0)
            parts.push(folders === 1 ? qsTr("1 folder") : qsTr("%1 folders").arg(folders));
        parts.push(root.model.totalSizeText);
        return parts.join(" · ");
    }
    /// "2 skipped", or "2 hidden" while they are hidden; empty with none skipped.
    readonly property string skippedText: root.model === null || root.model.skippedCount === 0
                                          ? "" : root.model.hideSkipped
                                            ? qsTr("%1 hidden").arg(root.model.skippedCount)
                                            : qsTr("%1 skipped").arg(root.model.skippedCount)

    // The spec's `.files { grid-template-columns: 1fr 70px 56px 72px }`, trimmed to fit the
    // real docked column (spec §2's Fig. 2 is a 400 px mock). Each fixed column holds its
    // widest value ("81.4 GB", "100 %", "maximal", "3860–3911"). The spacing scale need not
    // follow the font (shell.toml), so each is also at least its widest value as measured in
    // the font on screen: at base 18 a fixed 60 u cut "84.0 MB" to "84.0 …" (review RUN-6).
    readonly property int sizeWidth: Math.max(Theme.space(60), root._measured(sizeMetrics))
    readonly property int doneTextWidth: Math.max(Theme.space(40), root._measured(doneMetrics))
    readonly property int doneWidth: root.compact ? Math.max(Theme.space(46), root.doneTextWidth + Theme.space(6))
                                                  : Math.max(Theme.space(86), root.doneTextWidth + Theme.space(46))
    readonly property int priorityWidth: Math.max(Theme.space(58), root._measured(priorityMetrics))
    readonly property int remainingWidth: root.sizeWidth
    readonly property int availabilityWidth: Theme.space(50)
    readonly property int piecesWidth: Math.max(Theme.space(78), root._measured(piecesMetrics))
    readonly property int tickWidth: Theme.space(14)

    function _measured(metrics: TextMetrics): int {
        return Math.ceil(metrics.advanceWidth) + 1;
    }

    TextMetrics {
        id: sizeMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "999.9 MB"
    }

    TextMetrics {
        id: doneMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "100 %"
    }

    TextMetrics {
        id: priorityMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "maximal"
    }

    TextMetrics {
        id: piecesMetrics

        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: "3860–3911"
    }

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

    /// `/` or a click on the summary line. False, and nothing opens, on a single file (F2).
    function focusFilter(): bool {
        if (!root.canFilter)
            return false;
        root.filterOpen = true;
        filterField.focusInput();
        return true;
    }

    /// Give the keyboard back to the open rename's editor. A model reset rebuilds every row
    /// while the list is still being laid out, and an editor built then cannot take the
    /// focus itself; the pane calls this once the reset has settled.
    function focusRenameEditor(): void {
        const delegate = root.renamingRow >= 0 ? list.itemAtIndex(root.renamingRow) : null;
        const editor = delegate ? root._findEditor(delegate) : null;
        if (editor && !editor.activeFocus)
            editor.forceActiveFocus();
    }

    function _findEditor(item: Item): Item {
        if (item.objectName === "fileRenameEditor")
            return item;
        for (let i = 0; i < item.children.length; ++i) {
            const found = root._findEditor(item.children[i]);
            if (found)
                return found;
        }
        return null;
    }

    implicitHeight: Theme.space(200)
    implicitWidth: Theme.space(300)

    // A rename opened on a row that then scrolls or refreshes away is dropped.
    onCountChanged: {
        if (root.renamingRow >= root.count)
            root.renameCancelled();
    }

    // docs/12 F1: one row above the headers that is always there. At rest it is the summary
    // line, the facts about the files and the way into the filter; `/` or a click turns the
    // same row into the filter field, and `esc` turns it back. Both fill the same height, so
    // the list below never moves.
    Item {
        id: filterBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.controlHeight + Theme.space(8)

        Item {
            id: summary

            anchors.fill: parent
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            objectName: "filesSummary"
            visible: !root.filterShown

            Text {
                id: summaryCount

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "filesSummaryCount"
                text: root.summaryCountText
            }

            Text {
                id: summaryFacts

                anchors.left: summaryCount.right
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "filesSummaryFacts"
                text: root.summaryFactsText
                width: Math.max(0, Math.min(implicitWidth, summary.width - summaryCount.width
                                            - (skippedToggle.visible ? skippedToggle.width : 0)
                                            - (summaryGo.visible ? summaryGo.width
                                                                   + Theme.space(8) : 0)))
            }

            // #9: the skipped count is the switch that hides them. Dim while they show; in
            // the foreground, as "2 hidden", while they do not. `X` does the same; its key
            // cap is the first thing to go when the line is short (the docked pane), before
            // the facts are cut (the file menu and the ? sheet still name the key).
            Row {
                id: skippedToggle

                readonly property bool keyFits: summary.width - summaryCount.implicitWidth
                                                - summaryFacts.implicitWidth - skippedDot.implicitWidth
                                                - skippedText.implicitWidth - skippedKey.implicitWidth
                                                - 2 * skippedToggle.spacing
                                                - (summaryGo.visible ? summaryGo.width + Theme.space(8)
                                                                     : 0) >= 0

                anchors.left: summaryFacts.right
                anchors.verticalCenter: parent.verticalCenter
                objectName: "filesSkippedToggle"
                spacing: Theme.space(6)
                visible: root.skippedText.length > 0

                Text {
                    id: skippedDot

                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: " ·"
                }

                Text {
                    id: skippedText

                    anchors.verticalCenter: parent.verticalCenter
                    color: root.model !== null && root.model.hideSkipped ? Theme.foreground
                                                                         : Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "filesSkippedText"
                    text: root.skippedText
                }

                KeyCap {
                    id: skippedKey

                    anchors.verticalCenter: parent.verticalCenter
                    objectName: "filesSkippedKey"
                    text: "X"
                    visible: skippedToggle.keyFits
                }
            }

            Row {
                id: summaryGo

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                objectName: "filesSummaryGo"
                spacing: Theme.space(6)
                visible: root.canFilter

                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.dimForeground
                    pixelSize: Theme.font.small
                    text: Glyphs.jump
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: qsTr("filter")
                }

                KeyCap {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "/"
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                enabled: root.canFilter

                onClicked: root.focusFilter()
            }

            // Above the line's own area: a click on "N skipped" hides them, not the filter.
            MouseArea {
                cursorShape: Qt.PointingHandCursor
                height: summary.height
                objectName: "filesSkippedArea"
                visible: skippedToggle.visible
                width: skippedToggle.width
                x: skippedToggle.x

                onClicked: root.hideSkippedRequested()
            }
        }

        Hairline {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: !root.filterShown
        }
    }

    Field {
        id: filterField

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: filterBar.verticalCenter
        hint: Glyphs.search
        objectName: "filesFilter"
        // docs/12 F4: the placeholder teaches the syntax.
        placeholder: qsTr("filter files · *.srt · mkv|mp4")
        trailing: root.model && root.model.filterText.length > 0 ? qsTr("%1 of %2 files").arg(
                                                                        root.model.matchCount).arg(
                                                                        root.model.fileCount) : ""
        trailingKey: qsTr("esc")
        visible: root.filterShown

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                root.filterDone();
                event.accepted = true;
            } else if (event.key === Qt.Key_Escape) {
                // The Field passes on an `esc` it had nothing to clear: close the field.
                root.filterOpen = false;
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
                // Cleared from the list (`esc` there): the emptied field goes too.
                if (root.model.filterText.length === 0 && !filterField.editing)
                    root.filterOpen = false;
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
        anchors.top: filterBar.bottom
        anchors.topMargin: Theme.space(2)
        spacing: Theme.space(8)

        FileTick {
            Layout.preferredHeight: Theme.railRowHeight
            Layout.preferredWidth: root.tickWidth
            objectName: "filesHeadTick"
            tip: {
                const verb = root.model !== null && root.model.headWanted === 2 ? qsTr("skip")
                                                                               : qsTr("tick");
                if (root.model !== null && root.model.filterText.length > 0)
                    return qsTr("%1 the %2 files shown").arg(verb).arg(root.model.headFileCount);
                return qsTr("%1 every file · ctrl+a x").arg(verb);
            }
            tooltip: nameTip
            visible: root.fileCount > 0
            wanted: root.model !== null ? root.model.headWanted : 2

            onClicked: root.headTickClicked()
        }

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
            required property int wanted

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

            // Above the row's own MouseArea (`parent: row`, z 1), as the add tree's, so the
            // tick takes its clicks; a click anywhere else falls through to the row.
            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                parent: row
                spacing: Theme.space(8)
                z: 1

                FileTick {
                    Layout.preferredHeight: Theme.railRowHeight
                    Layout.preferredWidth: root.tickWidth
                    objectName: "fileTick_" + row.index
                    wanted: row.wanted

                    onClicked: root.tickClicked(row.index, row.wanted)
                }

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
                        // Complete in the seeding colour, where the percent reads "100 %":
                        // exactly 1, as Format::percentCompact has it (a folder's progress
                        // leaves out what it skips, so a folder that is done is exactly 1).
                        color: row.progress >= 1 ? Theme.stateSeeding : Theme.accent
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
                        width: root.doneTextWidth
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

    // #9: every file skipped, and the skipped hidden: say why the list is empty.
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: header.bottom
        anchors.topMargin: Theme.space(16)
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "filesAllSkipped"
        text: qsTr("every file is skipped · X shows them")
        visible: root.count === 0 && root.model !== null && root.model.filterText.length === 0
                 && root.model.hideSkipped && root.fileCount > 0
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
                onCursorPositionChanged: {
                    if (editor.ready)
                        root.renameCaret = editor.cursorPosition;
                }

                property bool ready: false

                Component.onCompleted: {
                    // A file opens with its name selected and its extension left alone, as
                    // a file manager's rename does; a folder with all of it. The cursor sits
                    // at the start, so a name wider than the editor shows its beginning
                    // rather than scrolling to its end (the table's editor does the same).
                    const dot = editor.text.lastIndexOf(".");
                    if (!root.renameFresh)
                        editor.cursorPosition = Math.min(root.renameCaret, editor.text.length);
                    else if (dot > 0 && !root.model.isFolder(root.renamingRow))
                        editor.select(dot, 0);
                    else
                        editor.select(editor.length, 0);
                    root.renameFresh = false;
                    editor.ready = true;
                    root.renameCaret = editor.cursorPosition;
                    editor.forceActiveFocus();
                }
            }
        }
    }
}
