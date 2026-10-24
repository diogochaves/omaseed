import QtQuick
import QtTest
import Omaseed.Kit

// The SelectionMark seam (#171, the console skin's design note "Seams"): Omarchy's face is a
// leading rule when asked for one, Console's four corner brackets in every case.
TestCase {
    id: testCase

    name: "SelectionMark"
    visible: true
    when: windowShown

    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    Component {
        id: markComponent

        SelectionMark {
            height: 40
            width: 200
        }
    }

    function test_the_skins_face_draws_it() {
        const mark = createTemporaryObject(markComponent, testCase);
        compare(mark.face.objectName, Theme.skin.name === "console" ? "selectionMarkFaceConsole"
                                                                    : "selectionMarkFaceOmarchy");
        compare(mark.color, Theme.litColor);
        compare(mark.ruleWidth, Theme.litWidth);
    }

    function test_omarchys_face_is_the_leading_rule() {
        if (Theme.skin.name !== "omarchy")
            skip("Omarchy's face is drawn under Omarchy's skin only");
        const mark = createTemporaryObject(markComponent, testCase, {
                                               "ruleWidth": 3
                                           });
        const rule = mark.face.children[0];
        verify(!rule.visible);
        mark.rule = true;
        verify(rule.visible);
        compare(rule.width, 3);
        compare(rule.height, mark.height);
        compare(rule.x, 0);
        compare(rule.color, mark.color);
    }

    function test_consoles_face_is_four_brackets() {
        TestTheme.setSkinChoice("console");
        const mark = createTemporaryObject(markComponent, testCase);
        compare(mark.face.objectName, "selectionMarkFaceConsole");
        const corners = [];
        for (let i = 0; i < mark.face.children.length; ++i) {
            if (mark.face.children[i].objectName === "selectionMarkCorner")
                corners.push(mark.face.children[i]);
        }
        compare(corners.length, 4);
        compare(corners[3].x, mark.width - Theme.selectionMarkArm);
        compare(corners[3].y, mark.height - Theme.selectionMarkArm);
        compare(corners[0].children[0].height, Theme.litWidth);
        compare(corners[0].children[0].color, mark.color);
        verify(corners.every(corner => corner.visible));
        // On a cut plate the two corners in the cut are left out.
        mark.cut = Theme.plateKeyCut;
        verify(corners[0].visible && corners[3].visible);
        verify(!corners[1].visible && !corners[2].visible);
    }
}
