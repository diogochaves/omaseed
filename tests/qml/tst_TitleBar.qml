import QtQuick
import QtTest
import Omaseed.Kit
import Omaseed.Views

// The title bar (board 46 `titleBar()`, DECISIONS C24 H5): the title and its sentence, then
// the tools in the board's order (a move's way back, move this folder…, the filter, + add),
// its form from `Theme.titleBar*` tokens, and its narrow steps.
TestCase {
    id: testCase

    height: 200
    name: "TitleBar"
    visible: true
    when: windowShown
    width: 1500

    Component {
        id: barComponent

        TitleBar {
            searchCountText: "16"
            sentence: "2 need you · 7 arriving, the next in 3 min · 2 landed today · giving back 41 KB/s"
            title: "Library"
            width: 1200
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    function make(props) {
        const bar = createTemporaryObject(barComponent, testCase, props || {});
        waitForItemPolished(bar);
        return bar;
    }

    function spy(target, signalName) {
        return createTemporaryObject(spyComponent, testCase, {
                                         "target": target,
                                         "signalName": signalName
                                     });
    }

    function xOf(bar, name) {
        return findChild(bar, name).mapToItem(bar, 0, 0).x;
    }

    // Board 46: the title at the left, its sentence under it, the filter and "+ add" at the
    // right edge; the bar is the rail mark's height.
    function test_the_title_its_sentence_and_the_tools() {
        const bar = make();
        compare(bar.height, Theme.titleBarHeight);
        compare(bar.level, 0);
        const title = findChild(bar, "titleText");
        const sentence = findChild(bar, "titleSentence");
        compare(title.text, "Library");
        compare(title.font.pixelSize, Theme.titleBarTitleSize);
        compare(title.font.weight, Theme.titleBarTitleWeight);
        compare(sentence.font.pixelSize, Theme.titleBarSentenceSize);
        compare(xOf(bar, "titleText"), Theme.titleBarInsetStart);
        verify(sentence.mapToItem(bar, 0, 0).y > title.mapToItem(bar, 0, 0).y);
        verify(sentence.visible);

        const field = findChild(bar, "titleFilter");
        verify(field.visible);
        compare(field.width, Theme.titleBarFindWidth);
        compare(field.trailing, "16");
        verify(!bar.filterTool.visible);
        verify(xOf(bar, "titleFilter") < xOf(bar, "titleAdd"));
        compare(Math.round(xOf(bar, "titleAdd") + bar.addTool.width), bar.width
                - Theme.titleBarInsetEnd);
        compare(bar.addTool.text, "add");
        verify(bar.addTool.primary);
        compare(bar.addTool.height, Theme.titleBarToolHeight);
        compare(bar.addTool.Accessible.name, "add");
        compare(bar.addTool.Accessible.description, "shortcut a");
        // The hero's buttons are gone (H9).
        verify(!bar.moveFolderTool.visible);
        verify(!bar.backTool.visible);
        verify(findChild(bar, "settingsButton") === null);
        verify(findChild(bar, "helpButton") === null);
    }

    // H5: a folder picked brings "move this folder…" before the filter.
    function test_a_folder_offers_its_move() {
        const bar = make({
                             "title": "games",
                             "sentence":
                             "49 torrents · 1.32 MB on disk · 2 loose files · /srv/games",
                             "moveFolderShown": true
                         });
        const clicked = spy(bar, "moveFolderClicked");
        verify(bar.moveFolderTool.visible);
        compare(bar.moveFolderTool.text, "move this folder…");
        compare(bar.moveFolderTool.Accessible.description, "shortcut shift+m");
        verify(xOf(bar, "titleMoveFolder") < xOf(bar, "titleFilter"));
        verify(xOf(bar, "titleText") < xOf(bar, "titleMoveFolder"));
        mouseClick(bar.moveFolderTool);
        compare(clicked.count, 1);
    }

    // C15 M10, C18: a move's torrents on show; the scope line's link is a tool now.
    function test_a_moves_show_has_its_way_back() {
        const bar = make({
                             "title": "From the move sheet",
                             "sentence": "3 torrents the move sheet listed",
                             "backText": "back to the sheet",
                             "backKey": "esc"
                         });
        const clicked = spy(bar, "backClicked");
        verify(bar.backTool.visible);
        compare(bar.backTool.text, "back to the sheet");
        compare(bar.backTool.Accessible.description, "shortcut esc");
        compare(bar.toolTip(bar.backTool.words, bar.backTool.key), "back to the sheet · esc");
        verify(xOf(bar, "titleBack") < xOf(bar, "titleFilter"));
        bar.backTool.Accessible.pressAction();
        compare(clicked.count, 1);
    }

    // The filter: typing writes it, enter says done, its count clears it.
    function test_the_filter_reports_its_words() {
        const bar = make();
        const changed = spy(bar, "searchChanged");
        const accepted = spy(bar, "searchAccepted");
        const cleared = spy(bar, "searchCleared");
        bar.openSearch();
        verify(bar.searchField.editing);
        keyClick("u");
        compare(changed.count, 1);
        compare(changed.signalArguments[0][0], "u");
        keyClick(Qt.Key_Return);
        compare(accepted.count, 1);
        verify(bar.searchField.trailingClickable);
        bar.searchField.trailingClicked();
        compare(bar.searchField.text, "");
        compare(cleared.count, 1);
    }

    // The steps: words leave the tools first (their tooltip keeps them), then the filter folds
    // into its `/` tool and opens over the title.
    function test_narrow_steps() {
        const bar = make({
                             "moveFolderShown": true
                         });
        bar.width = Theme.titleBarSteps[0] - 1;
        compare(bar.level, 1);
        compare(bar.addTool.text, "");
        compare(bar.moveFolderTool.text, "");
        compare(bar.addTool.Accessible.name, "add");
        compare(bar.toolTip(bar.moveFolderTool.words, bar.moveFolderTool.key),
                "move this folder… · shift+m");
        verify(findChild(bar, "titleFilter").visible);

        bar.width = Theme.titleBarSteps[1] - 1;
        compare(bar.level, 2);
        verify(bar.searchFolded);
        verify(bar.filterTool.visible);
        verify(!findChild(bar, "titleFilter").visible);
        mouseClick(bar.filterTool);
        verify(bar.searchOpen);
        verify(findChild(bar, "titleFilter").visible);
        verify(bar.searchField.editing);
        verify(!findChild(bar, "titleText").visible);
        keyClick("k");
        compare(bar.searchField.text, "k");
        // Leaving the field folds it again, lit while it filters.
        bar.forceActiveFocus();
        verify(!bar.searchOpen);
        verify(bar.filterTool.selected);
        verify(findChild(bar, "titleText").visible);
    }

    // The log keeps its own filter: none in the bar.
    function test_the_filter_can_leave() {
        const bar = make({
                             "filterShown": false
                         });
        verify(!findChild(bar, "titleFilter").visible);
        verify(!bar.filterTool.visible);
        bar.width = Theme.titleBarSteps[1] - 1;
        verify(!bar.filterTool.visible);
    }

    // DECISIONS C24 H10: alt held, each tool and the filter show their keys over their corner,
    // and nothing in the bar moves.
    function test_alt_badges_the_tools_keys() {
        const bar = make({
                             "moveFolderShown": true
                         });
        const badge = tool => findChild(tool, "keyBadge");
        const before = [bar.addTool.x, bar.addTool.width, bar.moveFolderTool.x, bar.searchField.x];
        verify(!badge(bar.addTool).visible);
        bar.keysShown = true;
        waitForItemPolished(bar);
        verify(badge(bar.addTool).visible);
        compare(badge(bar.addTool).key, "a");
        compare(badge(bar.moveFolderTool).key, "shift+m");
        verify(badge(bar.moveFolderTool).visible);
        compare(findChild(bar, "titleFilterKey").key, "/");
        verify(findChild(bar, "titleFilterKey").visible);
        // A hidden tool's badge stays hidden.
        verify(!badge(bar.backTool).visible);
        // Over the corner, never above the bar, which is the window's top.
        compare(badge(bar.addTool).y, -Math.min(Theme.keyBadgeOverhangY, bar.addTool.y));
        verify(badge(bar.addTool).mapToItem(bar, 0, 0).y >= 0);
        compare([bar.addTool.x, bar.addTool.width, bar.moveFolderTool.x, bar.searchField.x], before);
    }
}
