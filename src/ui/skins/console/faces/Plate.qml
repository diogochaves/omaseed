import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// Console's Plate (Kit/Plate.qml; board 46 `.pn`, `.pn.key`): the top right and bottom left
// corners cut by the skin's panel or key cut, `fill` over `base` inside the edge, and the edge
// all round, in the plate's `rule` when it has one, else its `edgeColor`. `radius` and `edges`
// are Omarchy's and not drawn here.
Shape {
    id: face

    required property Plate seam

    readonly property real cut: Math.max(0, Math.min(face.seam.key ? Theme.plateKeyCut : Theme.plateCut,
                                                     face.width / 2, face.height / 2))
    readonly property color line: face.seam.rule.a > 0 ? face.seam.rule : face.seam.edgeColor
    readonly property real edge: face.seam.edgeWidth

    /// The plate's outline inset by `inset` on every side, its cut corners kept at 45°.
    function outline(inset: real): list<point> {
        const w = face.width;
        const h = face.height;
        // A 45° cut moves in by inset·(√2 − 1) along each side when the outline does.
        const c = Math.max(0, face.cut - inset * (Math.SQRT2 - 1));
        return [Qt.point(inset, inset), Qt.point(w - inset - c, inset), Qt.point(w - inset, inset + c),
                Qt.point(w - inset, h - inset), Qt.point(inset + c, h - inset), Qt.point(inset, h - inset - c),
                Qt.point(inset, inset)];
    }

    objectName: "plateFaceConsole"
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        fillColor: face.seam.base
        strokeColor: "transparent"
        strokeWidth: 0

        PathPolyline {
            path: face.outline(0)
        }
    }

    ShapePath {
        fillColor: face.seam.fill
        strokeColor: "transparent"
        strokeWidth: 0

        PathPolyline {
            path: face.outline(face.edge)
        }
    }

    ShapePath {
        capStyle: ShapePath.FlatCap
        fillColor: "transparent"
        joinStyle: ShapePath.MiterJoin
        strokeColor: face.edge > 0 ? face.line : "transparent"
        strokeWidth: face.edge

        PathPolyline {
            path: face.outline(face.edge / 2)
        }
    }
}
