import QtQuick
import QtTest
import Omaseed.Kit

// The Lamp seam (#171, the console skin's design note "Seams"): Omarchy's dot, Console's
// square lamp, `size` across.
TestCase {
    id: testCase

    name: "Lamp"
    visible: true
    when: windowShown

    function cleanup() {
        TestTheme.setSkinChoice("omarchy"); // qmllint disable unqualified
    }

    Component {
        id: lampComponent

        Lamp {}
    }

    function test_the_skins_face_draws_it_at_its_size() {
        const lamp = createTemporaryObject(lampComponent, testCase, {
                                               "size": 9,
                                               "color": "red"
                                           });
        compare(lamp.face.objectName, Theme.skin.name === "console" ? "lampFaceConsole" : "lampFaceOmarchy");
        compare(lamp.width, 9);
        compare(lamp.height, 9);
        compare(lamp.face.width, 9);
        compare(lamp.face.color, Qt.color("red"));
    }

    function test_omarchys_is_round_and_consoles_square() {
        const lamp = createTemporaryObject(lampComponent, testCase, {
                                               "size": 8
                                           });
        if (Theme.skin.name === "omarchy")
            compare(lamp.face.radius, 4);
        TestTheme.setSkinChoice("console");
        compare(lamp.face.objectName, "lampFaceConsole");
        compare(lamp.face.radius, 0);
        compare(lamp.face.width, 8);
    }

    // A face that does not load gives way to Omarchy's, never to nothing.
    function test_a_face_that_fails_falls_back_to_omarchys() {
        const lamp = createTemporaryObject(lampComponent, testCase);
        const loader = lamp.children[0];
        ignoreWarning(new RegExp("NoSuchFace"));
        loader.load(Qt.resolvedUrl("NoSuchFace.qml"));
        compare(lamp.face.objectName, "lampFaceOmarchy");
    }
}
