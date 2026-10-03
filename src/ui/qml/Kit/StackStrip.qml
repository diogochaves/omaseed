pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// One row of proportional blocks (docs/22 `.strip`, "keeping alive": how many torrents have
// one seed, two, three to five…). `segments` are `{ value, color, hatched }`; each non-zero
// segment gets a block as wide as its share of the total, at least `Theme.space(3)`, with
// `Theme.space(2)` between blocks. Zero (or negative) segments are skipped, gap and all.
// `hatched` draws diagonal stripes in the foreground at 18 % (the board's `.b6`, "unknown")
// over the segment's `color`, which may be left out for stripes alone.
Item {
    id: root

    property var segments: []

    readonly property real _gap: Theme.space(2)
    readonly property real _minWidth: Theme.space(3)
    // One `{ x, width }` per segment, in order; a skipped segment has width 0.
    readonly property var _blocks: {
        const segs = root.segments || [];
        const shown = [];
        let total = 0;
        for (let i = 0; i < segs.length; i++) {
            if (segs[i].value > 0) {
                shown.push(i);
                total += segs[i].value;
            }
        }
        const out = segs.map(() => ({
                                        "x": 0,
                                        "width": 0
                                    }));
        if (shown.length === 0 || root.width <= 0)
            return out;
        const available = Math.max(0, root.width - root._gap * (shown.length - 1));
        // Proportional widths, lifted to the minimum; what the lifted ones took is then taken
        // back from the others in proportion to how far above the minimum they are.
        const widths = shown.map(i => Math.max(root._minWidth, available * segs[i].value / total));
        const excess = widths.reduce((a, w) => a + w, 0) - available;
        const slack = widths.reduce((a, w) => a + Math.max(0, w - root._minWidth), 0);
        if (excess > 0 && slack > 0) {
            const cut = Math.min(1, excess / slack);
            for (let k = 0; k < widths.length; k++)
                widths[k] -= Math.max(0, widths[k] - root._minWidth) * cut;
        }
        let x = 0;
        for (let k = 0; k < shown.length; k++) {
            out[shown[k]] = {
                "x": x,
                "width": widths[k]
            };
            x += widths[k] + root._gap;
        }
        return out;
    }

    implicitHeight: Theme.space(12)
    implicitWidth: Theme.space(240)

    Repeater {
        model: root.segments

        delegate: Rectangle {
            id: block

            required property int index
            required property var modelData

            readonly property var box: root._blocks[block.index] || {
                "x": 0,
                "width": 0
            }

            color: block.modelData.color !== undefined ? block.modelData.color : "transparent"
            height: root.height
            visible: block.box.width > 0
            width: block.box.width
            x: block.box.x

            Hatch {
                anchors.fill: parent
                color: Theme.fill(Theme.fillSelected)
                lineWidth: Theme.space(2)
                spacing: Theme.space(5) * Math.SQRT2
                visible: block.modelData.hatched === true
            }
        }
    }
}
