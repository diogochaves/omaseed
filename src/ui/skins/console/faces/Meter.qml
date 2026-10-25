import QtQuick
import Omaseed.Kit

// Console's Meter (Kit/Meter.qml; board 46 `.strip i`, `.card .grid i`): cells, never a bar.
// A line is one row of cells `Theme.meterCellWidth` wide (as many as its width holds); a grid
// is the seam's `columns` × `rows`. Filled cells are the seam's colour at full strength, the
// rest the skin's faint line; `trackColor` and `fillAlpha` are Omarchy's. A line of `cells`
// is a slot per cell, had in the colour and arriving in `arrivingColor` (board 46 `.pmap i`).
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
    readonly property color arriving: face.seam.arrivingColor
    readonly property var states: face.seam.cells
    /// A line of states is a slot per cell.
    readonly property int slots: face.line ? face.columns : face.columns * face.rows

    objectName: "meterFaceConsole"

    Binding {
        property: "slots"
        target: face.seam
        value: face.slots
    }

    onCellHeightChanged: face.requestPaint()
    onColumnsChanged: face.requestPaint()
    onFilledChanged: face.requestPaint()
    onHaveChanged: face.requestPaint()
    onPaint: {
        const context = face.getContext("2d");
        context.clearRect(0, 0, face.width, face.height);
        const gap = face.seam.cellGap;
        const cell = (face.width - gap * (face.columns - 1)) / face.columns;
        const states = face.states;
        const byState = face.line && states.length > 0;
        for (let i = 0; i < face.columns * face.rows; ++i) {
            if (byState)
                context.fillStyle = states[i] === 2 ? face.filled : states[i] === 1 ? face.arriving : face.track;
            else
                context.fillStyle = i < face.have ? face.filled : face.track;
            context.fillRect((i % face.columns) * (cell + gap), Math.floor(i / face.columns) * (face.cellHeight + gap),
                             cell, face.cellHeight);
        }
    }
    onArrivingChanged: face.requestPaint()
    onStatesChanged: face.requestPaint()
    onTrackChanged: face.requestPaint()
    onWidthChanged: face.requestPaint()
}
