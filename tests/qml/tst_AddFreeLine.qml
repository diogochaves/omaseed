import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Board 64 §5 (DECISIONS C47 I8–I11, #275): the add's room with a temp folder. Two stages,
// each with its own meter and colour; on one drive the second is a dim rename; the first
// stage's switch and its red line's link send this torrent straight to Save to; what the
// downloads in progress still take is said and taken from free. The room is the test's own
// `App.roomFor` map, set on the line as tst_AddDriverLine sets `freeSpaceFor`'s.
TestCase {
    id: testCase

    height: 400
    name: "AddFreeLine"
    visible: true
    when: windowShown
    width: 900

    readonly property real gb: 1000 * 1000 * 1000

    Component {
        id: linesComponent

        AddRoomLines {
            // The add window's form column (C47 I1): both lines have to fit it.
            width: Theme.space(440)
            neededBytes: 8e9
            savePath: "/home/d/tv"
            thresholdBytes: 10e9
            totalBytes: 8e9
        }
    }

    Component {
        id: requestSpy

        SignalSpy {
            signalName: "useTempRequested"
        }
    }

    function drive(where: string, free: real, promised: real, count: int): var {
        return {
            "source": "local",
            "bytesFree": free,
            "bytesTotal": free * 2,
            "where": where,
            "defaultFree": 0,
            "promised": promised,
            "promisedCount": count
        };
    }

    /// `App.roomFor`'s map for a qBittorrent with its temp folder on /fast, Save to on /home.
    function room(changes: var): var {
        const map = {
            "hasTemp": true,
            "switchable": true,
            "useTemp": true,
            "tempPath": "/fast/temp",
            "tempRule": "session",
            "savePath": "/home/d/tv",
            "engine": "qbittorrent",
            "sameDrive": false,
            "preallocates": false,
            "temp": testCase.drive("/fast", 200 * testCase.gb, 0, 0),
            "save": testCase.drive("/home", 1500 * testCase.gb, 0, 0)
        };
        return Object.assign(map, changes || {});
    }

    function lines(changes: var): var {
        return createTemporaryObject(linesComponent, testCase, {
                                         "room": testCase.room(changes)
                                     });
    }

    function what(line: var): string {
        return findChild(line, "addRoomWhat").text;
    }

    function note(line: var): var {
        return findChild(line, "addRoomNote");
    }

    function test_noTempIsTodaysLine() {
        const view = testCase.lines({
                                        "hasTemp": false,
                                        "temp": {}
                                    });
        verify(view.todayLine.visible);
        verify(!view.tempLine.visible);
        verify(!view.saveLine.visible);
        verify(!findChild(view, "addRoomSkipSwitch").visible);
        verify(!findChild(view, "addRoomSkip").visible);
        compare(view.todayLine.needsText, "needs " + App.sizeText(8 * testCase.gb));
    }

    function test_twoLinesTwoMeters() {
        const view = testCase.lines({});
        verify(view.twoStages);
        verify(!view.todayLine.visible);
        verify(view.tempLine.visible && view.saveLine.visible);
        compare(testCase.what(view.tempLine),
                "downloads first to /fast/temp · needs %1 · %2 free on /fast".arg(App.sizeText(8
                                                                                               * testCase.gb)).arg(
                    App.sizeText(200 * testCase.gb)));
        compare(testCase.what(view.saveLine),
                "then moves to /home/d/tv · needs %1 · %2 free on /home".arg(App.sizeText(8
                                                                                          * testCase.gb)).arg(
                    App.sizeText(1500 * testCase.gb)));
        verify(findChild(view.tempLine, "addRoomMeter").visible);
        verify(findChild(view.saveLine, "addRoomMeter").visible);
        compare(view.tempLine.level, "normal");
        compare(view.saveLine.level, "normal");
        compare(testCase.note(view.tempLine).text, "qBittorrent's setting.");
        compare(testCase.note(view.saveLine).text,
                "A copy to another drive when it finishes; the temp drive's room comes back after.");
        // The switch is on, in front of the first line; the second has its arrow.
        verify(findChild(view.tempLine, "addRoomSwitch").visible);
        verify(findChild(view.tempLine, "addRoomSwitch").checked);
        compare(findChild(view.saveLine, "addRoomGlyph").text, "→");
        // Both fit the form column: nothing runs past it.
        verify(view.tempLine.width <= Theme.space(440));
        verify(findChild(view.tempLine, "addRoomWhat").lineCount <= 2);
    }

    function test_sameDriveSecondLineDim() {
        const view = testCase.lines({
                                        "sameDrive": true
                                    });
        compare(view.saveLine.level, "dim");
        compare(testCase.what(view.saveLine),
                "then moved to /home/d/tv on the same drive: a rename, no second room");
        compare(findChild(view.saveLine, "addRoomWhat").color, Theme.dimForeground);
        verify(!findChild(view.saveLine, "addRoomMeter").visible);
        verify(!testCase.note(view.saveLine).visible);
    }

    function test_tempDriveRedOffersStraight() {
        const view = testCase.lines({
                                        "temp": testCase.drive("/fast", 5 * testCase.gb, 0, 0)
                                    });
        const asked = createTemporaryObject(requestSpy, testCase, {
                                                "target": view
                                            });
        compare(view.tempLine.level, "bad");
        compare(findChild(view.tempLine, "addRoomWhat").color, Theme.red);
        compare(view.tempLine.note, "Won't fit with what's still coming.");
        compare(view.tempLine.linkText, "download straight to /home/d/tv");
        verify(testCase.note(view.tempLine).text.indexOf("download straight to /home/d/tv") > 0);
        view.tempLine.activateLink();
        compare(asked.count, 1);
        compare(asked.signalArguments[0][0], false);
        // The second line stays as it was: its drive has the room.
        compare(view.saveLine.level, "normal");
    }

    function test_lowDiskLineYellow() {
        const view = testCase.lines({
                                        "temp": testCase.drive("/fast", 15 * testCase.gb, 0, 0)
                                    });
        compare(view.tempLine.level, "warn");
        compare(findChild(view.tempLine, "addRoomWhat").color, Theme.yellow);
        compare(view.tempLine.note,
                "Leaves less than your low-disk line (%1) while it downloads.".arg(App.sizeText(10
                                                                                                * testCase.gb)));
        compare(view.tempLine.linkText, "");
    }

    function test_saveDriveTooSmallSaysWhy() {
        const small = testCase.drive("/home", 5 * testCase.gb, 0, 0);
        const qbt = testCase.lines({
                                       "save": small
                                   });
        compare(qbt.saveLine.level, "bad");
        compare(findChild(qbt.saveLine, "addRoomWhat").color, Theme.red);
        compare(qbt.saveLine.note,
                "When it finishes, the move to /home won't fit: qBittorrent leaves part of it there and keeps it in the temp folder.");
        const deluge = testCase.lines({
                                          "save": small,
                                          "engine": "deluge"
                                      });
        compare(deluge.saveLine.note,
                "When it finishes, the move to /home won't fit: it stays in the temp folder, part of it copied.");
        compare(testCase.note(deluge.tempLine).text, "Deluge's download folder.");
    }

    function test_promisedSaid() {
        // 25 GB free, 21.4 GB of it still coming: an 8 GB torrent no longer fits (I10).
        const two = testCase.lines({
                                       "temp": testCase.drive("/fast", 25 * testCase.gb, 21.4
                                                              * testCase.gb, 2)
                                   });
        verify(testCase.what(two.tempLine).endsWith(" · %1 of it still coming for 2 downloads".arg(
                                                        App.sizeText(21.4 * testCase.gb))),
               testCase.what(two.tempLine));
        compare(two.tempLine.level, "bad");
        verify(findChild(two.tempLine, "addRoomPromised").width > 0);
        const one = testCase.lines({
                                       "temp": testCase.drive("/fast", 300 * testCase.gb, 3
                                                              * testCase.gb, 1)
                                   });
        verify(testCase.what(one.tempLine).endsWith(" · %1 of it still coming for 1 download".arg(
                                                        App.sizeText(3 * testCase.gb))),
               testCase.what(one.tempLine));
        compare(one.tempLine.level, "normal");
        // Nothing promised: nothing said.
        verify(testCase.what(testCase.lines({}).tempLine).indexOf("still coming") < 0);
    }

    function test_transmissionHasNoSwitch() {
        const view = testCase.lines({
                                        "switchable": false,
                                        "engine": "transmission",
                                        "temp": testCase.drive("/fast", 5 * testCase.gb, 0, 0)
                                    });
        verify(view.twoStages);
        verify(!findChild(view.tempLine, "addRoomSwitch").visible);
        compare(findChild(view.tempLine, "addRoomGlyph").text, "↓");
        verify(findChild(view.tempLine, "addRoomGlyph").visible);
        // Red, but no way straight to Save to: Transmission's folder is session-wide (I9).
        compare(view.tempLine.level, "bad");
        compare(view.tempLine.linkText, "");
        const words = testCase.lines({
                                         "switchable": false,
                                         "engine": "transmission"
                                     });
        compare(testCase.note(words.tempLine).text, "Transmission's setting, for every torrent.");
    }

    function test_switchOffSaysSkips() {
        const on = testCase.lines({});
        const asked = createTemporaryObject(requestSpy, testCase, {
                                                "target": on
                                            });
        findChild(on.tempLine, "addRoomSwitch").toggle();
        compare(asked.count, 1);
        compare(asked.signalArguments[0][0], false);
        // The switch stays what the owner says until the owner says otherwise.
        verify(findChild(on.tempLine, "addRoomSwitch").checked);

        const off = testCase.lines({
                                       "useTemp": false
                                   });
        const back = createTemporaryObject(requestSpy, testCase, {
                                               "target": off
                                           });
        verify(off.skipping);
        verify(!off.tempLine.visible && !off.saveLine.visible);
        verify(off.todayLine.visible);
        const skipSwitch = findChild(off, "addRoomSkipSwitch");
        verify(skipSwitch.visible);
        verify(!skipSwitch.checked);
        const skip = findChild(off, "addRoomSkip");
        compare(skip.note, "This torrent skips /fast/temp: it downloads straight here.");
        compare(skip.linkText, "use the temp folder");
        skip.activateLink();
        skipSwitch.toggle();
        compare(back.count, 2);
        compare(back.signalArguments[0][0], true);
        compare(back.signalArguments[1][0], true);
        verify(!skipSwitch.checked);
    }

    function test_unknownTempFree() {
        const view = testCase.lines({
                                        "temp": {
                                            "source": "unknown",
                                            "bytesFree": 0,
                                            "where": "",
                                            "promised": 0,
                                            "promisedCount": 0
                                        }
                                    });
        verify(testCase.what(view.tempLine).endsWith(" · free space unknown"), testCase.what(
                   view.tempLine));
        compare(view.tempLine.level, "normal");
        verify(!findChild(view.tempLine, "addRoomMeter").visible);
        // The second line still reads its own drive.
        verify(findChild(view.saveLine, "addRoomMeter").visible);
    }

    function test_aBatchOrFilesAlreadyThereKeepTodaysLine() {
        const batch = createTemporaryObject(linesComponent, testCase, {
                                                "room": testCase.room({}),
                                                "batch": true
                                            });
        verify(!batch.twoStages && !batch.skipping);
        verify(batch.todayLine.visible);
        const there = createTemporaryObject(linesComponent, testCase, {
                                                "room": testCase.room({}),
                                                "alreadyThere": true
                                            });
        verify(!there.twoStages);
        compare(there.todayLine.level, "good");
    }
}
