pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// Omarchy's Meter (Kit/Meter.qml): the rows' progress line (a track and a bar, rounded to
// whole pixels) and the cover's grid (cells in reading order), as they drew them.
Item {
    id: face

    required property Meter seam

    readonly property color filled: face.seam.fillAlpha < 1 ? Qt.alpha(face.seam.color, face.seam.fillAlpha)
                                                            : face.seam.color

    objectName: "meterFaceOmarchy"

    Rectangle {
        anchors.fill: parent
        color: face.seam.trackColor
        visible: face.seam.columns <= 0

        Rectangle {
            color: face.filled
            height: parent.height
            width: Math.round(parent.width * face.seam.clamped)
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
