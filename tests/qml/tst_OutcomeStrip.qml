import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// DECISIONS C18 D1–D3 (board 45 §2): the outcome strip on its own (the first case). Its
// colour per outcome, what it offers, the 6 s of a done strip (held while the pointer rests
// on it), the reason line under its words (C30 F10), and that a click on it never reaches the
// row under it. Then in the window (the second case, below the first so neither covers the
// other): where it floats (C24 H8) and when it offers "show" (C30 F8–F11).
Item {
    height: 1100
    width: 1360

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
        strip.showReason = "";
        under.clicks = 0;
        expiredSpy.clear();
        showSpy.clear();
        moveSpy.clear();
    }

    function test_nothing_to_say_shows_nothing() {
        verify(!strip.visible);
    }

    function test_done_is_green_with_show_and_its_line_runs_down() {
        strip.showReason = "it is out of view";
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

    // #146 (board 49 §7): a creation cancelled is a dim strip with its own action, no timer.
    function test_a_cancel_is_dim_with_its_own_action() {
        strip.outcome = {
            "kind": "dim",
            "mark": "×",
            "headline": "photo-walks wasn't made: cancelled at 61 %",
            "line": "nothing saved, nothing added, the folder is as it was",
            "more": {
                "id": "createAgain",
                "text": "create again…"
            },
            "serial": 7
        };
        compare(findChild(strip, "outcomeMark").color, Theme.dimForeground);
        verify(!findChild(strip, "outcomeTimeLine").visible);
        verify(!findChild(strip, "outcomeShow").visible);
        const more = findChild(strip, "outcomeMore");
        compare(more.text, "create again…");
        verify(more.visible);
        laidOut();
        const spy = createTemporaryObject(moreSpyComponent, testCase);
        mouseClick(more);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "createAgain");
        strip.outcome = testCase.failed;
        verify(!findChild(strip, "outcomeMore").visible);
    }

    Component {
        id: moreSpyComponent

        SignalSpy {
            signalName: "moreRequested"
            target: strip
        }
    }

    function test_some_failed_is_yellow() {
        const mixed = Object.assign({}, testCase.failed, {
                                        "kind": "mixed",
                                        "mark": "!",
                                        "doneHashes": ["a"]
                                    });
        strip.showReason = "they are not selected";
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

    // C30 F8, F10: no reason, no "show"; with one, "show" and the reason dim under the
    // headline, from its start, the strip a line taller.
    function test_show_comes_with_its_reason_under_the_words() {
        strip.outcome = testCase.done;
        laidOut();
        const plainHeight = strip.height;
        verify(!findChild(strip, "outcomeShow").visible, "show with nothing to bring back");
        verify(!findChild(strip, "outcomeShowReason").visible);
        strip.showReason = "they are resting now, out of giving back";
        laidOut();
        verify(findChild(strip, "outcomeShow").visible);
        const reason = findChild(strip, "outcomeShowReason");
        verify(reason.visible);
        compare(reason.text, "they are resting now, out of giving back");
        compare(reason.color, Theme.dimForeground);
        verify(strip.height > plainHeight, "the reason has no line of its own");
        const headline = findChild(strip, "outcomeHeadline");
        fuzzyCompare(reason.mapToItem(strip, 0, 0).x, headline.mapToItem(strip, 0, 0).x, 1);
        verify(reason.mapToItem(strip, 0, 0).y >= headline.mapToItem(strip, 0, headline.height).y - 1,
               "the reason is not under the headline");
        verify(reason.mapToItem(strip, 0, reason.height).y <= strip.height, "the reason falls out of the strip");
        verify(reason.x + reason.implicitWidth <= strip.width, "the strip is narrower than its reason");
        // A failure offers no "show", whatever the host says.
        strip.outcome = testCase.failed;
        verify(!findChild(strip, "outcomeShow").visible);
        verify(!findChild(strip, "outcomeShowReason").visible);
    }
}

