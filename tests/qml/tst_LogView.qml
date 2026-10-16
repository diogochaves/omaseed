import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// docs/32 §6, D9–D15 (#56): qBittorrent's log as the window's third face, driven by the keys the
// board gives it. It runs on the demo library, whose log starts at qBittorrent's start and
// holds the demo's failures; lines are added through App.serverLog as a read would add them.
TestCase {
    id: testCase

    height: 800
    name: "LogView"
    visible: true
    when: windowShown
    width: 1360

    MainView {
        id: view

        anchors.fill: parent
        focus: true
    }

    SignalSpy {
        id: copySpy

        signalName: "copyRequested"
        target: view.logView
    }

    readonly property var logView: view.logView

    function initTestCase() {
        App.loadDemo(16);
    }

    function cleanupTestCase() {
        // The whole runner shares one App: the next file gets the demo's own log back.
        App.loadDemo(16);
    }

    function init() {
        App.loadDemo(16); // the demo's own log, whatever the last case added
        view.commandPalette.close();
        view.sortPalette.close();
        view.helpOpen = false;
        view.setView("library");
        App.logLines.clearTorrent();
        App.logLines.levels = 15;
        App.logLines.clearMarks();
        testCase.logView.filterField.text = "";
        App.filtered.statusFilter = TorrentFilterModel.All;
        view.footerNote = "";
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
        copySpy.clear();
    }

    /// Lines as a read from the server brings them, after the last id held.
    function addLines(count, type, text) {
        const entries = [];
        let id = App.serverLog.lastId();
        const now = Math.floor(Date.now() / 1000);
        for (let i = 0; i < count; ++i)
            entries.push({
                             "id": ++id,
                             "timestamp": now,
                             "type": type,
                             "message": text + " " + id
                         });
        App.serverLog.applyEntries(entries);
    }

    function openLog() {
        keyClick(Qt.Key_AsciiTilde);
        compare(view.view, "log");
    }

    function rowOf(name) {
        for (let row = 0; row < App.filtered.count; ++row) {
            if (App.torrentRow(App.filtered.hashAt(row)).name === name)
                return row;
        }
        return -1;
    }

    // D9: `~` from anywhere, the whole body's width, read only while it shows; `~` again, or
    // `esc` with nothing to clear, goes back where the window was.
    function test_tilde_opens_the_log_and_goes_back() {
        verify(!App.serverLog.active);
        testCase.openLog();
        verify(view.logShown);
        verify(testCase.logView.visible);
        verify(testCase.logView.list.activeFocus, "the lines did not take the keys");
        verify(!view.rail.visible, "the rail is about torrents");
        verify(!view.split.visible);
        compare(view.hero.currentView, "log");
        verify(App.serverLog.active, "the face on screen reads the log");
        compare(view.focusArea, "log");

        keyClick(Qt.Key_AsciiTilde);
        compare(view.view, "library");
        verify(!App.serverLog.active, "nothing reads the log once the face is gone");
        verify(view.table.activeFocus);

        // From the seed console, back to the seed console.
        view.setView("seed");
        testCase.openLog();
        keyClick(Qt.Key_Escape);
        compare(view.view, "seed");
        view.setView("library");
    }

    // D10 (L1): four chips with their counts, `1`–`4` turn a level off and on.
    function test_level_chips_count_and_toggle() {
        testCase.openLog();
        const normal = findChild(testCase.logView, "levelChip1");
        const critical = findChild(testCase.logView, "levelChip4");
        compare(normal.count, String(App.serverLog.levelCounts[0]));
        compare(critical.count, String(App.serverLog.levelCounts[3]));
        verify(App.serverLog.levelCounts[3] > 0);
        verify(normal.selected);
        compare(critical.tone, Theme.red);

        const all = App.logLines.count;
        keyClick("1");
        verify(!normal.selected);
        compare(App.logLines.levels, 14);
        compare(App.logLines.count, all - App.serverLog.levelCounts[0]);
        // A click does what the key does.
        normal.clicked();
        verify(normal.selected);
        compare(App.logLines.count, all);

        keyClick("2");
        keyClick("1");
        compare(App.logLines.levels, ServerLog.Warning | ServerLog.Critical);
        for (let row = 0; row < App.logLines.count; ++row)
            verify(App.logLines.data(App.logLines.index(row, 0), 258) >= ServerLog.Warning);
        // The digits are the log's here: no status filter moved underneath.
        keyClick("5");
        compare(App.filtered.statusFilter, TorrentFilterModel.All);
    }

    // D10: `/` filters, every word must match, case ignored, and the bar says "N of M lines".
    function test_slash_filters_every_word() {
        testCase.openLog();
        keyClick("/");
        verify(testCase.logView.filterEditing);
        compare(view.focusArea, "search");
        for (const ch of "FAILED move")
            keyClick(ch);
        compare(App.logLines.text, "FAILED move");
        compare(App.logLines.count, 1);
        verify(App.logLines.messageAt(0).startsWith("Failed to move torrent."));
        compare(findChild(testCase.logView, "logSince").text, "1 of %1 lines".arg(App.logLines.levelTotal));

        keyClick(Qt.Key_Return);
        verify(testCase.logView.list.activeFocus);
        // `esc` on the lines clears the words before it leaves.
        keyClick(Qt.Key_Escape);
        compare(App.logLines.text, "");
        compare(view.view, "log");
        verify(findChild(testCase.logView, "logSince").text.startsWith("since qBittorrent started · "));
    }

    // D12 (L1, L2): newest at the bottom and followed while there; scrolling up pauses and counts
    // new lines; `G` goes back to the newest and follows; `f` turns following off and on.
    function test_follow_pauses_and_counts_new_lines() {
        testCase.openLog();
        testCase.addLines(200, 1, "Added new torrent. Torrent: \"filler\"");
        const list = testCase.logView.list;
        verify(testCase.logView.following);
        tryVerify(() => list.atYEnd, 2000, "the view did not follow to the newest line");
        compare(testCase.logView.cursor, App.logLines.count - 1);
        const state = findChild(view, "logFollowState");
        verify(state.visible);
        compare(state.text, "● following");
        compare(state.color, Theme.green);

        keyClick("g");
        compare(testCase.logView.cursor, 0);
        verify(!testCase.logView.following, "the view left the bottom and still follows");
        compare(state.text, "○ paused");
        // #79: a screen reader hears the word, without the ● or ○.
        compare(state.Accessible.role, Accessible.StaticText);
        compare(state.Accessible.name, "paused");
        const top = list.contentY;
        testCase.addLines(3, 4, "Failed to export torrent. Torrent: \"x\"");
        compare(testCase.logView.newLines, 3);
        verify(findChild(testCase.logView, "newerPill").visible);
        compare(findChild(testCase.logView, "newerText").text, "3 new lines below · follow is paused");
        compare(list.contentY, top, "new lines pulled a paused view down");
        compare(view.hintModel.hints.map(hint => hint.key).indexOf("G") >= 0, true);

        keyClick(Qt.Key_G, Qt.ShiftModifier);
        verify(testCase.logView.following);
        compare(testCase.logView.newLines, 0);
        compare(testCase.logView.cursor, App.logLines.count - 1);
        tryVerify(() => list.atYEnd, 2000);
        verify(!findChild(testCase.logView, "newerPill").visible);

        // #77: the pill is a "follow new lines" button a screen reader presses, as `G`.
        keyClick("g");
        testCase.addLines(2, 4, "Failed to export torrent. Torrent: \"y\"");
        const pill = findChild(testCase.logView, "newerPill");
        compare(pill.Accessible.role, Accessible.Button);
        compare(pill.Accessible.name, "follow new lines");
        compare(pill.Accessible.description, "2 new lines below · follow is paused");
        pill.Accessible.pressAction();
        verify(testCase.logView.following);
        compare(testCase.logView.newLines, 0);
        tryVerify(() => list.atYEnd, 2000);

        keyClick("f");
        verify(!testCase.logView.following);
        keyClick("f");
        verify(testCase.logView.following);
        // Following, a new line takes the cursor along to it.
        testCase.addLines(1, 2, "Detected external IP. IP: \"203.0.113.9\"");
        compare(testCase.logView.cursor, App.logLines.count - 1);
        tryVerify(() => list.atYEnd, 2000);
    }

    // D11: `y` copies the cursor's line, or the marked ones, as "time level message".
    function test_y_copies_the_cursor_or_the_marked_lines() {
        testCase.openLog();
        const last = App.logLines.count - 1;
        compare(testCase.logView.cursor, last);
        keyClick("y");
        compare(copySpy.count, 1);
        const one = copySpy.signalArguments[0][0];
        verify(/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \w+ /.test(one), one);
        verify(one.endsWith(App.logLines.messageAt(last)));
        compare(view.footerNote, "copied 1 line");

        keyClick("k");
        keyClick(Qt.Key_Space);
        keyClick("j");
        keyClick(Qt.Key_Space);
        compare(App.logLines.markedCount, 2);
        compare(view.hintModel.hints[0].label, "copy 2 marked lines");
        keyClick("y");
        const two = copySpy.signalArguments[1][0].split("\n");
        compare(two.length, 2);
        verify(two[0].endsWith(App.logLines.messageAt(last - 1)));
        verify(two[1].endsWith(App.logLines.messageAt(last)));
        compare(view.footerNote, "copied 2 lines");

        // `esc` clears the marks first, and stays on the log.
        keyClick(Qt.Key_Escape);
        compare(App.logLines.markedCount, 0);
        compare(view.view, "log");
        keyClick("a", Qt.ControlModifier);
        compare(App.logLines.markedCount, App.logLines.count);
        keyClick(Qt.Key_Escape);
        compare(App.logLines.markedCount, 0);
    }

    // D13 (F1, L2): a torrent's block opens its lines; `enter` on a line shows its torrent in
    // the library with General open; `t` narrows to the cursor line's torrent, `esc` widens.
    function test_a_block_opens_its_lines_and_enter_goes_back() {
        const row = testCase.rowOf("Spring (2019) 4K (CC-BY)");
        verify(row >= 0);
        view.table.moveCursorTo(row);
        const hash = view.cursorHash;
        const block = findChild(view.details, "failureBlock");
        verify(block.visible);
        block.actionTriggered("log");

        compare(view.view, "log");
        compare(App.logLines.torrentHash, hash);
        const chip = findChild(testCase.logView, "torrentChip");
        verify(chip.visible);
        verify(chip.text.startsWith("torrent: Spring (2019) 4K (CC-BY)"));
        verify(App.logLines.count >= 2, "the restore and the failed move");
        for (let line = 0; line < App.logLines.count; ++line)
            verify(App.logLines.messageAt(line).indexOf("Spring (2019) 4K (CC-BY)") >= 0);
        compare(findChild(testCase.logView, "logSince").text,
                "%1 of %2 lines".arg(App.logLines.count).arg(App.logLines.levelTotal));
        verify(App.logLines.messageAt(testCase.logView.cursor).startsWith("Failed to move torrent."));

        keyClick(Qt.Key_Return);
        compare(view.view, "library");
        compare(view.cursorHash, hash);
        compare(view.details.currentTab, 0);
        verify(!view.details.collapsed);

        // Back, and wider again with `esc`.
        testCase.openLog();
        verify(chip.visible);
        keyClick(Qt.Key_Escape);
        verify(!chip.visible);
        compare(view.view, "log");

        // `t` on a line that names a torrent; on one that names none, a word in the footer.
        let named = -1;
        for (let line = 0; line < App.logLines.count && named < 0; ++line) {
            if (App.logLines.torrentAt(line).length > 0)
                named = line;
        }
        verify(named >= 0);
        const other = App.logLines.torrentAt(named);
        testCase.logView.moveCursor(named);
        keyClick("t");
        compare(App.logLines.torrentHash, other);
        keyClick(Qt.Key_Escape);
        compare(App.logLines.torrentHash, "");
        testCase.logView.moveCursor(0);
        verify(App.logLines.messageAt(0).startsWith("qBittorrent v5.2.3 started"));
        keyClick("t");
        compare(App.logLines.torrentHash, "");
        compare(view.footerNote, "this line names no torrent in the library");
        keyClick(Qt.Key_Return);
        compare(view.view, "log");
    }

    // An errored torrent's red block has the same link.
    function test_an_errored_torrents_block_has_the_link_too() {
        let row = -1;
        for (let r = 0; r < App.filtered.count && row < 0; ++r) {
            if ((App.torrentRow(App.filtered.hashAt(r)).problem || {}).found === true)
                row = r;
        }
        verify(row >= 0, "the demo has no errored torrent with a line");
        view.table.moveCursorTo(row);
        const hash = view.cursorHash;
        const block = findChild(view.details, "errorBlock");
        verify(block.hints.some(hint => hint.action === "log" && hint.label === "its lines in the log"));
        // Every action shows, on a second line what the first has no room for.
        const first = findChild(block, "errorActions");
        const more = findChild(block, "errorActionsMore");
        tryVerify(() => first.shownCount + (more.visible ? more.model.length : 0) === block.hints.length, 2000,
                  "an action of the block shows nowhere");
        block.actionTriggered("log");
        compare(view.view, "log");
        compare(App.logLines.torrentHash, hash);
        verify(App.logLines.count >= 1);
    }

    // `w` and `W`: the next and previous warning or critical line.
    function test_w_jumps_between_warnings() {
        testCase.openLog();
        testCase.logView.moveCursor(0);
        const first = App.logLines.nextProblem(-1, 1);
        verify(first > 0);
        keyClick("w");
        compare(testCase.logView.cursor, first);
        verify(App.logLines.data(App.logLines.index(first, 0), 258) >= ServerLog.Warning);
        keyClick("W", Qt.ShiftModifier);
        compare(testCase.logView.cursor, first);
        compare(view.footerNote, "no warning above");
    }

    // D15: where qBittorrent's memory begins, and where its log file is; it is never read.
    function test_the_log_says_where_it_starts() {
        testCase.openLog();
        verify(App.serverLog.fromStart);
        compare(findChild(testCase.logView, "logSince").text,
                "since qBittorrent started · %1 · %2 lines".arg(App.serverLog.firstAtText).arg(App.serverLog.total));
        testCase.logView.list.positionViewAtBeginning();
        const start = findChild(testCase.logView, "logStart");
        verify(start !== null);
        verify(start.text.indexOf("starts when it started") >= 0, start.text);
        verify(start.text.indexOf("/home/you/.local/share/qBittorrent/qBittorrent/data/logs") >= 0, start.text);
    }

    // #61: the palette opened on the log lists the log's commands first; `~` is a view command.
    function test_the_palette_lists_the_log_commands_on_its_face() {
        testCase.openLog();
        keyClick(":");
        verify(view.commandPalette.opened);
        compare(view.commandPalette.title, "commands for the log");
        compare(view.commandPalette.items[0].group, "log");
        verify(view.commandPalette.items.some(row => row.action === "toggleLog" && row.title === "back from the log"));
        view.commandPalette.close();
        // And it runs: `G` by name.
        testCase.logView.moveCursor(0);
        verify(view.runAction("logNewest"));
        compare(testCase.logView.cursor, App.logLines.count - 1);
        verify(view.runAction("toggleLog"));
        compare(view.view, "library");
    }

    // #77: the lines are list items named by their message, marked ones selected; a screen
    // reader's press is a plain click: the cursor goes there and the marks go.
    function test_a_line_takes_the_accessible_press() {
        testCase.openLog();
        testCase.addLines(5, 1, "Added new torrent. Torrent: \"a11y\"");
        const list = testCase.logView.list;
        compare(list.Accessible.role, Accessible.List);
        compare(list.Accessible.name, "log");
        testCase.logView.moveCursor(0);
        waitForRendering(list);
        const row = list.itemAtIndex(1);
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, row.message);
        testCase.logView.lines.toggleMark(2);
        verify(list.itemAtIndex(2).Accessible.selected);
        row.Accessible.pressAction();
        compare(testCase.logView.cursor, 1);
        verify(!list.itemAtIndex(2).Accessible.selected, "a plain press kept the marks");
    }

    // #77: on the seed console the ranges are radio buttons, and pressing one shows it.
    function test_the_console_ranges_take_the_accessible_press() {
        view.setView("seed");
        const week = findChild(view, "rangeChip_7d");
        const month = findChild(view, "rangeChip_30d");
        compare(week.Accessible.role, Accessible.RadioButton);
        compare(week.Accessible.name, "7d");
        const before = App.console.range;
        month.Accessible.toggleAction();
        compare(App.console.range, "30d");
        verify(month.Accessible.checked);
        week.Accessible.pressAction();
        compare(App.console.range, "7d");
        App.console.range = before;
        view.setView("library");
    }

    Component {
        id: consoleRowComponent

        ConsoleRow {
            name: "debian.iso"
            note: "2 seeds"
            trailing: "3 waiting"
        }
    }

    // #77: a console row is a list item named by its torrent, its notes the description, and
    // a screen reader's press is its click; one that takes no click is text, and ignores it.
    function test_a_console_row_takes_the_accessible_press() {
        const row = createTemporaryObject(consoleRowComponent, testCase);
        compare(row.Accessible.role, Accessible.ListItem);
        compare(row.Accessible.name, "debian.iso");
        compare(row.Accessible.description, "2 seeds · 3 waiting");
        let clicks = 0;
        row.clicked.connect(() => clicks++);
        row.Accessible.pressAction();
        compare(clicks, 1);
        row.clickable = false;
        compare(row.Accessible.role, Accessible.StaticText);
        row.Accessible.pressAction();
        compare(clicks, 1, "a row that takes no click ran");
    }

    // The library's own keys do nothing on the log.
    function test_library_keys_do_nothing_on_the_log() {
        testCase.openLog();
        keyClick("s");
        verify(!view.sortPalette.opened);
        const folded = view.split.folded;
        keyClick("i");
        compare(view.split.folded, folded);
        compare(view.view, "log");
    }
}
