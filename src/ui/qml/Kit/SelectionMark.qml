import QtQuick
import Omaseed.Kit

// A seam (the console skin's design note, "Seams"): what marks the current or the selected
// thing, laid over it (filling it) and shown while it is marked (`visible`). Omarchy's face
// is today's: a rule of `ruleWidth` down the leading edge when `rule`, else nothing (the
// ground or the plate's edge says it). Console's face is board 46's corner brackets
// (`.row.sel::after`): four `Theme.selectionMarkArm` arms `Theme.litWidth` thick, whether or
// not Omarchy's draws a rule.
Item {
    id: root

    property color color: Theme.litColor
    /// Omarchy's face draws its leading rule.
    property bool rule: false
    property int ruleWidth: Theme.litWidth
    /// The corner cut of the plate it marks (`Theme.plateKeyCut` on a cover), 0 for none: a
    /// face that draws corners leaves out the two the plate cuts away, as board 46's clip
    /// does.
    property int cut: 0

    /// The face drawn, for tests.
    readonly property Item face: loader.item as Item

    FaceLoader {
        id: loader

        anchors.fill: parent
        seam: root
        seamName: "SelectionMark"
    }
}
