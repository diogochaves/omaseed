import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// A small trend line with a filled area beneath it (docs/22 `.hstat .spark`, the hero's
// faint upload trace, and `.tk svg.sp`, the trackers' 30-day column). `values` are plain
// numbers, oldest first, spread evenly across the width; a value that is not a finite number
// counts as 0. `maxValue` fixes the top of the scale; at 0 or below it is the largest value.
// Fewer than two values draw nothing. The hero passes the defaults (line 55 %, area 8 %);
// the trackers' table passes `lineAlpha: 1` and `fillAlpha: 0.12`.
Item {
    id: root

    property var values: []
    property color color: Theme.stateSeeding
    property real maxValue: 0
    property real lineAlpha: 0.55
    property real fillAlpha: 0.08

    readonly property real _max: {
        if (root.maxValue > 0)
            return root.maxValue;
        let m = 0;
        for (const v of root.values) {
            if (Number.isFinite(v) && v > m)
                m = v;
        }
        return m;
    }
    readonly property bool _drawn: root.values.length >= 2 && root.width > 0 && root.height > 0
    // The line's vertices, half a pixel inside the top and bottom so the 1 px stroke is whole.
    readonly property var _points: {
        if (!root._drawn)
            return [];
        const n = root.values.length;
        const out = [];
        for (let i = 0; i < n; i++)
            out.push(Qt.point(root.width * i / (n - 1), root._yAt(root.values[i])));
        return out;
    }
    // The line closed down to the bottom edge.
    readonly property var _areaPoints: root._points.length === 0 ? [] : root._points.concat([
                                                                                             Qt.point(root.width, root.height),
                                                                                             Qt.point(0, root.height)
                                                                                         ])

    function _yAt(value: var): real {
        const v = Number.isFinite(value) ? Math.max(0, Math.min(value, root._max)) : 0;
        const fraction = root._max > 0 ? v / root._max : 0;
        return root.height - 0.5 - fraction * (root.height - 1);
    }

    implicitHeight: Theme.space(30)
    implicitWidth: Theme.space(100)

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        visible: root._drawn

        ShapePath {
            fillColor: Qt.alpha(root.color, root.fillAlpha)
            strokeColor: "transparent"
            strokeWidth: 0

            PathPolyline {
                path: root._areaPoints
            }
        }

        ShapePath {
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            strokeColor: Qt.alpha(root.color, root.lineAlpha)
            strokeWidth: 1

            PathPolyline {
                path: root._points
            }
        }
    }
}
