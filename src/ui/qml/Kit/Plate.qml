import QtQuick
import Omaseed.Kit

// A seam (the console skin's design note, "Seams"): a panel's ground and edge, under a sheet,
// a menu, a notice or a cover. Omarchy's face is today's drawing: a rectangle of `fill` over
// `base`, its edge round it (or on the `edges` given, as hairlines), at `radius`, with `rule`
// down its leading edge. Console's face is board 46's cut plate (`.pn`, `.pn.key` when `key`):
// two corners cut by `Theme.plateCut`/`plateKeyCut`, its edge all round, in `rule`'s colour
// when there is one (`.notice { --pc: var(--nc) }`). The caller lays it under its content,
// filling it.
Item {
    id: root

    /// What is under the ground (a band that must hide what it covers: the window).
    property color base: "transparent"
    /// The ground.
    property color fill: "transparent"
    /// The edge, and its width (0 draws none).
    property color edgeColor: Theme.edgeColor
    property int edgeWidth: Theme.edgeWidth
    /// The sides the edge is on (Qt.TopEdge | …). All four is one outline; fewer are drawn as
    /// rules on those sides by a face that tells them apart (Omarchy's).
    property int edges: Qt.TopEdge | Qt.RightEdge | Qt.BottomEdge | Qt.LeftEdge
    property real radius: Theme.cornerRadius
    /// A small plate (a key, a card): the skin's key cut rather than its panel cut.
    property bool key: false
    /// The plate's kind, when it has one (a notice's): a rule down the leading edge in
    /// Omarchy's face, the whole edge in Console's. Transparent is none.
    property color rule: "transparent"
    property int ruleWidth: Theme.space(2)

    /// The face drawn, for tests.
    readonly property Item face: loader.item as Item

    FaceLoader {
        id: loader

        anchors.fill: parent
        seam: root
        seamName: "Plate"
    }
}
