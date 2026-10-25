pragma ComponentBehavior: Bound

import QtQuick
import Omaseed.Kit

// One search result as a row of the rows display (board 48 §4 `resRow()`, DECISIONS C26
// SR3–SR8, C24 H16): it reads like a torrent. At the left health as the glyph's colour (five
// seeds or more green, one to four yellow, "?" and 0 dim; one already in the library takes
// its torrent's arriving colour; a link to a .torrent shows ⋯); line 1 the name, "size · age"
// and at most one mark (in your library, likely on disk, added; new or +1 copy for a moment);
// line 2 who found it and what to know, then add… (trackers… on one you have) on the cursor's
// row and under the pointer; at the right the fact, the seeds, over its caption.
//
// The view owns the cursor (`SearchView.currentIndex`); a click puts it here and opens the
// result on the right, a double click is add…. Its form is the rows display's
// (`Theme.libraryRow*`), its words SearchModel's.
Item {
    id: row

    /// The ListView's index of the cursor row.
    property int cursorIndex: -1
    /// The view's narrow step (LibraryRows `level`): 1 and up drops the plugins from line 2.
    property int narrow: 0

    required property int index
    required property string key
    required property string name
    required property string meta
    required property string seedsText
    required property string seedsCaption
    required property string health
    required property string sentence
    required property string sentenceShort
    required property string mark
    required property string markText
    required property string fresh
    required property string addWord
    required property bool isLink

    readonly property bool isCursor: row.index === row.cursorIndex
    readonly property Ink ink: Ink {
        ground: Theme.ground(false, row.isCursor, pointer.containsMouse, false)
    }
    /// SR8: health as one colour.
    readonly property color healthColor: {
        if (row.mark === "library" || row.mark === "added")
            return row.ink.stateDownloading;
        switch (row.health) {
        case "good":
            return row.ink.stateSeeding;
        case "few":
            return row.ink.yellow;
        }
        return row.ink.dimForeground;
    }

    signal clicked(int index)
    signal addClicked(int index)
    signal doubleClicked(int index)

    Accessible.description: [row.seedsText + " " + row.seedsCaption, row.meta, row.markText,
        row.sentence].filter(part => part.length > 0).join(" · ")
    Accessible.name: row.name
    Accessible.role: Accessible.ListItem
    Accessible.onPressAction: row.clicked(row.index)
    height: Theme.libraryRowHeight
    objectName: "searchRow_" + row.index

    Rectangle {
        anchors.fill: parent
        border.color: row.isCursor && !Theme.cursorMarked ? Theme.fill(Theme.borderHover) : "transparent"
        border.width: row.isCursor && !Theme.cursorMarked ? Theme.hairWidth : 0
        color: row.ink.ground.fill
    }

    // The cursor's mark, as a library row's (board 46 `.row.sel::after`).
    SelectionMark {
        anchors.fill: parent
        color: Theme.accent
        objectName: "searchRowCursor_" + row.index
        visible: row.isCursor
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: Theme.fill(Theme.rowRuleAlpha)
        height: 1
    }

    MouseArea {
        id: pointer

        anchors.fill: parent
        hoverEnabled: true

        onClicked: row.clicked(row.index)
        onDoubleClicked: row.doubleClicked(row.index)
    }

    Item {
        id: glyphColumn

        height: parent.height
        width: Theme.libraryRowGlyphWidth
        x: Theme.libraryRowInsetStart

        Glyph {
            anchors.centerIn: parent
            color: row.healthColor
            objectName: "searchRowGlyph_" + row.index
            pixelSize: Theme.font.body
            text: row.mark === "library" || row.mark === "added" ? Glyphs.arrowDown : row.isLink
                                                                   ? Glyphs.searchLink :
                                                                     Glyphs.searchMagnet
        }
    }

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

            readonly property real tail: (metaText.visible ? Math.min(metaText.implicitWidth,
                                                                      nameLine.width / 3)
                                                             + nameLine.spacing : 0) + (
                                             markLabel.visible ? markLabel.implicitWidth
                                                                 + nameLine.spacing : 0) + (
                                             freshLabel.visible ? freshLabel.implicitWidth
                                                                  + nameLine.spacing : 0)

            spacing: Theme.libraryRowGap
            width: parent.width

            Text {
                id: nameText

                color: row.ink.brightForeground
                elide: Text.ElideRight
                // Board 48 `.srcn`, as the library's `.row .nm`.
                font.capitalization: Theme.rowNameUpper ? Font.AllUppercase : Font.MixedCase
                font.family: Theme.labelFamily
                font.letterSpacing: Theme.libraryRowNameSize * Theme.nameTrack
                font.pixelSize: Theme.libraryRowNameSize
                font.weight: Theme.libraryRowNameWeight
                objectName: "searchRowName_" + row.index
                text: row.name
                textFormat: Text.PlainText
                width: Math.min(nameText.implicitWidth, Math.max(0, nameLine.width - nameLine.tail))
            }

            Text {
                id: metaText

                anchors.baseline: nameText.baseline
                color: row.ink.dimForeground
                elide: Text.ElideRight
                font.family: Theme.valueFamily
                font.pixelSize: Theme.libraryRowMetaSize
                objectName: "searchRowMeta_" + row.index
                text: row.meta
                textFormat: Text.PlainText
                visible: row.narrow < 2
                width: Math.min(metaText.implicitWidth, nameLine.width / 3)
            }

            Text {
                id: freshLabel

                anchors.baseline: nameText.baseline
                color: row.ink.accent
                font.family: Theme.valueFamily
                font.pixelSize: Theme.libraryRowMetaSize
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                objectName: "searchRowFresh_" + row.index
                text: row.fresh === "copy" ? qsTr("+1 copy") : qsTr("new")
                textFormat: Text.PlainText
                visible: row.fresh.length > 0
            }

            Text {
                id: markLabel

                anchors.baseline: nameText.baseline
                color: row.mark === "disk" ? row.ink.yellow : row.ink.green
                font.family: Theme.valueFamily
                font.pixelSize: Theme.libraryRowMetaSize
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                objectName: "searchRowMark_" + row.index
                text: row.markText
                textFormat: Text.PlainText
                visible: row.markText.length > 0
            }
        }

        Row {
            id: sentenceLine

            spacing: Theme.libraryRowGap
            width: parent.width

            Text {
                id: sentenceText

                anchors.verticalCenter: parent.verticalCenter
                color: row.ink.dimForeground
                elide: Text.ElideRight
                font.family: Theme.valueFamily
                font.pixelSize: Theme.libraryRowSentenceSize
                objectName: "searchRowSentence_" + row.index
                text: row.narrow >= 1 ? row.sentenceShort : row.sentence
                textFormat: Text.PlainText
                width: Math.min(sentenceText.implicitWidth, Math.max(0, sentenceLine.width - (
                                                                         addButton.visible
                                                                         ? addButton.width
                                                                           + sentenceLine.spacing :
                                                                           0)))
            }

            // add… (`.fix`), on the cursor's row and under the pointer only (board 48 §4).
            Rectangle {
                id: addButton

                Accessible.name: row.addWord
                Accessible.role: Accessible.Button
                Accessible.onPressAction: row.addClicked(row.index)
                anchors.verticalCenter: parent.verticalCenter
                border.color: Qt.alpha(Theme.accent, Theme.libraryRowFixBorderAlpha)
                border.width: Theme.hairWidth
                color: addArea.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
                height: addText.implicitHeight
                objectName: "searchRowAdd_" + row.index
                visible: row.isCursor || pointer.containsMouse || addArea.containsMouse
                width: addText.implicitWidth + 2 * Theme.libraryRowFixPadding

                Text {
                    id: addText

                    anchors.centerIn: parent
                    color: Theme.inkOf(Theme.accent, addArea.containsMouse ? Theme.on.hover :
                                                                             row.ink.ground)
                    font.family: Theme.valueFamily
                    font.pixelSize: Theme.libraryRowSentenceSize
                    text: row.addWord
                    textFormat: Text.PlainText
                }

                MouseArea {
                    id: addArea

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: row.addClicked(row.index)
                }
            }
        }
    }

    // The fact: the seeds, over their caption (SR8).
    Column {
        id: factColumn

        anchors.right: parent.right
        anchors.rightMargin: Theme.libraryRowInsetEnd
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(Theme.libraryRowFactWidth, factText.implicitWidth,
                        captionText.implicitWidth)

        Text {
            id: factText

            color: row.health === "few" ? row.ink.yellow : row.health === "good"
                                          ? row.ink.brightForeground : row.ink.dimForeground
            font.family: Theme.valueFamily
            font.features: ({
                                "tnum": 1
                            })
            font.pixelSize: Theme.libraryRowFactSize
            font.weight: Theme.libraryRowFactWeight
            horizontalAlignment: Text.AlignRight
            objectName: "searchRowFact_" + row.index
            text: row.seedsText
            textFormat: Text.PlainText
            width: parent.width
        }

        Text {
            id: captionText

            color: row.ink.dimForeground
            font.capitalization: Font.AllUppercase
            font.family: Theme.labelFamily
            font.letterSpacing: Theme.libraryRowCaptionTracking
            font.pixelSize: Theme.libraryRowCaptionSize
            font.weight: Theme.libraryRowCaptionWeight
            horizontalAlignment: Text.AlignRight
            objectName: "searchRowCaption_" + row.index
            text: row.seedsCaption
            textFormat: Text.PlainText
            width: parent.width
        }
    }
}
