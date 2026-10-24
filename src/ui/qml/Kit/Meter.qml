import QtQuick
import Omaseed.Kit

// A seam (the console skin's design note, "Seams"): how far something has come, `value` from
// 0 to 1. With no `columns` it is a line (the rows' progress line): Omarchy's face is a track
// with a bar, Console's board 46's segmented strip (`.strip i`, cells `Theme.meterCellWidth`
// wide). With `columns` it is a grid of `columns` × `rows` cells `cellHeight` tall, filled in
// reading order (the cover's grid). Omarchy draws the fill in `color` at `fillAlpha` over
// `trackColor`; Console draws the board's: the fill at full strength over the faint line.
Item {
    id: root

    property real value: 0
    property color color: Theme.accent
    property real fillAlpha: 1
    property color trackColor: Theme.fill(Theme.hairlineAlpha)
    /// 0 for a line; else the grid's columns.
    property int columns: 0
    property int rows: 1
    property real cellHeight: root.height
    property real cellGap: Theme.meterCellGap

    /// The value held to 0..1.
    readonly property real clamped: Math.max(0, Math.min(1, root.value))
    /// The face drawn, for tests.
    readonly property Item face: loader.item as Item

    FaceLoader {
        id: loader

        anchors.fill: parent
        seam: root
        seamName: "Meter"
    }
}
