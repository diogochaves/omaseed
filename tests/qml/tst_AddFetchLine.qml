import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// #44. `Views/AddFetchLine.qml`: the add sheet's Files row while a magnet's list is not in yet
// (docs/29 §4, F1, F3-F5, F7, F8), one phase at a time.
TestCase {
    id: testCase

    name: "AddFetchLine"
    visible: true
    when: windowShown

    Component {
        id: lineComponent

        AddFetchLine {
            serverName: "box"
            width: 560
        }
    }

    function make(phase, seconds, kind) {
        return createTemporaryObject(lineComponent, testCase, {
                                         "phase": phase,
                                         "seconds": seconds || 0,
                                         "kind": kind || "magnet"
                                     });
    }

    // D2 a and D11 a: a quick answer, the tree, an old server, a refused source: nothing.
    function test_only_the_waits_show() {
        for (const phase of ["", "idle", "quiet", "done", "unsupported", "failed"]) {
            const line = testCase.make(phase);
            compare(line.shown, false, phase);
            compare(line.visible, false, phase);
            compare(line.summaryText, "", phase);
        }
        for (const phase of ["waiting", "fetching", "slow", "never", "private"]) {
            const line = testCase.make(phase);
            compare(line.shown, true, phase);
            compare(line.visible, true, phase);
            verify(line.headline.length > 0, phase);
            verify(line.lines.length > 0, phase);
            verify(line.height > 0, phase);
        }
    }

    function test_the_seconds_read_as_the_board_writes_them() {
        const line = testCase.make("fetching");
        compare(line.durationText(0), "0 s");
        compare(line.durationText(3), "3 s");
        compare(line.durationText(59), "59 s");
        compare(line.durationText(60), "1 min");
        compare(line.durationText(72), "1 min 12 s");
        compare(line.durationText(185), "3 min 5 s");
    }

    // F1: one line where the tree will be, the seconds in the head.
    function test_fetching() {
        const line = testCase.make("fetching", 3);
        compare(line.summaryText, "fetching · 3 s");
        compare(line.summaryWarn, false);
        compare(line.glyph, Glyphs.lookup);
        // On the line's 4 % block: the dim role as it reads there (DECISIONS C30 F1).
        compare(line.glyphColor, Theme.on.idle.dimForeground);
        compare(line.headline, "Asking qBittorrent for the file list");
        compare(line.lines.length, 1);
        verify(line.lines[0].indexOf("usually in a few seconds") > 0);
    }

    // #193: an add pressed before the list waits for it, and the line says why; at F4 it
    // has gone, and a link never waits.
    function test_an_add_waiting_for_the_list() {
        for (const phase of ["fetching", "slow"]) {
            const line = testCase.make(phase, 4);
            line.addWaiting = true;
            compare(line.lines.length, 1, phase);
            verify(line.lines[0].indexOf("goes out as soon as the list is in") > 0, phase);
            verify(line.lines[0].indexOf("qBittorrent 5.3") > 0, phase);
        }
        const never = testCase.make("never", 61);
        never.addWaiting = true;
        verify(never.lines[0].indexOf("goes out as soon as") < 0);
        const link = testCase.make("fetching", 2, "url");
        link.addWaiting = true;
        verify(link.lines[0].indexOf("goes out as soon as") < 0);
    }

    // F3: from 10 s, the two ways out.
    function test_still_looking() {
        const line = testCase.make("slow", 24);
        compare(line.summaryText, "still looking · 24 s");
        compare(line.glyph, Glyphs.lookupSlow);
        compare(line.glyphColor, Theme.yellow);
        compare(line.headline, "No peer has sent the file list yet");
        verify(line.lines[0].indexOf("Some magnets take minutes.") === 0);
        verify(line.lines[0].indexOf("enter") > 0);
        verify(line.lines[0].indexOf("shift+enter") > 0);
    }

    // F4: from 60 s, yellow, and what closing leaves behind (D6 a).
    function test_may_never_come() {
        const line = testCase.make("never", 72);
        compare(line.summaryText, "no list after 1 min 12 s");
        compare(line.summaryWarn, true);
        compare(line.glyph, Glyphs.lookupNever);
        compare(line.glyphColor, Theme.yellow);
        compare(line.headline, "The file list may never come");
        compare(line.lines.length, 2);
        verify(line.lines[0].indexOf("A magnet nobody seeds never gets one.") === 0);
        compare(line.lines[1], "qBittorrent goes on looking until it restarts, even after you close this sheet.");
    }

    // F5: the host, and the .torrent to use instead.
    function test_a_private_tracker() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "phase": "private",
                                               "privateHost": "tracker.privatesite.example"
                                           });
        compare(line.summaryText, "private tracker");
        compare(line.glyph, Glyphs.privateTracker);
        compare(line.glyphColor, Theme.magenta);
        compare(line.headline, "A private tracker's magnet: no file list before adding");
        verify(line.lines[0].indexOf("tracker.privatesite.example") > 0);
        verify(line.lines[0].indexOf(".torrent") > 0);
        compare(line.lines[1], "Added from this magnet, it may stay at \"metadata\" for good.");
    }

    // F7: handed over before the server answered.
    function test_waiting_for_the_server() {
        const line = testCase.make("waiting");
        compare(line.summaryText, "waiting for box");
        compare(line.headline, "Connecting to box");
        verify(line.lines[0].indexOf("it goes out once the server is up") > 0);
    }

    // F8: a link to a .torrent, which qBittorrent downloads; a dead one never says so.
    function test_a_link() {
        const fetching = testCase.make("fetching", 2, "url");
        compare(fetching.summaryText, "downloading the .torrent · 2 s");
        compare(fetching.headline, "qBittorrent is downloading the .torrent from the link");
        const slow = testCase.make("slow", 30, "url");
        compare(slow.headline, "The link hasn't answered");
        verify(slow.lines[0].indexOf("qBittorrent doesn't report a failed download here.") === 0);
        const never = testCase.make("never", 90, "url");
        compare(never.summaryText, "no answer after 1 min 30 s");
        compare(never.headline, "The link hasn't answered");
        compare(never.lines.length, 2);
    }
}
