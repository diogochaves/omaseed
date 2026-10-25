import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// DECISIONS C24 H3–H4 (#168): the library's three displays in the window, on the demo
// library. Rows grouped under the five questions and the shelves are faces of the table: the
// table keeps the cursor, the selection and the keys, so j/k, space, the click rules, the
// action bar and the outcome strip's "show" hold in each. The display is remembered per scope;
// rows for a new install, the table for an existing one. The whole runner shares one App, so
// every case leaves it on the table, as ctest starts it (tests/CMakeLists.txt).
TestCase {
    id: testCase

    height: 820
    name: "Library"
    visible: true
    when: windowShown
    width: 1360

    MainView {
        id: view

        anchors.fill: parent
        focus: true
    }

    function initTestCase() {
        App.loadDemo(16);
    }

    function init() {
        testCase.width = 1360;
        testCase.height = 820;
        view.actionsMenuPanel.close();
        view.displayMenu.close();
        view.renameSheetPanel.close();
        view.table.cancelRename();
        view.table.clearSelection();
        App.outcomes.clear();
        App.filtered.text = "";
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.folder = "";
        App.filtered.clearCategory();
        App.filtered.clearTag();
        App.filtered.clearTracker();
        App.filtered.setSortByRoleName("name");
        App.filtered.sortOrder = Qt.AscendingOrder;
        App.resetDisplays(true);
        view.setView("library");
        view.table.moveCursorTo(0);
        view.table.forceActiveFocus();
    }

    function cleanupTestCase() {
        App.resetDisplays(true);
    }

    function rowsShown(): void {
        App.setDisplay("rows");
        compare(App.display, "rows");
        verify(view.libraryRows.visible);
        compare(view.table.face, view.libraryRows);
        waitForRendering(view);
    }

    /// The delegate drawing proxy row `row` in the rows display.
    function rowItem(row: int): Item {
        return findChild(view.libraryRows, "libraryRow_" + row);
    }

    /// The first row of the next group after the one `row` is in.
    function groupStarts(): var {
        return App.filtered.groups.map(group => group.first);
    }

    // H4: rows for a new install, the table for an existing one, through the window.
    function test_a_new_install_shows_rows_an_existing_one_the_table() {
        App.resetDisplays(false);
        compare(App.display, "rows");
        compare(App.filtered.groupBy, TorrentFilterModel.ByQuestion);
        compare(view.table.face, view.libraryRows);
        verify(view.libraryRows.visible);
        compare(view.titleBar.displayTool.text, "rows ▾");
        App.resetDisplays(true);
        compare(App.display, "table");
        compare(App.filtered.groupBy, TorrentFilterModel.NoGroups);
        compare(view.table.face, null);
        verify(!view.libraryRows.visible);
        verify(findChild(view.table, "rows").visible);
        compare(view.titleBar.displayTool.text, "table ▾");
    }

    // H3: rows grouped under the questions, in the rail's order, each under its head; the
    // table stays one sorted list.
    function test_rows_are_grouped_under_the_questions() {
        testCase.rowsShown();
        const groups = App.filtered.groups;
        verify(groups.length >= 3, "the demo answers fewer than 3 questions");
        for (let i = 1; i < groups.length; ++i)
            verify(groups[i].question > groups[i - 1].question, "the groups out of the rail's order");
        let first = 0;
        for (const group of groups) {
            compare(group.first, first);
            for (let row = group.first; row < group.first + group.count; ++row)
                compare(App.filtered.data(App.filtered.index(row, 0), TorrentFilterModel.QuestionRole),
                        group.question);
            first += group.count;
        }
        compare(first, App.filtered.count);
        // The first head is drawn with its question and count; the first row says its words.
        const head = findChild(view.libraryRows, "groupTitle");
        verify(head !== null);
        compare(head.text, groups[0].title);
        // #171: the bar before the words (board 46 `.gh::before`) where the skin draws one,
        // the accent for needs you.
        const bar = findChild(view.libraryRows, "groupBar");
        compare(bar.visible, Theme.libraryGroupHeadBar > 0);
        compare(bar.color, groups[0].question === TorrentFilterModel.NeedsYou ? Theme.accent
                                                                             : Theme.hairColor);
        if (Theme.skin.name === "console")
            compare(bar.width, Theme.space(3));
        const row = testCase.rowItem(0);
        verify(row !== null);
        compare(findChild(row, "libraryRowName_0").text, App.filtered.data(App.filtered.index(0, 0),
                                                                             TorrentModel.NameRole));
        verify(findChild(row, "libraryRowFact_0").text.length > 0);
        // Board 46 `.row .fact b`: Console's fact is a step up the type scale (18 px at 13).
        compare(findChild(row, "libraryRowFact_0").font.pixelSize, Theme.libraryRowFactSize);
        compare(Theme.libraryRowFactSize, Theme.skin.name === "console" ? Theme.font.heading
                                                                        : Theme.font.body);
        verify(findChild(row, "libraryRowSentence_0").text.length > 0);
        App.setDisplay("table");
        compare(App.filtered.groups.length, 0);
    }

    // The keys are the table's: j and k cross from one group into the next, space selects
    // across groups, the cursor stays in view.
    function test_keys_and_selection_cross_the_groups() {
        testCase.rowsShown();
        const starts = testCase.groupStarts();
        verify(starts.length >= 2);
        view.table.moveCursorTo(starts[1] - 1);
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_J);
        compare(view.table.cursorIndex, starts[1]);
        keyClick(Qt.Key_Space);
        compare(view.table.selectedCount, 2);
        verify(view.selectionBar.visible, "the action bar while a selection is on");
        verify(view.libraryRows.rowInView(starts[1]));
        // The picked row's rule, or the picked ground alone where the skin says (#171 step 6);
        // the cursor's mark is on the cursor's row.
        compare(findChild(testCase.rowItem(starts[1]), "libraryRowEdge_" + starts[1]).visible, !Theme.pickedGround);
        compare(testCase.rowItem(starts[1]).ink.ground, Theme.rowGround(true, true, false, false));
        verify(findChild(testCase.rowItem(starts[1]), "libraryRowCursor_" + starts[1]).visible);
        keyClick(Qt.Key_K);
        compare(view.table.cursorIndex, starts[1] - 1);
        // G to the end: the last row is scrolled into view.
        keyClick(Qt.Key_G, Qt.ShiftModifier);
        compare(view.table.cursorIndex, App.filtered.count - 1);
        verify(view.table.hashInView(App.filtered.hashAt(App.filtered.count - 1)));
        keyClick(Qt.Key_Escape);
        compare(view.table.selectedCount, 0);
    }

    // "The click rules every list follows", on the rows.
    function test_the_click_rules_hold_on_rows() {
        testCase.rowsShown();
        const starts = testCase.groupStarts();
        mouseClick(testCase.rowItem(0), view.width / 3, Theme.libraryRowHeight / 2);
        compare(view.table.cursorIndex, 0);
        compare(view.table.selectedCount, 1);
        // shift+click into the next group: the range from the anchor.
        mouseClick(testCase.rowItem(starts[1]), view.width / 3, Theme.libraryRowHeight / 2, Qt.LeftButton,
                   Qt.ShiftModifier);
        compare(view.table.selectedCount, starts[1] + 1);
        // ctrl+click takes one out.
        mouseClick(testCase.rowItem(0), view.width / 3, Theme.libraryRowHeight / 2, Qt.LeftButton,
                   Qt.ControlModifier);
        compare(view.table.selectedCount, starts[1]);
        // A right-click inside the selection keeps it and opens the row's menu.
        mouseClick(testCase.rowItem(1), view.width / 3, Theme.libraryRowHeight / 2, Qt.RightButton);
        compare(view.table.selectedCount, starts[1]);
        tryVerify(() => view.actionsMenuPanel.opened);
        view.actionsMenuPanel.close();
        // enter opens it under the cursor's row.
        view.table.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryVerify(() => view.actionsMenuPanel.opened);
        view.actionsMenuPanel.close();
        // A click below the last row clears the selection.
        view.table.forceActiveFocus();
        const empty = findChild(view.libraryRows, "libraryRowsEmpty");
        if (empty.height > 2) {
            mouseClick(empty, empty.width / 2, empty.height - 2);
            compare(view.table.selectedCount, 0);
        }
    }

    // `r` renames inline on a row, and opens the sheet on a cover.
    function test_rename_inline_in_rows_and_the_sheet_on_shelves() {
        // The table's own editor first: the faces leave it alone.
        compare(view.table.face, null);
        keyClick(Qt.Key_R);
        verify(view.table.renaming);
        tryVerify(() => findChild(view.table, "renameEditor") !== null && findChild(view.table, "renameEditor").active);
        keyClick(Qt.Key_Escape);
        verify(!view.table.renaming);
        testCase.rowsShown();
        keyClick(Qt.Key_R);
        verify(view.table.renaming);
        const editor = findChild(testCase.rowItem(view.table.cursorIndex), "libraryRowRename");
        verify(editor !== null && editor.active, "no editor over the row");
        keyClick(Qt.Key_Escape);
        verify(!view.table.renaming);
        App.setDisplay("overview");
        view.table.forceActiveFocus();
        keyClick(Qt.Key_R);
        verify(!view.table.renaming);
        tryVerify(() => view.renameSheetPanel.opened);
        view.renameSheetPanel.close();
    }

    // The shelves: one per category, "no category" last, a cover per torrent; j goes cover by
    // cover across the shelves, a click selects.
    function test_shelves_hold_a_cover_per_torrent() {
        App.setDisplay("overview");
        compare(App.filtered.groupBy, TorrentFilterModel.ByCategory);
        compare(view.table.face, view.libraryShelves);
        verify(view.libraryShelves.visible);
        waitForRendering(view);
        const groups = App.filtered.groups;
        verify(groups.length >= 2);
        const last = groups[groups.length - 1];
        if (App.filtered.data(App.filtered.index(last.first, 0), TorrentModel.CategoryRole) === "")
            compare(last.title, "no category");
        const cover = findChild(view.libraryShelves, "cover_0");
        verify(cover !== null);
        compare(findChild(cover, "coverName").text, App.filtered.data(App.filtered.index(0, 0),
                                                                       TorrentModel.NameRole));
        verify(findChild(cover, "coverMonogram").text.length > 0);
        // #171: a cover that needs you takes the accent edge and fact where the skin says
        // (board 46 `.card.needs`), its fact red otherwise.
        for (let i = 0; i < App.filtered.count; ++i) {
            const each = findChild(view.libraryShelves, "cover_" + i);
            if (each === null || !each.needsYou || each.isSelected)
                continue;
            const plate = findChild(each, "coverPlate");
            compare(each.needsEdge, Theme.coverNeedsEdge);
            compare(findChild(each, "coverFact").color, Theme.coverNeedsEdge ? Theme.accent : Theme.red);
            if (Theme.coverNeedsEdge) {
                compare(plate.edgeColor, Theme.accent);
                compare(plate.edgeWidth, Theme.coverSelectedBorder);
            }
        }
        compare(Theme.coverNeedsEdge, Theme.skin.name === "console");
        const next = groups[1].first;
        view.table.moveCursorTo(next - 1);
        keyClick(Qt.Key_J);
        compare(view.table.cursorIndex, next);
        verify(view.libraryShelves.rowInView(next));
        mouseClick(cover);
        compare(view.table.cursorIndex, 0);
        compare(view.table.selectedCount, 1);
        compare(view.libraryShelves.rowAt(cover.mapToItem(view.libraryShelves, 4, 4).x,
                                          cover.mapToItem(view.libraryShelves, 4, 4).y), 0);
    }

    // H4: remembered per scope, folders included; a scope without its own shows the
    // library's.
    function test_the_display_is_remembered_per_scope() {
        App.setDisplay("rows");
        const folder = App.torrentRow(App.filtered.hashAt(0)).savePath;
        App.filtered.folder = folder;
        compare(App.display, "rows");
        App.setDisplay("overview");
        compare(view.table.face, view.libraryShelves);
        App.filtered.folder = "";
        compare(App.display, "rows");
        compare(view.table.face, view.libraryRows);
        App.filtered.question = TorrentFilterModel.GivingBack;
        compare(App.display, "rows");
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.folder = folder;
        compare(App.display, "overview");
        compare(view.titleBar.displayTool.text, "shelves ▾");
        App.filtered.folder = "";
    }

    // The tool's menu: the three, the scope's checked; a pick sets it.
    function test_the_display_tool_opens_its_menu() {
        verify(view.titleBar.displayTool.visible);
        mouseClick(view.titleBar.displayTool);
        tryVerify(() => view.displayMenu.opened);
        compare(view.displayMenu.items.map(item => item.action), ["rows", "table", "overview"]);
        compare(view.displayMenu.items.filter(item => item.checked).map(item => item.action), ["table"]);
        view.displayMenu.activated("rows");
        view.displayMenu.close();
        compare(App.display, "rows");
        // The palette's rows run the same.
        view.runCommand("displayShelves");
        compare(App.display, "overview");
        view.runCommand("displayTable");
        compare(App.display, "table");
        // Not on the seed console or the log.
        view.setView("seed");
        verify(!view.titleBar.displayTool.visible);
        view.setView("library");
    }

    // C30 F8–F11 in rows: "show" sees what the rows show, and brings them back.
    function test_the_strips_show_in_rows() {
        testCase.height = 420;
        testCase.rowsShown();
        const last = App.filtered.count - 1;
        const hashes = [App.filtered.hashAt(last)];
        view.table.selectHashes(hashes);
        view.table.moveCursorTo(last);
        App.outcomes.reportMap({
                                   "action": "addTags",
                                   "hashes": hashes,
                                   "name": App.torrentRow(hashes[0]).name
                               });
        verify(view.outcomeStrip.visible);
        compare(view.outcomeShowReason, "");
        view.table.moveCursorTo(0);
        verify(!view.table.hashInView(hashes[0]), "the window shows every row");
        verify(view.outcomeShowReason.indexOf("out of view") >= 0, view.outcomeShowReason);
        const show = findChild(view.outcomeStrip, "outcomeShow");
        waitForItemPolished(findChild(view.outcomeStrip, "outcomeActions"));
        verify(show.visible);
        mouseClick(show);
        compare(view.table.cursorHash, hashes[0]);
        verify(view.table.hashInView(hashes[0]));
        compare(view.outcomeShowReason, "");
        App.outcomes.dismiss();
    }

    // A row's fix: a needs-you torrent's first action, on the row.
    function test_a_needs_you_row_offers_its_fix() {
        testCase.rowsShown();
        const groups = App.filtered.groups;
        if (groups[0].question !== TorrentFilterModel.NeedsYou)
            skip("the demo has no torrent that needs you");
        const fix = findChild(testCase.rowItem(0), "libraryRowFix_0");
        verify(fix !== null);
        compare(fix.visible, App.filtered.data(App.filtered.index(0, 0), TorrentFilterModel.RowFixRole).length > 0);
        // #171: Console's fix is a filled accent tag in the control voice (board 46 `.row
        // .fix`); Omarchy's its words in an accent outline.
        const words = findChild(testCase.rowItem(0), "libraryRowFixText_0");
        compare(fix.border.width, Theme.libraryRowFixTag ? 0 : Theme.hairWidth);
        compare(words.font.family, Theme.libraryRowFixTag ? Theme.labelFamily : Theme.valueFamily);
        if (Theme.libraryRowFixTag) {
            compare(fix.color, Theme.accent);
            compare(words.color, Theme.background);
            compare(words.font.pixelSize, Theme.font.caption);
        }
        compare(Theme.libraryRowFixTag, Theme.skin.name === "console");
        compare(findChild(testCase.rowItem(0), "libraryRowCaption_0").text, "needs you");
    }

    // Narrow: the group's line and the meta go before the name and the fact do.
    function test_narrow_rows_keep_the_name_and_the_fact() {
        testCase.rowsShown();
        testCase.width = 620;
        waitForRendering(view);
        verify(view.libraryRows.level >= 1);
        compare(findChild(view.libraryRows, "groupLine").text, "");
        const row = testCase.rowItem(0);
        verify(findChild(row, "libraryRowName_0").width > 0);
        verify(findChild(row, "libraryRowFact_0").visible);
    }
}
