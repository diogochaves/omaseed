import QtQuick
import QtQuick.Shapes

// The omaseed exchange mark as an activity readout (contract §2): the app's mark
// (src/ui/qml/Kit/Icon.qml, "mark") split in its three parts on the same 24-unit grid.
//   the lower-left arrow  lights while downloading
//   the upper-right arrow lights while uploading
//   the square            carries the link: plain, an outline (alt-speed, or an
//                         unreachable server), or solid urgent (torrents in error)
Item {
  id: root

  property real size: 16
  property bool downLit: false
  property bool upLit: false
  property color downColor: "white"
  property color upColor: "white"
  // An unlit arrow: the foreground at 30 %.
  property color offColor: "gray"
  property color squareColor: "white"
  // true draws the square as an outline in `squareColor`.
  property bool squareOutline: false

  implicitWidth: size
  implicitHeight: size

  Shape {
    id: shape

    readonly property real unit: Math.min(root.width, root.height) / 24

    width: 24
    height: 24
    scale: unit
    transformOrigin: Item.TopLeft
    x: (root.width - 24 * unit) / 2
    y: (root.height - 24 * unit) / 2
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
      strokeColor: "transparent"
      strokeWidth: 0
      fillColor: root.downLit ? root.downColor : root.offColor
      PathSvg { path: "M1 8 H6 V18 H16 V23 H1 Z" }
    }

    ShapePath {
      strokeColor: "transparent"
      strokeWidth: 0
      fillColor: root.upLit ? root.upColor : root.offColor
      PathSvg { path: "M8 1 H23 V16 H18 V6 H8 Z" }
    }

    // The outline is the square with its middle cut out, filled even-odd, so it keeps
    // the mark's hard edges at every size.
    ShapePath {
      strokeColor: "transparent"
      strokeWidth: 0
      fillRule: ShapePath.OddEvenFill
      fillColor: root.squareColor
      PathSvg { path: root.squareOutline ? "M9 9 H15 V15 H9 Z M10.6 10.6 H13.4 V13.4 H10.6 Z" : "M9 9 H15 V15 H9 Z" }
    }
  }
}
