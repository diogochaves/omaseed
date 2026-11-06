import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// DECISIONS C24 H3–H4 (#168): the library's three displays in the window, on the demo
// library. Rows grouped under the five questions and space (C32 D12 a) are faces of the table: the
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
        compare(view.titleBar.displayTool.text, "rows · name ▾");
        App.resetDisplays(true);
        compare(App.display, "table");
        compare(App.filtered.groupBy, TorrentFilterModel.NoGroups);
        compare(view.table.face, null);
        verify(!view.libraryRows.visible);
        verify(findChild(view.table, "rows").visible);
        compare(view.titleBar.displayTool.text, "table · name ▾");
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

    // `r` renames inline on a row, and opens the sheet on a block of space.
    function test_rename_inline_in_rows_and_the_sheet_on_space() {
        // The table's own editor first: the faces leave it alone.
        compare(view.table.face, null);
        keyClick(Qt.Key_R);
        verify(view.table.renaming);
        // The cursor row's editor: a narrow case before can leave a delegate of another row
        // with one too, which a plain findChild may find first.
        tryVerify(() => {
                      const row = findChild(view.table, "row_" + view.table.renameIndex);
                      const editor = row !== null ? findChild(row, "renameEditor") : null;
                      return editor !== null && editor.active;
                  });
        keyClick(Qt.Key_Escape);
        verify(!view.table.renaming);
        testCase.rowsShown();
        keyClick(Qt.Key_R);
        verify(view.table.renaming);
        const editor = findChild(testCase.rowItem(view.table.cursorIndex), "libraryRowRename");
        verify(editor !== null && editor.active, "no editor over the row");
        keyClick(Qt.Key_Escape);
        verify(!view.table.renaming);
        App.setDisplay("space");
        view.table.forceActiveFocus();
        keyClick(Qt.Key_R);
        verify(!view.table.renaming);
        tryVerify(() => view.renameSheetPanel.opened);
        view.renameSheetPanel.close();
    }

    // Board 51 D12 a (C32): space, a size map by folder, then torrent. The folders go the
    // largest first, each torrent a block as big as its share, what is under 1 % in the line
    // of chips; j goes block by block in the map's order, a click selects as on a row.
    function test_space_maps_where_the_space_goes() {
        App.setDisplay("space");
        compare(App.filtered.groupBy, TorrentFilterModel.BySpace);
        compare(view.table.face, view.librarySpace);
        verify(view.librarySpace.visible);
        waitForRendering(view);
        const groups = App.filtered.groups;
        verify(groups.length >= 1);
        for (let i = 1; i < groups.length; ++i)
            verify(groups[i - 1].bytes >= groups[i].bytes, "the folders are not the largest first");
        const layout = view.librarySpace.layout;
        const total = groups.reduce((sum, group) => sum + group.bytes, 0);
        compare(layout.total, total);
        compare(layout.count, App.filtered.count);
        // Every row is a block or behind a chip, once.
        let shown = layout.tiles.length;
        for (const chip of layout.chips)
            shown += chip.count;
        compare(shown, App.filtered.count);
        verify(layout.tiles.length > 0);
        // A block's area is its share of the map's (the folders' frames and heads aside).
        const map = findChild(view.librarySpace, "spaceMap");
        const first = layout.tiles[0];
        verify(first.w * first.h > 0);
        const tile = findChild(view.librarySpace, "spaceTile_" + first.row);
        verify(tile !== null);
        compare(tile.name, App.filtered.data(App.filtered.index(first.row, 0), TorrentModel.NameRole));
        verify(Qt.colorEqual(findChild(tile, "spaceTileLine").color, tile.stateColor));
        compare(findChild(view.librarySpace, "spaceHeadWord").text, "Where the space goes");
        verify(findChild(view.librarySpace, "spaceHeadLine").text.indexOf("by folder, then torrent") > 0);

        // The cursor: its block's edge, its name over the map, its folder's line.
        view.table.moveCursorTo(first.row);
        waitForRendering(view);
        compare(findChild(tile, "spaceTileEdge").edgeWidth, Theme.spaceCursorEdge);
        verify(findChild(view.librarySpace, "spaceHeadLine").text.indexOf("the cursor on " + tile.name) > 0);
        verify(findChild(view.librarySpace, "spaceFolderWords").text.startsWith(view.librarySpace.cursorFolder.title));
        // j in the map's order, which is the rows'.
        keyClick(Qt.Key_J);
        compare(view.table.cursorIndex, first.row + 1);
        verify(view.librarySpace.rowInView(first.row + 1));
        compare(findChild(tile, "spaceTileEdge").edgeWidth, 0);

        // A click selects that block only; rowAt finds it.
        mouseClick(tile, 4, 4);
        compare(view.table.cursorIndex, first.row);
        compare(view.table.selectedCount, 1);
        verify(view.table.isRowSelected(first.row));
        const inSpace = tile.mapToItem(view.librarySpace, 4, 4);
        compare(view.librarySpace.rowAt(inSpace.x, inSpace.y), first.row);
        // The chips that fit are drawn, none wider than the cap (a long name elides).
        compare(findChild(view.librarySpace, "spaceSmall").visible, layout.chips.length > 0);
        for (const chip of layout.chips) {
            const item = findChild(view.librarySpace, (chip.folder ? "spaceFolderChip_" : "spaceChip_") + chip.first);
            verify(item !== null);
            verify(item.width <= Theme.spaceChipMaxWidth + 2 * Theme.spaceChipPadX);
        }
        // The map keeps its place while the action bar shows over its foot.
        compare(map.height, findChild(view.librarySpace, "spaceSmall").y - Theme.spaceGap - map.y);
        view.table.clearSelection();

        // "move this folder…" moves the cursor's folder.
        const move = findChild(view.librarySpace, "spaceMoveFolder");
        verify(move.visible);
        let asked = "";
        const take = folder => asked = folder;
        view.librarySpace.moveFolderRequested.connect(take);
        mouseClick(move);
        view.librarySpace.moveFolderRequested.disconnect(take);
        compare(asked, view.librarySpace.cursorFolder.key);
        tryVerify(() => view.moveSheet.opened);
        view.moveSheet.close();
        tryVerify(() => !view.moveSheet.opened);
    }

    // The map's layout, on rows of known sizes: board 51's halves, the 1 % floor, the chips.
    function test_space_lays_out_by_halves_and_gathers_the_small() {
        const out = [];
        view.librarySpace.treemap([{ "weight": 3 }, { "weight": 1 }], 0, 0, 400, 100, out);
        compare(out.length, 2);
        compare(out[0].w, 300);
        compare(out[0].h, 100);
        compare(out[1].x, 300);
        const tall = [];
        view.librarySpace.treemap([{ "weight": 1 }, { "weight": 1 }], 0, 0, 100, 400, tall);
        compare(tall[1].y, 200);
        compare(tall[1].w, 100);

        App.setDisplay("space");
        waitForRendering(view);
        // Groups as the proxy gives them, one folder under the floor.
        const groups = [
            { "key": "/a", "title": "/a", "line": "", "first": 0, "count": 2, "bytes": 9900, "question": 0 },
            { "key": "/b", "title": "/b", "line": "", "first": 2, "count": 3, "bytes": 50, "question": 0 }
        ];
        const layout = view.librarySpace.layOut(view.librarySpace.gather(0, groups), 400, 200);
        compare(layout.total, 9950);
        compare(layout.count, 5);
        compare(layout.folders.length, 1);
        compare(layout.folders[0].framed, false, "one folder needs no frame: the line over the map names it");
        const folderChip = layout.chips.find(chip => chip.folder);
        verify(folderChip !== undefined);
        compare(folderChip.first, 2);
        compare(folderChip.count, 3);
        verify(folderChip.text.startsWith("/b · 3"));
    }

    // #201 F16: h / l move the keys across rail, list and details; the list's cursor keeps its
    // fill but shows its edge only while the list has them, and the details' tab line is lit
    // only while the pane has them. The same in rows, table and space.
    function test_h_and_l_show_where_the_keys_are() {
        const tabLine = () => findChild(findChild(findChild(view.details, "detailsTabs"),
                                                  "tab_" + view.details.currentTab), "tabLine");
        const edges = {
            "rows": () => findChild(testCase.rowItem(view.table.cursorIndex),
                                    "libraryRowCursor_" + view.table.cursorIndex).visible,
            "table": () => findChild(findChild(view.table, "row_" + view.table.cursorIndex),
                                     "tableRowCursor").visible,
            "space": () => {
                const tile = findChild(view.librarySpace, "spaceTile_" + view.table.cursorIndex);
                const chip = view.librarySpace.chipOf(view.table.cursorIndex);
                return tile !== null ? findChild(tile, "spaceTileEdge").edgeWidth === Theme.spaceCursorEdge
                                     : chip !== null && findChild(chip, "spaceChipPlate").edgeWidth === Theme.spaceCursorEdge;
            }
        };
        for (const display of ["rows", "table", "space"]) {
            App.setDisplay(display);
            view.table.clearSelection();
            view.table.moveCursorTo(1);
            view.table.forceActiveFocus();
            waitForRendering(view);
            verify(view.table.keysHere, display);
            verify(edges[display](), display + ": the cursor had no edge with the keys");
            compare(tabLine().height, Theme.hairWidth);

            keyClick("l");
            compare(view.focusArea, "details");
            verify(!view.table.keysHere);
            verify(!edges[display](), display + ": the cursor kept its edge without the keys");
            compare(tabLine().height, Theme.litWidth);
            compare(tabLine().color, Theme.litColor);

            keyClick("h");
            compare(view.focusArea, "table");
            verify(edges[display](), display + ": h from details left no sign on the list");
            verify(!Qt.colorEqual(tabLine().color, Theme.litColor));

            keyClick("h");
            compare(view.focusArea, "rail");
            verify(!edges[display](), display + ": the rail has the keys, not the list");
            keyClick("l");
        }
    }

    // H4: remembered per scope, folders included; a scope without its own shows the
    // library's.
    function test_the_display_is_remembered_per_scope() {
        App.setDisplay("rows");
        const folder = App.torrentRow(App.filtered.hashAt(0)).savePath;
        App.filtered.folder = folder;
        compare(App.display, "rows");
        // The shelves' old name picks space (C32 D12 a).
        App.setDisplay("overview");
        compare(App.display, "space");
        compare(view.table.face, view.librarySpace);
        App.filtered.folder = "";
        compare(App.display, "rows");
        compare(view.table.face, view.libraryRows);
        App.filtered.question = TorrentFilterModel.GivingBack;
        compare(App.display, "rows");
        App.filtered.question = TorrentFilterModel.AnyQuestion;
        App.filtered.folder = folder;
        compare(App.display, "space");
        // Size orders the map: the tool says the display alone.
        compare(view.titleBar.displayTool.text, "space ▾");
        App.filtered.folder = "";
    }

    // #201 F17 (C32 "Questions"): a question from the rail or 1–5 replaces the place, as the
    // library does, and keeps a display of its own; a place picked leaves the question. The
    // place's display never changes through the question.
    function test_a_question_replaces_the_place() {
        App.setDisplay("rows");
        const folder = App.torrentRow(App.filtered.hashAt(0)).savePath;
        view.rail._pickFolder(folder, false);
        compare(App.filtered.folder, folder);
        App.setDisplay("table");
        compare(App.displayScope, "folder:" + folder);

        keyClick("4"); // giving back, from the folder
        compare(App.filtered.question, TorrentFilterModel.GivingBack);
        compare(App.filtered.folder, "");
        compare(App.displayScope, "question:givingBack");
        compare(App.display, "rows", "the question took the folder's display");
        App.setDisplay("space");
        compare(App.displayFor("folder:" + folder), "table");

        // A category picked leaves the question; the question asked again leaves the category.
        view.rail._rowMap[view.rail.categoriesBase + 1].select(); // the first named category
        compare(App.filtered.question, TorrentFilterModel.AnyQuestion);
        verify(App.filtered.category !== undefined);
        view.rail.selectQuestion(TorrentFilterModel.GivingBack);
        compare(App.filtered.category, undefined);
        compare(App.display, "space");

        view.rail._pickFolder(folder, false);
        compare(App.filtered.question, TorrentFilterModel.AnyQuestion);
        compare(App.filtered.folder, folder);
        compare(App.display, "table");
        App.filtered.folder = "";
        compare(App.display, "rows");
    }

    // The tool's menu: the three, the scope's checked; a pick sets it.
    function test_the_display_tool_opens_its_menu() {
        verify(view.titleBar.displayTool.visible);
        mouseClick(view.titleBar.displayTool);
        tryVerify(() => view.displayMenu.opened);
        compare(view.displayMenu.items.slice(0, 3).map(item => item.action), ["rows", "table", "space"]);
        compare(view.displayMenu.items.filter(item => item.checked && item.radio !== true).map(item => item.action),
                ["table"]);
        view.displayMenu.activated("rows");
        view.displayMenu.close();
        compare(App.display, "rows");
        // The palette's rows run the same.
        view.runCommand("displaySpace");
        compare(App.display, "space");
        // Space has no sort block: size orders its map, and the footer says so.
        compare(view.displayMenu.items.filter(item => item.radio === true).length, 0);
        compare(view.displayMenu.footer, "size orders the map, the largest first");
        view.runCommand("displayTable");
        compare(App.display, "table");
        // Not on the seed console or the log.
        view.setView("seed");
        verify(!view.titleBar.displayTool.visible);
        view.setView("library");
    }

    // #201 F13: however narrow the list, a row never draws its words over each other: the
    // progress line stays in the words' column, the fact and caption right of it, and the name
    // keeps its floor; the caption drops, then the fact shortens, first.
    function test_narrow_rows_never_draw_over_themselves() {
        testCase.rowsShown();
        App.filtered.question = TorrentFilterModel.Arriving;
        const placement = App.detailsPlacement;
        const hidden = App.railHidden;
        // The window's widths, then the narrowest list it allows: the rail held open by hand
        // and the details beside the list.
        const cases = [[1360, false], [760, false], [600, false], [480, false], [760, true], [600, true]];
        let captionsDropped = false;
        for (const [width, crowded] of cases) {
            testCase.width = width;
            if (crowded) {
                view.setRailFoldedByHand(false);
                App.detailsPlacement = "right";
            }
            waitForRendering(view);
            for (let row = 0; row < App.filtered.count; ++row) {
                const item = testCase.rowItem(row);
                if (item === null)
                    continue;
                const at = name => findChild(item, name + "_" + row);
                const name = at("libraryRowName");
                const line = at("libraryRowLine");
                const fact = at("libraryRowFact");
                const caption = at("libraryRowCaption");
                const factLeft = fact.parent.mapToItem(item, 0, 0).x;
                const right = part => part.mapToItem(item, part.width, 0).x;
                verify(right(name) <= factLeft, width + ": the name ran into the fact on row " + row);
                verify(right(line) <= factLeft, width + ": the line ran into the fact on row " + row);
                // The floor, or all the words' column has when the list is narrower still.
                verify(name.width >= Math.min(name.implicitWidth, Theme.libraryRowNameFloor, name.parent.width) - 1,
                       width + ": the name lost its floor on row " + row);
                if (caption.visible)
                    verify(caption.implicitWidth <= caption.width + 1, width + ": the caption was cut");
                else if (item.rowCaption.length > 0)
                    captionsDropped = true;
            }
        }
        verify(captionsDropped, "the narrowest list never squeezed a row");
        App.detailsPlacement = placement;
        view.railHeldOpen = false;
        App.railHidden = hidden;
        testCase.width = 1360;
    }

    // Board 51 D13 a (C32): the menu's sort block, the order in use picked with its direction
    // in words; a pick sorts, "reverse" turns it, and the tool's word says it.
    function test_the_display_tool_sorts() {
        testCase.rowsShown();
        const sorts = () => view.displayMenu.items.filter(item => (item.action ?? "").startsWith("sort:")
                                                           && item.action !== "sort:reverse");
        compare(sorts().map(item => item.text), ["added", "name", "size", "progress", "time to ready", "ratio"]);
        compare(view.displayMenu.items.find(item => item.caption === true).text, "sort, inside each group");
        compare(sorts().filter(item => item.checked).map(item => item.action), ["sort:name"]);
        compare(sorts().find(item => item.checked).note, "a to z");
        compare(view.displayMenu.footer, "also s, the sort palette");

        mouseClick(view.titleBar.displayTool);
        tryVerify(() => view.displayMenu.opened);
        view.displayMenu.activated("sort:addedOn");
        view.displayMenu.close();
        compare(App.filtered.sortRoleName, "addedOn");
        compare(App.filtered.sortOrder, Qt.DescendingOrder);
        compare(App.display, "rows", "a sort keeps the display");
        compare(view.titleBar.displayTool.text, "rows · added ▾");
        compare(view.titleBar.displayTool.Accessible.name, "display: rows, sorted by added");
        compare(sorts().find(item => item.checked).note, "newest first");
        // The one in use again keeps it; "reverse" turns it.
        view.displayMenu.activated("sort:addedOn");
        compare(App.filtered.sortOrder, Qt.DescendingOrder);
        view.displayMenu.activated("sort:reverse");
        compare(App.filtered.sortOrder, Qt.AscendingOrder);
        compare(sorts().find(item => item.checked).note, "oldest first");

        // A sort the block doesn't list joins it, picked, at its top.
        App.filtered.setSortByRoleName("dlspeed");
        compare(sorts()[0].text, "download speed");
        verify(sorts()[0].checked);
        compare(sorts().length, 7);
        compare(view.titleBar.displayTool.text, "rows · download speed ▾");
        App.setDisplay("table");
        compare(view.displayMenu.items.find(item => item.caption === true).text, "sort");
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
