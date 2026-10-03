import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// Diagonal hatching ("/") over the item's own rectangle, clipped to it: docs/22's
// `pattern#hatch` (the tide's closed-app spans and "before the ledger") and the keeping-alive
// strip's `.b6` stripes. The caller sizes it; it draws nothing else. Lines run from bottom
// left to top right, `spacing` apart along the x axis, `lineWidth` thick, in `color`
// (the foreground at the selected fill, 18 %, by default).
Item {
    id: root

    property color color: Theme.fill(Theme.fillSelected)
    property real spacing: Theme.space(6)
    property real lineWidth: 1

    // One "M x h L x+h 0" sub-path per stripe, starting a full height to the left so the
    // corners are covered too.
    readonly property string _path: {
        const w = root.width;
        const h = root.height;
        const step = Math.max(1, root.spacing);
        if (w <= 0 || h <= 0)
            return "";
        let d = "";
        for (let x = -h; x < w + step; x += step)
            d += "M" + x.toFixed(2) + " " + h.toFixed(2) + " L" + (x + h).toFixed(2) + " 0 ";
        return d;
    }
    readonly property int _lineCount: root._path.length === 0 ? 0 : root._path.split("M").length - 1

    clip: true

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            capStyle: ShapePath.FlatCap
            fillColor: "transparent"
            strokeColor: root.color
            strokeWidth: root.lineWidth

            PathSvg {
                path: root._path
            }
        }
    }
}
