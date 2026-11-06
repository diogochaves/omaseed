import QtQuick
import Omaseed.Kit

// A seam (the console skin's design note, "Seams"): how far something has come, `value` from
// 0 to 1. With no `columns` it is a line (the rows' progress line): Omarchy's face is a track
// with a bar, Console's board 46's segmented strip (`.strip i`, cells `Theme.meterCellWidth`
// wide). With `columns` it is a grid of `columns` × `rows` cells `cellHeight` tall, filled in
// reading order (the cover's grid). Omarchy draws the fill in `color` at `fillAlpha` over
// `trackColor`; Console draws the board's: the fill at full strength over the faint line.
//
// A line or a grid may be drawn from `cells` instead of `value` (the details' pieces bar,
// #171 step 6): one state per slot, 0 missing, 1 arriving (`arrivingColor`), 2 had (`color`).
// The face says how many slots it draws (`slots`: a pixel each in Omarchy's line, a cell each
// in Console's line and grid), so the caller folds its states into that many. Only Console's
// face draws a grid of states; a skin asks for one by `meter.piecesColumns`.
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
    /// A line's bar ends on a whole pixel (the rows' line); false draws it to the fraction (the
    /// table's progress cell, which always did).
    property bool snap: true
    /// A line's states, one per slot; empty draws `value`.
    property var cells: []
    property color arrivingColor: Theme.accent

    /// How many slots the face draws a line of `cells` as: the face binds it (a pixel each
    /// until one does).
    property int slots: Math.floor(root.width)

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
