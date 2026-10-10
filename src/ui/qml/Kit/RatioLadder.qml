pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// The ratio ladder (docs/22 `renderLadder`): every torrent as a dot on a log ratio axis, the
// 1.0 line marked "paid back". `points` are `{ ratio, size, name }`, size in bytes; `name` is
// for the caller's readout and not drawn.
//
// The axis runs from 0.1 to 30 (ratios outside are clamped onto its ends), ticked at 0.1 0.25
// 0.5 1 2 5 10 30, the last reading "30+". The zone below 1.0 has the idle fill. A dot's radius
// is sqrt(size in GiB) × 1.6, clamped to 2.5..11, times the Theme's spacing scale; bigger dots
// are placed first, each at the first free height in a small beeswarm (4-unit steps,
// alternating down and up, at most 40 tries). Below 1.0 a dot is the foreground at 35 %,
// from 1.0 the seeding colour at 75 %. Hover sets `hoveredIndex` (-1 when away), the index
// into `points` of the topmost dot under the pointer; the caller draws the readout.
Item {
    id: root

    property var points: []
    property string paidText: qsTr("paid back")
    property color color: Theme.stateSeeding

    readonly property int hoveredIndex: root._hoverX < 0 ? -1 : root.indexAt(root._hoverX, root._hoverY)

    // Hover position in item coordinates, -1 when the pointer is away. Writable so a test can
    // stand in for the pointer (offscreen delivers no hover).
    property real _hoverX: hover.hovered ? hover.point.position.x : -1
    property real _hoverY: hover.hovered ? hover.point.position.y : -1

    readonly property var _ticks: [0.1, 0.25, 0.5, 1, 2, 5, 10, 30]
    readonly property real _lo: Math.log10(0.1)
    readonly property real _hi: Math.log10(30)
    readonly property real _scale: Theme.space(100) / 100
    readonly property real _padLeft: Theme.space(8)
    readonly property real _padRight: Theme.space(8)
    readonly property real _top: Theme.space(6)
    readonly property real _axisY: Math.max(root._top + 1, root.height - Theme.space(22))
    readonly property real _midY: (root._axisY + root._top) / 2 + Theme.space(4)

    // Every dot in drawing order (largest first): `{ index, x, y, r }`, `index` into `points`.
    // The heights come from Beeswarm (core::Beeswarm, in C++): in JavaScript, testing each
    // dot against all the others took seconds on a library of thousands (#45).
    readonly property var _dots: {
        const pts = root.points || [];
        const order = pts.map((p, i) => i).sort((a, b) => pts[b].size - pts[a].size);
        const xs = order.map(i => root._x(pts[i].ratio));
        const rs = order.map(i => root._radius(pts[i].size));
        const ys = Beeswarm.place(xs, rs, root._midY, 4 * root._scale, 40);
        return order.map((i, k) => ({
                                        "index": i,
                                        "x": xs[k],
                                        "y": ys[k],
                                        "r": rs[k]
                                    }));
    }

    function _x(ratio: real): real {
        const r = Math.min(Math.max(Number.isFinite(ratio) ? ratio : 0.1, 0.1), 30);
        return root._padLeft + (root.width - root._padLeft - root._padRight) * (Math.log10(r) - root._lo)
                / (root._hi - root._lo);
    }

    function _radius(size: real): real {
        const gib = Math.max(0, size) / (1024 * 1024 * 1024);
        return Math.max(2.5, Math.min(11, Math.sqrt(gib) * 1.6)) * root._scale;
    }

    // The topmost dot under a point (the last drawn wins), or -1.
    function indexAt(x: real, y: real): int {
        for (let k = root._dots.length - 1; k >= 0; k--) {
            const d = root._dots[k];
            if (Math.hypot(d.x - x, d.y - y) <= d.r)
                return d.index;
        }
        return -1;
    }

    implicitHeight: Theme.space(150)
    implicitWidth: Theme.space(600)

    HoverHandler {
        id: hover
    }

    // The zone that still owes: 0.1 to 1.0.
    Rectangle {
        color: Theme.fill(Theme.fillIdle)
        height: root._axisY - root._top
        width: root._x(1) - root._x(0.1)
        x: root._x(0.1)
        y: root._top
    }

    Hairline {
        width: root.width - root._padLeft - root._padRight
        x: root._padLeft
        y: Math.round(root._axisY)
    }

    Repeater {
        model: root._ticks

        delegate: Item {
            id: tick

            required property int index
            required property real modelData

            x: Math.round(root._x(tick.modelData))
            y: Math.round(root._axisY)

            Hairline {
                height: Theme.space(4)
                vertical: true
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                text: tick.index === root._ticks.length - 1 ? "30+" : String(tick.modelData)
                x: tick.index === 0 ? 0 : tick.index === root._ticks.length - 1 ? -width : -width / 2
                y: Theme.space(6)
            }
        }
    }

    // Paid back: a dashed line at 1.0.
    Shape {
        anchors.fill: parent

        ShapePath {
            dashPattern: [3, 3]
            fillColor: "transparent"
            strokeColor: Qt.alpha(root.color, 0.8)
            strokeStyle: ShapePath.DashLine
            strokeWidth: 1

            PathPolyline {
                path: [Qt.point(root._x(1), root._top), Qt.point(root._x(1), root._axisY)]
            }
        }
    }

    Text {
        color: root.color
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: root.paidText
        x: root._x(1) + Theme.space(5)
        y: root._top + Theme.space(2)
    }

    // The dots go into one texture, drawn again only when a dot changes: a library's thousands
    // drawn one by one on every frame (the tide's halo pulses all the time) kept the render
    // thread busy (#45). The texture reaches as far past the ladder as a dot can (the 40th
    // try, 20 steps up or down, of the biggest dot), so none is cut off at its edges.
    Item {
        id: dotLayer

        readonly property real reach: Math.ceil(20 * 4 * root._scale + root._radius(Number.MAX_VALUE) + 1)

        height: root.height + 2 * dotLayer.reach
        layer.enabled: true
        visible: root._dots.length > 0
        width: root.width + 2 * dotLayer.reach
        x: -dotLayer.reach
        y: -dotLayer.reach

        Repeater {
            model: root._dots

            delegate: Rectangle {
                id: dot

                required property var modelData

                readonly property bool paid: root.points[dot.modelData.index].ratio >= 1
                readonly property bool hot: root.hoveredIndex === dot.modelData.index

                border.color: Theme.brightForeground
                border.width: dot.hot ? 1 : 0
                color: dot.paid ? root.color : Theme.foreground
                height: dot.modelData.r * 2
                opacity: dot.hot ? 1 : (dot.paid ? 0.75 : 0.35)
                radius: dot.modelData.r
                width: dot.modelData.r * 2
                x: dotLayer.reach + dot.modelData.x - dot.modelData.r
                y: dotLayer.reach + dot.modelData.y - dot.modelData.r
            }
        }
    }
}
