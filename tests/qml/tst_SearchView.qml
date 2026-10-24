import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The search face (board 48, DECISIONS C26) on the demo's search (App.loadDemoSearch: board
// 48's invented plugins and rows): the empty states (SR13), the rows and their words (SR3–SR8),
// the plugins' line and its answers (SR1–SR2), the bar and its fold (SR6), and the keys (SR14).
TestCase {
    id: testCase

    height: 700
    name: "SearchView"
    visible: true
    when: windowShown
    width: 1180

    Component {
        id: viewComponent

        SearchView {
            height: 640
            serverName: "public"
            width: 1180
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function cleanup() {
        App.loadDemoSearch("off");
        App.searchResults.hideDead = false;
        App.searchResults.order = "seeds";
        App.searchResults.filterText = "";
    }

    function make(stage, props) {
        App.loadDemoSearch(stage);
        const view = createTemporaryObject(viewComponent, testCase, props || {});
        waitForItemPolished(view);
        return view;
    }

    function spy(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
                                         "target": target,
                                         "signalName": signalName
                                     });
    }

    function text(view, name) {
        return findChild(view, name).text;
    }

    // SR13: no plugins: the view stays, says what plugins are naming none, links to the pane.
    function test_no_plugins() {
        const view = make("none");
        compare(view.emptyKind, "none");
        verify(findChild(view, "searchEmpty").visible);
        compare(text(view, "searchEmptyTitle"), "No search plugins");
        verify(text(view, "searchEmptyWords").indexOf("doesn't name, ship or suggest any") >= 0);
        verify(!view.bar.visible);
        compare(view.sentence, "no search plugins");
        const pane = spy(view, "pluginsPaneRequested");
        mouseClick(findChild(view, "searchEmptyLink"));
        compare(pane.count, 1);
    }

    function test_all_off_and_no_python() {
        let view = make("off");
        compare(view.emptyKind, "off");
        compare(text(view, "searchEmptyTitle"), "All 3 plugins are turned off");
        verify(text(view, "searchEmptyWords").indexOf(
                   "plugin a, plugin b and plugin c are installed") === 0);
        compare(findChild(view, "searchEmptyLink").text, "turn them on…");
        view = make("nopython");
        compare(view.emptyKind, "nopython");
        compare(text(view, "searchEmptyTitle"), "qBittorrent can't run plugins here");
        verify(view.bar.visible);
    }

    // SR1, SR3–SR8: one row per hash, most seeds first, "?" above 0, the words.
    function test_rows_and_their_words() {
        const view = make("done");
        tryVerify(() => view.rows.count >= 9);
        const first = findChild(view, "searchRow_0");
        compare(findChild(first, "searchRowName_0").text, "Open Movie Night (2026) 1080p");
        compare(findChild(first, "searchRowFact_0").text, "41");
        compare(findChild(first, "searchRowCaption_0").text, "seeds");
        verify(findChild(first, "searchRowSentence_0").text.indexOf(
                   "plugin a and plugin c · 2 copies folded, 5 trackers") === 0);
        const marks = [];
        for (let i = 0; i < view.rows.count; ++i)
            marks.push(App.searchResults.get(i).markText);
        verify(marks.indexOf("in your library") >= 0);
        verify(marks.indexOf("likely on disk") >= 0);
        // The last two: "?" (not reported) above 0.
        const n = view.rows.count;
        compare(App.searchResults.get(n - 2).seedsText, "?");
        compare(App.searchResults.get(n - 1).seedsText, "0");
        compare(view.sentence, "“open movie night” · " + App.searchResults.torrentCount
                + " torrents from 2 of 3 plugins");
        verify(text(view, "searchLineLeft").indexOf(" torrents from ") > 0);
        verify(findChild(view, "searchDidntAnswer").visible);
        compare(text(view, "searchDidntAnswer"), "1 of 3 plugins didn't answer ›");
    }

    // SR2: each plugin's answer; a failed one with its log line and the three actions.
    function test_the_answers() {
        const view = make("done");
        mouseClick(findChild(view, "searchDidntAnswer"));
        tryVerify(() => view.answers.opened);
        const texts = view.answers.items.map(item => item.text || "");
        verify(texts.indexOf("plugin b · failed after 3 s") >= 0);
        verify(texts.indexOf("qBittorrent's log, 16:01: “URLError: timed out”") >= 0);
        verify(texts.indexOf("search it again") >= 0);
        verify(texts.indexOf("turn it off…") >= 0);
        view.answers.activated("off:plugin_b");
        verify(!view.answers.opened);
        tryCompare(App.searchPlugins, "enabledCount", 2);
        App.loadDemoSearch("slow");
        const slow = view.answers.items.map(item => item.text || "");
        verify(slow.indexOf("plugin b · no answer in 30 s") >= 0);
        verify(slow.indexOf("stop asking") >= 0);
    }

    // SR4: hide dead hides a reported 0 only, and says so under the rows.
    function test_hide_dead() {
        const view = make("done");
        const before = view.rows.count;
        mouseClick(findChild(view, "searchHideDeadChip"));
        compare(view.rows.count, before - 1);
        compare(App.searchResults.hiddenDead, 1);
        compare(App.searchResults.get(view.rows.count - 1).seedsText, "?");
    }

    // SR6: the bar's menus; at 720 px the filters fold.
    function test_the_bar_and_its_fold() {
        const view = make("done");
        compare(findChild(view, "searchPluginsChip").text, "3 plugins ▾");
        compare(findChild(view, "searchSortChip").text, "most seeds ▾");
        mouseClick(findChild(view, "searchSortChip"));
        const menu = findChild(view, "searchBarMenu");
        tryVerify(() => menu.opened);
        menu.activated("s:name");
        compare(App.searchResults.order, "name");
        const narrow = make("done", {
                                "windowWidth": 700
                            });
        verify(narrow.narrow);
        verify(findChild(narrow, "searchFiltersChip").visible);
        verify(!findChild(narrow, "searchPluginsChip").visible);
        compare(text(narrow, "searchPillsFolded"), "2 of 3 answered");
    }

    // SR14: ↓ into the rows, space the result, esc back out step by step, enter add….
    function test_the_keys() {
        const view = make("done");
        const leave = spy(view, "leaveRequested");
        const add = spy(view, "addRequested");
        view.focusField();
        verify(view.field.editing);
        keyClick(Qt.Key_Down);
        verify(view.rowsHaveKeys);
        compare(view.currentIndex, 0);
        keyClick(Qt.Key_J);
        compare(view.currentIndex, 1);
        keyClick(Qt.Key_Space);
        verify(view.paneOpen);
        const pane = findChild(view, "searchResultPane");
        tryVerify(() => pane.visible);
        compare(findChild(pane, "searchPaneName").text, App.searchResults.get(1).name);
        keyClick(Qt.Key_Return);
        compare(add.count, 1);
        compare(add.signalArguments[0][0], App.searchResults.get(1).key);
        keyClick(Qt.Key_Escape);
        verify(!view.paneOpen);
        keyClick(Qt.Key_Escape);
        verify(view.field.editing);
        view.field.text = "";
        keyClick(Qt.Key_Escape);
        compare(leave.count, 1);
    }

    // SR5: the cursor's row is kept, by key, while the rows move.
    function test_the_cursor_row_stays() {
        const view = make("done");
        view.focusField();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_J);
        const key = view.keyAt(view.currentIndex);
        App.searchResults.order = "name";
        compare(view.keyAt(view.currentIndex), key);
    }

    // The first answer: the asking line, then a plugin's pill with its count.
    function test_asking() {
        const view = make("asking");
        verify(findChild(view, "searchAsking").visible);
        compare(text(view, "searchLineLeft"), "asking plugin a, plugin b and plugin c…");
        compare(view.sentence, "“open movie night” · asking 3 plugins");
        App.loadDemoSearch("one");
        tryVerify(() => view.rows.count > 0);
        verify(text(view, "searchLineLeft").indexOf(" so far") > 0);
        verify(view.sentence.indexOf("plugin a answered · 2 still looking") > 0);
        verify(!findChild(view, "searchDidntAnswer").visible);
    }

    // Form in tokens: the rows are the rows display's height; the bar's inset is its token.
    function test_form_from_tokens() {
        const view = make("done");
        compare(findChild(view, "searchRow_0").height, Theme.libraryRowHeight);
        compare(findChild(view, "searchField").mapToItem(view, 0, 0).x, Theme.searchBarInset);
    }

    // SR14: tab walks the filters, then "1 of 3 plugins didn't answer ›"; enter opens it.
    function test_tab_through_the_filters_to_the_answers() {
        const view = make("done");
        view.focusField();
        keyClick(Qt.Key_Tab);
        compare(view.bar.stop, 1);
        verify(findChild(view, "searchPluginsChip").hasCursor);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab);
        compare(view.bar.stop, 3);
        keyClick(Qt.Key_Space);
        verify(App.searchResults.hideDead);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab);
        verify(view.bar.answersHaveCursor);
        keyClick(Qt.Key_Return);
        tryVerify(() => view.answers.opened);
        view.answers.close();
        keyClick(Qt.Key_Tab);
        compare(view.bar.stop, 0);
    }
}
