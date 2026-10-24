pragma ComponentBehavior: Bound

import QtQuick
import Omaseed
import Omaseed.Kit

// One row of the rows display (board 46 `rowHtml()`, DECISIONS C24 H3, law 3): the state glyph,
// the name with its meta ("films · 716 MB"), and under them the progress line and one sentence
// ("48.9 % of 480 MB · from 3 seeds at 1.40 MB/s"), with a needs-you torrent's first fix as a
// button; at the right the one fact that matters ("3 min", "2.4×", "no access") over its
// caption ("to ready"). The words are the proxy's (core/RowWords); numbers stay in details and
// the table.
//
// The table owns the cursor and the selection (TorrentTable `face`): this row draws them on
// `Theme.ground` with every word on it through `Kit/Ink` (C30 F1–F4), and a click goes through
// the table's own functions ("The click rules every list follows"; C18 D6's tick is the glyph's
// column). `r` opens the inline editor over the name. Its form is `Theme.libraryRow*`.
Item {
    id: row

    required property TorrentTable table
    /// The display's narrow step (LibraryRows `level`).
    property int narrow: 0

    required property int index
    required property string hash
    required property string name
    required property real progress
    required property string stateRole
    required property string stateText
    required property int question
    required property string rowMeta
    required property string rowFact
    required property string rowCaption
    required property string rowSentence
    required property string rowFix
    required property string rowFixAction

    readonly property bool isCursor: row.index === row.table.cursorIndex
    readonly property bool isSelected: row.table.selectionRevision >= 0 && row.table.isRowSelected(row.index)
    readonly property bool editing: row.table.renaming && row.table.renameIndex === row.index
                                    && row.table.face !== null && row.table.face.inlineRename
    readonly property bool needsYou: row.question === TorrentFilterModel.NeedsYou
    readonly property string glyph: {
        const glyph = row.table.statusGlyphs[row.stateText];
        return glyph !== undefined ? glyph : Glyphs.dot;
    }
    /// C18 D1: a quick action's mark beside the meta (board 46 `.mark2`), or null.
    readonly property var mark: {
        const own = row.table.rowMarkFor !== null && row.table.rowMarkRevision >= 0
                  ? row.table.rowMarkFor(row.hash) : ({});
        return own.text !== undefined ? own : null;
    }
    /// The text roles on this row's ground (C30 F7).
    readonly property Ink ink: Ink {
        ground: Theme.ground(row.isSelected, row.isCursor, pointer.containsMouse, false)
    }

    /// The fix's button was clicked; the cursor and the selection are on this row already.
    signal fixClicked(string hash, string action)

    /// A left click with `modifiers`, as the table's row takes it.
    function click(modifiers: int): void {
        row.table.forceActiveFocus();
        if (modifiers & Qt.ShiftModifier) {
            row.table.selectRangeTo(row.index, (modifiers & Qt.ControlModifier) !== 0);
            return;
        }
        if (modifiers & Qt.ControlModifier) {
            row.table.cursorIndex = row.index;
            row.table.toggleSelection(row.index);
            return;
        }
        const had = row.table.selectedCount > 0;
        row.table.cursorIndex = row.index;
        row.table.selectOnly(row.index);
        row.table.plainClicked(had);
    }

    /// C18 D6: the glyph's column is the row's tick: a click adds or removes it alone.
    function tickClick(modifiers: int): void {
        row.table.forceActiveFocus();
        if (modifiers & Qt.ShiftModifier) {
            row.table.selectRangeTo(row.index, true);
            return;
        }
        row.table.cursorIndex = row.index;
        row.table.toggleSelection(row.index);
    }

    // #77: found by the torrent's name; what it says in the description.
    Accessible.description: [row.rowFact, row.rowCaption, row.rowSentence].filter(part => part.length > 0).join(
                                " · ")
    Accessible.name: row.name
    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: row.isSelected
    Accessible.onPressAction: if (!row.editing)
                                  row.click(Qt.NoModifier)
    height: Theme.libraryRowHeight
    objectName: "libraryRow_" + row.index

    // The ground: the window's, the pointer's, the cursor's, selected (C30), as a table row's.
    Rectangle {
        anchors.fill: parent
        border.color: row.isCursor && !row.isSelected ? Theme.fill(Theme.borderHover) : "transparent"
        border.width: row.isCursor && !row.isSelected ? Theme.hairWidth : 0
        color: row.ink.ground.fill

        Behavior on color {
            ColorAnimation {
                duration: Theme.fillChangeDuration
            }
        }
    }

    // A selected row's edge (`.row.picked`), drawn by the skin's SelectionMark face.
    SelectionMark {
        anchors.fill: parent
        color: Theme.accent
        objectName: "libraryRowEdge_" + row.index
        rule: true
        ruleWidth: Theme.libraryRowSelectedEdge
        visible: row.isSelected
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: Theme.fill(Theme.rowRuleAlpha)
        height: 1
    }

    // Under the words, so the fix's own button takes its clicks.
    MouseArea {
        id: pointer

        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        anchors.fill: parent
        enabled: !row.editing
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                row.table.forceActiveFocus();
                const point = pointer.mapToItem(row.table, mouse.x, mouse.y);
                row.table.contextClick(row.index, point.x, point.y);
                return;
            }
            // #51 D8: a middle-click toggles the row, as ctrl+click does.
            if (mouse.button === Qt.MiddleButton) {
                row.table.forceActiveFocus();
                row.table.cursorIndex = row.index;
                row.table.toggleSelection(row.index);
                return;
            }
            if (glyphColumn.onTick)
                row.tickClick(mouse.modifiers);
            else
                row.click(mouse.modifiers);
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return;
            if (glyphColumn.onTick) {
                row.tickClick(mouse.modifiers);
                return;
            }
            if (!(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                row.table.activated(row.hash);
        }
    }

    // The state glyph, or the tick under the pointer and on a selected row (C18 D6).
    Item {
        id: glyphColumn

        readonly property bool onTick: pointer.containsMouse && pointer.mouseX < glyphColumn.x
                                       + glyphColumn.width

        height: parent.height
        width: Theme.libraryRowGlyphWidth
        x: Theme.libraryRowInsetStart

        Glyph {
            anchors.centerIn: parent
            color: row.ink[row.stateRole] ?? row.ink.foreground
            objectName: "libraryRowGlyph_" + row.index
            pixelSize: Theme.font.body
            text: row.glyph
            visible: !(row.isSelected || glyphColumn.onTick)
        }

        // a11y-ignore A2: the row is selectable already; this only draws its state
        Text {
            anchors.centerIn: parent
            color: row.isSelected || glyphColumn.onTick ? row.ink.accent : row.ink.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "libraryRowTick_" + row.index
            text: row.isSelected ? "■" : "□"
            visible: row.isSelected || glyphColumn.onTick
        }
    }

    // The name's line and the sentence's.
    Column {
        id: words

        anchors.left: glyphColumn.right
        anchors.leftMargin: Theme.libraryRowGap
        anchors.right: factColumn.left
        anchors.rightMargin: Theme.libraryRowGap
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.libraryRowLineGap

        Row {
            id: nameLine

            spacing: Theme.libraryRowGap
            width: parent.width

            Text {
                id: nameText

                color: row.ink.brightForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowNameSize
                font.weight: Theme.libraryRowNameWeight
                objectName: "libraryRowName_" + row.index
                opacity: row.editing ? 0 : 1
                text: row.name
                textFormat: Text.PlainText
                width: Math.min(nameText.implicitWidth, nameLine.width - (metaText.visible ? Math.min(
                                                                                                 metaText.implicitWidth,
                                                                                                 nameLine.width
                                                                                                 / 3) + nameLine.spacing
                                                                                             : 0)
                                - (markText.visible ? markText.implicitWidth + nameLine.spacing : 0))
            }

            Text {
                id: metaText

                anchors.baseline: nameText.baseline
                color: row.ink.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowMetaSize
                objectName: "libraryRowMeta_" + row.index
                text: row.rowMeta
                textFormat: Text.PlainText
                visible: row.narrow < 2 && row.rowMeta.length > 0 && !row.editing
                width: Math.min(metaText.implicitWidth, Math.max(0, nameLine.width - nameText.width
                                                                 - nameLine.spacing
                                                                 - (markText.visible ? markText.implicitWidth
                                                                                       + nameLine.spacing : 0)))
            }

            Text {
                id: markText

                anchors.baseline: nameText.baseline
                color: row.mark !== null && row.mark.kind === "failed" ? row.ink.red : row.ink.green
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowMetaSize
                objectName: "libraryRowMark_" + row.index
                text: row.mark !== null ? row.mark.text : ""
                textFormat: Text.PlainText
                visible: row.mark !== null && !row.editing
            }
        }

        Row {
            id: sentenceLine

            spacing: Theme.libraryRowGap
            width: parent.width

            // The progress line: a mark, so it keeps the window's colours (C30 lifts words).
            // The skin's Meter face draws it (Console's: board 46's segmented `.strip`).
            Meter {
                id: progressLine

                anchors.verticalCenter: parent.verticalCenter
                color: row.question === TorrentFilterModel.Resting ? Theme.dimForeground
                                                                   : Theme[row.stateRole] ?? Theme.foreground
                height: Theme.libraryRowLineThickness
                objectName: "libraryRowLine_" + row.index
                trackColor: Theme.fill(Theme.libraryRowLineTrack)
                value: row.progress
                width: row.narrow >= 1 ? Theme.libraryRowLineWidthNarrow : Theme.libraryRowLineWidth
            }

            Text {
                id: sentenceText

                anchors.verticalCenter: parent.verticalCenter
                color: row.ink.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowSentenceSize
                objectName: "libraryRowSentence_" + row.index
                text: row.rowSentence
                textFormat: Text.PlainText
                // As long as its words, so the fix follows them (board 46 `.l2`).
                width: Math.min(sentenceText.implicitWidth, Math.max(0, sentenceLine.width - progressLine.width
                                                                     - sentenceLine.spacing
                                                                     - (fixButton.visible ? fixButton.width
                                                                                            + sentenceLine.spacing : 0)))
            }

            // The fix (`.fix`): the error or failure block's first action, here on the row.
            Rectangle {
                id: fixButton

                Accessible.name: row.rowFix
                Accessible.role: Accessible.Button
                Accessible.onPressAction: fixArea.activate()
                anchors.verticalCenter: parent.verticalCenter
                border.color: Qt.alpha(Theme.accent, Theme.libraryRowFixBorderAlpha)
                border.width: Theme.hairWidth
                color: fixArea.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
                height: fixText.implicitHeight
                objectName: "libraryRowFix_" + row.index
                visible: row.rowFix.length > 0
                width: fixText.implicitWidth + 2 * Theme.libraryRowFixPadding

                Text {
                    id: fixText

                    anchors.centerIn: parent
                    color: Theme.inkOf(Theme.accent, fixArea.containsMouse ? Theme.on.hover : row.ink.ground)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.libraryRowSentenceSize
                    text: row.rowFix
                    textFormat: Text.PlainText
                }

                MouseArea {
                    id: fixArea

                    function activate(): void {
                        row.table.forceActiveFocus();
                        row.table.cursorIndex = row.index;
                        if (!row.isSelected && row.table.selectedCount > 0)
                            row.table.selectOnly(row.index);
                        row.fixClicked(row.hash, row.rowFixAction);
                    }

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    z: 2

                    onClicked: fixArea.activate()
                }
            }
        }
    }

    // The fact and its caption.
    Column {
        id: factColumn

        anchors.right: parent.right
        anchors.rightMargin: Theme.libraryRowInsetEnd
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(Theme.libraryRowFactWidth, factText.implicitWidth, captionText.implicitWidth)

        Text {
            id: factText

            color: row.needsYou ? row.ink.red : row.ink.brightForeground
            elide: Text.ElideLeft
            font.family: Theme.fontFamily
            font.features: ({
                    "tnum": 1
                })
            font.pixelSize: Theme.libraryRowFactSize
            font.weight: Theme.libraryRowFactWeight
            horizontalAlignment: Text.AlignRight
            objectName: "libraryRowFact_" + row.index
            text: row.rowFact
            textFormat: Text.PlainText
            width: parent.width
        }

        Text {
            id: captionText

            color: row.ink.dimForeground
            font.capitalization: Font.AllUppercase
            font.family: Theme.fontFamily
            font.letterSpacing: Theme.libraryRowCaptionTracking
            font.pixelSize: Theme.libraryRowCaptionSize
            horizontalAlignment: Text.AlignRight
            objectName: "libraryRowCaption_" + row.index
            text: row.rowCaption
            textFormat: Text.PlainText
            width: parent.width
        }
    }

    // `r`: the inline editor over the name, as the table's (its words on the field's fill).
    Loader {
        active: row.editing
        height: nameText.height + 2 * Theme.libraryRenameRise
        objectName: "libraryRowRename"
        width: words.width
        x: words.x
        y: words.y + nameLine.y - Theme.libraryRenameRise
        z: 3

        sourceComponent: Rectangle {
            border.color: Theme.accent
            border.width: Theme.hairWidth
            color: Theme.background

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: Theme.on.idle.fill
            }

            TextInput {
                id: editor

                Accessible.name: qsTr("torrent name")
                Accessible.role: Accessible.EditableText
                anchors.fill: parent
                anchors.leftMargin: Theme.libraryRenamePadding
                anchors.rightMargin: Theme.libraryRenamePadding
                clip: true
                color: Theme.on.idle.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.libraryRowNameSize
                selectByMouse: true
                selectedTextColor: Theme.on.highlight.brightForeground
                selectionColor: Theme.on.highlight.fill
                text: row.table.renameText
                verticalAlignment: TextInput.AlignVCenter

                Keys.onEscapePressed: row.table.cancelRename()
                onAccepted: row.table.commitRename()
                onActiveFocusChanged: {
                    if (!editor.activeFocus && row.table.renaming && editor.Window.active)
                        row.table.commitRename();
                }
                onTextChanged: row.table.renameText = editor.text

                Component.onCompleted: {
                    editor.select(editor.length, 0);
                    editor.forceActiveFocus();
                }
            }
        }
    }
}
