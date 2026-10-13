import QtQuick
import QtTest
import Omaseed.Kit

// T4.1. `Kit/Scrim.qml`: the 70 % ground behind every overlay (spec v2 §3 `--scrim`).
TestCase {
    id: testCase

    height: 300
    name: "Scrim"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: scrimComponent

        Scrim {
            anchors.fill: parent
        }
    }

    function test_paints_the_background_at_seventy_percent() {
        const scrim = createTemporaryObject(scrimComponent, testCase);
        compare(scrim.scrimAlpha, 0.7);
        fuzzyCompare(scrim.color.a, 0.7, 0.01);
        fuzzyCompare(scrim.color.r, Theme.background.r, 0.01);
        fuzzyCompare(scrim.color.g, Theme.background.g, 0.01);
        fuzzyCompare(scrim.color.b, Theme.background.b, 0.01);
    }

    function test_covers_what_it_is_anchored_over() {
        const scrim = createTemporaryObject(scrimComponent, testCase);
        compare(scrim.width, testCase.width);
        compare(scrim.height, testCase.height);
    }

    function test_reports_a_click_on_itself_as_a_dismissal() {
        const scrim = createTemporaryObject(scrimComponent, testCase);
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": scrim
                                          });
        scrim.dismissed();
        compare(spy.count, 1);
    }

    // Every button dismisses, the right one too, unless `passRightClick` asks for it as
    // `rightClicked(x, y)` (#51 D17).
    function test_a_right_click_dismisses_unless_it_is_passed_on() {
        const scrim = createTemporaryObject(scrimComponent, testCase);
        const dismissed = createTemporaryObject(spyComponent, testCase, {
                                                    "target": scrim
                                                });
        const right = createTemporaryObject(rightSpyComponent, testCase, {
                                                "target": scrim
                                            });
        waitForRendering(scrim, 250);
        mouseClick(scrim, 50, 60, Qt.RightButton);
        compare(dismissed.count, 1);
        compare(right.count, 0);
        scrim.passRightClick = true;
        mouseClick(scrim, 50, 60, Qt.RightButton);
        compare(dismissed.count, 1);
        compare(right.count, 1);
        compare(right.signalArguments[0][0], 50);
        compare(right.signalArguments[0][1], 60);
        mouseClick(scrim, 50, 60);
        compare(dismissed.count, 2);
    }

    Component {
        id: spyComponent

        SignalSpy {
            signalName: "dismissed"
        }
    }

    Component {
        id: rightSpyComponent

        SignalSpy {
            signalName: "rightClicked"
        }
    }
}
