pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The whole main window, minus the window (board 46's frame, DECISIONS C24 H2, H14): the rail
// down the whole left side, and beside it a column of the title bar (H5), the bands (the
// server's notice, a move's), the body (table | details, or the seed console, or the log) and
// the status line (H15). `Main.qml` is a `Window` that holds exactly one of these; the split exists so the qmltestrunner can build the shell without
// a top-level window and still drive its keys and focus.
//
// What lives here and nowhere else:
//   - the wiring between App and the views (Rail, TitleBar, TorrentTable, Details, StatusLine);
//   - key routing. A key press reaches the focused view first — Qt propagates what the view
//     does not accept up the parent chain to this item's `Keys.onPressed` — so `routeKey`
//     only ever sees what no view claimed. That is what lets `a` add a tracker inside the
//     details pane and a torrent everywhere else, with one table of keys (`Keymap`);
//   - which state the window is in (first sync, empty, lost server, filter with no result)
//     and therefore which of §7's views covers the body.
// Keys are taught on demand (C24 H10): menus, tooltips, the palette and `?` read `Keymap`; the
// status line teaches none but "? keys".
//
// Do NOT run `qmlformat` on this file: it oscillates, de-indenting everything after the
// `body` Item on every other pass (the same defect T3.3 hit in `TorrentTable.qml`). It is
// hand-formatted to qmlformat's own style instead.
Item {
    id: root

    /// `q`: hide to the tray. The window handles it (Main.qml); a test can watch it.
    signal hideRequested
    /// `ctrl+q`: really quit.
    signal quitRequested
    /// First run's done screen said "open the library": Main.qml lets its window go from the
    /// floating setup size to a normal, tiled one.
    signal setupFinished

    // the pieces, exposed for tests and for Main
    readonly property alias titleBar: titleBar
    readonly property alias rail: rail
    readonly property alias table: table
    readonly property alias libraryRows: libraryRows
    readonly property alias libraryShelves: libraryShelves
    readonly property alias displayMenu: displayMenu
    readonly property alias details: details
    readonly property alias split: split
    readonly property alias statusLine: statusLine
    readonly property alias helpSheet: helpSheet
    readonly property alias helpSheetPanel: helpSheetPanel
    // T4.3
    readonly property alias actionsMenuPanel: actionsMenuPanel
    /// C19 T3: the rail folder row's menu.
    readonly property alias folderMenuPanel: folderMenuPanel
    readonly property alias selectionBar: selectionBar
    readonly property alias outcomeStrip: outcomeStrip
    readonly property alias footerNoticeItem: footerNotice
    readonly property alias deleteConfirmPanel: deleteConfirmPanel
    readonly property alias moveSheet: moveSheet
    readonly property alias freeSpaceSheet: freeSpaceSheet
    readonly property alias addAgainConfirm: addAgainConfirm
    readonly property alias moveBand: moveBand
    // #146: create a torrent, its band and "+ add ▾"'s menu.
    readonly property alias createSheet: createSheet
    readonly property alias createBand: createBand
    readonly property alias addMenu: addMenu
    readonly property alias moveQuitConfirm: moveQuitConfirm
    readonly property alias sharedMoveConfirm: sharedMoveConfirm
    readonly property alias limitsSheetPanel: limitsSheetPanel
    readonly property alias appMenu: appMenu
    readonly property alias globalLimitsSheet: globalLimitsSheet
    readonly property alias renameSheetPanel: renameSheetPanel
    // ruling Q4: the Trackers/Peers tabs' `a` and the Peers tab's `b`
    readonly property alias detailsAddSheet: detailsAddSheet
    readonly property alias renameFilesSheet: renameFilesSheet
    readonly property alias banPeerConfirm: banPeerConfirm
    // T4.8: the rest of the overlays, for the stacking and focus cases of `tst_Main`.
    readonly property alias addSheet: addSheet
    readonly property alias duplicateSheet: duplicateSheet
    readonly property alias settingsModal: settingsModal
    readonly property alias jumpPalette: jumpPalette
    readonly property alias categoryPalette: categoryPalette
    readonly property alias tagPalette: tagPalette
    readonly property alias sortPalette: sortPalette
    readonly property alias columnsPalette: columnsPalette
    readonly property alias commandPalette: commandPalette
    readonly property alias serverPalette: serverPalette
    readonly property alias serverNotice: banner
    readonly property alias logView: logView

    /// The `?` sheet.
    property bool helpOpen: false

    // ---- overlays (T4.8) -----------------------------------------------------------------
    // Sheets, palettes and the actions menu are all modal: while one is up it owns the
    // keyboard, `esc` closes that one and nothing else, and its own foot teaches its keys.
    // They are listed topmost-first, by the `z` each one is given further down, so the one
    // the user is actually looking at is the one that answers.
    /// The overlay on top, or null.
    readonly property var activeOverlay: {
        if (serverPalette.opened)
            return serverPalette;
        if (commandPalette.opened)
            return commandPalette;
        if (categoryPalette.opened)
            return categoryPalette;
        if (tagPalette.opened)
            return tagPalette;
        if (jumpPalette.opened)
            return jumpPalette;
        if (sortPalette.opened)
            return sortPalette;
        if (columnsPalette.opened)
            return columnsPalette;
        if (sharedMoveConfirm.opened)
            return sharedMoveConfirm;
        if (duplicateSheet.opened)
            return duplicateSheet;
        if (addSheet.opened)
            return addSheet;
        if (settingsModal.opened)
            return settingsModal;
        if (helpSheetPanel.opened)
            return helpSheetPanel;
        if (renameSheetPanel.opened)
            return renameSheetPanel;
        if (detailsAddSheet.opened)
            return detailsAddSheet;
        if (renameFilesSheet.opened)
            return renameFilesSheet;
        if (banPeerConfirm.opened)
            return banPeerConfirm;
        if (limitsSheetPanel.opened)
            return limitsSheetPanel;
        if (globalLimitsSheet.opened)
            return globalLimitsSheet;
        if (moveQuitConfirm.opened)
            return moveQuitConfirm;
        if (moveLooseList.opened)
            return moveLooseList;
        if (moveRecheckSheet.opened)
            return moveRecheckSheet;
        if (moveSheet.opened)
            return moveSheet;
        if (createSheet.opened)
            return createSheet;
        if (deleteConfirmPanel.opened)
            return deleteConfirmPanel;
        if (actionsMenuPanel.opened)
            return actionsMenuPanel;
        if (folderMenuPanel.opened)
            return folderMenuPanel;
        if (appMenu.opened)
            return appMenu;
        return null;
    }

    /// The rail groups folded on this server (docs/08 §5), saved with the table's state.
    property var railFolded: []

    /// DECISIONS C24 H10: the frame shows its keys (alt held alone a moment, see altWatch).
    readonly property bool keysShown: root._altLong && !root.overlayOpen && !root.firstRun
    property bool _altLong: false
    /// altWatch, for the tests.
    readonly property alias altWatch: altWatch

    // ---- board 42: moving a folder (DECISIONS C15, #97) ---------------------------------
    /// "show" (J1, S1): the table lists these torrents, with the Move column while they are
    /// the job's. Empty: the library as the rail filters it.
    property var moveShowHashes: []
    /// The "show" came from the move sheet: esc brings the sheet back.
    property bool moveSheetWaiting: false
    readonly property bool moveShowing: root.moveShowHashes.length > 0
    /// The job's torrents are on show (J1), not a preflight line's.
    readonly property bool moveShowingJob: root.moveShowing && !root.moveSheetWaiting

    /// The folder a move starts from: the one picked in the rail, else the cursor's save path.
    function moveFolderTarget(): string {
        if (App.filtered.folder.length > 0)
            return App.filtered.folder;
        return root.cursorHash.length > 0 ? App.move.folderOf(root.cursorHash) : "";
    }

    /// #146 (C27 CR1): ctrl+n, "+ add ▾", `:`, a folder row's menu and a dropped folder open
    /// the create sheet, on `folder` when one is given. Not on a server that can't create.
    function openCreate(folder: string): void {
        if (root.firstRun || !App.createOffered)
            return;
        createSheet.openFor(folder);
    }

    /// "+ add ▾" (C24 H17): its menu under the tool, right edges together; with nothing but
    /// "add" in it, the add sheet straight away.
    function openAddMenu(): void {
        if (!App.createOffered && !App.searchOffered) {
            App.addRequested();
            return;
        }
        const tool = titleBar.addTool;
        const corner = tool.mapToItem(root, tool.width, tool.height);
        addMenu.openAt(Math.round(corner.x - addMenu.menuWidth), Math.round(corner.y + Theme.railMenuGap));
    }

    /// A folder dropped on the window (C27 CR1): the create sheet on it.
    function createDropped(folder: string): void {
        if (root.firstRun || !App.createOffered) {
            root.flashFooterNote(qsTr("nothing to add"));
            return;
        }
        createSheet.openFor(folder);
    }

    /// `shift+m`, the palette's "move folder…", the title bar's tool (C24 H5); `newPath` for
    /// J3's "move the rest…" and T2's "change the folder…".
    function openMoveFolder(folder: var, newPath: var): void {
        const from = typeof folder === "string" && folder.length > 0 ? folder : root.moveFolderTarget();
        if (from.length === 0) {
            root.flashFooterNote(qsTr("pick a folder in the rail"));
            return;
        }
        root.leaveMoveShow();
        moveSheet.openFor(from, newPath);
    }

    /// "show": the table lists `hashes`. From the sheet, esc brings it back.
    function showMoveHashes(hashes: var, fromSheet: bool): void {
        if (hashes.length === 0)
            return;
        root.setView("library");
        root.moveSheetWaiting = fromSheet;
        root.moveShowHashes = hashes;
        App.filtered.onlyHashes = hashes;
        table.forceActiveFocus();
    }

    /// J1's "esc all torrents".
    function leaveMoveShow(): void {
        if (!root.moveShowing)
            return;
        root.moveShowHashes = [];
        App.filtered.onlyHashes = [];
        if (root.moveSheetWaiting) {
            root.moveSheetWaiting = false;
            moveSheet.reopen();
        }
    }

    /// ctrl+q, the palette's quit, the read-only guard's: a running move asks first (J5).
    function requestQuit(): void {
        if (App.quitAllowed())
            root.quitRequested();
    }

    /// The job's torrents that haven't moved (yet): what "move the rest…" and "change the
    /// folder…" open a selection's sheet on.
    function moveRestHashes(): var {
        return App.move.jobHashes.filter(hash => (App.move.stepFor(hash).kind ?? "") !== "ok");
    }

    /// An action of the move's band.
    function moveBandAction(key: string): void {
        switch (key) {
        case "pause":
            App.move.pause();
            break;
        case "resume":
            App.move.resume();
            break;
        case "end":
            App.move.end();
            break;
        case "keepWaiting":
            App.move.keepWaiting();
            break;
        case "show":
            root.showMoveHashes(App.move.state === "running" || App.move.state === "pausing"
                                || App.move.state === "paused" || App.move.state === "ending"
                                ? App.move.jobHashes : App.move.failedHashes().concat(App.move.jobHashes.filter(
                                                                                          hash => App.move.failedHashes(
                                                                                              ).indexOf(hash) < 0)),
                                false);
            break;
        case "changeFolder": {
            // C16 L1: a selection's job opens the selection's sheet on what didn't move.
            const rest = root.moveRestHashes();
            const folder = App.move.kind === "folder";
            App.move.end();
            if (folder)
                root.openMoveFolder(App.move.root, App.move.newPath);
            else
                moveSheet.openSelection(rest, App.move.newPath);
            break;
        }
        case "moveRest":
            if (App.move.kind === "folder")
                root.openMoveFolder(App.move.root, App.move.newPath);
            else
                moveSheet.openSelection(root.moveRestHashes(), App.move.newPath);
            break;
        case "moveBack":
            // C16 L11: each torrent back to where it was, checked as any move.
            moveSheet.openBack("", "");
            break;
        case "recheck":
            moveRecheckSheet.openFor(App.move.failedHashes());
            break;
        case "retry":
            App.move.retryFailed();
            break;
        case "stayed":
            moveLooseList.openStayed(App.move.root);
            break;
        case "close":
            root.leaveMoveShow();
            App.move.closeSummary();
            break;
        }
    }

    /// True while anything is over the window.
    readonly property bool overlayOpen: root.activeOverlay !== null

    // the window's state (spec §7)
    /// A configured, non-demo server that stopped answering: Fig. 23.
    /// docs/35 N6: only once the engine declared it lost, so a switch waiting for the keychain
    /// shows no loss.
    readonly property bool serverLost: root._testServerLost
                                       || (App.hasServer && !App.demoMode && App.serverLost
                                           && !App.globalState.connected && !root.firstSync)
    /// A test seam: the QML tests run on the demo library, which never loses its server.
    property bool _testServerLost: false
    /// A server that has not finished its first sync yet: Fig. 25.
    readonly property bool firstSync: App.hasServer && !App.demoMode
                                      && App.globalState.syncing && App.torrents.count === 0
    /// Nothing to show at all: Fig. 22, which doubles as the first-run screen.
    readonly property bool libraryEmpty: App.torrents.count === 0 && !root.firstSync
                                         && !root.serverLost
    /// docs/33 D7: lost (or refused) before it ever gave a list: the empty library's layout,
    /// with a line saying so, instead of an empty table.
    readonly property bool noList: root.serverLost && App.torrents.count === 0
    /// docs/33: every switch starts at two servers (D19).
    readonly property bool multiServer: App.serverCount > 1
    /// D1, D12: the rail's servers block shows (General may hide it).
    readonly property bool railServers: App.showServersGroup && root.multiServer
    /// D8, D9, D23: the lost server's words; docs/35 N3: yellow while omaseed retries.
    readonly property var serverProblem: App.serverProblem(App.globalState.lastError,
                                                           App.globalState.retryInSeconds,
                                                           App.globalState.silentSeconds)
    /// docs/44 D11: yellow only while it is there but not answering (a timeout); nothing
    /// there (refused, no route) is red, retried or not.
    readonly property bool serverRetrying: root.serverLost && !App.serverRefused
                                           && root.serverProblem.kind === "waiting"

    /// A filter that matches nothing: Fig. 24.
    readonly property bool filterEmpty: App.torrents.count > 0 && App.filtered.count === 0
    /// There are numbers worth printing in the status line.
    readonly property bool live: App.demoMode || App.globalState.connected

    /// #24 (docs/22 D1 a): the window's face, "library" or "seed" (the seed console); docs/32
    /// D9 (#56) adds "log", qBittorrent's log.
    property string view: "library"
    readonly property bool consoleShown: root.view === "seed" && !root.libraryEmpty && !root.firstSync
                                         && !root.firstRun
    /// docs/32 D9: the log face. Any server has a log, an empty library's too.
    readonly property bool logShown: root.view === "log" && App.hasServer && !root.firstRun
    /// The face `~` and `esc` go back to from the log ("back to where you were").
    property string _viewBeforeLog: "library"
    /// #145 (DECISIONS C26, C24 H16): search, a face of its own after log (ctrl+f). Only on a
    /// server that has search plugins to offer (C25 K30).
    readonly property bool searchShown: root.view === "search" && App.hasServer && !root.firstRun
    /// The face esc goes back to from search's empty field (SR14).
    property string _viewBeforeSearch: "library"

    /// ctrl+f (SR14), `:` "search…" and "search plugins for ‘…’", the add sheet's empty state:
    /// the search face, its field focused with the last words selected; `text`, when given,
    /// asked for at once.
    function openSearch(text: string): void {
        if (root.firstRun)
            return;
        if (!App.searchOffered) {
            root.flashFooterNote(qsTr("%1 has no search").arg(App.serverName));
            return;
        }
        root.setView("search");
        searchView.focusField();
        if (text.length > 0) {
            searchView.field.text = text;
            searchView.ask();
        }
    }

    /// `` ` ``, `~` and the rail's views. With a selection, the console opens on it (D2 a).
    function setView(next: string): void {
        if (next === root.view)
            return;
        if (next === "seed")
            App.console.setScopeHashes(table.selectedCount > 0 ? table.selectedHashes() : []);
        else
            App.console.setScopeHashes([]);
        if (next === "log")
            root._viewBeforeLog = root.view;
        if (next === "search" && root.view !== "search") {
            root._viewBeforeSearch = root.view;
            App.refreshSearchPlugins();
        }
        // `/` filters the face on show: the library's torrents, or search's results (SR14).
        const leavingSearch = root.view === "search";
        // docs/32 §6: the lines are kept only while the log is open. A demo's stay: no server
        // would read them again.
        if (root.view === "log" && !App.demoMode)
            App.serverLog.release();
        root.view = next;
        if (leavingSearch || next === "search")
            titleBar.searchField.text = next === "search" ? App.searchResults.filterText : App.filtered.text;
        if (next === "seed")
            seedConsole.forceActiveFocus();
        else
            root.focusHome();
        // D12: the log opens on its newest line, following.
        if (next === "log")
            logView.followNewest();
    }

    /// `~` (docs/32 D9): the log, or back to where the window was.
    function toggleLog(): void {
        root.setView(root.view === "log" ? root._viewBeforeLog : "log");
    }

    /// docs/32 D13: "its lines in the log" on a torrent's block: the log face, narrowed to it.
    function showTorrentLog(hash: string): void {
        if (hash.length === 0)
            return;
        root.setView("log");
        logView.showTorrentLines(hash);
    }

    /// The log's `enter` (D13): the line's torrent in the library, its story open (C24 H6).
    function showLogTorrent(hash: string): void {
        root.setView("library");
        if (table.revealHash(hash)) {
            root.showDetailsTab(details.tabStory);
            root.focusHome();
        }
    }

    /// The console's scope in words, from what the rail selects.
    readonly property string consoleScopeName: {
        const parts = [];
        const question = rail.questionRow(App.filtered.question);
        if (question !== null)
            parts.push(question.text);
        if (App.filtered.category !== undefined)
            parts.push(App.filtered.category === "" ? qsTr("no category") : qsTr("category %1").arg(App.filtered.category));
        if (App.filtered.tag !== undefined)
            parts.push(App.filtered.tag === "" ? qsTr("untagged") : qsTr("tag %1").arg(App.filtered.tag));
        if (App.filtered.tracker !== undefined)
            parts.push(qsTr("tracker %1").arg(rail.trackerText(App.filtered.tracker)));
        if (App.filtered.text.length > 0)
            parts.push(qsTr("“%1”").arg(App.filtered.text));
        return parts.length > 0 ? parts.join(" · ") : qsTr("every torrent");
    }

    /// The console's `esc`: the selection scope, then the rail's filters, then the library.
    function consoleEscape(): void {
        if (App.console.selectionScope) {
            App.console.setScopeHashes([]);
            return;
        }
        if (App.console.scoped) {
            App.filtered.question = TorrentFilterModel.AnyQuestion;
            App.filtered.clearCategory();
            App.filtered.clearTag();
            App.filtered.clearTracker();
            root.clearFilter();
            seedConsole.forceActiveFocus();
            return;
        }
        root.setView("library");
    }

    /// A click on a torrent in the console: the library, its cursor on it.
    function showInLibrary(hash: string): void {
        root.setView("library");
        table.revealHash(hash);
    }

    // docs/32 §6: the log is read from the server only while its face is on screen.
    Binding {
        target: App.serverLog
        property: "active"
        value: root.logShown && root.visible && (root.Window.window?.visible ?? false)
    }

    // Hidden to the tray, nobody sees the console: it stops refreshing until it is shown (#50).
    Binding {
        target: App.console
        property: "active"
        value: root.consoleShown && root.visible && (root.Window.window?.visible ?? false)
    }

    // #51 D8: the mouse's back button anywhere in the window is one `esc`, where the keyboard
    // is: the top sheet or menu closes, then the selection clears, then the filter. Not on
    // first run, where `esc` on the first step quits.
    BackButtonEscape {
        id: backButton

        enabled: !root.firstRun
    }

    /// Which view holds the focus, for the palette's context (`commandContext`).
    readonly property string focusArea: {
        if (titleBar.searchField.editing || details.filterEditing || logView.filterEditing)
            return "search";
        if (root.logShown)
            return "log";
        if (root.searchShown && !rail.activeFocus)
            return "searchView";
        if (root.consoleShown && !rail.activeFocus)
            return "console";
        if (rail.activeFocus)
            return "rail";
        if (details.activeFocus || details.editing)
            return "details";
        return "table";
    }

    /// The table's cursor row, which the details pane follows.
    readonly property string cursorHash: table.cursorHash

    // the status line's facts (board 46 `statusLine()`: count · free · sync)
    /// `[{value, label}]`, drawn value-in-foreground / label-in-dim and joined with `·`.
    readonly property var factParts: {
        if (root.serverLost)
            return [
                {
                    "value": "",
                    // Board 46: the list's time, as the notice says it ("The list below is
                    // from HH:MM", C12); docs/33 D9: never a time with nothing after it.
                    "label": App.globalState.lastSyncText.length > 0 ? qsTr("list from %1").arg(
                                                                           App.globalState.lastSyncText)
                                                                     : qsTr("not synced yet")
                }
            ]; // Fig. 23
        if (root.firstSync)
            return [
                {
                    "value": "",
                    "label": qsTr("syncing")
                }
            ]; // Fig. 25
        // Board 46: count · free · sync, "synced now" while the server answers; what the
        // Files tab adds comes after.
        const synced = root.live ? [
                                       {
                                           "value": "",
                                           "label": qsTr("synced now")
                                       }
                                   ] : [];
        if (root.filterEmpty)
            return [
                {
                    "value": "0",
                    "label": qsTr("of %1").arg(App.counts.total)
                }
            ].concat(synced); // Fig. 24
        // Fig. 1 (and Fig. 22, which simply has no downloading torrents to count). docs/10 §3:
        // on the status line's narrow steps the words shorten, then the least-read facts go.
        const step = statusLine.level;
        // `spoken` (#79): the words a screen reader says where the narrow steps shorten them.
        const torrentsWord = App.counts.total === 1 ? qsTr("torrent") : qsTr("torrents");
        const parts = [
            {
                "value": String(App.counts.total),
                "label": step >= 3 ? "" : torrentsWord,
                "spoken": torrentsWord
            }
        ];
        // Not the rail's count: that one takes stopped downloads too (Q16), which a status
        // line would misread as "downloading now".
        if (App.counts.inProgress > 0 && step < 4)
            parts.push({
                "value": String(App.counts.inProgress),
                "label": step >= 2 ? Glyphs.arrowDown : qsTr("downloading"),
                "spoken": qsTr("downloading")
            });
        if (step < 4)
            parts.push({
                "value": App.globalState.freeSpaceText,
                "label": qsTr("free")
            });
        // From step 3 the dot says it.
        // A selection is the action bar's to count, in the line's place (C24 H8).
        if (step < 3)
            parts.push(...synced);
        // C17 D1: what the skipped files still hold, while the Files tab shows them.
        const held = App.details.files.holdingOnDisk;
        if (held > 0 && details.currentTab === 1 && !details.collapsed && step < 3) {
            const heldWords = qsTr("%n skipped hold %1", "", held).arg(App.sizeText(App.details.files.heldOnDisk));
            parts.push({
                "value": "",
                "label": heldWords,
                "spoken": heldWords
            });
        }
        // docs/12 F3: the Files tab's own selection (`space`, `v`, `ctrl+a`), while it shows.
        const files = App.details.files.selectedCount;
        if (files > 0 && details.currentTab === 1 && !details.collapsed) {
            const filesWords = files === 1 ? qsTr("1 file selected") : qsTr("%1 files selected").arg(files);
            parts.push({
                "value": "",
                "label": step >= 2 ? qsTr("%1 files sel").arg(files) : filesWords,
                "spoken": filesWords
            });
        }
        return parts;
    }

    /// The same line as plain text, for the tests and for a log.
    readonly property string factsText: root.factParts.map(
                                            part => (part.value + " " + part.label).trim()).join(" · ")
    /// #79: the line as a screen reader says it: whole words, never the ↓, joined by commas.
    readonly property string factsSpoken: root.factParts.map(
                                              part => (part.value + " " + (part.spoken ?? part.label)).trim()).join(", ")

    implicitHeight: Theme.space(800)
    implicitWidth: Theme.space(1360)

    // the API Main and the tests drive

    /// `/`: put the caret in the title bar's filter.
    function focusSearch(): void {
        titleBar.openSearch();
    }

    /// `esc` in the field, and the field's own clear: empty the filter and go back to the
    /// table.
    function clearFilter(): void {
        titleBar.searchField.text = "";
        if (root.searchShown)
            App.searchResults.filterText = "";
        else
            App.filtered.text = "";
        root.focusHome();
    }

    /// Where the keyboard lives when nothing else claims it: the table, or the read-only
    /// guard over it while the server is lost (Fig. 23). Never while an overlay or the
    /// first-run page is up: they own the keyboard until they close, and a state change
    /// underneath them (the server dropping or coming back) must not take it away.
    function focusHome(): void {
        if (root.firstRun || root.overlayOpen)
            return;
        if (root.logShown) // read-only already: the lines stay with the server lost
            logView.focusLines();
        else if (root.searchShown)
            searchView.focusHome();
        else if (root.serverLost)
            dimmed.forceActiveFocus();
        else
            table.forceActiveFocus();
    }

    /// An overlay that closes hands the keyboard back to whatever had it when it opened. That
    /// may no longer be a fair home: the table while the server is lost (it would act past
    /// the read-only guard), the guard once the server is back, or an item hidden meanwhile.
    function _settleFocus(): void {
        if (root.firstRun || root.overlayOpen)
            return;
        const item = root.Window.activeFocusItem;
        const wrong = root.logShown ? !logView.activeFocus
                                    : root.searchShown ? !searchView.activeFocus && !titleBar.searchField.editing
                                    : root.serverLost ? !dimmed.activeFocus
                                                      : (dimmed.activeFocus || !item || !item.visible);
        if (wrong)
            root.focusHome();
    }

    onOverlayOpenChanged: {
        if (!root.overlayOpen)
            Qt.callLater(root._settleFocus);
    }

    /// `i`: fold the details pane to its strip, or unfold it (docs/17 M10.3: the same fold as
    /// a double-click on the divider). The split owns the state; `details.collapsed` follows.
    function toggleDetails(): void {
        split.toggleFold();
    }

    /// Unfold the details pane when folded and show tab `index` (docs/25 D3 a: Trackers
    /// after a merge).
    function showDetailsTab(index: int): void {
        if (split.folded)
            split.toggleFold();
        details.activateTab(index);
    }

    /// A fold stops the details' polls, and gives the keyboard to the table when the pane
    /// that held it went away (a folded pane takes no keys).
    function _detailsFoldChanged(): void {
        App.setDetailsVisible(!split.folded);
        if (split.folded)
            Qt.callLater(root._focusAfterFold);
    }

    function _focusAfterFold(): void {
        const item = root.Window.activeFocusItem;
        if (item === null || item === root || item === root.Window.contentItem || !item.visible)
            table.forceActiveFocus();
    }

    /// `I` (docs/10 §5): auto -> right -> bottom -> auto, shown in the footer for a moment. A
    /// hidden pane comes back, since moving it is a way of asking for it.
    function cycleDetailsPlacement(): void {
        const order = ["auto", "right", "bottom"];
        const next = order[(order.indexOf(App.detailsPlacement) + 1) % order.length];
        App.detailsPlacement = next;
        if (details.collapsed)
            root.toggleDetails();
        const words = {
            "auto": qsTr("auto"),
            "right": qsTr("right"),
            "bottom": qsTr("bottom")
        };
        root.flashFooterNote(qsTr("details: %1 · I to change").arg(words[next]));
    }

    /// #51 D1: a double-click on a torrent: the details pane unfolded, with the keys (`i`
    /// when it is folded, then `l`). The click before it already put the cursor there.
    function openDetails(): void {
        if (split.folded)
            split.toggleFold();
        Qt.callLater(() => {
            if (!details.collapsed && !root.overlayOpen)
                details.forceActiveFocus();
        });
    }

    /// `esc`, in the order Fig. 26 promises: clear, then filter, then close.
    function handleEscape(): void {
        // DECISIONS C18 D3: esc closes the outcome strip first, as its × does; its band
        // before it (docs/35 N8).
        if (root.footerNoticeOpen) {
            root.footerNoticeOpen = false;
            return;
        }
        if (outcomeStrip.visible) {
            App.outcomes.dismiss();
            return;
        }
        if (root.moveShowing && table.selectedCount === 0) {
            root.leaveMoveShow();
            return;
        }
        if (table.selectedCount > 0) {
            table.clearSelection();
            return;
        }
        if (App.filtered.text.length > 0)
            root.clearFilter();
    }

    /// The rail's own word for a question, for Fig. 24's `in <status>`; "" for the library.
    function questionName(question: int): string {
        const row = rail.questionRow(question);
        return row === null ? "" : row.text.charAt(0).toUpperCase() + row.text.slice(1);
    }

    /// `1`–`5`: a question of the rail, `0` the library (DECISIONS C24 H2), so the rail's own
    /// selection follows the key.
    function selectQuestion(question: int): void {
        rail.selectQuestion(question);
    }

    /// The mark and ctrl+. (DECISIONS C24 H9): the omaseed menu under the mark, left edges
    /// flush. First run holds it, as it holds `:`.
    /// The display tool (C24 H3–H4): its menu under it, right edges together.
    function openDisplayMenu(): void {
        const tool = titleBar.displayTool;
        const corner = tool.mapToItem(root, tool.width, tool.height);
        displayMenu.openAt(Math.round(corner.x - displayMenu.menuWidth),
                           Math.round(corner.y + Theme.railMenuGap));
    }

    /// The scope's display, from the tool's menu or the palette; the library on show.
    function pickDisplay(display: string): void {
        root.setView("library");
        App.setDisplay(display);
        root.focusHome();
    }

    function openAppMenu(): void {
        if (root.firstRun)
            return;
        const button = rail.markButton;
        const corner = button.mapToItem(root, Theme.railMarkInset, button.height);
        appMenu.openAt(Math.round(corner.x), Math.round(corner.y + Theme.railMenuGap));
    }

    /// A row of the omaseed menu, which has closed and given the focus back. The window's own
    /// rows and `A`'s row run as their keys do (alt-speed's note, docs/31 F1); quit asks
    /// first while a move runs (J5); the rest are C13's, through the one invoke the tray and
    /// the widget use.
    function appMenuAction(id: string): void {
        if (id === "quit")
            root.requestQuit();
        else if (id === "alt-speed")
            root.toggleAltSpeed();
        else if (appMenu.windowActions.indexOf(id) >= 0)
            root.runAction(id);
        else
            App.invokeAppMenu(id);
    }

    /// Put `text` on the clipboard and say `note`. An empty value leaves the clipboard as it
    /// was, so the footer must not claim a copy (review RUN-7: `Y` on a v2-only torrent).
    function copyText(text: string, note: string): void {
        if (text.length === 0) {
            root.flashFooterNote(qsTr("nothing to copy"));
            return;
        }
        App.copyToClipboard(text);
        root.flashFooterNote(note);
    }

    /// `y` / `Y` / `n` (ruling Q14): the magnet, hash or name of each of `hashes`, one per
    /// line, skipping torrents that have none (a magnet still fetching its metadata). A
    /// v2-only torrent has no v1 hash: its v2 one (or the id qBittorrent keys it by) instead.
    function copyLines(action: string, hashes: var): var {
        const lines = [];
        for (const hash of hashes) {
            const row = App.torrentRow(hash);
            let value = row.name;
            if (action === "copyMagnet")
                value = row.magnetUri;
            else if (action === "copyHash")
                value = row.infohashV1 || row.infohashV2 || row.hash;
            if (value)
                lines.push(value);
        }
        return lines;
    }

    /// Copies `copyLines()`. One torrent says "copied"; a selection says how many, and of how
    /// many when some had nothing to copy.
    function copyTorrents(action: string, hashes: var): void {
        const lines = root.copyLines(action, hashes);
        if (hashes.length <= 1 || lines.length === 0) {
            root.copyText(lines.join("\n"), qsTr("copied"));
            return;
        }
        let noun = qsTr("names");
        if (action === "copyMagnet")
            noun = qsTr("magnets");
        else if (action === "copyHash")
            noun = qsTr("hashes");
        const note = lines.length === hashes.length
                   ? qsTr("copied %1 %2").arg(lines.length).arg(noun)
                   : qsTr("copied %1 of %2 %3").arg(lines.length).arg(hashes.length).arg(noun);
        root.copyText(lines.join("\n"), note);
    }

    /// The hashes a table action applies to: the selection, or the cursor row alone.
    function actionHashes(): var {
        if (table.selectedCount > 0)
            return table.selectedHashes();
        return root.cursorHash.length > 0 ? [root.cursorHash] : [];
    }

    /// The actions menu's head line (Q24): "2 selected" when its rows act on a selection
    /// rather than on the row it opened on alone, so a jump + `tab` onto a third torrent
    /// does not read as acting on that one. Empty when the selection is just `hash` or none.
    function actionsHeading(hash: string): string {
        if (root._actionsOnSeveral(hash))
            return qsTr("%n selected", "", table.selectedCount);
        // Board 46 `menuHtml()`'s `.mh` (C24 H9): one torrent's state under its name.
        if (hash.length === 0)
            return "";
        const row = App.torrentRow(hash);
        return [row.stateText ?? "", row.progressText ?? "", (row.category ?? "").length > 0
                ? row.category : qsTr("no category")].filter(part => part.length > 0).join(" · ");
    }

    /// The actions menu's bright title (board 46 `.mh b`): the torrent's name when it acts on
    /// one; none when Q24's "N selected" heads it.
    function actionsHeadingTitle(hash: string): string {
        if (root._actionsOnSeveral(hash) || hash.length === 0)
            return "";
        return App.torrentRow(hash).name ?? "";
    }

    /// The actions menu acts on a selection other than `hash`'s row alone (Q24).
    function _actionsOnSeveral(hash: string): bool {
        const count = table.selectedCount;
        return count > 1 || (count === 1 && table.selectedHashes()[0] !== hash);
    }

    /// Every action of Fig. 11/26: the direct ones call straight through to `App`, the ones
    /// that need a question or a form open the matching overlay of the T4.3 block below.
    /// `category`/`tags` are T4.4's palettes to answer; this only raises the request.
    function torrentAction(action: string): void {
        const hashes = root.actionHashes();
        if (hashes.length === 0)
            return;
        switch (action) {
        case "stopStart":
            App.stopStart(hashes);
            break;
        // docs/30 D5: start and stop on the whole selection, as the selection bar's buttons.
        case "start":
            App.startTorrents(hashes);
            break;
        case "stop":
            App.stopTorrents(hashes);
            break;
        // DECISIONS C20 (#107): the errored ones of the targets, tried again.
        case "retry": {
            const errored = hashes.filter(hash => App.torrentRow(hash).stateText === "error");
            if (errored.length > 0)
                App.retryTorrents(errored);
            break;
        }
        case "forceStart":
            App.forceStart(hashes);
            break;
        case "recheck":
            App.recheck(hashes);
            break;
        case "reannounce":
            App.reannounce(hashes);
            break;
        case "delete":
            deleteConfirmPanel.openFor(hashes, App.deleteFilesDefault);
            break;
        case "deleteWithFiles":
            deleteConfirmPanel.openFor(hashes, true);
            break;
        case "location": {
            // docs/32 F1: `m` on the one torrent whose move didn't happen tries it again.
            const failure = hashes.length === 1 ? (App.torrentRow(hashes[0]).failure || {}) : {};
            moveSheet.openSelection(hashes, failure.destination || "");
            break;
        }
        case "limits":
            limitsSheetPanel.openFor(hashes);
            break;
        case "rename":
            renameSheetPanel.openFor(root.cursorHash);
            break;
        case "category":
            App.categoryPickerRequested(hashes);
            break;
        case "tags":
            App.tagPickerRequested(hashes);
            break;
        case "queueUp":
            App.queueUp(hashes);
            break;
        case "queueDown":
            App.queueDown(hashes);
            break;
        case "copyMagnet":
        case "copyHash":
        case "copyName":
            root.copyTorrents(action, hashes);
            break;
        case "openFolder":
            App.openFolder(root.cursorHash);
            break;
        // docs/12 G4: the other side of the cursor torrent's option, on every hash.
        case "sequential":
            App.setSequentialDownload(hashes, App.torrentRow(root.cursorHash).seqDl !== true);
            break;
        case "firstLast":
            App.setFirstLastPiecePrio(hashes,
                                      App.torrentRow(root.cursorHash).firstLastPiecePrio !== true);
            break;
        case "superSeeding":
            App.setSuperSeeding(hashes, App.torrentRow(root.cursorHash).superSeeding !== true);
            break;
        case "autoManagement":
            root.setAutoManagement(hashes, App.torrentRow(root.cursorHash).autoTmm !== true);
            break;
        default:
            console.log("action:", action, hashes.length, "torrents");
            break;
        }
    }

    /// Automatic management on or off for `hashes`. Turning it on moves the files to the
    /// category's folder, so it asks first when another torrent uses them (#65, docs/26).
    function setAutoManagement(hashes: var, on: bool): void {
        if (on)
            sharedMoveConfirm.turnOnAutoManagement(hashes);
        else
            App.setAutoManagement(hashes, false);
    }

    /// The action bar's `p` word (DECISIONS C24 H8, #170): whether the selection holds stopped
    /// torrents, running ones, or both; read again on every sync.
    readonly property var selectionRunStates: {
        App.globalState.lastSyncTime; // the dependency: a sync that starts or stops one
        App.torrents.count;
        const states = {
            "stopped": false,
            "running": false
        };
        if (table.selectedCount === 0)
            return states;
        for (const hash of table.selectedHashes()) {
            if (App.torrentRow(hash).stopped === true)
                states.stopped = true;
            else
                states.running = true;
            if (states.stopped && states.running)
                break;
        }
        return states;
    }

    /// The selection bar's buttons (docs/17 M9.3): the actions menu's own path, plus start
    /// and stop as two buttons, since a selection can hold both kinds and `stopStart`
    /// follows the cursor torrent alone.
    function selectionBarAction(action: string): void {
        root.torrentAction(action);
    }

    /// The selection bar's ⋯: the full actions menu hung over the button, right edges flush,
    /// with the actions the bar folded away at this width listed first (docs/17 M9.5).
    function openSelectionMore(button: Item, folded: var): void {
        const corner = button.mapToItem(root, button.width, 0);
        const hash = root.cursorHash;
        actionsMenuPanel.cursorStopped = hash.length > 0 && App.torrentRow(hash).stopped === true;
        actionsMenuPanel.cursorRow = hash.length > 0 ? App.torrentRow(hash) : ({});
        actionsMenuPanel.leadActions = folded;
        actionsMenuPanel.heading = root.actionsHeading(hash);
        actionsMenuPanel.headingTitle = root.actionsHeadingTitle(hash);
        actionsMenuPanel.openAbove(Math.round(corner.x - actionsMenuPanel.menuWidth),
                                   Math.round(corner.y - Theme.space(4)));
    }

    /// The Trackers and Peers tabs' keys (Fig. 3/4). `R` reannounces the torrent (the API has
    /// no per-tracker reannounce) and `d` removes the trackers it is given (the tab's
    /// selection, or the cursor's row, #51 D15). `a` opens the input sheet for trackers or
    /// peers, and `b` bans the peers it is given once the confirm dialog has said the ban is
    /// permanent and global (ruling Q4, review VIEW-6).
    function detailsRowAction(kind: string, action: string, values: var): void {
        const hash = App.details.hash;
        if (hash.length === 0)
            return;
        // DHT, PeX and LSD are rows of the list but not trackers of the torrent.
        const targets = (values || []).filter(value => value.length > 0 && !value.startsWith("** ["));
        const removed = targets.length === 1 ? qsTr("removed %1").arg(targets[0])
                                             : kind === "tracker" ? qsTr("removed %n trackers", "", targets.length)
                                                                  : qsTr("removed %n HTTP sources", "", targets.length);
        if (kind === "tracker" && action === "reannounce") {
            App.reannounce([hash]);
            root.flashFooterNote(qsTr("reannounced"));
        } else if (kind === "tracker" && action === "remove") {
            if (targets.length === 0)
                return;
            App.removeTrackers(hash, targets);
            root.flashFooterNote(removed);
        } else if (kind === "webseed" && action === "remove") {
            if (targets.length === 0)
                return;
            App.removeWebSeeds(hash, targets);
            root.flashFooterNote(removed);
        } else if (action === "add") {
            detailsAddSheet.openFor(kind, hash);
        } else if (kind === "peer" && action === "ban") {
            if (targets.length > 0)
                banPeerConfirm.openFor(targets);
        }
    }

    /// The details pane follows the table's cursor. It is a snapshot, not a binding, so it
    /// is taken again on every sync (see the Connections below).
    function refreshDetails(): void {
        App.selectTorrent(root.cursorHash);
        details.torrent = root.cursorHash.length > 0 ? App.torrentRow(root.cursorHash) : null;
    }

    onCursorHashChanged: root.refreshDetails()
    onHelpOpenChanged: {
        if (root.helpOpen) {
            helpSheet.filterField.text = ""; // every opening starts on the whole map
            helpSheetPanel.open(); // the sheet hands the keyboard to HelpSheet and remembers
        } else {                   // what had it, so `esc` lands back where it came from
            helpSheetPanel.close();
        }
    }

    // Key routing. Only what no focused view claimed arrives here.
    Keys.onPressed: event => root._route(event)

    function _route(event) {
        // An overlay is modal: what it does not handle itself dies here rather than reaching
        // the window underneath. `Kit/Menu` and `Kit/Palette` deliberately leave keys they
        // have no use for unaccepted (a palette's `ctrl+k`, a menu's `i`), and without this
        // guard those went on to open a second overlay under the first.
        // The first-run page (T4.6) is a full-window page rather than an overlay, but the
        // window underneath it must stay just as deaf.
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;

        if (root.overlayOpen || root.firstRun) {
            if (ctrl && event.key === Qt.Key_Q) // quitting is never modal
                root.requestQuit();
            return;
        }
        // docs/35 N8: esc closes the failure's band first.
        if (root.footerNoticeOpen && event.key === Qt.Key_Escape) {
            root.footerNoticeOpen = false;
            event.accepted = true;
            return;
        }

        if (ctrl) {
            switch (event.key) {
            case Qt.Key_Q:
                root.requestQuit();
                break;
            case Qt.Key_K:
                // The jump palette moves the library's cursor: from the log or search, the
                // library first.
                if (root.logShown || root.searchShown)
                    root.setView("library");
                jumpPalette.open();
                break;
            case Qt.Key_P: // docs/30 D1: ctrl+shift+p, the editors' command palette key
                if (!shift)
                    return;
                root.openCommandPalette();
                break;
            case Qt.Key_Comma: // the desktop's settings key, beside bare `,` (Keymap "ctrl+,")
                if (shift)
                    root.openQbtSettings();
                else
                    App.openSettings();
                break;
            case Qt.Key_Less: // shift+, on most layouts: the qBittorrent group (docs/16 §2)
                root.openQbtSettings();
                break;
            case Qt.Key_Period:
                root.openAppMenu();
                break;
            case Qt.Key_B: // #167 assumed 4: folds the rail, which every face has
                root.toggleRail();
                break;
            case Qt.Key_S: // docs/33 D2: the server palette, app-level like ctrl+k
                root.openServerPalette();
                break;
            case Qt.Key_F: // #145 (C26 SR14): search, from anywhere
                root.openSearch("");
                break;
            case Qt.Key_N: // #146 (C24 H17, C27 CR1): create a torrent from a folder
                root.openCreate("");
                break;
            default:
                return;
            }
            event.accepted = true;
            return;
        }

        // #24: the console's own keys, while it is the face on show and the rail is not
        // where the keyboard is.
        if (root.consoleShown && !rail.activeFocus && !titleBar.searchField.editing) {
            switch (event.key) {
            case Qt.Key_BracketLeft:
                App.console.stepRange(-1);
                event.accepted = true;
                return;
            case Qt.Key_BracketRight:
                App.console.stepRange(1);
                event.accepted = true;
                return;
            case Qt.Key_J:
            case Qt.Key_Down:
                seedConsole.scrollBy(Theme.space(60));
                event.accepted = true;
                return;
            case Qt.Key_K:
            case Qt.Key_Up:
                seedConsole.scrollBy(-Theme.space(60));
                event.accepted = true;
                return;
            case Qt.Key_G:
                seedConsole.scrollBy(shift ? seedConsole.contentHeight : -seedConsole.contentHeight);
                event.accepted = true;
                return;
            case Qt.Key_H: // the rail, folded or not
                rail.forceActiveFocus();
                event.accepted = true;
                return;
            case Qt.Key_Escape:
                root.consoleEscape();
                event.accepted = true;
                return;
            }
        }

        // docs/32 §6: the log face takes its own keys (LogView). Of what it leaves, the keys
        // about the library's table, rail and details do nothing there; nor on search's face,
        // which takes its own (SearchView), but for `/`, which filters its results (SR14).
        if (root.searchShown && event.key === Qt.Key_Slash) {
            root.focusSearch();
            event.accepted = true;
            return;
        }
        if (root.logShown || root.searchShown) {
            switch (event.key) {
            case Qt.Key_I:
            case Qt.Key_Less:
            case Qt.Key_Greater:
            case Qt.Key_S:
            case Qt.Key_Bar:
            case Qt.Key_Slash:
            case Qt.Key_Escape:
            case Qt.Key_0:
            case Qt.Key_1:
            case Qt.Key_2:
            case Qt.Key_3:
            case Qt.Key_4:
            case Qt.Key_5:
            case Qt.Key_6:
            case Qt.Key_7:
            case Qt.Key_8:
            case Qt.Key_9:
                event.accepted = true;
                return;
            }
        }

        switch (event.key) {
        case Qt.Key_QuoteLeft: // #24: the seed console, a nod to a Quake console
            root.setView(root.view === "seed" ? "library" : "seed");
            break;
        case Qt.Key_AsciiTilde: // docs/32 D9: qBittorrent's log, beside the console's key
            root.toggleLog();
            break;
        case Qt.Key_Question:
        case Qt.Key_F1: // the desktop's help key, same sheet (Keymap's "f1" entry)
            root.helpOpen = true;
            break;
        case Qt.Key_Colon: // docs/30 D1: the command palette, vim's command line
            root.openCommandPalette();
            break;
        case Qt.Key_Slash:
            root.focusSearch();
            break;
        case Qt.Key_I:
            if (shift)
                root.cycleDetailsPlacement();
            else
                root.toggleDetails();
            break;
        case Qt.Key_Less: // docs/10 §5: the details pane smaller and larger, as vim's ctrl+w < >
            if (!details.collapsed)
                split.resizeBy(-Theme.space(40));
            break;
        case Qt.Key_Greater:
            if (!details.collapsed)
                split.resizeBy(Theme.space(40));
            break;
        case Qt.Key_A:
            if (shift)
                root.toggleAltSpeed();
            else
                App.addRequested();
            break;
        case Qt.Key_S:
            sortPalette.openOnCurrent();
            break;
        case Qt.Key_Comma:
            App.openSettings();
            break;
        case Qt.Key_Bar: // the columns palette: the pipe looks like a column rule
            columnsPalette.open();
            break;
        case Qt.Key_Q:
            root.hideRequested();
            break;
        case Qt.Key_Escape:
            root.handleEscape();
            break;
        case Qt.Key_0:
        case Qt.Key_1:
        case Qt.Key_2:
        case Qt.Key_3:
        case Qt.Key_4:
        case Qt.Key_5:
            root.selectQuestion(event.key - Qt.Key_0); // C24 H2: 0 the library, 1–5 the questions
            break;
        case Qt.Key_6:
        case Qt.Key_7:
        case Qt.Key_8:
        case Qt.Key_9:
            break; // five questions; 6–9 are spare
        default:
            if (!table.activeFocus || !root._tableAction(event, shift))
                return;
        }
        event.accepted = true;
    }

    /// docs/30 D9 (DECISIONS C1): the one runner behind the `?` sheet's `enter` and the
    /// command palette, so the two can never disagree. Each id does what its key does, through
    /// the same functions the keys call; keys keep their own routing. Rows of the sheet that
    /// name a movement rather than a command (j k, g G, h l, 1–5, 1–4, space, v, esc) are not
    /// here: false, and nothing ran.
    function runAction(action: string): bool {
        // Fig. 23's read-only guard: with the server lost, only what the guard itself still
        // lets through runs.
        if (root.serverLost && !Keymap.isOffline(action))
            return false;
        // docs/33 D4: "switch to NAME", straight there.
        if (action.startsWith(Keymap.switchToPrefix))
            return App.switchServer(action.slice(Keymap.switchToPrefix.length));
        const run = root._runners[action];
        if (run === undefined)
            return false;
        run();
        return true;
    }

    /// A torrent command from the sheet or the palette: on the table's selection or cursor
    /// row, with the keyboard on the table, as its key there would.
    function _runOnTorrents(action: string): void {
        table.forceActiveFocus();
        root.torrentAction(action);
    }

    /// A Files tab command: the pane shows, on Files, with the keyboard, then `run`.
    function _runInFiles(run: var): void {
        if (details.collapsed)
            root.toggleDetails();
        details.activateTab(1);
        details.forceActiveFocus();
        run();
    }

    /// A command of the details tab on show (Trackers, Peers, HTTP sources): the keyboard on
    /// the pane, then that tab's row menu does what its key does.
    function _runInTab(rowAction: string): void {
        details.forceActiveFocus();
        details.rowMenuAction(rowAction);
    }

    /// What `runAction` runs, by action id: every command `Keymap.commandRows()` lists, and
    /// the `?` sheet's rows that lead no palette command (stopStart, actions, more, copy,
    /// moveColumnUp). tst_Main checks both lists against the Keymap.
    readonly property var _runners: ({
        // torrents
        "start": () => root._runOnTorrents("start"),
        "stop": () => root._runOnTorrents("stop"),
        "stopStart": () => root._runOnTorrents("stopStart"),
        "retry": () => root._runOnTorrents("retry"),
        "forceStart": () => root._runOnTorrents("forceStart"),
        "recheck": () => root._runOnTorrents("recheck"),
        "reannounce": () => root._runOnTorrents("reannounce"),
        "delete": () => root._runOnTorrents("delete"),
        "deleteWithFiles": () => root._runOnTorrents("deleteWithFiles"),
        "category": () => root._runOnTorrents("category"),
        "tags": () => root._runOnTorrents("tags"),
        "location": () => root._runOnTorrents("location"),
        "moveFolder": () => root.openMoveFolder("", ""),
        // Board 45 B3 (C18 D7): the newest finished move only.
        "undoLastMove": () => moveSheet.openBack(App.move.newestMove().id ?? "", ""),
        "limits": () => root._runOnTorrents("limits"),
        "rename": () => root._runOnTorrents("rename"),
        "openFolder": () => root._runOnTorrents("openFolder"),
        "queueUp": () => root._runOnTorrents("queueUp"),
        "queueDown": () => root._runOnTorrents("queueDown"),
        "copyMagnet": () => root._runOnTorrents("copyMagnet"),
        "copyHash": () => root._runOnTorrents("copyHash"),
        "copyName": () => root._runOnTorrents("copyName"),
        "sequential": () => root._runOnTorrents("sequential"),
        "firstLast": () => root._runOnTorrents("firstLast"),
        "superSeeding": () => root._runOnTorrents("superSeeding"),
        "autoManagement": () => root._runOnTorrents("autoManagement"),
        "actions": () => {
            table.forceActiveFocus();
            table.requestActions();
        },
        // selection
        "selectAll": () => {
            table.forceActiveFocus();
            table.selectAll();
        },
        "clearSelection": () => table.clearSelection(),
        // library
        "add": () => App.addRequested(),
        "create": () => root.openCreate(""),
        "createFromFolder": () => root.openCreate(App.filtered.folder),
        "filter": () => root.focusSearch(),
        "jump": () => jumpPalette.open(),
        "sort": () => sortPalette.openOnCurrent(),
        // view
        "columns": () => columnsPalette.open(),
        "moveColumnUp": () => columnsPalette.open(), // the columns palette is where they move
        "toggleDetails": () => root.toggleDetails(),
        "cycleDetailsPlacement": () => root.cycleDetailsPlacement(),
        "toggleRail": () => root.toggleRail(),
        "toggleConsole": () => root.setView(root.view === "seed" ? "library" : "seed"),
        "toggleLog": () => root.toggleLog(),
        "search": () => root.openSearch(""),
        "detailsSmaller": () => {
            if (details.collapsed)
                root.toggleDetails();
            split.resizeBy(-Theme.space(40));
        },
        "detailsLarger": () => {
            if (details.collapsed)
                root.toggleDetails();
            split.resizeBy(Theme.space(40));
        },
        // speed
        "altSpeed": () => root.toggleAltSpeed(),
        "globalLimits": () => globalLimitsSheet.openFor(false),
        "altLimits": () => globalLimitsSheet.openFor(true),
        // DECISIONS C24 H3–H4: the display tool's three, from the palette.
        "displayRows": () => root.pickDisplay("rows"),
        "displayTable": () => root.pickDisplay("table"),
        "displayShelves": () => root.pickDisplay("overview"),
        // app
        "settings": () => App.openSettings(),
        "switchServer": () => root.openServerPalette(),
        "qbtSettings": () => root.openQbtSettings(),
        "help": () => root.helpOpen = true,
        "commands": () => root.openCommandPalette(),
        "appMenu": () => root.openAppMenu(),
        "hide": () => root.hideRequested(),
        "quit": () => root.requestQuit(),
        // the Files tab
        "priorityMenu": () => root._runInFiles(() => details.openPriorityMenu()),
        "raisePriority": () => root._runInFiles(() => details.setPriority("raise")),
        "lowerPriority": () => root._runInFiles(() => details.setPriority("lower")),
        "skipFile": () => root._runInFiles(() => details.toggleSkip()),
        "openFile": () => root._runInFiles(() => details.fileAction("openFile")),
        "revealFile": () => root._runInFiles(() => details.fileAction("reveal")),
        "renameFile": () => root._runInFiles(() => details.startRename()),
        "copyLocalPath": () => root._runInFiles(() => details.copyLocalPath()),
        "copyFileName": () => root._runInFiles(() => details.copyName()),
        "sortFiles": () => root._runInFiles(() => details.openSortMenu()),
        "filterFiles": () => root._runInFiles(() => details.focusFileFilter()),
        "selectAllFiles": () => root._runInFiles(() => details.selectAllFiles()),
        "collapseAll": () => root._runInFiles(() => details.fileAction("collapseAll")),
        "expandAll": () => root._runInFiles(() => details.fileAction("expandAll")),
        "hideSkipped": () => root._runInFiles(() => details.fileAction("hideSkipped")),
        "freeSpace": () => root._runInFiles(() => details.fileAction("freeSpace")),
        // Trackers, Peers, HTTP sources, and the pane's `y` everywhere (docs/12 G5)
        "addRow": () => root._runInTab("add"),
        "removeRow": () => root._runInTab("remove"),
        "trackersReannounce": () => root._runInTab("reannounce"),
        "ban": () => root._runInTab("ban"),
        "copy": () => {
            if (details.collapsed)
                root.toggleDetails();
            details.forceActiveFocus();
            details.copyCurrent();
        },
        // the seed console (#24)
        "consoleShorter": () => App.console.stepRange(-1),
        "consoleLonger": () => App.console.stepRange(1),
        "consoleEscape": () => root.consoleEscape(),
        // the log face (docs/32 §6, #56)
        "logFilter": () => logView.focusFilter(),
        "logNewest": () => logView.followNewest(),
        "logNextProblem": () => logView.nextProblem(1),
        "logPreviousProblem": () => logView.nextProblem(-1),
        "logFollow": () => logView.toggleFollow(),
        "logMarkAll": () => App.logLines.markAll(),
        "logCopy": () => logView.copyLines(),
        "logShowTorrent": () => logView.showLineTorrent(),
        "logTorrent": () => logView.narrowToLineTorrent(),
        "logEscape": () => logView.stepBack(),
        "logNormal": () => logView.toggleLevel(ServerLog.Normal),
        "logInfo": () => logView.toggleLevel(ServerLog.Info),
        "logWarning": () => logView.toggleLevel(ServerLog.Warning),
        "logCritical": () => logView.toggleLevel(ServerLog.Critical)
    })

    /// `:` and `ctrl+shift+p` (docs/30, DECISIONS C1): the command palette, on what the
    /// window shows as it opens.
    function openCommandPalette(): void {
        if (root.firstRun)
            return;
        commandPalette.context = root.commandContext();
        commandPalette.open();
    }

    /// `Keymap.commandRows()`'s context: where the keyboard is, what the torrent commands act
    /// on (the selection, or the cursor row), the focused tab's marked rows and cursor row, and
    /// the states the rows read. Plain data only.
    function commandContext(): var {
        let area = root.focusArea;
        if (area === "search")
            area = details.filterEditing ? "files" : logView.filterEditing ? "log" : "table";
        if (area === "details")
            area = ["story", "files", "peers", "trackers", "facts", "webseeds"][details.currentTab] || "story";
        const targets = root.actionHashes().map(hash => App.torrentRow(hash));
        const stopped = targets.filter(row => row.stopped === true).length;
        const cursor = root.cursorHash.length > 0 ? App.torrentRow(root.cursorHash) : ({});
        let marked = 0;
        let row = "";
        let rowIsFolder = false;
        const has = details.rowCount > 0;
        if (area === "files") {
            marked = App.details.files.selectedCount;
            row = has ? App.details.files.nameAt(details.filesCursor) : "";
            rowIsFolder = has && App.details.files.isFolder(details.filesCursor);
        } else if (area === "trackers") {
            marked = details.trackersSelection.count;
            row = has ? App.details.trackers.urlAt(details.trackersCursor) : "";
        } else if (area === "peers") {
            marked = details.peersSelection.count;
            row = has ? App.details.peers.addressAt(details.peersCursor) : "";
        } else if (area === "webseeds") {
            marked = details.webSeedsSelection.count;
            row = has ? App.details.webSeeds.urlAt(details.webSeedsCursor) : "";
        } else if (area === "log") {
            marked = App.logLines.markedCount;
            row = logView.cursor >= 0 ? logView.cursorMessage : "";
        }
        return {
            "area": area,
            "selected": table.selectedCount,
            "cursor": cursor.name || "",
            // Board 42 (M1): the folder "move folder…" would move.
            "folder": root.moveFolderTarget(),
            // Board 45 B3 (C18 D7): what "undo last move" would move back.
            "lastMove": App.move.newestMove(),
            "stopped": stopped,
            "running": targets.length - stopped,
            // DECISIONS C20: what "retry" would try again.
            "errored": targets.filter(row => row.stateText === "error").length,
            // #160 (C17 D3): "free the space…", while the details torrent's skipped files hold
            // space on a disk omaseed sees.
            "freeable": App.skippedOnDisk.hash === App.details.hash && App.skippedOnDisk.seen === true
                        && (App.skippedOnDisk.holding || 0) > 0,
            // C30 F13: "add it again to free SIZE…" while only that frees them.
            "readdText": App.skippedOnDisk.hash === App.details.hash && App.skippedOnDisk.outlook === "readd"
                         ? (App.skippedOnDisk.readdText || "") : "",
            // App.stopStart follows the first hash: `p` starts when that one is stopped.
            "firstStopped": targets.length > 0 && targets[0].stopped === true,
            "marked": marked,
            "row": row,
            "rowIsFolder": rowIsFolder,
            "serverLost": root.serverLost,
            // C26 SR14: the palette's "search…" rows, on a server with search.
            "searchOffered": App.searchOffered,
            // #146 (C27 CR1): "create torrent…", and "create torrent from NAME…" on the folder
            // picked in the rail.
            "createOffered": App.createOffered,
            "createFolder": App.filtered.folder,
            // docs/33 D4: the servers, for "switch server…" and one "switch to NAME" each.
            "servers": App.serverRows.map(row => ({
                                                      "id": row.key,
                                                      "name": row.title,
                                                      "current": row.current,
                                                      "last": row.state === "failed" ? row.lastReason
                                                                                     : row.lastCount >= 0 ? (row.lastCount === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(row.lastCount)) : ""
                                                  })),
            "altKind": App.altSpeed.kind,
            "recent": App.recentCommands(),
            "states": {
                "detailsShown": !details.collapsed,
                "railShown": !App.railHidden,
                "consoleShown": root.view === "seed",
                "logShown": root.view === "log",
                "searchShown": root.view === "search",
                "logFollowing": logView.following,
                "logNormal": (App.logLines.levels & ServerLog.Normal) !== 0,
                "logInfo": (App.logLines.levels & ServerLog.Info) !== 0,
                "logWarning": (App.logLines.levels & ServerLog.Warning) !== 0,
                "logCritical": (App.logLines.levels & ServerLog.Critical) !== 0,
                "consoleScoped": App.console.selectionScope || App.console.scoped,
                "altSpeed": App.globalState.useAltSpeedLimits === true,
                "placement": App.detailsPlacement,
                "hideSkipped": App.details.files.hideSkipped === true,
                "seqDl": cursor.seqDl === true,
                "firstLast": cursor.firstLastPiecePrio === true,
                "superSeeding": cursor.superSeeding === true,
                "autoTmm": cursor.autoTmm === true
            }
        };
    }

    /// The palette's `enter`. Its last five run come first next time (D7).
    function runCommand(action: string): void {
        // C26 SR14: "search plugins for ‘…’", the palette's second no-match row.
        if (action.startsWith(Keymap.searchForPrefix)) {
            root.openSearch(action.slice(Keymap.searchForPrefix.length));
            return;
        }
        // C24 H2: "show stalled…" types `is:stalled` into the filter and leaves the caret there.
        if (action.startsWith(Keymap.showStatePrefix)) {
            root.showState(action.slice(Keymap.showStatePrefix.length));
            App.noteCommandRun(action);
            return;
        }
        if (root.runAction(action))
            App.noteCommandRun(action);
    }

    /// The palette's "show STATE…" rows: the library, filtered by `is:STATE`, the caret in the
    /// field so more words can follow.
    function showState(word: string): void {
        root.setView("library");
        root.focusSearch();
        titleBar.searchField.text = "is:" + word + " ";
    }

    /// `ctrl+b` (docs/08 §5): fold the rail to its glyphs or open it again (#167 assumed 4).
    /// Folding it never hides a filter: the questions keep their glyphs, and the title bar's
    /// title and sentence name the place picked (C24 H5).
    function toggleRail(): void {
        App.railHidden = !App.railHidden;
    }

    /// ctrl+shift+, (docs/16 §2): Settings on the qBittorrent group. First run holds it like
    /// `,` does.
    function openQbtSettings(): void {
        if (root.firstRun)
            return;
        settingsModal.openQbt();
    }

    /// Fig. 26's "act" column, for the table only: each key goes through `torrentAction()`,
    /// which calls App or opens the overlay that asks first.
    function _tableAction(event, shift) {
        switch (event.key) {
        case Qt.Key_P:
            root.torrentAction(shift ? "forceStart" : "stopStart");
            return true;
        case Qt.Key_E:
            if (shift)
                return false;
            root.torrentAction("retry"); // DECISIONS C20: `p` stops an errored torrent
            return true;
        case Qt.Key_D:
            root.torrentAction(shift ? "deleteWithFiles" : "delete");
            return true;
        case Qt.Key_C:
            root.torrentAction(shift ? "category" : "recheck");
            return true;
        case Qt.Key_R:
            root.torrentAction("reannounce"); // plain `r` renames, and the table took it
            return true;
        case Qt.Key_T:
            root.torrentAction("tags");
            return true;
        case Qt.Key_M:
            // Board 42 (M1): shift+m moves the picked folder; m moves the selection (C16 L2).
            if (shift)
                root.openMoveFolder("", "");
            else
                root.torrentAction("location");
            return true;
        case Qt.Key_L:
            root.torrentAction("limits"); // plain `l` focuses the details, and the table took it
            return true;
        case Qt.Key_BracketLeft:
            root.torrentAction("queueUp");
            return true;
        case Qt.Key_BracketRight:
            root.torrentAction("queueDown");
            return true;
        case Qt.Key_O:
            root.torrentAction("openFolder");
            return true;
        case Qt.Key_Y:
            root.torrentAction(shift ? "copyHash" : "copyMagnet");
            return true;
        case Qt.Key_N:
            root.torrentAction("copyName");
            return true;
        case Qt.Key_X:
            // docs/32 D3: the failure block's `x`, only while the pane shows it on the story.
            if (details.collapsed || details.currentTab !== details.tabStory || root.cursorHash.length === 0
                    || (App.torrentRow(root.cursorHash).failure || {}).kind === undefined)
                return false;
            App.dismissFailure(root.cursorHash);
            return true;
        default:
            return false;
        }
    }

    /// The startup state. `Component.onCompleted` of a Window runs before its composite
    /// children's, so this is deferred a frame (T3.3's finding).
    function _applyStartupState(): void {
        // The first-run page (T4.6) already asked for the keyboard, and so has the add sheet
        // when the app was started with a magnet or a .torrent: that hand-off opens it before
        // this runs, and `enter` has to add, not open the table's actions behind it.
        root.focusHome();
        root.refreshDetails();
        App.setDetailsVisible(!details.collapsed);
        if (!App.demoMode || App.torrents.count !== 16)
            return;
        // `--demo table` reproduces spec §1 Fig. 1 exactly: Arriving (C24 H2) selected in the rail
        // with the cursor parked on "movies", the table's cursor on row 1, rows 1 and 2
        // selected, row 3 in inline rename, the details open on General for row 1.
        App.filtered.setSortByRoleName(table.initialSortRoleName);
        App.filtered.sortOrder = table.initialSortOrder;
        rail.selectQuestion(TorrentFilterModel.Arriving);
        rail.cursorIndex = rail.categoriesBase + 2;
        table.moveCursorTo(0);
        table.toggleCursorSelection();
        table.moveCursor(1);
        table.toggleCursorSelection();
        table.moveCursor(1);
        table.startRename();
        table.moveCursorTo(0);
        details.activateTab(0);
        // The inline editor grabs the keyboard when it appears; the figure wants it open but
        // the window still driveable, so the table takes the focus back.
        table.forceActiveFocus();
    }

    Component.onCompleted: {
        // Latched, not bound: `App.hasServer` flips the moment the first-run page saves the
        // server, and the page must survive that to show its last step (T4.6).
        root.firstRun = !App.hasServer && !App.demoMode;
        Qt.callLater(root._applyStartupState);
    }

    // A sync brings new numbers for the row the details pane is showing, and the server's log
    // a reason for its error (docs/28).
    Connections {
        function onProblemsChanged(): void {
            details.torrent = root.cursorHash.length > 0 ? App.torrentRow(root.cursorHash) : null;
        }
        function onUpdated(): void {
            details.torrent = root.cursorHash.length > 0 ? App.torrentRow(root.cursorHash) : null;
        }

        target: App.torrents
    }

    // Everything `App` asks the window for, in one place. Each Phase 4 task had declared its
    // own `Connections { target: App }`; two of them handled `showTorrentRequested`, so the
    // table's cursor moved twice for one notification click. One block also makes it plain
    // which signal is answered where.
    Connections {
        /// #146: "create again…", "try again": the sheet on the choices it had.
        function onCreateAgainRequested(request: var): void {
            if (!root.firstRun)
                createSheet.openWith(request);
        }

        /// `a`, the title bar's "+ add", the tray.
        function onAddRequested(): void {
            if (root.firstRun) // the tray's `add…`: held like a hand-off (review REG-6)
                root._pendingBlankAdd = true;
            else
                addSheet.openBlank();
        }

        /// A browser's or file manager's `magnet:` / `.torrent` hand-off (T1.4's second instance).
        function onAddUrisRequested(uris: list<string>): void {
            if (root.firstRun) // held until there is a server to add them to
                root._pendingAdds = root._pendingAdds.concat(uris);
            else
                addSheet.openList(uris, "handoff");
        }

        /// `,`, the tray's `settings…`, the disconnected banner.
        function onOpenSettings(): void {
            if (root.firstRun) // the tray's `settings…`: held like a hand-off (review REG-6)
                root._pendingSettings = true;
            else
                settingsModal.open();
        }

        /// Issue #2: the live server's certificate was refused. Someone in Settings (HTTPS just
        /// turned on, say) gets the server sheet with it at once; anyone else, the banner's
        /// `review certificate`.
        function onCertificateRefused(id: string): void {
            if (settingsModal.opened)
                settingsModal.reviewServer(id);
        }

        /// #124 (DECISIONS C20): an add the sheet no longer waits for (cancelled while it was
        /// out) and the server refused: the footer says so, as an action's refusal.
        function onAddFinished(ok: bool, message: string, hash: string): void {
            if (ok || addSheet.opened)
                return;
            root.flashFooterNote(message);
            const failure = App.addFailure;
            if (failure.sentence === message) {
                root.footerFailure = failure;
                root.footerNoteMark = "✕";
                footerNoteTimer.stop();
                root.footerNoteSticky = true;
            }
        }

        /// T4.3's seam: an action the server refused, on the footer for a few seconds.
        function onActionFailed(action: string, message: string): void {
            // docs/33 D23: a refusal comes worded already (ErrorWords), the subject named; it
            // stays until the next key, with "details" (docs/35 N8).
            if (action.length > 0) {
                root.flashFooterNote(qsTr("%1 failed: %2").arg(action).arg(message));
                return;
            }
            root.flashFooterNote(message);
            const failure = App.actionFailure;
            if (failure.sentence === message) {
                root.footerFailure = failure;
                root.footerNoteMark = "✕";
                footerNoteTimer.stop();
                root.footerNoteSticky = true;
            }
        }

        /// docs/32 N1: answered "OK", then failed, says the log.
        function onLoggedFailure(text: string, hashes: var): void {
            root.flashFailureNote(text, hashes);
        }

        /// #11: a rename-many-files run ended; the footer says how it went.
        function onTorrentFilesRenamed(hash: string, done: int, total: int, failedPath: string,
                                       error: string): void {
            if (failedPath.length === 0)
                root.flashFooterNote(qsTr("renamed %n files", "", done));
            else
                root.flashFooterNote(qsTr("renamed %1 of %2 · %3: %4").arg(done).arg(total)
                                     .arg(failedPath.slice(failedPath.lastIndexOf("/") + 1)).arg(error));
        }

        /// T4.7's "show" notification action, and the duplicate sheet's `show it`.
        function onShowTorrentRequested(hash: string): void {
            root.showTorrent(hash);
        }

        /// #24 T8: the weekly recap's "open the console": the seed console on its week.
        function onShowConsoleRequested(range: string): void {
            App.console.range = range;
            root.setView("seed");
        }

        /// docs/25 D3 a: Fig. 10's merge went through. The cursor goes to the torrent, its
        /// details open on Trackers, and the footer says how many were added.
        function onTrackersMerged(hash: string, count: int): void {
            root.showTorrent(hash);
            root.showDetailsTab(details.tabTrackers);
            const name = App.torrentRow(hash).name || "";
            root.flashFooterNote(count === 1 ? qsTr("1 tracker added to %1").arg(name)
                                             : qsTr("%1 trackers added to %2").arg(count).arg(name));
        }

        /// The jump palette's `tab` (Fig. 12's "tab actions"): jump there, then open Fig. 11
        /// on that row. The palette has already moved the cursor and closed itself.
        function onShowActionsRequested(hash: string): void {
            root.showTorrent(hash);
            if (root.cursorHash === hash)
                table.requestActions();
        }

        /// `C` / the menu's category row.
        function onCategoryPickerRequested(hashes: list<string>): void {
            categoryPalette.openFor(hashes);
        }

        /// `t` / the menu's tags row.
        function onTagPickerRequested(hashes: list<string>): void {
            tagPalette.openFor(hashes);
        }

        /// Another server's columns, order, sort, folds and pane size.
        function onServerChanged(): void {
            root.restoreTableState();
        }

        /// #34: a restore wrote the settings; the layout shows now, not at the next start.
        function onTableStatesRestored(): void {
            root.reloadTableState();
        }

        target: App
    }

    // The rail, the window's full height (board 46, DECISIONS C24 H2): the mark, the views,
    // the five questions and the groups, the servers at its foot. Every face has it; the log's
    // shows no filters (docs/32 L1), nor does a window with no list (docs/33 D1, D7).
    Rail {
        id: rail

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        categories: App.categories
        // #167 assumed 4: ctrl+b folds it to its glyphs.
        compact: App.railHidden
        counts: App.counts
        filtered: App.filtered
        // Board 42 F1 (M1, M2): the library's folders, after the categories.
        folders: App.move.folders
        // The mark's rule meets the title bar's.
        headHeight: titleBar.height
        keysShown: root.keysShown
        markAlert: App.hasServer && root.serverLost && !root.firstSync
        servers: App.servers
        // T4.5: the SERVERS group is off unless the General pane asks for it.
        serverCount: App.showServersGroup ? App.serverCount : 0
        // T4.8: the group draws the real names, and picking one switches server.
        serverList: App.serverList
        tags: App.tags
        trackers: App.trackers
        view: root.view
        // docs/14 A1: setup has no library chrome.
        visible: !root.firstRun
        serversOnly: root.libraryEmpty || root.firstSync || root.noList || root.logShown || root.searchShown
        searchOffered: App.searchOffered
        serversFolded: App.serversFolded
        serverRetrying: root.serverRetrying

        collapsedGroups: root.railFolded
        scrollHint: App.railScrollHint
        trackerHostStyle: App.trackerHostStyle

        onFocusTable: table.forceActiveFocus()
        onGroupToggled: group => {
            // docs/33 D17: the servers block folds app-wide, not per server.
            if (group === "servers") {
                App.serversFolded = !App.serversFolded;
                return;
            }
            root.railFolded = root.railFolded.indexOf(group) >= 0 ? root.railFolded.filter(
                                                                        g => g !== group) :
                                                                    root.railFolded.concat(
                                                                        [group]);
            root.saveTableState();
        }
        onMarkClicked: root.openAppMenu()
        // The rail hands the keyboard back to the table once it has switched, as it did.
        onServerActivated: id => {
            table.forceActiveFocus();
            App.switchServer(id);
        }
        onMoreServersRequested: root.openServerPalette()
        onFolderFoldToggled: root.saveTableState()
        onFolderMenuRequested: (path, x, y) => {
            const point = rail.mapToItem(root, x, y);
            folderMenuPanel.folder = path;
            folderMenuPanel.openAt(Math.round(point.x), Math.round(point.y));
        }
        onUnfoldRequested: App.railHidden = false
        onViewPicked: view => root.setView(view)
    }

    /// Where the column beside the rail starts.
    readonly property real contentLeft: rail.visible ? rail.width : 0

    /// The title bar's title (board 46 `titleBar()`, DECISIONS C24 H5): the face, a move's
    /// torrents on show, the place the rail picked (folder, category, tag, tracker), the
    /// question, or the library. A question picked with a place goes in the sentence.
    readonly property string scopeTitle: {
        if (root.logShown)
            return qsTr("Log");
        if (root.searchShown)
            return qsTr("Search");
        if (root.consoleShown)
            return qsTr("Seed console");
        if (root.moveShowingJob)
            return App.move.title.length > 0 ? qsTr("The move of %1").arg(App.move.title) : qsTr("The move");
        if (root.moveShowing)
            return qsTr("From the move sheet");
        const folder = App.filtered.folder;
        if (folder.length > 0)
            return folder.substring(folder.lastIndexOf("/") + 1) || folder;
        const category = App.filtered.category;
        if (category !== undefined)
            return category === "" ? qsTr("No category") : category;
        const tag = App.filtered.tag;
        if (tag !== undefined)
            return tag === "" ? qsTr("Untagged") : tag;
        const tracker = App.filtered.tracker;
        if (tracker !== undefined)
            return rail.trackerText(tracker);
        const question = root.questionName(App.filtered.question);
        return question.length > 0 ? question : qsTr("Library");
    }

    /// Its sentence (core/ScopeSentence): App's for the rail's scope; the faces', a move's
    /// show and a lost server's from what the window knows.
    readonly property string scopeSentence: {
        if (root.firstRun || root.libraryEmpty && !root.logShown && !root.searchShown)
            return "";
        if (root.searchShown)
            return searchView.sentence;
        if (root.serverLost)
            return App.lostSentence(App.torrents.count > 0 ? App.globalState.lastSyncText : "",
                                    root.serverProblem.headline ?? "");
        if (root.logShown)
            return App.logSentence(App.serverName, App.serverLog.total, App.serverLog.firstAtText);
        if (root.consoleShown)
            return App.seedSentence(App.globalState.upInfoSpeed > 0 ? App.globalState.upSpeedText : "",
                                    App.console.todayText,
                                    App.console.selectionScope ? qsTr("the selection") : root.consoleScopeName);
        if (root.moveShowingJob)
            return App.moveJobSentence(App.move.jobHashes.length,
                                       App.move.revision >= 0 ? App.move.band.withServer ?? 0 : 0);
        if (root.moveShowing)
            return App.moveSheetSentence(root.moveShowHashes.length);
        return App.scopeSentence;
    }

    // The title bar (board 46 `titleBar()`, DECISIONS C24 H5; supersedes v2's hero, H9): where
    // the window is and its sentence, then a move's way back, "move this folder…", the filter
    // and "+ add"; across the content's top beside the rail's mark.
    TitleBar {
        id: titleBar

        // docs/14 A1: setup has no library chrome; FirstRunPage draws its own footer.
        visible: !root.firstRun

        anchors.left: parent.left
        anchors.leftMargin: root.contentLeft
        anchors.right: parent.right
        anchors.top: parent.top
        title: root.scopeTitle
        sentence: root.scopeSentence
        // The log filters its own lines (docs/32 D11); search's `/` filters its results
        // (C26 SR14), while there are some.
        filterShown: !root.logShown && (!root.searchShown || App.searchResults.torrentCount > 0)
        keysShown: root.keysShown
        searchCountText: root.searchShown ? (App.searchResults.filterText.length > 0 ? qsTr("%1 of %2").arg(
                                                                                         App.searchResults.count).arg(App.searchResults.torrentCount)
                                                                                   : String(App.searchResults.count))
                                          : App.filtered.text.length > 0 ? qsTr("%1 of %2").arg(App.filtered.count).arg(
                                                                               App.counts.total) : String(App.filtered.count)
        // H5: "move this folder…" while a folder is picked (shift+m, the row's menu and `:`
        // keep it).
        moveFolderShown: root.view === "library" && !root.moveShowing && App.filtered.folder.length > 0
                         && !root.serverLost
        // C15 M10 (J1), C18: the scope line's link, now a tool: esc back to the sheet, or all
        // torrents.
        backText: !root.moveShowing || root.view !== "library" ? "" : root.moveShowingJob ? qsTr("all torrents")
                                                                                          : qsTr("back to the sheet")
        backKey: root.moveShowing ? "esc" : ""
        // C24 H3–H4: the scope's display, in the library only, and not over a move's show (the
        // table's step column, #168 assumed 5) or an empty library.
        display: root.view === "library" && !root.moveShowing && !root.libraryEmpty ? App.display : ""

        // C24 H17: "+ add ▾" with create and search under it (#146).
        addMenuShown: App.createOffered || App.searchOffered
        onAddClicked: root.openAddMenu()
        onDisplayClicked: root.openDisplayMenu()
        onBackClicked: root.leaveMoveShow()
        onMoveFolderClicked: root.openMoveFolder(App.filtered.folder, "")
        onSearchChanged: text => {
            if (root.searchShown)
                App.searchResults.filterText = text;
            else
                App.filtered.text = text;
        }
        // Review RUN-12: enter keeps the filter and gives the table back its keys, as `/`
        // then enter does in vim or less.
        onSearchAccepted: root.focusHome()
        onSearchCleared: root.clearFilter()
    }

    // The lost server's notice (spec §7 Fig. 23; docs/33 D8, D9 in docs/35's look): a band
    // across the content under the title bar, the rail beside it usable (DECISIONS C24 H14), and
    // the body starts under its line (docs/35 N2). It grows in once the server is declared
    // lost (N6).
    DisconnectedBanner {
        id: banner

        anchors.left: titleBar.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        animated: !App.reducedMotion
        listTime: App.torrents.count > 0 ? App.globalState.lastSyncText : ""
        multiServer: root.multiServer
        objectName: "serverNotice"
        problem: root.serverProblem
        shown: root.serverLost && !root.firstRun
        z: 1

        onCertificateRequested: settingsModal.reviewServer(App.serverId)
        onCopyRequested: text => App.copyToClipboard(text)
        onLoginRequested: settingsModal.editServerLogin(App.serverId)
        onRetryRequested: App.retryNow()
        onSwitchRequested: root.openServerPalette()
    }

    // Board 42 §4–§6 (DECISIONS C15 M10): a folder's move, under the lost server's notice,
    // through every view, until its summary is closed; across the content (C24 H14).
    MoveBand {
        id: moveBand

        anchors.left: titleBar.left
        anchors.right: parent.right
        anchors.top: banner.bottom
        animated: !App.reducedMotion
        band: App.move.band
        // Read again whenever the job moves (`revision`).
        record: App.move.revision >= 0 && App.move.shown ? App.move.detailsText() : ""
        queued: App.move.queued
        shown: App.move.shown && !root.firstRun
        z: 1

        onActionTriggered: key => root.moveBandAction(key)
        onDropRequested: index => App.move.dropQueued(index)
        onCopyRequested: text => root.copyText(text, qsTr("copied"))
    }

    // #146 (C24 H14, C27 CR13–CR14): a creation in the move's band, under it when both show.
    MoveBand {
        id: createBand

        anchors.left: titleBar.left
        anchors.right: parent.right
        anchors.top: moveBand.bottom
        animated: !App.reducedMotion
        band: App.createBand
        copyable: false
        extra: (App.createBand.file ?? null) !== null ? createFileChip : null
        objectName: "createBand"
        shown: App.createShown && !root.firstRun
        z: 1

        onActionTriggered: key => App.createAction(key)
    }

    Component {
        id: createFileChip

        CreateFileChip {
            name: App.createBand.file?.name ?? ""
            path: App.createBand.file?.path ?? ""
            sizeText: App.createBand.file?.sizeText ?? ""
        }
    }

    // body: table | details, beside the rail
    Item {
        id: body

        // The first-run page (T4.6) stands in for the whole body, not over it.
        visible: !root.firstRun
        anchors.bottom: statusLine.top
        anchors.left: titleBar.left
        anchors.right: parent.right
        anchors.top: createBand.bottom

        SplitLayout {
            id: split

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            main: tableHost
            // docs/10 §5: the breakpoint is the window's width, not what is left beside the
            // rail, so hiding the rail no longer moves Details.
            referenceWidth: root.width
            referenceHeight: root.height
            side: details
            // Two columns under the table (Details.wide) fit the General tab in less height;
            // docs/13's band over the tabs costs about 40, so 220 became 260.
            defaultSideHeight: Theme.space(260)
            // docs/11 J1: wide enough for the Files tab's four columns and a name that can be
            // told apart (it was 340, which cut every list in the pane).
            defaultSideWidth: Theme.space(400)
            // docs/10 §5: Settings -> Appearance, or `I`; the sizes are the table state's.
            placement: App.detailsPlacement
            // docs/17 M10.1: folded at the bottom, the pane leaves this one line.
            foldedBar: detailsStrip
            // #77: what a screen reader calls the folded column on the right.
            foldedName: qsTr("show details")
            visible: !root.libraryEmpty && !root.firstSync && !root.noList && !root.consoleShown
                     && !root.logShown && !root.searchShown

            onFoldToggled: root.saveTableState()
            onFoldedChanged: root._detailsFoldChanged()
            onResized: root.saveTableState()
        }

        // #24: the seed console, in the table's and details' place.
        Console {
            id: seedConsole

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            focus: root.consoleShown
            scopeName: root.consoleScopeName
            seed: App.console
            visible: root.consoleShown

            Hairline {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.top: parent.top
                vertical: true
                visible: rail.visible
            }

            onAttentionRequested: host => {
                root.setView("library");
                if (host.length > 0)
                    App.filtered.tracker = host;
            }
            onClearScope: root.consoleEscape()
            onScopeTracker: host => App.filtered.tracker = host
            onShowTorrent: hash => root.showInLibrary(hash)
        }

        // docs/32 D9 (#56): qBittorrent's log, the third face, over the body's whole width.
        LogView {
            id: logView

            anchors.fill: parent
            focus: root.logShown
            lines: App.logLines
            log: App.serverLog
            // D15: named, never read. Only while qBittorrent writes one.
            logFilePath: App.serverPrefs.values.file_log_enabled === false ? ""
                                                                           : (App.serverPrefs.values.file_log_path ?? "")
            serverLost: root.serverLost
            visible: root.logShown

            onCopyRequested: (text, note) => root.copyText(text, note)
            onLeaveRequested: root.toggleLog()
            // Board 45 B2 (C18 D7): a move's line moves it back, through the Move sheet.
            onMoveBackRequested: id => moveSheet.openBack(id, "")
            onNoteRequested: text => root.flashFooterNote(text)
            onShowTorrent: hash => root.showLogTorrent(hash)
        }

        // #145 (C24 H16, DECISIONS C26): search, the fourth face, over the body's whole width.
        SearchView {
            id: searchView

            anchors.fill: parent
            objectName: "searchView"
            focus: root.searchShown
            multiServer: root.multiServer
            serverLost: root.serverLost
            serverName: App.serverName
            visible: root.searchShown
            windowWidth: root.width

            onAddRequested: key => addSheet.openFromSearch(App.searchAddable(key))
            onCopyRequested: (text, note) => root.copyText(text, note)
            onLeaveRequested: root.setView(root._viewBeforeSearch === "search" ? "library" : root._viewBeforeSearch)
            onLogRequested: root.setView("log")
            onNoteRequested: text => root.flashFooterNote(text)
            onPluginsPaneRequested: settingsModal.openSearchPlugins()
        }

        DetailsStrip {
            id: detailsStrip

            // The row `details` already holds, refreshed on every sync: no polls of its own.
            torrent: details.torrent

            onUnfoldRequested: split.toggleFold()
        }

        // `main` and `side` are reparented into the split; they are declared here so the
        // table can keep the figure's 16 px gutter without the split knowing about it.
        Item {
            id: tableHost

            TorrentTable {
                id: table

                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space(12)
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                // Board 42 J1: the job's step for each of its torrents.
                showMoveColumn: root.moveShowingJob
                moveRevision: App.move.revision
                moveStepFor: hash => App.move.stepFor(hash)
                // DECISIONS C18 D1: a quick action's mark in its rows' status cells.
                rowMarkRevision: App.outcomes.revision
                rowMarkFor: hash => App.outcomes.rowMark(hash)
                escapeTaken: outcomeStrip.visible
                // docs/17 M9.6: the last row can always scroll clear of the outcome strip over
                // it (C18 D1); the action bar is docked under the rows (C24 H8).
                // Its tallest (C30 F10's reason line counted), so the reason coming and going
                // never moves the rows it is read from.
                bottomRoom: outcomeStrip.visible ? outcomeStrip.roomHeight + Theme.outcomeStripLift : 0
                filtered: App.filtered
                // C24 H3: the display the scope shows draws the rows; the table keeps the rest.
                face: App.display === "rows" ? libraryRows : App.display === "overview" ? libraryShelves
                                                                                      : null
                // #51 D9: rows hold still under the open actions menu, for D17's right-click.
                holdOrder: actionsMenuPanel.opened
                // Fig. 23: the last list stays, dimmed and read-only.
                opacity: root.serverLost ? 0.45 : 1.0
                visible: !root.filterEmpty

                // T4.3: `enter` and right-click already moved the cursor (and, for a
                // right-click outside the selection, the selection) onto `hash` before this
                // fires, so the menu's own `activated` can go straight through
                // `torrentAction()` without carrying `hash` along.
                onActionsRequested: (hash, x, y) => {
                    const point = table.mapToItem(root, x, y);
                    actionsMenuPanel.cursorStopped = App.torrentRow(hash).stopped === true;
                    actionsMenuPanel.cursorRow = App.torrentRow(hash);
                    actionsMenuPanel.leadActions = [];
                    actionsMenuPanel.heading = root.actionsHeading(hash);
                    actionsMenuPanel.headingTitle = root.actionsHeadingTitle(hash);
                    actionsMenuPanel.openAt(point.x, point.y);
                }
                onActivated: root.openDetails()
                onColumnsRequested: columnsPalette.open()
                onEscapePressed: root.handleEscape()
                // C18 D6: the action bar's mouse hint (C24 H8), the first 3 times it opens
                // after a plain click; it goes with the bar.
                onPlainClicked: hadSelection => {
                    if (hadSelection || App.selectionHintsShown >= 3)
                        return;
                    root.selectionHint = qsTr("ctrl+click add · shift+click range");
                    App.selectionHintsShown = App.selectionHintsShown + 1;
                }
                onSelectedCountChanged: {
                    if (table.selectedCount === 0)
                        root.selectionHint = "";
                }
                onFocusDetails: {
                    if (!details.collapsed)
                        details.forceActiveFocus();
                }
                onFocusRail: rail.forceActiveFocus() // folded or not (#167 assumed 4)
                // The inline editor is the primary rename; the sheet (`renameSheetPanel`) is
                // only the menu/multi-selection fallback, and shelves' `r` (#168 assumed 9).
                onRenameRequested: (hash, name) => App.rename(hash, name)
                onRenameSheetRequested: hash => renameSheetPanel.openFor(hash)
            }

            // DECISIONS C24 H3–H4 (#168): the rows display, a face of the table above (its
            // `face`) in the same place: the table keeps the cursor, the selection and the keys.
            LibraryRows {
                id: libraryRows

                anchors.fill: table
                bottomRoom: table.bottomRoom
                opacity: table.opacity
                table: table
                visible: table.visible && App.display === "rows"

                onFixRequested: (hash, action) => root.runRowFix(hash, action)
            }

            // The overview's shelves (#168 assumed 6), the table's other face.
            LibraryShelves {
                id: libraryShelves

                anchors.fill: table
                bottomRoom: table.bottomRoom
                opacity: table.opacity
                table: table
                visible: table.visible && App.display === "overview"
            }

            FilteredEmpty {
                anchors.centerIn: parent
                objectName: "filteredEmpty"
                // #51 D13: every rail scope but the status, which has its own words.
                scopeName: {
                    const parts = [];
                    const category = App.filtered.category;
                    if (category !== undefined && category !== null)
                        parts.push(category === "" ? qsTr("no category") : String(category));
                    const tag = App.filtered.tag;
                    if (tag !== undefined && tag !== null)
                        parts.push(tag === "" ? qsTr("untagged") : String(tag));
                    const tracker = App.filtered.tracker;
                    if (tracker !== undefined && tracker !== null)
                        parts.push(rail.trackerText(String(tracker)));
                    return parts.join(" · ");
                }
                statusName: root.questionName(App.filtered.question)
                text: App.filtered.text
                visible: root.filterEmpty

                onAllStatusesRequested: rail.pickLibrary()
                onClearRequested: root.handleEscape()
            }

            // DECISIONS C18 D1–D3 (board 45 §2): what a quick action on torrents came to. C24
            // H8 (supersedes D1's place): it floats bottom middle of the rows, so with a
            // selection on it sits just above the action bar, and both read together.
            OutcomeStrip {
                id: outcomeStrip

                anchors.bottom: table.bottom
                anchors.bottomMargin: Theme.outcomeStripLift
                anchors.horizontalCenter: table.horizontalCenter
                availableWidth: table.width - Theme.space(16)
                height: implicitHeight
                outcome: App.outcomes.current
                reducedMotion: App.reducedMotion
                // C30 F8, F10: "show" only when it brings something back, and why.
                showReason: root.outcomeShowReason
                // Over the rows, or over the empty scope they left (C30 F9: "show" takes the
                // rail where they went).
                visible: outcomeStrip.shown && (table.visible || root.filterEmpty) && !root.serverLost
                width: implicitWidth

                onCloseRequested: App.outcomes.dismiss()
                onDetailsRequested: root.openOutcomeDetails()
                onMoreRequested: id => App.createAction(id)
                onExpired: serial => App.outcomes.expire(serial)
                onMoveRequested: (hashes, target) => moveSheet.openSelection(hashes, target)
                onShowRequested: hashes => root.showOutcome(hashes)
            }

            // #24 T7: seeding moments in the bar's spot, while nothing is selected.
            // a11y-ignore A1: it names itself by the moment it shows, pushed at run time (#77)
            SeedToast {
                id: seedToast

                allowed: table.selectedCount === 0 && table.visible && !root.serverLost && root.view === "library"
                         && !outcomeStrip.visible
                anchors.bottom: table.bottom
                anchors.bottomMargin: Theme.space(12)
                anchors.horizontalCenter: table.horizontalCenter
                height: implicitHeight
                width: implicitWidth

                onShowRequested: hash => table.revealHash(hash)
            }

            Connections {
                function onSeedNotice(kind: string, hash: string, title: string, detail: string): void {
                    seedToast.push(kind, hash, title, detail);
                }

                target: App
            }

            // Fig. 23's read-only guard: it eats every click and every key but the five the
            // figure's footer still promises.
            DimmedOverlay {
                id: dimmed

                visible: root.serverLost

                onCommandsRequested: root.openCommandPalette()
                onHelpRequested: root.helpOpen = true
                onHideRequested: root.hideRequested()
                onQuitRequested: root.requestQuit()
                onRetryRequested: App.retryNow()
                onSettingsRequested: App.openSettings()
                onServersRequested: root.openServerPalette()
                // docs/33 D23: a key it stops says so.
                onBlocked: key => root.flashFooterNote(App.guardNote(key))
                // Not under an open overlay or the first-run page: `focusHome` knows.
                onVisibleChanged: root.focusHome()
            }
        }

        Details {
            id: details

            // docs/17 M10: `i`, the divider and the strip fold it, all through the split.
            collapsed: split.folded
            details: App.details
            // Keys are taught on demand in the window (DECISIONS C24 H10: menus, tooltips,
            // the palette, `?`), so the pane draws no hint row inside it.
            showHints: false
            // SplitLayout draws the divider between the table and the pane.
            showEdge: false
            // docs/10 §5 and docs/13: under the table, the head band over full-width tabs.
            wide: !split.docked

            asksBeforeAutoManagement: true
            onAutoManagementRequested: (hashes, on) => root.setAutoManagement(hashes, on)
            onFocusTable: table.forceActiveFocus()
            onCopyRequested: text => root.copyText(text, qsTr("copied"))
            onOpenFileRequested: path => App.openTorrentFile(App.details.hash, path)
            onNoteRequested: text => root.flashFooterNote(text)
            // docs/12 G5: "copied hash v1".
            onValueCopyRequested: (text, name) => root.copyText(text, qsTr("copied %1").arg(name))
            // docs/28: the error block acts on the torrent it explains, never on a selection.
            onProblemActionRequested: action => root.runProblemAction(root.cursorHash, action)
            // Board 44 F1 (C17 D3): the Files tab's band, or shift+f there.
            onFreeSpaceRequested: root.freeSpaceFor(App.details.hash)
            // C17 D4: Settings › Servers, on this server, where its path maps are.
            onPathMapsRequested: settingsModal.showServer(App.serverId)
            // docs/32 F1–F3: the failure block, on the torrent it is about. `m` tries the
            // move again with the destination already in the sheet.
            onFailureActionRequested: action => root.runFailureAction(root.cursorHash, action)
            // Board 45 B1 (C18 D7): the last move's block.
            onMoveBackRequested: (id, only) => moveSheet.openBack(id, only)
            onMoveShowRequested: hashes => root.showMoveHashes(hashes, false)

            onOpenFolderRequested: path => App.revealTorrentFile(App.details.hash, path)
            onPeerAction: (action, addresses) => root.detailsRowAction("peer", action, addresses)
            onTrackerAction: (action, urls) => root.detailsRowAction("tracker", action, urls)
            onWebSeedAction: (action, urls) => root.detailsRowAction("webseed", action, urls)
            onRenameFilesRequested: (paths, fromSelection) => renameFilesSheet.openFor(App.details.hash, paths, fromSelection)
        }

        // §7's whole-body states
        EmptyState {
            id: emptyState

            readonly property var other: App.busiestOtherServer

            anchors.centerIn: parent
            multiServer: root.multiServer
            objectName: "emptyState"
            otherCount: emptyState.other.count !== undefined ? emptyState.other.count : -1
            otherName: emptyState.other.name || ""
            serverName: App.serverName
            // docs/33 D7: the same place for a server lost (or refused) before it gave a list;
            // C28: another engine's server says so in its problem's words.
            problem: root.serverProblem
            variant: !root.noList ? "empty" : root.serverProblem.kind === "engine" ? "engine" : App.serverRefused ? "refused" : "noList"
            visible: (root.libraryEmpty || root.noList) && !root.firstRun && !root.logShown && !root.searchShown // T4.6: first run replaces this state
            width: Math.min(parent.width, Theme.space(520))

            onAddRequested: App.addRequested()
            onHelpRequested: root.helpOpen = true
            onSettingsRequested: App.openSettings()
            onSwitchRequested: root.openServerPalette()
        }

        LoadingState {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.top: parent.top
            anchors.topMargin: Theme.space(12)
            receivedText: App.sizeText(App.globalState.dlInfoData)
            torrentCount: App.torrents.count
            visible: root.firstSync && !root.logShown && !root.searchShown
        }
    }

    // docs/35 N8: an action's failure, its details docked above the status line over the table's
    // last rows, opaque; nothing moves. esc, a click outside or "details" again close it.
    // a11y-ignore A2: a click outside, not a control; esc and "details" close the band too (#77)
    MouseArea {
        anchors.fill: parent
        enabled: root.footerNoticeOpen
        visible: root.footerNoticeOpen
        z: 7

        onPressed: mouse => {
            root.footerNoticeOpen = false;
            mouse.accepted = false;
        }
    }

    Notice {
        id: footerNotice

        anchors.bottom: statusLine.top
        // DECISIONS C18 D2 (changes docs/35 N8): a table action's details open above the
        // strip, the same band one line higher.
        anchors.bottomMargin: root.footerFailure.fromStrip === true && outcomeStrip.visible
                              ? root._outcomeBandLift : 0
        anchors.left: titleBar.left
        anchors.right: parent.right
        animated: !App.reducedMotion
        body: root.footerFailure.line || ""
        details: root.footerFailure.details || ""
        detailsOpen: true
        edge: "top"
        kind: "error"
        objectName: "footerNotice"
        opaque: true
        shown: root.footerNoticeOpen && root.footerFailure.headline !== undefined
        title: root.footerFailure.headline || ""
        z: 8

        onCopyRequested: text => App.copyToClipboard(text)
    }

    // The status line (board 46 `statusLine()`, DECISIONS C24 H15; supersedes v2's footer and
    // its hints, H10): the server lane, ↓ ↑, alt speeds and the network, the facts, the note
    // slot and "? keys", across the content under the body.
    StatusLine {
        id: statusLine

        anchors.bottom: parent.bottom
        anchors.left: titleBar.left
        anchors.right: parent.right
        // While torrents are selected the action bar stands in its place (C24 H8).
        visible: !root.firstRun && !selectionBar.visible

        serverText: root.serverLaneText()
        // docs/10 §2: from the first narrow step the lane drops the version (its tooltip keeps
        // it); the states that matter more than the version keep their words.
        serverShortText: !App.hasServer || root.serverLost || root.firstSync ? "" : App.serverName
        // docs/33 D3, H15: with two servers the lane opens the server palette.
        serverSwitchable: root.multiServer
        keysShown: root.keysShown
        serverConfigured: App.hasServer
        connected: !root.serverLost
        syncing: root.firstSync
        retrying: root.serverRetrying
        connectionStatus: App.globalState.connectionStatus
        // docs/16 §5: the port behind "not reachable", and anonymous mode with private
        // torrents in the library.
        listenPort: App.serverPrefs.values.listen_port ?? 0
        anonymousWarning: App.serverPrefs.values.anonymous_mode === true && App.counts.privateCount > 0

        downSpeed: root.live ? App.globalState.dlSpeedText : "—"
        downSpeedShort: root.live ? App.speedShortText(App.globalState.dlInfoSpeed) : "—"
        upSpeed: root.live ? App.globalState.upSpeedText : "—"
        upSpeedShort: root.live ? App.speedShortText(App.globalState.upInfoSpeed) : "—"
        upNote: root.live ? App.console.todayText : ""
        // #24: ↑ opens the seed console, as the rail's "seed" does.
        upOpensSeed: true

        // docs/31: burst, turtle or today's alt-speed, until when, and how it came about.
        altSpeedActive: App.globalState.useAltSpeedLimits
        altDownLimit: App.globalState.dlRateLimitText
        altUpLimit: App.globalState.upRateLimitText
        altDownLimitShort: App.limitShortText(App.globalState.dlRateLimit)
        altUpLimitShort: App.limitShortText(App.globalState.upRateLimit)
        altKind: App.altSpeed.kind
        altTooltip: App.altSpeed.chipTooltip
        altUntil: App.altSpeed.untilText

        facts: root.factParts
        factsSpoken: root.factsSpoken

        noteText: root.footerNote
        noteMark: root.footerNoteMark
        noteWarn: root.footerNoteWarn
        noteSticky: root.footerNoteSticky
        noteHasDetails: root.footerFailure.headline !== undefined
        noteDetailsOpen: root.footerNoticeOpen
        noteHintShown: root.footerNoteHint && details.collapsed
        logFollowShown: root.logShown && !root.overlayOpen
        logFollowing: logView.following

        onServerClicked: root.openServerPalette()
        onUpClicked: root.setView("seed")
        onAltSpeedClicked: root.toggleAltSpeed()
        onKeysClicked: root.helpOpen = true
        // docs/35 N8: the sentence's "details" open the band above the line.
        onDetailsToggled: root.footerNoticeOpen = !root.footerNoticeOpen
        onNoteHintClicked: root.toggleDetails()
    }

    // The action bar (board 46 `statusLine()` while selecting, DECISIONS C24 H8; supersedes
    // docs/17's floating bar): while one or more torrents are selected the status line becomes
    // it, docked in the line's place. Never while the server is lost (Fig. 23 is read-only) or
    // with no rows on show: the status line stays then.
    SelectionBar {
        id: selectionBar

        anchors.fill: statusLine
        // The line's width, not the bar's own: while the bar is made, its width follows its
        // implicit width for a moment, which follows its folds (a binding loop).
        availableWidth: statusLine.width - Theme.actionBarInsetStart - Theme.actionBarInsetEnd
        count: table.selectedCount
        // #170: `p`'s word, as the selection needs it.
        startable: root.selectionRunStates.stopped
        stoppable: root.selectionRunStates.running
        hint: root.selectionHint
        keysShown: root.keysShown
        // The line's note, while the bar hides the line.
        note: root.footerNote
        noteWarn: root.footerNoteWarn
        // #24 T1: re-read when the selection or the library moves.
        summary: {
            App.torrents.count; // the dependency: a sync that adds or drops a row
            return table.selectedCount > 0 ? App.console.selectionSummary(table.selectedHashes()) : ({});
        }
        visible: table.selectedCount > 0 && table.visible && !root.serverLost && !root.firstRun

        onClearRequested: table.clearSelection()
        onMoreRequested: (button, folded) => root.openSelectionMore(button, folded)
        onSummaryClicked: root.setView("seed")
        onTriggered: action => root.selectionBarAction(action)
    }

    // the `?` sheet (spec §7 Fig. 26: 760 wide, no buttons — a sheet may have none)
    Sheet {
        id: helpSheetPanel

        anchors.fill: parent
        glyph: Glyphs.help
        sheetWidth: Theme.space(760)
        // docs/30 D10: the sheet names the palette that runs them, on a line of its own: the
        // head's one line at 760 cut it off (#66). C24 H10: and alt, which badges the frame.
        subtitle: qsTr("single letters act on the table \u00b7 capitals are the stronger variant \u00b7 ctrl is app-level\nhold alt to see the keys on screen \u00b7 : runs any of them by name")
        subtitleWraps: true
        title: qsTr("Shortcuts")
        z: 10

        onRejected: root.helpOpen = false

        HelpSheet {
            id: helpSheet

            Layout.fillWidth: true
            altKind: App.altSpeed.kind

            onCloseRequested: root.helpOpen = false
            onRunRequested: action => {
                root.helpOpen = false;
                if (!root.runAction(action))
                    root.flashFooterNote(qsTr("a key to press where it applies, not a command"));
            }
        }
    }

    // --- T4.3: actions ---
    // The overlays `torrentAction()` opens, a short-lived footer note for "copied" and for
    // `App.actionFailed`, and the notice `App.showTorrentRequested` (T4.7's seam) lands on.

    /// A few words shown next to the footer's facts for `_footerNoteSeconds`, then cleared.
    property string footerNote: ""
    /// docs/32 N1: the note is a failure qBittorrent's log told of (yellow), and one about a
    /// torrent still in the list, whose block `i` shows.
    property bool footerNoteWarn: false
    property bool footerNoteHint: false
    readonly property int _footerNoteSeconds: 5

    /// docs/31 F1: the note after A stays until the next key, not for a few seconds.
    property bool footerNoteSticky: false
    /// The set's mark before the note after A ("▲", "▼"), "" for none.
    property string footerNoteMark: ""

    /// docs/35 N8: the failure the footer's note is, for its "details"; {} when it is none.
    property var footerFailure: ({})
    /// The band with the failure's details, docked above the footer.
    property bool footerNoticeOpen: false

    /// `y`/`Y`'s "copied", and (through `Connections` below) `App.actionFailed`.
    function flashFooterNote(text: string): void {
        root.footerFailure = {};
        root.footerNoticeOpen = false;
        root.footerNoteWarn = false;
        root.footerNoteHint = false;
        root.footerNoteSticky = false;
        root.footerNoteMark = "";
        root.footerNote = text;
        footerNoteTimer.restart();
    }

    // --- DECISIONS C18 D1–D3 (board 45 §2): the outcome strip ---
    // A quick action on torrents says how it went in the strip in the selection bar's place,
    // and on its rows (App.outcomes), no longer in the footer's note (C16 L7, L9; C2 D1's
    // footer sentence; C12 D22 and docs/35 N8 for every table action).

    /// C18 D6: the selection bar's mouse hint while it shows; "" for none.
    property string selectionHint: ""

    /// How far the band of the strip's "details" sits above the footer: just over the strip.
    property real _outcomeBandLift: 0

    /// The strip's "details": the band with the failure's words, above the strip.
    function openOutcomeDetails(): void {
        const outcome = App.outcomes.current;
        if (root.footerNoticeOpen && root.footerFailure.fromStrip === true) {
            root.footerNoticeOpen = false;
            return;
        }
        root.footerFailure = {
            "headline": outcome.headline || "",
            "line": outcome.line || "",
            "details": outcome.details || "",
            "fromStrip": true
        };
        const top = outcomeStrip.mapToItem(statusLine.parent, 0, 0).y;
        root._outcomeBandLift = Math.max(0, statusLine.y - top + Theme.space(6));
        root.footerNoticeOpen = true;
    }

    /// C17 D3, C30 F12–F16: "Free the space…" (the band), shift+f and the palette. While the
    /// Files tab's reading says only adding it again frees them, straight to that confirm
    /// (F13), which plans again before the add; otherwise, and before the reading is this
    /// torrent's, the sheet, whose plan reads the files and pieces and takes F14's form when
    /// only adding it again frees anything.
    function freeSpaceFor(hash: string, given: var): void {
        // `given`: the tests' reading in place of App's.
        const reading = given ?? App.skippedOnDisk;
        if (reading.hash === hash && reading.outlook === "readd")
            addAgainConfirm.openWhy(hash, reading.name || "", reading.readdFiles || 0,
                                    reading.readdText || "", reading.version || "hybrid");
        else
            freeSpaceSheet.openFor(hash);
    }

    // --- the error and failure blocks' actions, from details or a row's fix (#168) ---

    /// docs/28's error block: `action` on `hash` (the cursor's, or a rows display row's fix).
    function runProblemAction(hash: string, action: string): void {
        if (hash.length === 0)
            return;
        const hashes = [hash];
        if (action === "log")
            root.showTorrentLog(hash);
        // docs/44 D13: a stuck move's sheet is not aimed at the destination it never
        // reached: qBittorrent ignores a move to the save path it already has
        // (scripts/dev/research/121-stuck-move), so the sheet starts as for any move.
        else if (action === "location")
            moveSheet.openSelection(hashes, "");
        else if (action === "bothFolders")
            App.showBothFolders(hash);
        else if (action === "start") // DECISIONS C20: "retry", a start that forgets the stop
            App.retryTorrents(hashes);
        else if (action === "recheck")
            App.recheck(hashes);
        else if (action === "addAgain") // board 44 §2: libtorrent's hybrid error
            addAgainConfirm.openFor(hash, App.torrentRow(hash).name || "", 0);
    }

    /// docs/32's failure block: `action` on `hash`. `m` tries the move again with the
    /// destination already in the sheet.
    function runFailureAction(hash: string, action: string): void {
        if (hash.length === 0)
            return;
        if (action === "log")
            root.showTorrentLog(hash);
        else if (action === "location")
            moveSheet.openSelection([hash], (App.torrentRow(hash).failure || {}).destination || "");
        else if (action === "folder")
            App.openFolder(hash);
        else if (action === "dismiss")
            App.dismissFailure(hash);
    }

    /// C24 H3 (law 3): a needs-you row's fix, RowWords' "problem:<action>" or
    /// "failure:<action>".
    function runRowFix(hash: string, fix: string): void {
        const at = fix.indexOf(":");
        if (at < 0)
            return;
        if (fix.substring(0, at) === "failure")
            root.runFailureAction(hash, fix.substring(at + 1));
        else
            root.runProblemAction(hash, fix.substring(at + 1));
    }

    // --- DECISIONS C30 F8–F11 (board 50 §2): "show" only when it brings something back ---

    /// The strip's torrents "show" would bring back: its done ones still in the library.
    function _outcomeShowHashes(outcome: var): var {
        if ((outcome.kind ?? "") === "" || outcome.kind === "failed" || (outcome.doneRow ?? "").length === 0)
            return [];
        return (outcome.doneHashes ?? []).filter(hash => App.torrents.indexOfHash(hash) >= 0);
    }

    /// "it is", "they are", or "2 of them are", for `count` of `total`.
    function _showSubject(count: int, total: int): string {
        if (count === total)
            return total === 1 ? qsTr("it is") : qsTr("they are");
        return count === 1 ? qsTr("1 of them is") : qsTr("%1 of them are").arg(count);
    }

    /// Where a question's torrents are now, in a whole clause: "they are resting now".
    function _questionNow(question: int, one: bool): string {
        switch (question) {
        case TorrentFilterModel.NeedsYou:
            return one ? qsTr("it needs you now") : qsTr("they need you now");
        case TorrentFilterModel.Arriving:
            return one ? qsTr("it is arriving now") : qsTr("they are arriving now");
        case TorrentFilterModel.LandedToday:
            return one ? qsTr("it landed today") : qsTr("they landed today");
        case TorrentFilterModel.GivingBack:
            return one ? qsTr("it is giving back now") : qsTr("they are giving back now");
        case TorrentFilterModel.Resting:
            return one ? qsTr("it is resting now") : qsTr("they are resting now");
        }
        return "";
    }

    /// The scope `gone` left, in the strip's words: the filter's words when they alone hide
    /// them, else the rail's place, else the question.
    function _showScopeWords(gone: var): string {
        if (App.filtered.text.length > 0 && gone.every(hash => App.filtered.hashInScope(hash)))
            return qsTr("the filter");
        if (root.moveShowing)
            return qsTr("the move's list");
        const folder = App.filtered.folder;
        if (folder.length > 0)
            return folder.substring(folder.lastIndexOf("/") + 1) || folder;
        const category = App.filtered.category;
        if (category !== undefined)
            return category === "" ? qsTr("no category") : String(category);
        const tag = App.filtered.tag;
        if (tag !== undefined)
            return tag === "" ? qsTr("untagged") : String(tag);
        const tracker = App.filtered.tracker;
        if (tracker !== undefined)
            return rail.trackerText(String(tracker));
        const question = rail.questionRow(App.filtered.question);
        if (question !== null)
            return question.text;
        return qsTr("the filter");
    }

    /// The one value `valueOf` gives every hash, or undefined when they differ.
    function _sharedValue(hashes: var, valueOf: var): var {
        const first = valueOf(hashes[0]);
        return hashes.every(hash => valueOf(hash) === first) ? first : undefined;
    }

    /// F8, F10: why "show" is there, as the dim line under the strip's words; "" when it
    /// would bring nothing back (each torrent in the scope, wholly on screen, and selected, or
    /// for one acted on alone the cursor's row).
    function outcomeShowWhy(outcome: var): string {
        const hashes = root._outcomeShowHashes(outcome);
        if (hashes.length === 0)
            return "";
        const one = hashes.length === 1;
        // Out of the scope: the folder, question, category, tag or the filter's words.
        const gone = hashes.filter(hash => App.filtered.rowOfHash(hash) < 0);
        if (gone.length > 0) {
            const scope = root._showScopeWords(gone);
            if (gone.length === hashes.length && scope !== qsTr("the filter")) {
                const folder = App.filtered.folder;
                const place = folder.length > 0 ? root._sharedValue(hashes, hash => App.torrentRow(hash).savePath ?? "")
                                                : undefined;
                if (place !== undefined && place.length > 0)
                    return qsTr("%1 in %2 now, out of %3").arg(root._showSubject(gone.length, hashes.length)).arg(
                                place.substring(place.lastIndexOf("/") + 1) || place).arg(scope);
                const question = folder.length === 0 && App.filtered.question !== TorrentFilterModel.AnyQuestion
                               ? root._sharedValue(hashes, hash => App.filtered.questionOfHash(hash)) : undefined;
                if (question !== undefined && question !== TorrentFilterModel.AnyQuestion)
                    return qsTr("%1, out of %2").arg(root._questionNow(question, one)).arg(scope);
            }
            return qsTr("%1 out of %2").arg(root._showSubject(gone.length, hashes.length)).arg(scope);
        }
        // Scrolled away: board 50's "they are out of view, under giving back".
        const away = hashes.filter(hash => !table.hashInView(hash));
        if (away.length > 0) {
            const words = qsTr("%1 out of view").arg(root._showSubject(away.length, hashes.length));
            const question = App.filtered.question === TorrentFilterModel.AnyQuestion
                           ? root._sharedValue(away, hash => App.filtered.questionOfHash(hash)) : undefined;
            const row = question !== undefined ? rail.questionRow(question) : null;
            return row !== null ? qsTr("%1, under %2").arg(words).arg(row.text) : words;
        }
        const loose = hashes.filter(hash => !table.hashSelected(hash)
                                    && !(one && table.selectedCount === 0 && table.cursorHash === hash));
        if (loose.length > 0)
            return qsTr("%1 not selected").arg(root._showSubject(loose.length, hashes.length));
        return "";
    }

    /// F11: moves while the strip is up whenever its torrents may have come into the rows or
    /// left them (a sync, a sort, a filter), so "show" follows them live.
    property int _showTick: 0

    Connections {
        function onCountChanged(): void {
            root._showTick += 1;
        }

        function onDataChanged(): void {
            root._showTick += 1;
        }

        function onLayoutChanged(): void {
            root._showTick += 1;
        }

        function onModelReset(): void {
            root._showTick += 1;
        }

        enabled: outcomeStrip.shown
        target: App.filtered
    }

    /// The strip's reason for "show", live (F11): the rows, the scroll, the selection, the
    /// cursor and the scope are what it reads.
    readonly property string outcomeShowReason: {
        root._showTick;
        table.scrollY;
        table.height;
        table.selectionRevision;
        table.cursorHash;
        App.filtered.text;
        App.filtered.question;
        App.filtered.folder;
        App.filtered.category;
        App.filtered.tag;
        App.filtered.tracker;
        root.moveShowing;
        return outcomeStrip.shown ? root.outcomeShowWhy(App.outcomes.current) : "";
    }

    /// F9: the strip's "show" selects them (one acted on alone gets the cursor, as C18 had
    /// it), the cursor on the first, scrolled to it. When one left the scope the rail goes
    /// where they all are, their folder or their question, else the library, and the filter
    /// clears; never a filter of its own (board 42 J1).
    function showOutcome(hashes: var): void {
        const present = hashes.filter(hash => App.torrents.indexOfHash(hash) >= 0);
        if (present.length === 0)
            return;
        root.setView("library");
        root.leaveMoveShow();
        if (present.some(hash => App.filtered.rowOfHash(hash) < 0))
            root._scopeForShow(present);
        const rows = present.map(hash => App.filtered.rowOfHash(hash)).filter(row => row >= 0);
        if (rows.length === 0)
            return;
        if (present.length > 1 || table.selectedCount > 0)
            table.selectHashes(present);
        // The last first, then the cursor's: as many of them in view as fit, the first always.
        table.scrollToRow(Math.max(...rows));
        table.moveCursorTo(Math.min(...rows));
        root.focusHome();
    }

    /// F9's scope: the filter's words go; if one is still out, the rail goes to the folder
    /// (a folder was picked) or the question (a question was) they all share, else the
    /// library.
    function _scopeForShow(hashes: var): void {
        root.clearFilter();
        if (hashes.every(hash => App.filtered.rowOfHash(hash) >= 0))
            return;
        const folder = App.filtered.folder;
        const question = App.filtered.question;
        rail.pickLibrary();
        if (folder.length > 0) {
            const place = root._sharedValue(hashes, hash => App.torrentRow(hash).savePath ?? "");
            if (place !== undefined && place.length > 0)
                App.filtered.folder = place;
        } else if (question !== TorrentFilterModel.AnyQuestion) {
            const shared = root._sharedValue(hashes, hash => App.filtered.questionOfHash(hash));
            if (shared !== undefined && shared !== TorrentFilterModel.AnyQuestion)
                rail.selectQuestion(shared);
        }
    }

    Connections {
        // The strip went or another took its place (×, esc, its time, the next action): its
        // band goes with it.
        function onCurrentChanged(): void {
            if (root.footerFailure.fromStrip === true)
                root.footerNoticeOpen = false;
        }

        target: App.outcomes
    }

    // --- docs/31 D7: A, and the footer's note after it ---
    // With the schedule on, a hand toggle lasts until the schedule next flips the other way,
    // which is later than most people expect. Once the server has flipped, the footer says
    // so in its note slot (M16) until the next key: "burst on by hand · the schedule ends it
    // at 07:00 tomorrow". With the schedule off there is nothing to add.

    /// The state A asked to leave, while its flip has not come back yet; null otherwise.
    property var _altFlipFrom: null

    /// A, the status line's chip and the palette's row.
    function toggleAltSpeed(): void {
        root._altFlipFrom = App.globalState.useAltSpeedLimits === true;
        altFlipTimer.restart();
        App.toggleAltSpeed();
    }

    /// App.altSpeed moved. Once the server has flipped, the note shows as soon as AltSpeed
    /// has one: the flip itself re-reads the preferences (D13), so a schedule changed in the
    /// Web UI can turn the first reading's "by the schedule" into "by hand" a moment later.
    /// A note already up follows AltSpeed, and goes when it no longer applies (the schedule,
    /// the bar or the tray flipped it back).
    function _altFlipped(): void {
        const note = App.altSpeed.footerNote;
        if (root._altFlipFrom !== null && App.altSpeed.on !== root._altFlipFrom) {
            if (note.length === 0)
                return; // still armed, until altFlipTimer
            root._altFlipFrom = null;
            altFlipTimer.stop();
            root.flashFooterNote(note);
            footerNoteTimer.stop();
            root.footerNoteSticky = true;
        } else if (root.footerNoteSticky && root.footerNote.length > 0) {
            root.footerNote = note;
        } else {
            return;
        }
        root.footerNoteMark = App.altSpeed.kind === "burst" ? "▲" : App.altSpeed.kind === "turtle"
                                                                    ? "▼" : "";
    }

    Connections {
        function onChanged(): void {
            root._altFlipped();
        }

        target: App.altSpeed
    }

    // A toggle the server never answered (a refusal, a lost server) leaves no note behind.
    Timer {
        id: altFlipTimer

        interval: 15000

        onTriggered: root._altFlipFrom = null
    }

    // DECISIONS C24 H10: keys on demand. Alt held alone for a moment (Theme.keyBadgeDelay)
    // badges the frame's keys: the rail, the title bar's tools, the status line's server lane
    // and the action bar. alt+j's alt is let go before the delay, so nothing flashes; an
    // overlay has its own keys written in it, so the frame's stay hidden under it.
    KeyWatch {
        id: altWatch

        onAltHeldChanged: {
            if (altWatch.altHeld) {
                altBadgeTimer.restart();
            } else {
                altBadgeTimer.stop();
                root._altLong = false;
            }
        }
    }

    Timer {
        id: altBadgeTimer

        interval: Theme.keyBadgeDelay

        onTriggered: root._altLong = altWatch.altHeld
    }

    // The next key takes the note away.
    KeyWatch {
        enabled: root.footerNoteSticky && root.footerNote.length > 0

        onKeyPressed: {
            root.footerNote = "";
            root.footerNoteSticky = false;
            root.footerNoticeOpen = false; // docs/35 N8: esc, or any key
        }
    }

    /// docs/32 N1 (`App.loggedFailure`): a move, rename or delete omaseed asked for didn't
    /// happen. `hashes` are the torrents it is about that are still there.
    function flashFailureNote(text: string, hashes: var): void {
        root.flashFooterNote(text);
        root.footerNoteWarn = true;
        root.footerNoteHint = hashes.length > 0;
    }

    Timer {
        id: footerNoteTimer

        interval: root._footerNoteSeconds * 1000

        onTriggered: root.footerNote = ""
    }

    // #146 (C24 H17): "+ add ▾"'s menu.
    AddMenu {
        id: addMenu

        anchors.fill: parent
        createShown: App.createOffered
        searchShown: App.searchOffered
        z: 9

        onActivated: action => {
            if (action === "create")
                root.openCreate("");
            else if (action === "search")
                root.openSearch("");
            else
                App.addRequested();
        }
    }

    // #146 (board 49, C27): the create sheet.
    CreateSheet {
        id: createSheet

        anchors.fill: parent
        multiServer: root.multiServer
        serverDown: root.serverLost
        z: 9
    }

    DisplayMenu {
        id: displayMenu

        anchors.fill: parent
        current: App.display
        z: 9

        onActivated: display => root.pickDisplay(display)
    }

    AppMenu {
        id: appMenu

        anchors.fill: parent
        altKind: App.altSpeed.kind
        serverLine: root.serverLaneText()
        sharedRows: App.appMenuRows
        version: App.versionText.replace(/^omaseed /, "")
        z: 9

        onActivated: id => root.appMenuAction(id)
    }

    GlobalLimitsSheet {
        id: globalLimitsSheet

        anchors.fill: parent
        z: 9
    }

    ActionsMenu {
        id: actionsMenuPanel

        anchors.fill: parent
        // #51 D17: a right-click on another row closes the menu and opens it again there.
        passRightClick: true
        z: 9

        onActivated: action => root.torrentAction(action)
        onRightClickedOutside: (x, y) => {
            if (!table.visible || root.serverLost)
                return;
            const at = actionsMenuPanel.mapToItem(table, x, y);
            table.contextClickAt(at.x, at.y);
        }
    }

    // DECISIONS C19 T3 (C15 M1's "the row's menu"): a rail folder row's menu. It moves that
    // row's folder, picked or not; a torrent's menu no longer offers it.
    Menu {
        id: folderMenuPanel

        /// The save path the menu was opened on.
        property string folder: ""

        anchors.fill: parent
        // The folder's own name: a whole path would be cut at its end, the part that tells.
        heading: folderMenuPanel.folder.substring(folderMenuPanel.folder.lastIndexOf("/") + 1)
        items: {
            const rows = [{
                    "text": qsTr("move this folder…"),
                    "key": "M",
                    "action": "moveFolder"
                }];
            // #146 (C27 CR1): beside the move.
            if (App.createOffered)
                rows.push({
                              "text": qsTr("create a torrent from a folder in it…"),
                              "glyph": Glyphs.create,
                              "key": "ctrl+n",
                              "action": "createFrom"
                          });
            return rows;
        }
        menuWidth: Theme.addMenuWidth
        objectName: "folderMenu"
        z: 9

        onActivated: action => {
            if (action === "moveFolder")
                root.openMoveFolder(folderMenuPanel.folder, "");
            else if (action === "createFrom")
                root.openCreate(folderMenuPanel.folder.replace(/\/*$/, "/"));
        }
    }

    DeleteConfirm {
        id: deleteConfirmPanel

        anchors.fill: parent
        z: 9
    }

    // Board 42 §3 (M4–M9): the move sheet, its loose files' list, the failures' recheck, and
    // quit's question while a move runs (J5).
    MoveSheet {
        id: moveSheet

        anchors.fill: parent
        z: 9

        onLooseListRequested: folder => moveLooseList.openFor(folder)
        onShowRequested: hashes => root.showMoveHashes(hashes, true)
    }

    MoveLooseList {
        id: moveLooseList

        anchors.fill: parent
        z: 10

        onCopyRequested: text => root.copyText(text, qsTr("copied the list"))
    }

    FreeSpaceSheet {
        id: freeSpaceSheet

        anchors.fill: parent
        z: 9

        onAddAgainRequested: (hash, name, files, bytesText, version) => addAgainConfirm.openWhy(hash, name, files, bytesText, version)
        onShowFolderRequested: hash => App.openFolder(hash)
    }

    AddAgainConfirm {
        id: addAgainConfirm

        anchors.fill: parent
        z: 9
    }

    // Board 44 §2: what the free and the add-again did, in the footer's note.
    Connections {
        function onSkippedSpaceFreed(outcome: var): void {
            if (!outcome.ok) {
                root.flashFooterNote(qsTr("free the space failed: %1").arg(outcome.error));
                return;
            }
            const kept = outcome.kept === 1 ? " · " + qsTr("1 stays")
                       : outcome.kept > 1 ? " · " + qsTr("%1 stay").arg(outcome.kept) : "";
            const deleted = outcome.deleted === 1 ? qsTr("freed %1 · 1 skipped file deleted").arg(outcome.freedText)
                                                  : qsTr("freed %1 · %2 skipped files deleted").arg(outcome.freedText).arg(
                                                        outcome.deleted);
            root.flashFooterNote(deleted + kept + (outcome.rechecked ? " · " + qsTr("rechecking") : ""));
        }

        function onTorrentAddedAgain(outcome: var): void {
            root.flashFooterNote(outcome.ok ? qsTr("%1 added again").arg(outcome.name)
                                            : qsTr("adding %1 again failed: %2").arg(outcome.name).arg(outcome.error));
        }

        target: App
    }

    MoveRecheckSheet {
        id: moveRecheckSheet

        anchors.fill: parent
        z: 9

        onSetLocationRequested: hashes => moveSheet.openSelection(hashes, "")
    }

    MoveQuitConfirm {
        id: moveQuitConfirm

        anchors.fill: parent
        z: 15

        onHideRequested: root.hideRequested()
    }

    // A picked folder that the move emptied: the pick follows it to its new place (F3), or
    // goes when the folder is gone for another reason.
    Connections {
        function onRevisionChanged(): void {
            const folder = App.filtered.folder;
            if (folder.length === 0 || Object.keys(App.move.folders.figures(folder)).length > 0)
                return;
            const moved = App.move.root === folder && App.move.newPath.length > 0
                    && Object.keys(App.move.folders.figures(App.move.newPath)).length > 0;
            App.filtered.folder = moved ? App.move.newPath : "";
        }

        target: App.move.folders
    }

    Connections {
        function onMoveQuitAsked(): void {
            moveQuitConfirm.open();
        }

        function onMoveShowRequested(): void {
            root.moveBandAction("show");
        }

        target: App
    }

    LimitsSheet {
        id: limitsSheetPanel

        anchors.fill: parent
        z: 9
    }

    RenameSheet {
        id: renameSheetPanel

        anchors.fill: parent
        z: 9
    }

    DetailsAddSheet {
        id: detailsAddSheet

        anchors.fill: parent
        z: 9
    }

    RenameFilesSheet {
        id: renameFilesSheet

        anchors.fill: parent
        z: 9
    }

    BanPeerConfirm {
        id: banPeerConfirm

        anchors.fill: parent
        z: 9
    }

    // --- T4.4: palettes ---------------------------------------------------------------------
    // Jump (`ctrl+k`), category (`C`), tags (`t`), sort (`s`) and columns (`|`), plus the
    // table-state round trip through `state.json`. `App.categoryPickerRequested` /
    // `tagPickerRequested` are T4.3's seam (its `C`/`t` actions); the add sheet's `…` chips
    // open the same two palettes in `openForChoice` mode, which answers the caller instead of
    // touching torrents.
    //
    // Every palette sits above every sheet (`z: 14` against the add sheet's 12 and the
    // duplicate's 13), because the category and tag ones can be opened from inside the add
    // sheet. `Kit/Palette.open()` remembers `Window.activeFocusItem`, so closing one hands the
    // keyboard back to the chip row it came from.
    JumpPalette {
        id: jumpPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        table: table
        z: 14
    }

    CategoryPalette {
        id: categoryPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 14

        onChosen: name => addSheet.applyChosenCategory(name)
        onSharedMoveFound: (hashes, name, create, moved) => sharedMoveConfirm.openFor(hashes, name, create, moved)
    }

    // #28: a category change that would move files another torrent uses asks first.
    SharedMoveConfirm {
        id: sharedMoveConfirm

        anchors.fill: parent
        z: 14
    }

    TagPalette {
        id: tagPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 14

        onChosen: tags => addSheet.applyChosenTags(tags)
    }

    SortPalette {
        id: sortPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        table: table
        z: 14
    }

    ColumnsPalette {
        id: columnsPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        table: table
        z: 14
    }

    // docs/30: `:` and `ctrl+shift+p`.
    // docs/33 S1 (D2–D4, D14): the server palette. Above the add sheet, which opens it too.
    ServerPalette {
        id: serverPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 15

        onManageRequested: id => settingsModal.showServer(id)
    }

    /// The status line's server lane (DECISIONS C24 H15, C12's words): "NAME · qBittorrent
    /// 5.2.3", or the name with its state while the server is down or on its first sync.
    /// The one place the lane's words are made, for engines' K11 (C25: "NAME · ENGINE
    /// VERSION", C12's state words in its place while it is down).
    function serverLaneText(): string {
        if (!App.hasServer)
            return qsTr("no server");
        // DECISIONS C28: another engine's server, never asked ("seedbox · Transmission server").
        if (root.serverLost && root.serverProblem.kind === "engine")
            return qsTr("%1 · %2").arg(App.serverName).arg(root.serverProblem.reason);
        // docs/33 D9: a server that answered and said no is not "unreachable".
        if (root.serverLost && App.serverRefused)
            return qsTr("%1 · sign-in refused").arg(App.serverName);
        // docs/44 D11: a timeout is there but slow; refused or no route, nothing is.
        if (root.serverLost && App.serverWaiting)
            return qsTr("%1 · isn't answering").arg(App.serverName);
        if (root.serverLost)
            return qsTr("%1 · unreachable").arg(App.serverName);
        if (root.firstSync)
            return qsTr("%1 · first sync").arg(App.serverName);
        // The API answers "v5.2.3"; the line prints "qBittorrent 5.2.3".
        const version = App.globalState.serverVersion.replace(/^v/, "");
        return version.length > 0 ? qsTr("%1 · qBittorrent %2").arg(App.serverName).arg(version)
                                  : App.serverName;
    }

    /// `ctrl+s`, the status line's server lane, "switch server…", the rail's "… N more", the
    /// add sheet's chip. With one server there is nothing to switch to: the note says so (D19).
    function openServerPalette(): void {
        if (root.firstRun)
            return;
        if (!root.multiServer) {
            root.flashFooterNote(qsTr("one server · add another in Settings › Servers"));
            return;
        }
        serverPalette.openOnNext();
    }

    CommandPalette {
        id: commandPalette

        anchors.fill: parent
        scrollHint: App.railScrollHint
        z: 14

        onJumpRequested: text => {
            jumpPalette.open();
            jumpPalette.query = text;
        }
        onRunRequested: action => root.runCommand(action)
        // C26 SR14: the second no-match row.
        onSearchRequested: text => root.openSearch(text)
    }

    Connections {
        function onSortOrderChanged() {
            root.saveTableState();
        }

        function onSortRoleChanged() {
            root.saveTableState();
        }

        // A filter cleared from elsewhere (the jump palette, a notice's "show") empties the
        // title bar's field too, which would otherwise keep a word that no longer filters.
        // Only the emptying: the model trims, so mirroring every change would eat a space
        // the user is still typing.
        function onTextChanged() {
            if (App.filtered.text.length === 0 && titleBar.searchField.text.trim().length > 0)
                titleBar.searchField.text = "";
        }

        target: App.filtered
    }

    Connections {
        function onVisibleColumnsChanged() {
            root.saveTableState();
        }

        function onColumnOrderChanged() {
            root.saveTableState();
        }

        // #52: once per drag or fit; a drag in progress is the table's own.
        function onColumnWidthsChanged() {
            root.saveTableState();
        }

        target: table
    }

    // The columns palette toggles `table.visibleColumns` and moves `table.columnOrder` live
    // (a header drag moves it too; a grip drag sets `table.columnWidths`, #52), and the header
    // (and every other sort trigger, including
    // a click) can change `App.filtered`'s sort at any time; all of it round-trips through `state.json`, keyed by the chosen server (or "demo").
    function saveTableState(): void {
        const key = App.tableStateKey();
        // Not halfway through a restore, and never the layout of another server under this
        // one's key: after a switch nothing is saved until the new key has been read.
        if (key.length === 0 || root._restoringState || key !== root._stateKey)
            return;
        App.saveTableState(key, {
                               "columns": table.visibleColumns,
                               "columnOrder": table.columnOrder,
                               // #52: base-12 units, like `columns[].width`; only the changed.
                               "columnWidths": table.columnWidths,
                               "sortRole": App.filtered.sortRoleName,
                               "sortOrder": App.filtered.sortOrder,
                               "railFolded": root.railFolded,
                               // Board 42 F2: the folders group's folds.
                               "foldedFolders": App.move.folders.folded,
                               // In base-12 units, so a base size change keeps the proportion.
                               // 0 while it is the default, so a later default reaches it.
                               "detailsWidth": split.sideWidth === split.defaultSideWidth ? 0
                                                                                          : Math.round(
                                                                                                split.sideWidth
                                                                                                * 12
                                                                                                / Theme.font.baseSize),
                               "detailsHeight": Math.round(split.sideHeight * 12
                                                           / Theme.font.baseSize),
                               // docs/17 M10.5: a restart reopens it as it was left.
                               "detailsFolded": split.folded
                           });
    }

    /// The key `restoreTableState` last read: the server whose layout the table shows.
    property string _stateKey: ""
    property bool _restoringState: false

    /// Read the chosen server's layout, unless it is the one already shown. Runs at startup,
    /// on every server switch (the rail's SERVERS group, Settings › Servers `d`) and when
    /// first run hands over.
    function restoreTableState(): void {
        const key = App.tableStateKey();
        if (key.length === 0 || key === root._stateKey)
            return;
        root._restoringState = true;
        root._applyTableState(App.tableState(key));
        root._stateKey = key;
        root._restoringState = false;
    }

    /// Read the chosen server's layout again even when it is the one shown: after a restore
    /// (#34) state.json holds another layout for the same server.
    function reloadTableState(): void {
        root._stateKey = "";
        root.restoreTableState();
    }

    function _applyTableState(state: var): void {
        root.railFolded = state.railFolded !== undefined ? state.railFolded : [];
        App.move.folders.folded = state.foldedFolders !== undefined ? state.foldedFolders : [];
        // 340 was the default before docs/11 J1, saved by every earlier version whether or
        // not anybody dragged it; it reads as "the default" so the wider one arrives.
        split.sideWidth = state.detailsWidth > 0 && state.detailsWidth !== 340
                ? Theme.space(state.detailsWidth) : split.defaultSideWidth;
        // Likewise 220 for the height, the default before docs/13's band.
        split.sideHeight = state.detailsHeight > 0 && state.detailsHeight !== 220
                ? Theme.space(state.detailsHeight) : split.defaultSideHeight;
        // Absent before docs/17 (an older state file): open, as every earlier version started.
        split.folded = state.detailsFolded === true;
        if (state.columns !== undefined && state.columns.length > 0)
            table.visibleColumns = state.columns;
        table.setColumnOrder(state.columnOrder !== undefined ? state.columnOrder
                                                             : table.defaultColumnOrder);
        // Absent before #52, and for a server whose columns were never dragged: the defaults.
        table.setColumnWidths(state.columnWidths);
        if (state.sortRole !== undefined && String(state.sortRole).length > 0) {
            App.filtered.setSortByRoleName(state.sortRole);
            App.filtered.sortOrder = state.sortOrder !== undefined ? state.sortOrder : Qt.DescendingOrder;
        }
    }

    /// `OMASEED_DEMO_PALETTE=jump|category|tags|sort|columns|commands` opens one palette on top of
    /// `--demo table` for the phase's screenshot procedure; no production path reads it.
    function _openDemoPalette(): void {
        switch (DemoEnv.value("OMASEED_DEMO_PALETTE")) {
        case "jump":
            jumpPalette.open();
            break;
        case "category":
            categoryPalette.openFor(root.actionHashes());
            break;
        case "tags":
            tagPalette.openFor(root.actionHashes());
            break;
        case "sort":
            sortPalette.openOnCurrent();
            break;
        case "columns":
            columnsPalette.open();
            break;
        case "commands":
            root.openCommandPalette();
            break;
        default:
            break;
        }
    }

    Item {
        id: paletteStartup

        Component.onCompleted: Qt.callLater(() => {
                                                 root.restoreTableState();
                                                 root._openDemoPalette();
                                             })
    }
    // --- end T4.4 ------------------------------------------------------------------------

    // --- T4.2: add flow ---
    // Every source of an add ends up in one sheet (spec §3 Fig. 5): `a` and the title bar's
    // "+ add" arrive as `App.addRequested`, a drop as `Main.qml`'s `addDropped`, the browser's
    // `magnet:`/`.torrent` hand-off as `App.addUrisRequested`. An infohash the library
    // already holds opens Fig. 10 instead of being sent.
    AddSheet {
        id: addSheet

        anchors.fill: parent
        z: 12

        onAddRequested: request => App.addTorrent(request)
        onDuplicateFound: (duplicate, source) => duplicateSheet.openFor(duplicate, source)
        // C24 H16: the empty state's "search your plugins…", and typed words no add takes.
        onSearchRequested: text => root.openSearch(text)
        // The `…` chips: T4.4's palettes, asked to answer instead of to act.
        onCategoryPaletteRequested: categoryPalette.openForChoice()
        onTagPaletteRequested: tagPalette.openForChoice(addSheet.tagChips.selected)
        // docs/33 D14: the head's chip or ctrl+s in the sheet.
        onServerPaletteRequested: root.openServerPalette()
    }

    DuplicateSheet {
        id: duplicateSheet

        anchors.fill: parent
        z: 13

        // `show it` gives up on the add: the sheet behind this one goes away with it.
        onShowRequested: hash => {
            addSheet.dismiss();
            // From search's add… too (C26 SR9): `show it` is the library's torrent.
            if (root.searchShown)
                root.setView("library");
            root.showTorrent(hash);
        }
        // docs/25 D3 a: `trackersMerged` then moves to the torrent's Trackers tab.
        onMergeRequested: (hash, urls) => {
            addSheet.dismiss();
            App.mergeTrackers(hash, urls);
        }
    }

    /// The drop target in `Main.qml` hands its addables over here. A drop that held none (a
    /// file that is not a .torrent, text that is not a magnet) says so in the footer.
    function addDropped(values: var): void {
        if (values.length === 0)
            root.flashFooterNote(qsTr("nothing to add"));
        else if (root.firstRun)
            root._pendingAdds = root._pendingAdds.concat(values);
        else if (values.length === 1)
            addSheet.openWith(values[0], "drop");
        else
            addSheet.openList(values, "drop");
    }

    /// Move the table's cursor to `hash` and let the details pane follow. Used by the add
    /// flow once a new torrent turns up, and by T4.7's "show" notification action, which may
    /// well name a row the filter of the moment hides (a finished download under the rail's
    /// "Downloading"): `revealHash` clears whatever hides it, as the jump palette does.
    function showTorrent(hash: string): void {
        if (root.logShown) // a notice's click, the jump palette: the library has the torrent
            root.setView("library");
        if (table.revealHash(hash))
            root.focusHome();
    }

    // --- end T4.2 ---

    // --- T4.5: settings ---
    // The settings modal of spec §5 Fig. 16, with Fig. 9's server sheet inside it. `,`, the
    // mark's menu, the tray's `settings…` and the disconnected banner all arrive through
    // App.openSettings().
    SettingsModal {
        id: settingsModal

        anchors.fill: parent
        z: 11

        // The Keybindings pane's `?`, clicked: the help sheet sits under the modal, so the
        // modal goes first.
        onHelpRequested: {
            settingsModal.dismiss();
            root.helpOpen = true;
        }
    }

    // DECISIONS C24 H13: the ground is opaque unless Appearance says translucent; then the
    // transparency override, or with none the shell's `[launcher] background-alpha` (Q1),
    // which a theme change can move under us. super+backspace is Hyprland's, as for any app.
    Binding {
        property: "windowAlpha"
        target: Theme
        value: App.windowGround !== "translucent" ? 1 : App.windowAlphaOverride > 0
                                                       ? App.windowAlphaOverride / 100
                                                       : Theme.defaultWindowAlpha
    }
    // --- end T4.5 ---

    // --- T4.6: first run ---
    // The full-window page of spec v2 §5 Fig. 15, shown instead of the body's rail/table/
    // details/EmptyState while nothing is configured yet. It sits above everything else
    // (`z` higher than the settings modal, which is 11: first run pre-empts settings too —
    // there is nothing to configure servers *of* yet) and closes itself once step 3 is
    // answered; `App.hasServer`/`App.firstRunNeeded` already flip the moment `addServer()`
    // and `setDefaultServer()` run, so the table underneath is already filling in by then —
    // which is why this is latched at startup and only released by `finished()`: a plain
    // binding unloaded the page the moment step 2 saved the server, and step 3 never showed.
    property bool firstRun: false // latched below in Component.onCompleted, never a binding

    /// Magnets and .torrent files handed over (or dropped) during first run. The add sheet
    /// would open invisible under the page and take its keyboard, with no server to add to;
    /// they wait here and open the sheet once setup is done.
    property var _pendingAdds: []
    /// The tray's `add…` and `settings…` during first run, held the same way.
    property bool _pendingBlankAdd: false
    property bool _pendingSettings: false
    /// The page, for the tests; `_firstRunDetect: false` keeps them off port 8080's probe.
    readonly property alias firstRunLoader: firstRunLoader
    property bool _firstRunDetect: true

    function _openPendingAdds(): void {
        const pending = root._pendingAdds;
        const blank = root._pendingBlankAdd;
        const settings = root._pendingSettings;
        root._pendingAdds = [];
        root._pendingBlankAdd = false;
        root._pendingSettings = false;
        // One overlay at a time: an add asked for wins over settings.
        if (pending.length > 0)
            addSheet.openList(pending, "handoff");
        else if (blank)
            addSheet.openBlank();
        else if (settings)
            settingsModal.open();
    }

    // The one release besides `finished()`: a demo library arriving later (the QML tests
    // create MainView first and `App.loadDemo()` afterwards) is never a first run.
    Connections {
        function onDemoModeChanged(): void {
            if (App.demoMode)
                root.firstRun = false;
        }

        target: App
    }

    // A `Loader` rather than a plain child: `FirstRunPage` starts a real `ServerDetector`
    // (a TCP probe of 127.0.0.1:8080) from `Component.onCompleted`, and that must only ever
    // run while first run is actually showing — never for every other window state (demo,
    // qmltestrunner, a configured server) a plain `visible: root.firstRun` child would still
    // instantiate and complete once.
    Loader {
        id: firstRunLoader

        active: root.firstRun
        anchors.fill: parent
        z: 20

        sourceComponent: FirstRunPage {
            autoDetect: root._firstRunDetect
            onFinished: {
                root.firstRun = false;
                root.restoreTableState();
                root.focusHome();
                root.setupFinished();
                root._openPendingAdds();
            }
            onQuitRequested: root.quitRequested()
        }
        // Whatever the window focused while starting up, the page gets the keyboard once it
        // exists (FirstRunPage also asks for it itself; this covers a later re-appearance).
        onLoaded: (item as Item).forceActiveFocus()
    }
    // --- end T4.6 ---
}
