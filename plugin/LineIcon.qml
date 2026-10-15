import QtQuick
import QtQuick.Shapes

// The few line icons the popup draws, from the app's own set (src/ui/qml/Kit/Icon.qml,
// MIT, same author) on its 24-unit grid: square ends, mitred joins, one colour.
Item {
  id: root

  property string name: ""
  property color color: "white"
  property real size: 16

  readonly property var paths: ({
    altSpeed: { stroke: "M4.2 17.5 A8.6 8.6 0 1 1 19.8 17.5 M12 14.2 L7.2 10.4", fill: "" },
    burst: { stroke: "M4.2 17.5 A8.6 8.6 0 1 1 19.8 17.5 M12 14.2 L16.8 10.4", fill: "" },
    gear: { stroke: "M10.14 5.05 L10.64 2.30 L13.36 2.30 L13.86 5.05 L15.60 5.76 L17.90 4.17 L19.83 6.10 L18.24 8.40 L18.95 10.14 L21.70 10.64 L21.70 13.36 L18.95 13.86 L18.24 15.60 L19.83 17.90 L17.90 19.83 L15.60 18.24 L13.86 18.95 L13.36 21.70 L10.64 21.70 L10.14 18.95 L8.40 18.24 L6.10 19.83 L4.17 17.90 L5.76 15.60 L5.05 13.86 L2.30 13.36 L2.30 10.64 L5.05 10.14 L5.76 8.40 L4.17 6.10 L6.10 4.17 L8.40 5.76 Z M15 12 A3 3 0 1 1 9 12 A3 3 0 1 1 15 12 Z", fill: "" },
    folder: { stroke: "M3.5 6 H10 L12 8.2 H20.5 V18.5 H3.5 Z", fill: "" },
    check: { stroke: "M5 12.5 L10 17.5 L19 7", fill: "" },
    mark: { stroke: "", fill: "M1 8 H6 V18 H16 V23 H1 Z M8 1 H23 V16 H18 V6 H8 Z M9 9 H15 V15 H9 Z" },
    open: { stroke: "M8 16 L17 7 M9.5 7 H17 V14.5", fill: "" },
    back: { stroke: "M14.5 5.5 L8 12 L14.5 18.5", fill: "" }
  })

  readonly property var entry: paths[name] || { stroke: "", fill: "" }

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
      capStyle: ShapePath.SquareCap
      joinStyle: ShapePath.MiterJoin
      fillColor: "transparent"
      strokeColor: root.entry.stroke !== "" ? root.color : "transparent"
      strokeWidth: Math.max(1.5, 1.1 / Math.max(0.01, shape.unit))
      PathSvg { path: root.entry.stroke }
    }

    ShapePath {
      strokeColor: "transparent"
      strokeWidth: 0
      fillColor: root.entry.fill !== "" ? root.color : "transparent"
      PathSvg { path: root.entry.fill }
    }
  }
}
