pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// Omarchy's Meter (Kit/Meter.qml): the rows' progress line (a track and a bar, rounded to
// whole pixels unless the seam says not to: the table's progress cell) and the cover's grid
// (cells in reading order), as they drew them.
Item {
    id: face

    required property Meter seam

    readonly property color filled: face.seam.fillAlpha < 1 ? Qt.alpha(face.seam.color, face.seam.fillAlpha)
                                                            : face.seam.color

    /// A line of cells is a slot per pixel (DetailsPiecesBar's canvas, as it was).
    readonly property int slots: Math.floor(face.width)

    objectName: "meterFaceOmarchy"

    Binding {
        property: "slots"
        target: face.seam
        value: face.slots
    }

    Rectangle {
        anchors.fill: parent
        color: face.seam.trackColor
        visible: face.seam.columns <= 0

        Rectangle {
            color: face.filled
            visible: face.seam.cells.length === 0
            height: parent.height
            width: face.seam.snap ? Math.round(parent.width * face.seam.clamped) : parent.width * face.seam.clamped
        }
    }

    // A line of states: one rectangle per run of equal slots, had in the colour, arriving in
    // `arrivingColor`, missing left on the track.
    Loader {
        active: face.seam.columns <= 0 && face.seam.cells.length > 0
        anchors.fill: parent

        sourceComponent: Canvas {
            id: runs

            readonly property var cells: face.seam.cells
            readonly property color have: face.filled
            readonly property color arriving: face.seam.arrivingColor

            objectName: "meterRuns"

            onArrivingChanged: runs.requestPaint()
            onCellsChanged: runs.requestPaint()
            onHaveChanged: runs.requestPaint()
            onHeightChanged: runs.requestPaint()
            onPaint: {
                const ctx = runs.getContext("2d");
                ctx.clearRect(0, 0, runs.width, runs.height);
                const cells = runs.cells;
                const n = cells.length;
                if (n === 0)
                    return;
                const step = runs.width / n;
                let start = 0;
                for (let i = 1; i <= n; ++i) {
                    if (i < n && cells[i] === cells[start])
                        continue;
                    const state = cells[start];
                    if (state !== 0) {
                        ctx.fillStyle = state === 2 ? runs.have : runs.arriving;
                        const x = Math.round(start * step);
                        ctx.fillRect(x, 0, Math.round(i * step) - x, runs.height);
                    }
                    start = i;
                }
            }
            onWidthChanged: runs.requestPaint()
        }
    }

    Loader {
        active: face.seam.columns > 0
        anchors.fill: parent

        sourceComponent: Canvas {
            id: grid

            readonly property color filled: face.filled
            readonly property color track: face.seam.trackColor
            readonly property int have: Math.floor(face.seam.clamped * face.seam.columns * face.seam.rows)

            objectName: "meterGrid"

            onFilledChanged: grid.requestPaint()
            onHaveChanged: grid.requestPaint()
            onPaint: {
                const context = grid.getContext("2d");
                context.clearRect(0, 0, grid.width, grid.height);
                const columns = face.seam.columns;
                const gap = face.seam.cellGap;
                const cell = (grid.width - gap * (columns - 1)) / columns;
                for (let i = 0; i < columns * face.seam.rows; ++i) {
                    context.fillStyle = i < grid.have ? grid.filled : grid.track;
                    context.fillRect((i % columns) * (cell + gap), Math.floor(i / columns) * (face.seam.cellHeight + gap),
                                     cell, face.seam.cellHeight);
                }
            }
            onTrackChanged: grid.requestPaint()
            onWidthChanged: grid.requestPaint()
        }
    }
}
