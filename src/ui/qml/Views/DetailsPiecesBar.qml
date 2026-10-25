pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The General tab's pieces bar (docs/12 G3): "pieces · 617 of 1 264" over one bar
// `Theme.space(10)` tall, with the percent at the right. The bar is drawn from
// `torrents/pieceStates`: had pieces in the theme's green, pieces in flight in the accent,
// and missing ones left empty over the track's 4 % fill.
//
// With more pieces than pixels, TorrentDetails::pieceBuckets() folds them into one cell per
// pixel (a cell is green only when every piece in it is had), or one per cell of a skin that
// draws cells. Omarchy's Meter paints one rectangle per run of equal cells, so a 50 000-piece
// torrent is still a handful of fills.
Item {
    id: root

    required property TorrentDetails details
    /// "617 of 1 264", from TorrentDetails.properties.piecesHave.
    property string haveText: ""
    /// "48.9 %", the torrent's own progress, the same figure as the strip's "done".
    property string percentText: ""

    readonly property bool hasPieces: root.details !== null && root.details.pieceCount > 0
    readonly property color haveColor: Theme.green
    readonly property color downloadingColor: Theme.accent

    implicitHeight: labels.height + Theme.space(4) + track.height
    visible: root.hasPieces

    onHasPiecesChanged: track.refresh()

    Item {
        id: labels

        height: pieceLabel.implicitHeight
        width: parent.width

        Label {
            id: pieceLabel

            // #79: "pieces", what it has and the percent the description: both move while it
            // downloads. The percent's own label is the same figure and stays out.
            Accessible.description: [root.haveText, root.percentText].filter(part => part.length > 0).join(" · ")
            Accessible.name: qsTr("pieces")
            Accessible.role: Accessible.StaticText
            labelId: "pieces"
            objectName: "piecesLabel"
            text: root.haveText.length > 0 ? qsTr("pieces · %1").arg(root.haveText) : qsTr(
                                                                                          "pieces")
            tracked: true
        }

        Label {
            anchors.right: parent.right
            objectName: "piecesPercent"
            text: root.percentText
            tracked: true
        }
    }

    // The skin's Meter draws it (#171 step 6): a slot per pixel in Omarchy's face, a cell
    // each in Console's (board 46 `.pmap`). `cells` keeps what was asked for last, for the
    // tests.
    Meter {
        id: track

        function refresh(): void {
            track.cells = root.hasPieces && track.slots > 0 ? root.details.pieceBuckets(track.slots) : [];
        }

        anchors.top: labels.bottom
        anchors.topMargin: Theme.space(4)
        arrivingColor: root.downloadingColor
        color: root.haveColor
        height: Theme.space(10)
        objectName: "piecesCanvas"
        trackColor: Theme.on.idle.fill
        width: parent.width

        onSlotsChanged: track.refresh()
    }

    Connections {
        function onPieceStatesChanged(): void {
            track.refresh();
        }

        target: root.details
    }
}
