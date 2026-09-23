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
// pixel (a cell is green only when every piece in it is had). The canvas paints one
// rectangle per run of equal cells, so a 50 000-piece torrent is still a handful of fills.
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

    onDownloadingColorChanged: canvas.requestPaint()
    onHaveColorChanged: canvas.requestPaint()

    Item {
        id: labels

        height: pieceLabel.implicitHeight
        width: parent.width

        Label {
            id: pieceLabel

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

    Rectangle {
        id: track

        anchors.top: labels.bottom
        anchors.topMargin: Theme.space(4)
        color: Theme.fill(Theme.fillIdle)
        height: Theme.space(10)
        width: parent.width

        Canvas {
            id: canvas

            /// What was painted last, for the tests.
            property var cells: []

            anchors.fill: parent
            objectName: "piecesCanvas"

            onHeightChanged: canvas.requestPaint()
            onPaint: {
                const ctx = canvas.getContext("2d");
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                const cells = root.hasPieces ? root.details.pieceBuckets(Math.floor(canvas.width))
                                             : [];
                canvas.cells = cells;
                const n = cells.length;
                if (n === 0)
                    return;
                const step = canvas.width / n;
                let start = 0;
                for (let i = 1; i <= n; ++i) {
                    if (i < n && cells[i] === cells[start])
                        continue;
                    const state = cells[start];
                    if (state !== 0) {
                        ctx.fillStyle = state === 2 ? root.haveColor : root.downloadingColor;
                        const x = Math.round(start * step);
                        ctx.fillRect(x, 0, Math.round(i * step) - x, canvas.height);
                    }
                    start = i;
                }
            }
            onWidthChanged: canvas.requestPaint()
        }
    }

    Connections {
        function onPieceStatesChanged(): void {
            canvas.requestPaint();
        }

        target: root.details
    }
}
