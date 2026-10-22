import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// DECISIONS C18 D1–D3 (board 45 §2): the outcome strip on its own. Its colour per outcome,
// what it offers, the 6 s of a done strip (held while the pointer rests on it), and that a
// click on it never reaches the row under it. Where it sits and when it shows are
// MainView's (tst_Actions).
TestCase {
    id: testCase

    height: 300
    name: "OutcomeStrip"
    visible: true
    when: windowShown
    width: 900

    // Stands for the table row under the strip.
    MouseArea {
        id: under

        property int clicks: 0

        anchors.fill: parent

        onClicked: under.clicks = under.clicks + 1
    }

    OutcomeStrip {
        id: strip

        anchors.centerIn: parent
        availableWidth: 800
        doneSeconds: 1
        height: implicitHeight
        width: implicitWidth
    }

    SignalSpy {
        id: expiredSpy

        signalName: "expired"
        target: strip
    }

    SignalSpy {
        id: showSpy

        signalName: "showRequested"
        target: strip
    }

    SignalSpy {
        id: moveSpy

        signalName: "moveRequested"
        target: strip
    }

    readonly property var done: ({
                                     "kind": "done",
                                     "mark": "✓",
                                     "headline": "Arcade Set 01 moved to /srv/library/archive",
                                     "line": "",
                                     "details": "",
                                     "action": "move",
                                     "doneRow": "moved · archive",
                                     "doneHashes": ["a"],
                                     "failedHashes": [],
                                     "serial": 1
                                 })
    readonly property var failed: ({
                                       "kind": "failed",
                                       "mark": "✕",
                                       "headline": "Short Film 07 didn't move",
                                       "line": "qBittorrent can't write to /srv/locked",
                                       "details": "Permission denied",
                                       "action": "move",
                                       "target": "/srv/locked",
                                       "doneRow": "moved · locked",
                                       "doneHashes": [],
                                       "failedHashes": ["b"],
                                       "serial": 2
                                   })

    function laidOut() {
        waitForItemPolished(findChild(strip, "outcomeContent"));
        waitForItemPolished(findChild(strip, "outcomeActions"));
    }

    function init() {
        mouseMove(testCase, 1, 1);
        strip.outcome = {};
        under.clicks = 0;
        expiredSpy.clear();
        showSpy.clear();
        moveSpy.clear();
    }

    function test_nothing_to_say_shows_nothing() {
        verify(!strip.visible);
    }

    function test_done_is_green_with_show_and_its_line_runs_down() {
        strip.outcome = testCase.done;
        verify(strip.visible);
        compare(findChild(strip, "outcomeMark").color, Theme.green);
        verify(findChild(strip, "outcomeShow").visible);
        verify(!findChild(strip, "outcomeDetails").visible);
        verify(!findChild(strip, "outcomeMove").visible);
        const line = findChild(strip, "outcomeTimeLine");
        verify(line.visible);
        tryVerify(() => line.width < strip.width, 800, "the line never ran down");        tryCompare(expiredSpy, "count", 1, 2000);
        compare(expiredSpy.signalArguments[0][0], 1);
        laidOut();
        mouseClick(findChild(strip, "outcomeShow"));
        compare(showSpy.count, 1);
        compare(showSpy.signalArguments[0][0], ["a"]);
        compare(under.clicks, 0, "the click went through to the row under the strip");
    }

    function test_the_pointer_holds_a_done_strip() {
        strip.outcome = testCase.done;
        laidOut();
        mouseMove(strip, strip.width / 2, strip.height / 2);
        wait(1300);
        compare(expiredSpy.count, 0, "it went from under the pointer");
        mouseMove(testCase, 1, 1);
        tryCompare(expiredSpy, "count", 1, 2000);
    }

    function test_a_failure_is_red_and_stays() {
        strip.outcome = testCase.failed;
        compare(findChild(strip, "outcomeMark").color, Theme.red);
        compare(findChild(strip, "outcomeLine").text, "qBittorrent can't write to /srv/locked");
        verify(!findChild(strip, "outcomeTimeLine").visible);
        verify(!findChild(strip, "outcomeShow").visible);
        verify(findChild(strip, "outcomeDetails").visible);
        laidOut();
        mouseClick(findChild(strip, "outcomeMove"));
        compare(moveSpy.count, 1);
        compare(moveSpy.signalArguments[0][0], ["b"]);
        compare(moveSpy.signalArguments[0][1], "/srv/locked");
        wait(1300);
        compare(expiredSpy.count, 0, "a failure has no timer");
    }

    function test_some_failed_is_yellow() {
        const mixed = Object.assign({}, testCase.failed, {
                                        "kind": "mixed",
                                        "mark": "!",
                                        "doneHashes": ["a"]
                                    });
        strip.outcome = mixed;
        compare(findChild(strip, "outcomeMark").color, Theme.yellow);
        verify(findChild(strip, "outcomeShow").visible);
        verify(!findChild(strip, "outcomeMove").visible);
    }

    function test_it_never_grows_past_its_room() {
        strip.outcome = Object.assign({}, testCase.failed, {
                                          "line": "x".repeat(400)
                                      });
        verify(strip.width <= strip.availableWidth);
        laidOut();
        const close = findChild(strip, "outcomeClose");
        verify(close.mapToItem(strip, close.width, 0).x <= strip.width, "× fell off the end");
    }
}
