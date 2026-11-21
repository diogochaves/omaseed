pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// #293 (docs/62 §5 V17, DECISIONS C45 V17): `Views/AgentAskWindow.qml`, the ask, against a
// stand-in for App.agentAsk. Its fixed first title, never a transient child, a size fixed to
// the sheet's; what it says (head, names, "and N more", who, the yellow line); no key allows:
// ↵ with no button focused does nothing, tab then ↵ answers on the button it reached; esc,
// refuse and the window's close refuse; the ask closing (two minutes) hides it; the Console
// skin's kana on the head.
TestCase {
    id: testCase

    height: 300
    name: "AgentAskWindow"
    visible: true
    when: windowShown
    width: 400

    Component {
        id: askComponent

        QtObject {
            id: ask

            property bool open: false
            property var request: ({})
            property int allowed: 0
            property int refused: 0

            function allow(ticks) {
                ask.allowed += 1;
                ask.open = false;
            }

            function refuse() {
                ask.refused += 1;
                ask.open = false;
            }
        }
    }

    Component {
        id: windowComponent

        AgentAskWindow {
            keyboardAskLimit: 0
        }
    }

    function recheckRequest(count) {
        const torrents = [];
        for (let i = 0; i < Math.min(count, 4); ++i)
            torrents.push({
                              "name": "Lantern Festival " + (i + 1),
                              "line": "in /data/music · 1.2 GB · stopped"
                          });
        return {
            "action": "recheck",
            "head": count === 1 ? "An agent asks to recheck a torrent" :
                                  "An agent asks to recheck " + count + " torrents",
            "torrents": torrents,
            "more": count > 4 ? "and " + (count - 4) + " more" : "",
            "word": "omaseed do",
            "wordPid": 900,
            "parent": "claude",
            "parentPid": 48213,
            "at": "15:19",
            "allowText": "allow"
        };
    }

    /// A window over a stand-in ask now open with `request`.
    function makeOpen(request) {
        const ask = createTemporaryObject(askComponent, testCase);
        const window = createTemporaryObject(windowComponent, testCase, {
                                                 "ask": ask
                                             });
        verify(window !== null);
        ask.request = request;
        ask.open = true;
        window.openAsk();
        tryCompare(window, "active", true);
        return {
            "ask": ask,
            "window": window
        };
    }

    function buttonText(button) {
        return button.modelData.text;
    }

    function test_it_opens_under_its_first_title_and_alone() {
        const ask = createTemporaryObject(askComponent, testCase);
        const window = createTemporaryObject(windowComponent, testCase, {
                                                 "ask": ask
                                             });
        compare(window.title, "omaseed · agent");
        compare(window.transientParent, null);
        window.openAsk();
        compare(window.visible, false, "nothing asked, nothing opens");

        ask.request = testCase.recheckRequest(1);
        ask.open = true;
        window.openAsk();
        compare(window.visible, true);
        compare(window.sheet.opened, true);
        compare(window.sheet.framed, false);
        tryCompare(window.sheet.panel, "height", window.height);
        compare(window.minimumWidth, window.maximumWidth);
        compare(window.minimumHeight, window.maximumHeight);
        compare(window.width, window.sheet.sheetWidth);
        window.refuse();
    }

    function test_it_says_what_and_who() {
        const opened = testCase.makeOpen(testCase.recheckRequest(6));
        const window = opened.window;
        compare(window.sheet.title, "An agent asks to recheck 6 torrents");
        const names = findChild(window.sheet, "askNames");
        verify(names !== null);
        compare(findChild(window.sheet, "askMore").text, "and 2 more");
        const who = findChild(window.sheet, "askWho").text;
        verify(who.indexOf("omaseed do") > 0 && who.indexOf("claude") > 0 && who.indexOf(
                   "(process 48213), at 15:19.") > 0, who);
        compare(findChild(window.sheet, "askWarning").text,
                "omaseed can't tell who is behind a command. Allow it only if you asked for this.");
        // The head carries the skin's kana word where it draws one (Console's 依頼).
        const head = findChild(window.sheet, "sheetTitle");
        compare(head.labelId, "agentAsk");
        compare(head.kana, Theme.labelKana ? "依頼" : "");
        window.refuse();
    }

    // #293 (docs/62 §5's move ask): names alone, then where to, how much and the room, the
    // sheet's lines, and that the move's own checks run; the allow button says the move.
    function test_a_move_says_where_how_much_and_the_room() {
        const opened = testCase.makeOpen({
                                             "action": "move",
                                             "head": "An agent asks to move 2 torrents",
                                             "torrents": [
                                                 {
                                                     "name": "Lantern Festival",
                                                     "line": ""
                                                 },
                                                 {
                                                     "name": "Harbour Bells",
                                                     "line": ""
                                                 }
                                             ],
                                             "more": "",
                                             "where": "to /data/music-new · 48.0 GB · 512 GB free there",
                                             "checks": ["1 is already in /data/music-new: left out."],
                                             "note": "The move's own checks still run: room on the drive, names already there. Moves always ask, whatever Settings says.",
                                             "word": "omaseed do",
                                             "wordPid": 900,
                                             "parent": "claude",
                                             "parentPid": 48213,
                                             "at": "15:19",
                                             "allowText": "allow the move"
                                         });
        const window = opened.window;
        compare(window.sheet.title, "An agent asks to move 2 torrents");
        compare(findChild(window.sheet, "askWhere").text,
                "to /data/music-new · 48.0 GB · 512 GB free there");
        compare(findChild(window.sheet, "askWhere").visible, true);
        compare(findChild(window.sheet, "askCheck").text,
                "1 is already in /data/music-new: left out.");
        verify(findChild(window.sheet, "askNote").text.indexOf("Moves always ask") > 0);
        window.refuse();
        compare(opened.ask.refused, 1);

        // A fix's ask has none of them.
        const fix = testCase.makeOpen(testCase.recheckRequest(1));
        compare(findChild(fix.window.sheet, "askWhere").visible, false);
        compare(findChild(fix.window.sheet, "askNote").visible, false);
        compare(findChild(fix.window.sheet, "askCheck"), null);
        fix.window.refuse();
    }

    function test_enter_with_no_button_does_nothing() {
        const opened = testCase.makeOpen(testCase.recheckRequest(1));
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Enter);
        compare(opened.ask.allowed, 0, "a key allowed it");
        compare(opened.ask.refused, 0);
        compare(opened.window.visible, true);
        compare(opened.window.sheet.focusedIndex(), -1, "a button had the keyboard from the start");
        opened.window.refuse();
    }

    function test_tab_then_enter_answers_on_that_button() {
        let opened = testCase.makeOpen(testCase.recheckRequest(1));
        const sheet = opened.window.sheet;
        keyClick(Qt.Key_Tab);
        compare(testCase.buttonText(sheet.focusables()[sheet.focusedIndex()]), "refuse");
        keyClick(Qt.Key_Tab);
        compare(testCase.buttonText(sheet.focusables()[sheet.focusedIndex()]), "allow");
        keyClick(Qt.Key_Return);
        compare(opened.ask.allowed, 1);
        compare(opened.ask.refused, 0);
        compare(opened.window.visible, false);

        opened = testCase.makeOpen(testCase.recheckRequest(1));
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Return);
        compare(opened.ask.refused, 1);
        compare(opened.ask.allowed, 0);
        compare(opened.window.visible, false);
    }

    function test_escape_refuses() {
        const opened = testCase.makeOpen(testCase.recheckRequest(1));
        keyClick(Qt.Key_Escape);
        compare(opened.ask.refused, 1);
        compare(opened.ask.allowed, 0);
        compare(opened.window.visible, false);
    }

    function test_closing_refuses() {
        const opened = testCase.makeOpen(testCase.recheckRequest(1));
        opened.window.close();
        compare(opened.ask.refused, 1);
        compare(opened.window.visible, false);
    }

    function test_the_ask_closing_hides_it() {
        // Two minutes without an answer: AgentAsk closes the ask, the window goes.
        const opened = testCase.makeOpen(testCase.recheckRequest(1));
        opened.ask.open = false;
        compare(opened.window.visible, false);
        compare(opened.window.sheet.opened, false);
        compare(opened.ask.refused, 0);
    }

    function test_the_real_ask_is_apps() {
        const window = createTemporaryObject(windowComponent, testCase);
        compare(window.ask, App.agentAsk);
        compare(App.agentAsk.open, false);
        compare(App.agentAsk.timeoutMs > 0, true);
    }
}
