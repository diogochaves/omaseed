pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The file pane's head (board 64, DECISIONS C47 I5, I7): the torrent's kinds as chips on the
// left, the count on the right. The add sheet and details › Files draw the same one over
// their own lists.
//
// A chip is a box and a word. The box stands for every file of its kind (`files.kinds`'
// `wanted`: ✓ none skipped, – some, · all) and a press asks the owner to tick or skip them
// all (`kindTicked`): the box changes only when the model does, never by itself (C28). The
// word ("9 videos", the biggest kind with its size dim after it) shows only that kind, and a
// second press shows all again (`files.kindFilter`).
//
// The count is the host's own words (`countText`: the add's "26 of 28 files · 12.7 GB"),
// or, while a kind, a typed filter or hide skipped narrows the list, "showing N of M" with
// a `× show all` that drops all three (`files.clearFilters()`).
//
//     FilesPaneHead {
//         files: App.addFiles.tree
//         countText: App.addFiles.summaryText
//         onKindTicked: kind => App.addFiles.toggleKindWanted(kind)
//     }
RowLayout {
    id: root

    /// The pane's model: its kinds, filters and counts.
    property TorrentFilesModel files: null
    /// What the count says while nothing narrows the list.
    property string countText: ""
    /// Each chip has its box; off, the word alone (C47 I1's one-file pane).
    property bool boxes: true

    /// The box of `kind` was pressed: tick or skip every file of it (the owner decides).
    signal kindTicked(string kind)

    /// The model's kinds (a test may hand its own).
    property var kinds: root.files !== null ? root.files.kinds : []
    /// The kind that takes the most room: its chip also says its size.
    readonly property string biggestKind: {
        let biggest = "";
        let bytes = -1;
        for (const entry of root.kinds) {
            if (entry.bytes > bytes) {
                bytes = entry.bytes;
                biggest = entry.kind;
            }
        }
        return biggest;
    }
    readonly property bool filtering: root.files !== null && root.files.filtering

    /// "%n videos": the kind's word for `count` (counted as FileKinds counts: a split archive
    /// set is one archive).
    function kindWords(kind: string, count: int): string {
        switch (kind) {
        case "video":
            return qsTr("%n videos", "", count);
        case "audio":
            return qsTr("%n audio files", "", count);
        case "subtitles":
            return qsTr("%n subtitles", "", count);
        case "images":
            return qsTr("%n images", "", count);
        case "books":
            return qsTr("%n books", "", count);
        case "archives":
            return qsTr("%n archives", "", count);
        case "discImages":
            return qsTr("%n disc images", "", count);
        case "extras":
            return qsTr("%n extras", "", count);
        }
        return qsTr("%n other files", "", count);
    }

    /// "video": the kind's word for one file, without a count ("download every video").
    function kindWord(kind: string): string {
        switch (kind) {
        case "video":
            return qsTr("video");
        case "audio":
            return qsTr("audio file");
        case "subtitles":
            return qsTr("subtitle");
        case "images":
            return qsTr("image");
        case "books":
            return qsTr("book");
        case "archives":
            return qsTr("archive");
        case "discImages":
            return qsTr("disc image");
        case "extras":
            return qsTr("extra");
        }
        return qsTr("other file");
    }

    /// A press on a kind's word: only that kind, or all again.
    function filterKind(kind: string): void {
        if (root.files === null)
            return;
        root.files.kindFilter = root.files.kindFilter === kind ? "" : kind;
    }

    spacing: Theme.space(10)

    // The chips flow as a Flow would, several to a line, wrapping only when the next one does
    // not fit; but only the first line shares the row with the count. A long count (details'
    // summary) takes up to half a narrow row, and a Flow in what is left (~190 px of a docked
    // 400 px pane) fits no two chips, so six kinds stood one per line. The lines under the
    // count run the head's whole width.
    Item {
        id: chips

        readonly property real spacing: Theme.space(6)
        /// Where each chip goes, `{line, x, y}` by index.
        readonly property var places: {
            const out = [];
            let x = 0;
            let line = 0;
            for (let i = 0; i < chipRepeater.count; ++i) {
                const item = chipRepeater.itemAt(i);
                const w = item !== null ? item.width : 0;
                const room = line === 0 ? chips.width : root.width;
                if (x > 0 && x + w > room) {
                    ++line;
                    x = 0;
                }
                out.push({
                             "line": line,
                             "x": x,
                             "y": line * (Theme.railRowHeight + chips.spacing)
                         });
                x += w + chips.spacing;
            }
            return out;
        }
        readonly property int lines: chips.places.length > 0 ? 1 + chips.places[chips.places.length
                                                                                - 1].line : 0

        Layout.alignment: Qt.AlignTop
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: chips.lines > 0 ? chips.lines * Theme.railRowHeight + (chips.lines - 1)
                                          * chips.spacing : 0
        // One line's worth, as a Flow says when it has the room: the add sheet's row asks it.
        implicitWidth: {
            let sum = 0;
            for (let i = 0; i < chipRepeater.count; ++i) {
                const item = chipRepeater.itemAt(i);
                sum += (item !== null ? item.width : 0) + (i > 0 ? chips.spacing : 0);
            }
            return sum;
        }

        Repeater {
            id: chipRepeater

            // By count, not the list: a tick changes `kinds` (its `wanted`), and the chips stay
            // the same items, so the pointer and the tip stay where they were.
            model: root.kinds.length

            delegate: Rectangle {
                id: chip

                required property int index
                readonly property var modelData: root.kinds[chip.index] ?? {}
                readonly property string kind: chip.modelData.kind
                readonly property bool on: root.files !== null && root.files.kindFilter
                                           === chip.kind

                border.color: Theme.fill(Theme.borderIdle)
                border.width: Theme.hairWidth
                color: "transparent"
                implicitHeight: Theme.railRowHeight
                implicitWidth: chipRow.implicitWidth + 2 * Theme.hairWidth
                x: chips.places[chip.index]?.x ?? 0
                y: chips.places[chip.index]?.y ?? 0
                objectName: "filesKindChip_" + chip.kind
                radius: Theme.cornerRadius

                Row {
                    id: chipRow

                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.hairWidth
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.hairWidth
                    anchors.top: parent.top
                    anchors.topMargin: Theme.hairWidth

                    FileTick {
                        id: box

                        // "every video": the kind's one-file word, whatever the count.
                        Accessible.name: qsTr("download every %1").arg(root.kindWord(chip.kind))
                        height: parent.height
                        objectName: "filesKindBox_" + chip.kind
                        tip: chip.modelData.wanted === 2 ? qsTr("skip every %1").arg(root.kindWord(
                                                                                         chip.kind)) :
                                                           qsTr("tick every %1").arg(root.kindWord(
                                                                                         chip.kind))
                        tooltip: chipTip
                        visible: root.boxes
                        wanted: chip.modelData.wanted
                        width: Theme.space(20)

                        onClicked: root.kindTicked(chip.kind)
                    }

                    Rectangle {
                        color: Theme.fill(Theme.borderIdle)
                        height: parent.height
                        visible: root.boxes
                        width: Theme.hairWidth
                    }

                    // The word: on, the selected fill (18 %) behind it, as board 64's `.kc.on`.
                    Rectangle {
                        id: word

                        // #77: the word is a button that shows only its kind, checked while it
                        // does.
                        Accessible.checkable: true
                        Accessible.checked: chip.on
                        Accessible.name: wordText.text
                        Accessible.role: Accessible.Button
                        Accessible.onPressAction: root.filterKind(chip.kind)
                        color: chip.on ? Theme.on.selected.fill : wordMouse.containsMouse
                                         ? Theme.on.hover.fill : "transparent"
                        height: parent.height
                        objectName: "filesKindWord_" + chip.kind
                        width: wordRow.implicitWidth + 2 * Theme.space(8)

                        Row {
                            id: wordRow

                            anchors.centerIn: parent
                            spacing: Theme.space(4)

                            Text {
                                id: wordText

                                anchors.baseline: parent.verticalCenter
                                anchors.baselineOffset: Math.round(font.pixelSize / 3)
                                color: chip.on ? Theme.on.selected.brightForeground :
                                                 Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                objectName: "filesKindText_" + chip.kind
                                text: root.kindWords(chip.kind, chip.modelData.count)
                            }

                            Text {
                                anchors.baseline: wordText.baseline
                                color: chip.on ? Theme.on.selected.dimForeground :
                                                 Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.caption
                                objectName: "filesKindSize_" + chip.kind
                                text: App.sizeText(chip.modelData.bytes)
                                visible: chip.kind === root.biggestKind
                            }
                        }

                        MouseArea {
                            id: wordMouse

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true

                            onClicked: root.filterKind(chip.kind)
                        }
                    }
                }
            }
        }
    }

    Label {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.alignment: Qt.AlignTop
        // A long count (details' summary with its skipped tail, #189) takes at most half a
        // narrow row and gives way from its end; the chips keep the rest.
        Layout.maximumWidth: Math.max(Theme.space(120), root.width / 2)
        Layout.topMargin: Math.round((Theme.railRowHeight - height) / 2)
        dim: true
        elide: Text.ElideRight
        objectName: "filesPaneCount"
        size: "caption"
        text: root.filtering ? qsTr("showing %1 of %2").arg(root.files.matchCount).arg(
                                   root.files.fileCount) : root.countText
    }

    Label {
        id: showAll

        Accessible.name: qsTr("show all")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: showAll.clear()
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: Math.round((Theme.railRowHeight - height) / 2)
        color: Theme.accent
        objectName: "filesShowAll"
        size: "caption"
        text: qsTr("× show all")
        visible: root.filtering

        function clear(): void {
            if (root.files !== null)
                root.files.clearFilters();
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: showAll.clear()
        }
    }

    // On the window's content item: the head is a short row, and the tip hangs under it.
    Tooltip {
        id: chipTip

        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
    }
}
