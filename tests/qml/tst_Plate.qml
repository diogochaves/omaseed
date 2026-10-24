import QtQuick
import QtTest
import Omaseed.Kit

// The Plate seam (#171, the console skin's design note "Seams"): the skin's face draws it,
// with the seam's API; Omarchy's face is the default, and a live switch reloads it.
TestCase {
    id: testCase

    name: "Plate"
    visible: true
    when: windowShown

    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    // The face the skin drawn names (the Console run pins Console).
    function expectedFace(): string {
        return Theme.skin.name === "console" ? "plateFaceConsole" : "plateFaceOmarchy";
    }

    Component {
        id: plateComponent

        Plate {
            height: 60
            width: 120
        }
    }

    function test_the_skins_face_draws_it() {
        const plate = createTemporaryObject(plateComponent, testCase);
        verify(plate.face !== null);
        compare(plate.face.objectName, testCase.expectedFace());
        compare(plate.face.width, plate.width);
        compare(plate.face.height, plate.height);
    }

    function test_its_defaults_are_the_skins_edge() {
        const plate = createTemporaryObject(plateComponent, testCase);
        compare(plate.edgeColor, Theme.edgeColor);
        compare(plate.edgeWidth, Theme.edgeWidth);
        compare(plate.radius, Theme.cornerRadius);
        compare(plate.rule.a, 0);
        verify(!plate.key);
    }

    function test_a_switch_reloads_the_face() {
        const plate = createTemporaryObject(plateComponent, testCase);
        TestTheme.setSkinChoice("console");
        compare(plate.face.objectName, "plateFaceConsole");
        compare(plate.face.seam, plate);
    }

    // Omarchy's face is the drawing it replaced: a rectangle with the edge round it, or the
    // edges given as rules, and the kind's rule down the leading edge.
    function test_omarchys_face_draws_the_rectangle() {
        if (Theme.skin.name !== "omarchy")
            skip("Omarchy's face is drawn under Omarchy's skin only");
        const plate = createTemporaryObject(plateComponent, testCase, {
                                                "fill": "red",
                                                "edgeColor": "blue",
                                                "edgeWidth": 2
                                            });
        const ground = plate.face.children[1];
        compare(ground.color, Qt.color("red"));
        compare(ground.border.color, Qt.color("blue"));
        compare(ground.border.width, 2);
        plate.edges = Qt.BottomEdge;
        compare(ground.border.width, 0);
        verify(plate.face.children[3].visible);  // the bottom rule
        verify(!plate.face.children[4].visible); // no top rule
        verify(!plate.face.children[2].visible); // no kind, no leading rule
        plate.rule = "green";
        verify(plate.face.children[2].visible);
        compare(plate.face.children[2].width, plate.ruleWidth);
    }

    function test_consoles_face_cuts_two_corners() {
        TestTheme.setSkinChoice("console");
        const plate = createTemporaryObject(plateComponent, testCase);
        compare(plate.face.cut, Theme.plateCut);
        verify(Theme.plateCut > 0);
        plate.key = true;
        compare(plate.face.cut, Theme.plateKeyCut);
        verify(Theme.plateKeyCut < Theme.plateCut);
        // The kind's colour is the whole edge.
        compare(plate.face.line, plate.edgeColor);
        plate.rule = "green";
        compare(plate.face.line, Qt.color("green"));
    }
}
