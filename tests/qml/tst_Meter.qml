import QtQuick
import QtTest
import Omaseed.Kit

// The Meter seam (#171, the console skin's design note "Seams"): a line or a grid of cells.
TestCase {
    id: testCase

    name: "Meter"
    visible: true
    when: windowShown

    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    Component {
        id: meterComponent

        Meter {
            height: 4
            width: 155
        }
    }

    function test_the_skins_face_draws_it() {
        const meter = createTemporaryObject(meterComponent, testCase);
        compare(meter.face.objectName, Theme.skin.name === "console" ? "meterFaceConsole" : "meterFaceOmarchy");
    }

    function test_the_value_is_held_to_zero_to_one() {
        const meter = createTemporaryObject(meterComponent, testCase, {
                                                "value": 1.4
                                            });
        compare(meter.clamped, 1);
        meter.value = -0.2;
        compare(meter.clamped, 0);
    }

    function test_omarchys_line_is_a_bar_on_a_track() {
        if (Theme.skin.name !== "omarchy")
            skip("Omarchy's face is drawn under Omarchy's skin only");
        const meter = createTemporaryObject(meterComponent, testCase, {
                                                "value": 0.5,
                                                "color": "red",
                                                "trackColor": "blue"
                                            });
        const track = meter.face.children[0];
        verify(track.visible);
        compare(track.color, Qt.color("blue"));
        compare(track.children[0].width, Math.round(155 * 0.5));
        compare(track.children[0].color, Qt.color("red"));
    }

    function test_omarchys_grid_is_cells() {
        if (Theme.skin.name !== "omarchy")
            skip("Omarchy's face is drawn under Omarchy's skin only");
        const meter = createTemporaryObject(meterComponent, testCase, {
                                                "value": 0.25,
                                                "columns": 10,
                                                "rows": 6,
                                                "cellHeight": 6,
                                                "fillAlpha": 0.55
                                            });
        verify(!meter.face.children[0].visible);
        const grid = findChild(meter, "meterGrid");
        verify(grid !== null);
        compare(grid.have, 15);
        compare(grid.filled.a.toFixed(2), "0.55");
    }

    function test_consoles_line_is_cells() {
        TestTheme.setSkinChoice("console");
        const meter = createTemporaryObject(meterComponent, testCase, {
                                                "value": 0.5,
                                                "color": "red",
                                                "fillAlpha": 0.55
                                            });
        compare(meter.face.objectName, "meterFaceConsole");
        const pitch = Theme.meterCellWidth + Theme.meterCellGap;
        compare(meter.face.columns, Math.floor((155 + Theme.meterCellGap) / pitch));
        compare(meter.face.rows, 1);
        compare(meter.face.have, Math.floor(meter.face.columns * 0.5));
        // The board's fill at full strength over the faint line.
        compare(meter.face.filled, Qt.color("red"));
        compare(meter.face.track, Theme.faintColor);
        meter.columns = 9;
        meter.rows = 6;
        compare(meter.face.columns, 9);
        compare(meter.face.have, 27);
    }
}
