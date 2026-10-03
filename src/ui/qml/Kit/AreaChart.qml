pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Omaseed.Kit

// "The tide" (docs/22 `renderTide`): upload above a baseline, download below it, one point
// per bucket, oldest first, spread evenly across the plot.
//
// `points`: `{ up, down, gap }`, rates in bytes/s. `up`/`down` null means the bucket is from
// before the history began; such points are expected only at the start. `gap: true` marks a
// bucket whose amount is known but whose spread is not (the app was closed): every run of
// them gets a hatched band, and `gapCaption(first, last)` text inside it when the band is
// wide enough. Points before the first known one are hatched more faintly, with `beforeText`.
//
// Scale: upload gets `upShare` of the plot's height above the baseline, scaled to its own
// maximum; download gets the rest below it, scaled to max(maxDown, maxUp / 2) so a small
// download never towers over the upload. Hairline grid at half and full upload, labelled by
// `formatValue` in the right gutter. A dashed marker at the upload peak, a dot at the last
// point with a slow halo (only while the chart is visible and `animate` is set).
//
// `labels`: `{ index, text }` for the time axis, made by the caller; `nowText` is drawn at
// the last index, right-aligned, and a caller label that would run into it is hidden.
// Hover sets `hoveredIndex` (-1 when not hovering) and draws a vertical crosshair; the caller
// draws the readout, placed with `xAt(hoveredIndex)`.
Item {
    id: root

    property var points: []
    property var labels: []
    property var formatValue: function (v) {
        return String(v);
    }
    property var gapCaption: function (firstIndex, lastIndex) {
        return "";
    }
    property color upColor: Theme.stateSeeding
    property color downColor: Theme.stateDownloading
    property real upShare: 0.66
    property string beforeText: qsTr("before the ledger")
    property string nowText: qsTr("now")
    /// `%1` is `formatValue(peak)`.
    property string peakText: qsTr("peak %1")
    property bool animate: true

    readonly property int hoveredIndex: root._hoverX >= 0 && root._hoverX <= root._right && root._geo.n > 0
                                        ? root.indexAt(root._hoverX) : -1

    // Hover position in item coordinates, -1 when the pointer is away. Writable so a test can
    // stand in for the pointer (offscreen delivers no hover).
    property real _hoverX: hover.hovered ? hover.point.position.x : -1

    readonly property real _left: 0
    readonly property real _right: Math.max(root._left + 1, root.width - Theme.space(54))
    readonly property real _top: Theme.space(8)
    readonly property real _bottom: Math.max(root._top + 1, root.height - Theme.space(22))

    readonly property var _geo: {
        const pts = root.points || [];
        const n = pts.length;
        let maxUp = 0;
        let maxDown = 0;
        let known = 0;
        let firstKnown = -1;
        let peakIndex = -1;
        const gaps = [];
        for (let i = 0; i < n; i++) {
            const p = pts[i];
            if (p.up !== null && p.up !== undefined) {
                known++;
                if (firstKnown < 0)
                    firstKnown = i;
                if (peakIndex < 0 || p.up > pts[peakIndex].up)
                    peakIndex = i;
                maxUp = Math.max(maxUp, p.up);
            }
            if (p.down !== null && p.down !== undefined)
                maxDown = Math.max(maxDown, p.down);
            if (p.gap) {
                if (gaps.length > 0 && gaps[gaps.length - 1].last === i - 1)
                    gaps[gaps.length - 1].last = i;
                else
                    gaps.push({
                                  "first": i,
                                  "last": i
                              });
            }
        }
        const scaleUp = Math.max(1, maxUp);
        const mid = root._top + (root._bottom - root._top) * root.upShare;
        return {
            "n": n,
            "known": known,
            "firstKnown": firstKnown,
            "peakIndex": maxUp > 0 ? peakIndex : -1,
            "maxUp": scaleUp,
            "maxDown": Math.max(1, maxDown),
            "mid": mid,
            "sU": (mid - root._top) / scaleUp,
            "sD": (root._bottom - mid) / Math.max(1, maxDown, scaleUp * 0.5),
            "gaps": gaps
        };
    }

    readonly property var _upLine: root._line("up", 1, root._geo.sU)
    readonly property var _downLine: root._line("down", -1, root._geo.sD)
    readonly property var _upArea: root._area(root._upLine)
    readonly property var _downArea: root._area(root._downLine)

    readonly property int _lastIndex: root._geo.n - 1
    readonly property bool _lastKnown: root._lastIndex >= 0 && root.points[root._lastIndex].up !== null
                                       && root.points[root._lastIndex].up !== undefined
    readonly property real _lastY: root._lastKnown ? root._upY(root.points[root._lastIndex].up) : root._geo.mid
    readonly property bool _haloRunning: root.visible && root.animate && root._lastKnown

    function xAt(index: int): real {
        const n = root._geo.n;
        if (n <= 1)
            return root._left;
        return root._left + (root._right - root._left) * index / (n - 1);
    }

    function indexAt(x: real): int {
        const n = root._geo.n;
        if (n === 0)
            return -1;
        if (n === 1)
            return 0;
        const k = Math.round((x - root._left) / (root._right - root._left) * (n - 1));
        return Math.max(0, Math.min(n - 1, k));
    }

    function _upY(value: real): real {
        return root._geo.mid - value * root._geo.sU;
    }

    function _downY(value: real): real {
        return root._geo.mid + value * root._geo.sD;
    }

    // The vertices of one series, skipping the points from before the history began.
    function _line(key: string, sign: int, scale: real): var {
        const out = [];
        const pts = root.points || [];
        for (let i = 0; i < pts.length; i++) {
            const v = pts[i][key];
            if (v === null || v === undefined)
                continue;
            out.push(Qt.point(root.xAt(i), root._geo.mid - sign * v * scale));
        }
        return out;
    }

    // A series closed down (or up) to the baseline.
    function _area(line: var): var {
        if (line.length < 2)
            return [];
        return line.concat([Qt.point(line[line.length - 1].x, root._geo.mid), Qt.point(line[0].x, root._geo.mid)]);
    }

    implicitHeight: Theme.space(190)
    implicitWidth: Theme.space(600)

    HoverHandler {
        id: hover
    }

    // ---- grid: upload at half and full, labelled in the right gutter -------------------
    Repeater {
        model: root._geo.known > 0 ? [0.5, 1] : []

        delegate: Item {
            id: gridLine

            required property real modelData

            readonly property real lineY: root._upY(root._geo.maxUp * gridLine.modelData)

            Hairline {
                width: root._right - root._left
                x: root._left
                y: Math.round(gridLine.lineY)
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                text: root.formatValue(root._geo.maxUp * gridLine.modelData)
                x: root._right + Theme.space(6)
                y: gridLine.lineY - height / 2
            }
        }
    }

    Text {
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: "0"
        visible: root._geo.known > 0
        x: root._right + Theme.space(6)
        y: root._geo.mid - height / 2
    }

    // ---- hatched spans: the app was closed ---------------------------------------------
    Repeater {
        model: root._geo.gaps

        delegate: Item {
            id: gapBand

            required property var modelData

            readonly property string caption: root.gapCaption(gapBand.modelData.first, gapBand.modelData.last)

            height: root._bottom - root._top
            width: Math.max(2, root.xAt(gapBand.modelData.last) - root.xAt(gapBand.modelData.first))
            x: root.xAt(gapBand.modelData.first)
            y: root._top

            Hatch {
                anchors.fill: parent
            }

            Text {
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.caption
                text: gapBand.caption
                visible: gapBand.caption.length > 0 && gapBand.width > Theme.space(150)
                width: gapBand.width - Theme.space(12)
                x: Theme.space(6)
                y: Theme.space(4)
            }
        }
    }

    // ---- before the history began --------------------------------------------------------
    Item {
        id: before

        readonly property int endIndex: root._geo.firstKnown < 0 ? root._lastIndex : root._geo.firstKnown

        height: root._bottom - root._top
        visible: root._geo.n > 1 && before.endIndex > 0
        width: root.xAt(before.endIndex) - root._left
        x: root._left
        y: root._top

        Hatch {
            anchors.fill: parent
            opacity: 0.5
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(8)
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: root.beforeText
            // Only where the faint band can hold it.
            visible: width + Theme.space(16) <= before.width
            y: root._geo.mid - root._top - Theme.space(4) - height
        }
    }

    // ---- the two series ------------------------------------------------------------------
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: Qt.alpha(root.downColor, Theme.fillSelected)
            strokeColor: "transparent"
            strokeWidth: 0

            PathPolyline {
                path: root._downArea
            }
        }

        ShapePath {
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            strokeColor: root._downLine.length > 1 ? root.downColor : "transparent"
            strokeWidth: 1.5

            PathPolyline {
                path: root._downLine
            }
        }

        ShapePath {
            fillColor: Qt.alpha(root.upColor, Theme.fillSelected)
            strokeColor: "transparent"
            strokeWidth: 0

            PathPolyline {
                path: root._upArea
            }
        }

        ShapePath {
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            strokeColor: root._upLine.length > 1 ? root.upColor : "transparent"
            strokeWidth: 1.5

            PathPolyline {
                path: root._upLine
            }
        }
    }

    // The baseline, the dim foreground at 60 % as the board's `.tide .base`.
    Rectangle {
        color: Qt.alpha(Theme.dimForeground, 0.6)
        height: 1
        width: root._right - root._left
        x: root._left
        y: Math.round(root._geo.mid)
    }

    // ---- the peak --------------------------------------------------------------------------
    Item {
        id: peak

        readonly property int index: root._geo.peakIndex
        readonly property real peakX: peak.index < 0 ? 0 : root.xAt(peak.index)
        readonly property real peakY: peak.index < 0 ? 0 : root._upY(root.points[peak.index].up)

        anchors.fill: parent
        visible: peak.index >= 0

        Shape {
            anchors.fill: parent

            ShapePath {
                dashPattern: [2, 3]
                fillColor: "transparent"
                strokeColor: Qt.alpha(root.upColor, 0.6)
                strokeStyle: ShapePath.DashLine
                strokeWidth: 1

                PathPolyline {
                    path: [Qt.point(peak.peakX, peak.peakY), Qt.point(peak.peakX, root._geo.mid)]
                }
            }
        }

        Text {
            color: root.upColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: peak.index < 0 ? "" : root.peakText.arg(root.formatValue(root.points[peak.index].up))
            x: Math.max(root._left, Math.min(peak.peakX + Theme.space(5), root._right - width))
            y: Math.max(0, peak.peakY - Theme.space(4) - height)
        }
    }

    // ---- now: the last point, with a slow halo ---------------------------------------------
    Rectangle {
        id: halo

        color: root.upColor
        height: Theme.space(6)
        opacity: 0
        radius: height / 2
        visible: root._haloRunning
        width: height
        x: root.xAt(root._lastIndex) - width / 2
        y: root._lastY - height / 2

        ParallelAnimation {
            loops: Animation.Infinite
            running: root._haloRunning

            NumberAnimation {
                duration: 2400
                easing.type: Easing.OutQuad
                from: 1
                property: "scale"
                target: halo
                to: 3.2
            }

            NumberAnimation {
                duration: 2400
                easing.type: Easing.OutQuad
                from: 0.35
                property: "opacity"
                target: halo
                to: 0
            }
        }
    }

    Rectangle {
        color: root.upColor
        height: Theme.space(6)
        radius: height / 2
        visible: root._lastKnown
        width: height
        x: root.xAt(root._lastIndex) - width / 2
        y: root._lastY - height / 2
    }

    // ---- time axis -------------------------------------------------------------------------
    Text {
        id: nowLabel

        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.caption
        text: root.nowText
        visible: root._geo.n > 0
        x: root.xAt(root._lastIndex) - width
        y: root.height - height - Theme.space(2)
    }

    Repeater {
        model: root.labels

        delegate: Text {
            id: axisLabel

            required property var modelData

            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: axisLabel.modelData.text
            visible: axisLabel.modelData.index >= 0 && axisLabel.modelData.index < root._geo.n
                     && axisLabel.x + axisLabel.width <= nowLabel.x - Theme.space(6)
            x: root.xAt(axisLabel.modelData.index)
            y: nowLabel.y
        }
    }

    // ---- hover crosshair -------------------------------------------------------------------
    Rectangle {
        color: Theme.fill(Theme.borderIdle)
        height: root._bottom - root._top
        visible: root.hoveredIndex >= 0
        width: 1
        x: Math.round(root.xAt(Math.max(0, root.hoveredIndex)))
        y: root._top
    }
}
