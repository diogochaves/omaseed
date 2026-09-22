import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// A monochrome line icon drawn from SVG path data on a 24-unit grid, tinted by one Theme
// role (docs/08 §2, option D). It exists because the glyphs it replaces are not in the UI
// font: fontconfig hands them to whatever font has them, and for ⚙ and ⌨ that is the
// colour emoji font, which ignores every Theme role. Typographic symbols (arrows, ✓, ■, ◆…)
// stay text; see `Glyph.qml` for the switch between the two.
//
// Square ends and mitred joins, matching the kit's zero radius.
Item {
    id: root

    /// One of the keys of `_paths` below. An unknown name draws nothing.
    property string name: ""
    property color color: Theme.foreground

    readonly property bool known: root._paths[root.name] !== undefined

    // stroke: outlined sub-paths; fill: solid sub-paths (dots, the filled half of contrast).
    readonly property var _paths: ({
                                       "mark": {
                                           "stroke": "",
                                           "fill": "M1 8 H6 V18 H16 V23 H1 Z M8 1 H23 V16 H18 V6 H8 Z M8 8 H16 V16 H8 Z"
                                       },
                                       "gear": {
                                           "stroke": "M10.14 5.05 L10.64 2.30 L13.36 2.30 L13.86 5.05 L15.60 5.76 L17.90 4.17 L19.83 6.10 L18.24 8.40 L18.95 10.14 L21.70 10.64 L21.70 13.36 L18.95 13.86 L18.24 15.60 L19.83 17.90 L17.90 19.83 L15.60 18.24 L13.86 18.95 L13.36 21.70 L10.64 21.70 L10.14 18.95 L8.40 18.24 L6.10 19.83 L4.17 17.90 L5.76 15.60 L5.05 13.86 L2.30 13.36 L2.30 10.64 L5.05 10.14 L5.76 8.40 L4.17 6.10 L6.10 4.17 L8.40 5.76 Z M15 12 A3 3 0 1 1 9 12 A3 3 0 1 1 15 12 Z",
                                           "fill": ""
                                       },
                                       "keyboard": {
                                           "stroke": "M3 6.5 H21 V17.5 H3 Z M7.5 14 H16.5",
                                           "fill": "M6 9.2 H7.6 V10.8 H6 Z M9.5 9.2 H11.1 V10.8 H9.5 Z M13 9.2 H14.6 V10.8 H13 Z M16.5 9.2 H18.1 V10.8 H16.5 Z"
                                       },
                                       "altSpeed": {
                                           "stroke": "M4.2 17.5 A8.6 8.6 0 1 1 19.8 17.5 M12 14.2 L7.2 10.4",
                                           "fill": ""
                                       },
                                       "jump": {
                                           "stroke": "M16.5 10.5 A6 6 0 1 1 4.5 10.5 A6 6 0 1 1 16.5 10.5 Z M15 15 L20 20",
                                           "fill": ""
                                       },
                                       "checking": {
                                           "stroke": "M19 12 A7 7 0 1 1 16.95 7.05 M19 4.5 V8 H15.5",
                                           "fill": ""
                                       },
                                       "folder": {
                                           "stroke": "M3.5 6 H10 L12 8.2 H20.5 V18.5 H3.5 Z",
                                           "fill": ""
                                       },
                                       "appearance": {
                                           "stroke": "M20 12 A8 8 0 1 1 4 12 A8 8 0 1 1 20 12 Z",
                                           "fill": "M12 4 A8 8 0 0 1 12 20 Z"
                                       },
                                       "server": {
                                           "stroke": "M4 4.5 H20 V10.5 H4 Z M4 13.5 H20 V19.5 H4 Z",
                                           "fill": "M6.7 6.7 H8.3 V8.3 H6.7 Z M6.7 15.7 H8.3 V17.3 H6.7 Z"
                                       },
                                       "more": {
                                           "stroke": "",
                                           "fill": "M4.6 10.6 H7.4 V13.4 H4.6 Z M10.6 10.6 H13.4 V13.4 H10.6 Z M16.6 10.6 H19.4 V13.4 H16.6 Z"
                                       },
                                       "columns": {
                                           "stroke": "M4 5 H20 V19 H4 Z M9.4 5 V19 M14.6 5 V19",
                                           "fill": ""
                                       },
                                       "sidebar": {
                                           "stroke": "M3.5 5 H20.5 V19 H3.5 Z M9 5 V19",
                                           "fill": ""
                                       },
                                       "limits": {
                                           "stroke": "M4 7 H13 M17 7 H20 M15 4.5 V9.5 M4 17 H6 M10 17 H20 M8 14.5 V19.5",
                                           "fill": ""
                                       },
                                       "selectAll": {
                                           "stroke": "M4 4 H20 V20 H4 Z M8 12 L11 15 L16 9",
                                           "fill": ""
                                       }
                                   })

    implicitHeight: Theme.font.icon
    implicitWidth: Theme.font.icon

    Shape {
        id: shape

        // Drawn on the 24-unit grid, then scaled to the item. The stroke is kept near one
        // device pixel at the sizes the kit uses rather than scaling up with the grid.
        readonly property real unit: Math.min(root.width, root.height) / 24

        height: 24
        preferredRendererType: Shape.CurveRenderer
        scale: shape.unit
        transformOrigin: Item.TopLeft
        visible: root.known
        width: 24
        x: (root.width - 24 * shape.unit) / 2
        y: (root.height - 24 * shape.unit) / 2

        ShapePath {
            capStyle: ShapePath.SquareCap
            fillColor: "transparent"
            joinStyle: ShapePath.MiterJoin
            strokeColor: root.known && root._paths[root.name].stroke.length > 0 ? root.color
                                                                                  : "transparent"
            strokeWidth: Math.max(1.5, 1.1 / Math.max(0.01, shape.unit))

            PathSvg {
                path: root.known ? root._paths[root.name].stroke : ""
            }
        }

        ShapePath {
            fillColor: root.known && root._paths[root.name].fill.length > 0 ? root.color
                                                                              : "transparent"
            strokeColor: "transparent"
            strokeWidth: 0

            PathSvg {
                path: root.known ? root._paths[root.name].fill : ""
            }
        }
    }
}
