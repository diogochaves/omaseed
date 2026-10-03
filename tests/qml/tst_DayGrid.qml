import QtQuick
import QtTest
import Omaseed.Kit

TestCase {
    id: testCase

    height: 300
    name: "DayGrid"
    visible: true
    when: windowShown
    width: 1000

    Component {
        id: gridComponent

        DayGrid {}
    }

    SignalSpy {
        id: hoverSpy

        signalName: "dayHovered"
    }

    // `count` contiguous days from y-m-d, each valued by `value(i)`.
    function makeDays(y, m, d, count, value) {
        const out = [];
        for (let i = 0; i < count; i++) {
            const date = new Date(y, m - 1, d + i);
            const iso = date.getFullYear() + "-" + String(date.getMonth() + 1).padStart(2, "0") + "-"
                    + String(date.getDate()).padStart(2, "0");
            out.push({
                         "date": iso,
                         "value": value(i)
                     });
        }
        return out;
    }

    // 26 Aug 2026 is a Wednesday: two empty rows above it in the first column.
    function test_weeks_start_on_monday() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 8, 26, 14, i => i)
                                           });
        compare(grid._offset, 2);
        compare(grid._column(0), 0);
        compare(grid._row(0), 2);
        compare(grid._column(4), 0);
        compare(grid._row(4), 6);
        // Monday 31 Aug opens the second column.
        compare(grid._column(5), 1);
        compare(grid._row(5), 0);
        compare(grid._weeks, 3);
    }

    function test_monday_start_has_no_offset() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 9, 28, 7, i => i)
                                           });
        compare(grid._offset, 0);
        compare(grid._row(0), 0);
        compare(grid._row(6), 6);
        compare(grid._weeks, 1);
    }

    // The board's ramp: 0 is idle; then 22 / 45 / 70 / 100 % at 12 / 30 / 55 % of the max.
    function test_levels_follow_the_ramp() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 9, 28, 3, i => [0, 50, 100][i])
                                           });
        compare(grid._max, 100);
        compare(grid._level(-1), -1);
        compare(grid._level(0), 0);
        compare(grid._level(5), 1);
        compare(grid._level(11.9), 1);
        compare(grid._level(12), 2);
        compare(grid._level(29), 2);
        compare(grid._level(30), 3);
        compare(grid._level(54), 3);
        compare(grid._level(55), 4);
        compare(grid._level(100), 4);
    }

    function test_level_colours() {
        const grid = createTemporaryObject(gridComponent, testCase);
        compare(grid._levelColor(0), Theme.fill(Theme.fillIdle));
        fuzzyCompare(grid._levelColor(1).a, 0.22, 0.01);
        fuzzyCompare(grid._levelColor(2).a, 0.45, 0.01);
        fuzzyCompare(grid._levelColor(3).a, 0.70, 0.01);
        compare(grid._levelColor(4), Theme.stateSeeding);
        compare(grid._levelColor(-1).a, 0);
    }

    function test_nothing_given_is_all_idle() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 9, 28, 3, i => i === 0 ? -1 : 0)
                                           });
        compare(grid._level(0), 0);
        compare(grid._level(-1), -1);
    }

    // August opens column 0; September's first Monday (7 Sep, column 2) is too close and is
    // skipped; October's (5 Oct, column 6) is labelled.
    function test_month_labels_skip_crowded_ones() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 8, 26, 50, i => i)
                                           });
        const labels = grid._monthLabels;
        compare(labels.length, 2);
        compare(labels[0].column, 0);
        compare(labels[0].text, "Aug");
        compare(labels[1].column, 6);
        compare(labels[1].text, "Oct");
    }

    function test_day_labels_are_monday_wednesday_friday() {
        const grid = createTemporaryObject(gridComponent, testCase);
        compare(grid._dayLabels, ["Mon", "Wed", "Fri"]);
    }

    function test_cells_and_index_round_trip() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 8, 26, 30, i => i)
                                           });
        const step = grid.cellSize + grid.gap;
        compare(grid.cellX(0), grid._labelWidth);
        compare(grid.cellY(0), grid._labelHeight + 2 * step);
        for (let i = 0; i < 30; i++)
            compare(grid.indexAt(grid.cellX(i) + 1, grid.cellY(i) + 1), i);
        // In the gap between two cells, on a label, or in the empty rows before the first day.
        compare(grid.indexAt(grid.cellX(0) + grid.cellSize + 1, grid.cellY(0) + 1), -1);
        compare(grid.indexAt(1, grid.cellY(0) + 1), -1);
        compare(grid.indexAt(grid._labelWidth + 1, grid._labelHeight + 1), -1);
    }

    function test_size_follows_the_weeks() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 9, 28, 14, i => i)
                                           });
        compare(grid.cellSize, Theme.space(12));
        compare(grid.gap, Theme.space(3));
        const step = grid.cellSize + grid.gap;
        compare(grid.implicitWidth, grid._labelWidth + 2 * step - grid.gap);
        compare(grid.implicitHeight, grid._labelHeight + 7 * step - grid.gap);
    }

    function test_hover_reports_the_day() {
        const grid = createTemporaryObject(gridComponent, testCase, {
                                               "days": makeDays(2026, 9, 28, 14, i => i)
                                           });
        hoverSpy.target = grid;
        hoverSpy.clear();
        compare(grid.hoveredIndex, -1);
        grid._hoverY = grid.cellY(9) + 2;
        grid._hoverX = grid.cellX(9) + 2;
        compare(grid.hoveredIndex, 9);
        compare(hoverSpy.count, 1);
        compare(hoverSpy.signalArguments[0][0], 9);
        grid._hoverX = -1;
        compare(grid.hoveredIndex, -1);
        compare(hoverSpy.count, 2);
        compare(hoverSpy.signalArguments[1][0], -1);
    }
}