// C24 H8 and C30 F8–F11 in the window, on the demo library.
TestCase {
    id: showCase

    height: 800
    name: "OutcomeStripShow"
    visible: true
    when: windowShown
    width: 1360
    y: 300

    MainView {
        id: view

        anchors.fill: parent
        focus: true
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function init() {
        showCase.height = 800;
        view.table.clearSelection();
        App.filtered.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.folder = "";
        App.filtered.clearCategory();
        App.filtered.clearTag();
        App.filtered.clearTracker();
        App.filtered.setSortByRoleName("name");
        App.filtered.sortOrder = Qt.AscendingOrder;
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
        App.outcomes.clear();
    }

    /// A quick action's done strip about `hashes`.
    function report(action, hashes) {
        App.outcomes.reportMap({
                                   "action": action,
                                   "hashes": hashes,
                                   "name": hashes.length === 1 ? App.torrentRow(hashes[0]).name : ""
                               });
        verify(view.outcomeStrip.visible);
    }

    function showLink() {
        return findChild(view.outcomeStrip, "outcomeShow");
    }

    function clickShow() {
        const strip = view.outcomeStrip;
        waitForItemPolished(findChild(strip, "outcomeContent"));
        waitForItemPolished(findChild(strip, "outcomeActions"));
        verify(showCase.showLink().visible, "no show to click");
        mouseClick(showCase.showLink());
    }

    function hashesAt(rows) {
        return rows.map(row => App.filtered.hashAt(row));
    }

    // C24 H8: bottom middle of the rows, 12 u over their foot; with a selection on, that is
    // just above the action bar, which is docked under the rows.
    function test_it_floats_bottom_middle_of_the_rows() {
        const table = view.table;
        const strip = view.outcomeStrip;
        showCase.report("stop", showCase.hashesAt([0]));
        waitForRendering(view);
        const at = strip.mapToItem(view, 0, 0);
        const tableAt = table.mapToItem(view, 0, 0);
        fuzzyCompare(at.x + strip.width / 2, tableAt.x + table.width / 2, 1);
        compare(at.y + strip.height, tableAt.y + table.height - Theme.outcomeStripLift);
        // The last row can scroll clear of it, its reason line counted whether it shows.
        compare(table.bottomRoom, strip.roomHeight + Theme.outcomeStripLift);
        verify(strip.roomHeight >= strip.height);
        // With a selection: the same place, the bar under it.
        table.toggleCursorSelection();
        verify(view.selectionBar.visible);
        compare(strip.mapToItem(view, 0, 0).y, at.y);
        verify(strip.mapToItem(view, 0, strip.height).y <= view.selectionBar.mapToItem(view, 0, 0).y,
               "the strip sits over the action bar");
        App.outcomes.dismiss();
        compare(table.bottomRoom, 0);
    }

    // F8: acting from a selection, everything is in view and selected: only ×.
    function test_no_show_when_everything_is_in_view_and_selected() {
        view.table.selectHashes(showCase.hashesAt([0, 1, 2]));
        showCase.report("addTags", view.table.selectedHashes());
        compare(view.outcomeShowReason, "");
        verify(!showCase.showLink().visible);
        verify(!findChild(view.outcomeStrip, "outcomeShowReason").visible);
    }

    // F8: one torrent acted on alone (the cursor's row, nothing selected) is in front of you.
    // F11: move the cursor away and "show" comes; back, and it goes.
    function test_one_on_the_cursor_and_live() {
        const hash = App.filtered.hashAt(0);
        showCase.report("stop", [hash]);
        compare(view.outcomeShowReason, "");
        verify(!showCase.showLink().visible);
        view.table.moveCursorTo(1);
        compare(view.outcomeShowReason, "it is not selected");
        verify(showCase.showLink().visible);
        view.table.moveCursorTo(0);
        compare(view.outcomeShowReason, "");
        verify(!showCase.showLink().visible);
    }

    // F8, F9: not selected: "show" selects them, the cursor on the first, and goes (F11).
    function test_not_selected_then_show_selects_them() {
        const hashes = showCase.hashesAt([2, 3]);
        showCase.report("stop", hashes);
        compare(view.outcomeShowReason, "they are not selected");
        view.table.selectHashes([hashes[0]]);
        compare(view.outcomeShowReason, "1 of them is not selected");
        view.table.clearSelection();
        showCase.clickShow();
        compare(view.table.selectedHashes().sort(), hashes.slice().sort());
        compare(view.table.cursorHash, hashes[0]);
        compare(view.outcomeShowReason, "");
        verify(!showCase.showLink().visible);
        verify(view.table.activeFocus);
    }

    // F8, F10: scrolled out of view, with the question they all answer (board 50's "they are
    // out of view, under giving back"); F11: scrolled back, it goes; F9: "show" scrolls them in.
    function test_out_of_view_and_back() {
        showCase.height = 420;
        waitForRendering(view);
        const last = App.filtered.count - 1;
        const hashes = showCase.hashesAt([last - 1, last]);
        view.table.selectHashes(hashes);
        view.table.moveCursorTo(last);
        showCase.report("addTags", hashes);
        compare(view.outcomeShowReason, "");
        view.table.moveCursorTo(0);
        verify(!view.table.hashInView(hashes[1]), "the window is tall enough to show every row");
        const question = App.filtered.questionOfHash(hashes[0]);
        const words = question === App.filtered.questionOfHash(hashes[1])
                    ? "they are out of view, under " + view.rail.questionRow(question).text
                    : "they are out of view";
        verify(!view.table.hashInView(hashes[0]), "the window is tall enough to show the row before last");
        compare(view.outcomeShowReason, words);
        verify(showCase.showLink().visible);
        view.table.moveCursorTo(last);
        compare(view.outcomeShowReason, "");
        view.table.moveCursorTo(0);
        verify(showCase.showLink().visible);
        showCase.clickShow();
        compare(view.table.cursorHash, hashes[0]);
        verify(view.table.hashInView(hashes[0]));
        verify(view.table.hashInView(hashes[1]), "show left the second out of view, with room for both");
        compare(view.outcomeShowReason, "");
        compare(view.table.selectedHashes().sort(), hashes.slice().sort());
    }

    // F8, F9, F10: stopped from giving back, they are resting now; "show" takes the rail to
    // resting, selects them, and the reason goes.
    function test_they_left_the_question() {
        App.filtered.question = TorrentFilterModel.GivingBack;
        verify(App.filtered.count >= 2, "the demo has fewer than 2 giving back");
        const hashes = showCase.hashesAt([0, 1]);
        view.table.selectHashes(hashes);
        showCase.report("stop", hashes);
        compare(view.outcomeShowReason, "");
        const patch = {};
        for (const hash of hashes)
            patch[hash] = {
                "state": "stoppedUP"
            };
        App.patchDemoTorrents(patch);
        compare(App.filtered.rowOfHash(hashes[0]), -1);
        compare(view.outcomeShowReason, "they are resting now, out of giving back");
        verify(showCase.showLink().visible);
        showCase.clickShow();
        compare(App.filtered.question, TorrentFilterModel.Resting);
        compare(view.table.selectedHashes().sort(), hashes.slice().sort());
        verify(hashes.indexOf(view.table.cursorHash) >= 0);
        compare(view.outcomeShowReason, "");
        App.loadDemo(16);
    }

    // F9: the last torrent left the question, which is empty now: the strip stays, over the
    // empty scope, and "show" takes the rail where it went.
    function test_the_last_one_left_an_empty_scope() {
        const hash = App.filtered.hashAt(0);
        const question = App.filtered.questionOfHash(hash);
        App.filtered.question = question;
        // Every other torrent in that question goes resting too, so this one is its last.
        const patch = {};
        for (let row = 0; row < App.filtered.count; ++row)
            patch[App.filtered.hashAt(row)] = {
                "state": "stoppedUP"
            };
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        showCase.report("stop", [hash]);
        App.patchDemoTorrents(patch);
        if (question === TorrentFilterModel.Resting) {
            App.loadDemo(16);
            skip("the demo's first row is resting already");
        }
        compare(App.filtered.count, 0);
        verify(view.outcomeStrip.visible, "the strip went with the rows");
        compare(view.outcomeShowReason, "it is resting now, out of " + view.rail.questionRow(question).text);
        showCase.clickShow();
        compare(App.filtered.question, TorrentFilterModel.Resting);
        compare(view.table.cursorHash, hash);
        App.loadDemo(16);
    }

    // F9: a move took one out of the rail's folder: "show" switches the folder to its new
    // place; the filter's words go too.
    function test_it_left_the_folder() {
        const hash = App.filtered.hashAt(0);
        const folder = App.torrentRow(hash).savePath;
        App.filtered.folder = folder;
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        showCase.report("move", [hash]);
        compare(view.outcomeShowReason, "");
        const patch = {};
        patch[hash] = {
            "save_path": "/srv/elsewhere/keep",
            "content_path": "/srv/elsewhere/keep/" + App.torrentRow(hash).name
        };
        App.patchDemoTorrents(patch);
        compare(view.outcomeShowReason, "it is in keep now, out of "
                + (folder.substring(folder.lastIndexOf("/") + 1) || folder));
        showCase.clickShow();
        compare(App.filtered.folder, "/srv/elsewhere/keep");
        compare(view.table.cursorHash, hash);
        compare(view.outcomeShowReason, "");
        App.loadDemo(16);
    }

    // F9: out of the filter's words alone: they clear, and nothing else moves.
    function test_it_left_the_filter() {
        const hash = App.filtered.hashAt(0);
        App.filtered.question = App.filtered.questionOfHash(hash);
        const name = App.torrentRow(hash).name;
        view.table.moveCursorTo(App.filtered.rowOfHash(hash));
        showCase.report("rename", [hash]);
        // Another row's name: the words hide this one and leave the table on show.
        App.filtered.text = App.torrentRow(App.filtered.hashAt(1)).name;
        compare(App.filtered.rowOfHash(hash), -1);
        verify(App.filtered.count > 0);
        compare(view.outcomeShowReason, "it is out of the filter");
        const question = App.filtered.question;
        showCase.clickShow();
        compare(App.filtered.text, "");
        compare(App.filtered.question, question, "the rail moved for the filter's words");
        compare(view.table.cursorHash, hash);
        compare(App.torrentRow(hash).name, name);
    }
}
}
