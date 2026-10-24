pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The details' one fact and its sentence (board 46 `details()`'s `.dhero`, DECISIONS C24 H6):
// under the name, the one thing that matters about the torrent now, large and in its state's
// colour ("7 min", "2.4×", "no access"), its caption beside it ("to ready", "given", "needs
// you"), then one sentence ("48.9 % of 480 MB · from 3 seeds at 1.40 MB/s."). The words are
// the rows display's (core/RowWords, through `App.torrentRow()`'s rowFact, rowCaption and
// rowSentence), so the library and the details never say two things about one torrent.
//
//     DetailsHero { torrent: App.torrentRow(hash) }
Item {
    id: root

    /// `App.torrentRow()`'s map for the details torrent, or null.
    property var torrent: null
    /// The hairline under it: off under the table, where the band's own rule closes it.
    property bool rule: true

    readonly property bool hasTorrent: root.torrent !== null && root.torrent !== undefined && (
                                           root.torrent.rowFact ?? "").length > 0
    readonly property string factText: root.hasTorrent ? root.torrent.rowFact : ""
    readonly property string captionText: root.hasTorrent ? (root.torrent.rowCaption ?? "") : ""
    /// The row's sentence, as a sentence: a capital, and a full stop.
    readonly property string sentenceText: {
        const words = root.hasTorrent ? (root.torrent.rowSentence ?? "") : "";
        if (words.length === 0)
            return "";
        const first = words.charAt(0).toUpperCase() + words.slice(1);
        return /[.!?…]$/.test(first) ? first : first + ".";
    }

    // #79: the fact with its caption as the name, the sentence as its description.
    Accessible.description: root.sentenceText
    Accessible.name: [root.factText, root.captionText].filter(part => part.length > 0).join(" ")
    Accessible.role: Accessible.StaticText
    height: implicitHeight
    implicitHeight: root.hasTorrent ? column.implicitHeight + Theme.detailsHeroPaddingTop
                                      + Theme.detailsHeroPaddingBottom : 0
    objectName: "detailsHero"
    visible: root.hasTorrent

    Column {
        id: column

        anchors.left: parent.left
        anchors.leftMargin: Theme.detailsHeroInset
        anchors.right: parent.right
        anchors.rightMargin: Theme.detailsHeroInset
        anchors.top: parent.top
        anchors.topMargin: Theme.detailsHeroPaddingTop
        spacing: Theme.detailsHeroGap

        Item {
            height: factLabel.implicitHeight
            width: parent.width

            Text {
                id: factLabel

                color: root.hasTorrent ? Theme[root.torrent.stateRole] ?? Theme.foreground :
                                         Theme.foreground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.features: ({
                                    "tnum": 1
                                })
                font.pixelSize: Theme.detailsFactSize
                font.weight: Theme.detailsFactWeight
                objectName: "detailsFact"
                text: root.factText
                textFormat: Text.PlainText
                width: Math.min(implicitWidth, parent.width)
            }

            Text {
                anchors.baseline: factLabel.baseline
                anchors.left: factLabel.right
                anchors.leftMargin: Theme.detailsCaptionGap
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.detailsCaptionSize
                objectName: "detailsCaption"
                text: root.captionText
                textFormat: Text.PlainText
                width: Math.max(0, parent.width - factLabel.width - Theme.detailsCaptionGap)
            }
        }

        Text {
            color: Theme.foreground
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.detailsSentenceSize
            maximumLineCount: Theme.detailsSentenceLines
            objectName: "detailsSentence"
            text: root.sentenceText
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.rule && root.hasTorrent
    }
}
