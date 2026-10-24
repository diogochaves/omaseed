import QtQuick
import Omaseed.Kit

// Console's Meter (Kit/Meter.qml; board 46 `.strip i`, `.card .grid i`): cells, never a bar.
// A line is one row of cells `Theme.meterCellWidth` wide (as many as its width holds); a grid
// is the seam's `columns` × `rows`. Filled cells are the seam's colour at full strength, the
// rest the skin's faint line; `trackColor` and `fillAlpha` are Omarchy's.
Canvas {
    id: face

    required property Meter seam

    readonly property bool line: face.seam.columns <= 0
    readonly property int columns: face.line ? Math.max(1, Math.floor((face.width + face.seam.cellGap)
                                                                      / (Theme.meterCellWidth + face.seam.cellGap)))
                                             : face.seam.columns
    readonly property int rows: face.line ? 1 : face.seam.rows
    readonly property real cellHeight: face.line ? face.height : face.seam.cellHeight
    readonly property int have: Math.floor(face.seam.clamped * face.columns * face.rows)
    readonly property color filled: face.seam.color
    readonly property color track: Theme.faintColor

    objectName: "meterFaceConsole"

    onCellHeightChanged: face.requestPaint()
    onColumnsChanged: face.requestPaint()
    onFilledChanged: face.requestPaint()
    onHaveChanged: face.requestPaint()
    onPaint: {
        const context = face.getContext("2d");
        context.clearRect(0, 0, face.width, face.height);
        const gap = face.seam.cellGap;
        const cell = (face.width - gap * (face.columns - 1)) / face.columns;
        for (let i = 0; i < face.columns * face.rows; ++i) {
            context.fillStyle = i < face.have ? face.filled : face.track;
            context.fillRect((i % face.columns) * (cell + gap), Math.floor(i / face.columns) * (face.cellHeight + gap),
                             cell, face.cellHeight);
        }
    }
    onTrackChanged: face.requestPaint()
    onWidthChanged: face.requestPaint()
}
