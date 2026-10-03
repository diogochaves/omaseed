pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// "The year" (docs/22 `renderYear`): one cell per day, a column per week starting on Monday,
// seven rows, the way a contributions graph shows commits. `days` are `{ date, value }`,
// `date` as "yyyy-MM-dd", oldest first and contiguous; the last one is today and gets an
// accent outline. A negative value means the day is from before the ledger: outline only.
//
// The chart ramp (board 22's, kept here until Theme grows a function for it): level 0, a
// day that gave nothing, is the idle fill (`Theme.fill(Theme.fillIdle)`, 4 %); above that the
// seeding colour at 22 / 45 / 70 / 100 % alpha, the steps at 12 / 30 / 55 % of the largest
// value. `_level(value)` gives -1 (before the ledger) or 0..4.
//
// Month short names sit above the first column of each month, skipped when closer than three
// columns to the previous one; "Mon", "Wed" and "Fri" sit on the left. Both in the C locale,
// as `Format` writes every date in the app. Hover sets `hoveredIndex` (-1 when away) and emits `dayHovered`; the caller draws
// the readout, placed with `cellX`/`cellY`.
Item {
    id: root

    property var days: []
    property color color: Theme.stateSeeding
    property real cellSize: Theme.space(12)
    property real gap: Theme.space(3)

    readonly property int hoveredIndex: root._hoverX < 0 ? -1 : root.indexAt(root._hoverX, root._hoverY)
    readonly property var _rampAlphas: [0.22, 0.45, 0.70, 1]
    // Rows 0, 2 and 4: Monday, Wednesday, Friday (QML's Locale counts from Sunday = 0).
    readonly property var _dayLabels: [1, 3, 5].map(d => Qt.locale("C").dayName(d, Locale.ShortFormat))

    // Hover position in item coordinates, -1 when the pointer is away. Writable so a test can
    // stand in for the pointer (offscreen delivers no hover).
    property real _hoverX: hover.hovered ? hover.point.position.x : -1
    property real _hoverY: hover.hovered ? hover.point.position.y : -1

    readonly property real _step: root.cellSize + root.gap
    readonly property real _labelWidth: Theme.space(26)
    readonly property real _labelHeight: Theme.space(16)
    // Monday-based weekday of the first day: the rows above it in the first column are empty.
    readonly property int _offset: root.days.length === 0 ? 0 : (root._date(0).getDay() + 6) % 7
    readonly property int _weeks: Math.ceil((root.days.length + root._offset) / 7)
    readonly property real _max: {
        let m = 0;
        for (const d of root.days) {
            if (d.value > m)
                m = d.value;
        }
        return m;
    }
    readonly property var _monthLabels: {
        const out = [];
        let lastMonth = -1;
        let lastColumn = -99;
        for (let i = 0; i < root.days.length; i++) {
            if (i > 0 && root._row(i) !== 0)
                continue;
            const date = root._date(i);
            const month = date.getMonth();
            if (month === lastMonth)
                continue;
            lastMonth = month;
            const column = root._column(i);
            if (column - lastColumn < 3)
                continue;
            lastColumn = column;
            out.push({
                         "column": column,
                         "text": Qt.locale("C").standaloneMonthName(month, Locale.ShortFormat)
                     });
        }
        return out;
    }

    signal dayHovered(int index)

    function _date(index: int): var {
        const parts = String(root.days[index].date).split("-");
        return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
    }

    function _column(index: int): int {
        return Math.floor((index + root._offset) / 7);
    }

    function _row(index: int): int {
        return (index + root._offset) % 7;
    }

    function _level(value: real): int {
        if (value < 0)
            return -1;
        if (value <= 0 || root._max <= 0)
            return 0;
        const share = value / root._max;
        if (share < 0.12)
            return 1;
        if (share < 0.30)
            return 2;
        if (share < 0.55)
            return 3;
        return 4;
    }

    function _levelColor(level: int): color {
        if (level <= 0)
            return level === 0 ? Theme.fill(Theme.fillIdle) : "transparent";
        return Qt.alpha(root.color, root._rampAlphas[level - 1]);
    }

    function cellX(index: int): real {
        return root._labelWidth + root._column(index) * root._step;
    }

    function cellY(index: int): real {
        return root._labelHeight + root._row(index) * root._step;
    }

    // The day under a point, or -1 over a gap, a label or an empty cell.
    function indexAt(x: real, y: real): int {
        const cx = x - root._labelWidth;
        const cy = y - root._labelHeight;
        if (cx < 0 || cy < 0)
            return -1;
        const column = Math.floor(cx / root._step);
        const row = Math.floor(cy / root._step);
        if (row > 6 || cx - column * root._step >= root.cellSize || cy - row * root._step >= root.cellSize)
            return -1;
        const index = column * 7 + row - root._offset;
        return index >= 0 && index < root.days.length ? index : -1;
    }

    implicitHeight: root._labelHeight + 7 * root._step - root.gap
    implicitWidth: root._labelWidth + Math.max(0, root._weeks * root._step - root.gap)

    onHoveredIndexChanged: root.dayHovered(root.hoveredIndex)

    HoverHandler {
        id: hover
    }

    Repeater {
        model: root._monthLabels

        delegate: Text {
            id: monthLabel

            required property var modelData

            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: monthLabel.modelData.text
            x: root._labelWidth + monthLabel.modelData.column * root._step
            y: 0
        }
    }

    Repeater {
        model: root._dayLabels

        delegate: Text {
            id: dayLabel

            required property int index
            required property string modelData

            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            text: dayLabel.modelData
            x: 0
            y: root._labelHeight + dayLabel.index * 2 * root._step + (root.cellSize - height) / 2
        }
    }

    Repeater {
        model: root.days

        delegate: Rectangle {
            id: cell

            required property int index
            required property var modelData

            readonly property int level: root._level(cell.modelData.value)
            readonly property bool today: cell.index === root.days.length - 1

            border.color: cell.today ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
            border.width: cell.today ? 1.5 : (cell.level < 0 ? 1 : 0)
            color: root._levelColor(cell.level)
            height: root.cellSize
            width: root.cellSize
            x: root.cellX(cell.index)
            y: root.cellY(cell.index)
        }
    }
}
