pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The whole main window, minus the window (spec v2 §1 Fig. 1 and the `.win.main` grid:
// hero row, then rail | table | details, then footer). `Main.qml` is a `Window` that holds
// exactly one of these; the split exists so the qmltestrunner can build the shell without
// a top-level window and still drive its keys and focus.
//
// What lives here and nowhere else:
//   - the wiring between App and the four views (Rail, TorrentTable, Details, Hero);
//   - key routing. A key press reaches the focused view first — Qt propagates what the view
//     does not accept up the parent chain to this item's `Keys.onPressed` — so `routeKey`
//     only ever sees what no view claimed. That is what lets `a` add a tracker inside the
//     details pane and a torrent everywhere else, with one table of keys (`Keymap`);
//   - which state the window is in (first sync, empty, lost server, filter with no result)
//     and therefore which of §7's views covers the body.
// Everything the footer teaches comes from `HintModel`, which reads the same `Keymap`.
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
    readonly property alias hero: hero
    readonly property alias rail: rail
    readonly property alias table: table
    readonly property alias details: details
    readonly property alias split: split
    readonly property alias hintModel: hintModel
    readonly property alias helpSheet: helpSheet
    readonly property alias helpSheetPanel: helpSheetPanel
    // T4.3
    readonly property alias actionsMenuPanel: actionsMenuPanel
    /// C19 T3: the rail folder row's menu.
    readonly property alias folderMenuPanel: folderMenuPanel
    readonly property alias selectionBar: selectionBar
    readonly property alias outcomeStrip: outcomeStrip
    readonly property alias deleteConfirmPanel: deleteConfirmPanel
    readonly property alias moveSheet: moveSheet
    readonly property alias moveBand: moveBand
    readonly property alias moveScope: moveScope
    readonly property alias moveQuitConfirm: moveQuitConfirm
    readonly property alias sharedMoveConfirm: sharedMoveConfirm
    readonly property alias limitsSheetPanel: limitsSheetPanel
    readonly property alias moreMenu: moreMenu
    readonly property alias filterMenu: filterMenu
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
    // keyboard, `esc` closes that one and nothing else, and the footer teaches its keys
    // instead of the table's. They are listed topmost-first, by the `z` each one is given
    // further down, so the one the user is actually looking at is the one that answers.
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
        if (deleteConfirmPanel.opened)
            return deleteConfirmPanel;
        if (actionsMenuPanel.opened)
            return actionsMenuPanel;
        if (folderMenuPanel.opened)
            return folderMenuPanel;
        if (moreMenu.opened)
            return moreMenu;
        if (filterMenu.opened)
            return filterMenu;
        return null;
    }

    /// The rail groups folded on this server (docs/08 §5), saved with the table's state.
    property var railFolded: []

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

    /// `shift+m`, the palette's "move folder…", the scope line's link (M1); `newPath` for
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
    /// There are numbers worth printing in the hero.
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

    /// `` ` ``, `~` and the hero's tabs. With a selection, the console opens on it (D2 a).
    function setView(next: string): void {
        if (next === root.view)
            return;
        if (next === "seed")
            App.console.setScopeHashes(table.selectedCount > 0 ? table.selectedHashes() : []);
        else
            App.console.setScopeHashes([]);
        if (next === "log")
            root._viewBeforeLog = root.view;
        // docs/32 §6: the lines are kept only while the log is open. A demo's stay: no server
        // would read them again.
        if (root.view === "log" && !App.demoMode)
            App.serverLog.release();
        root.view = next;
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

    /// The log's `enter` (D13): the line's torrent in the library, its General tab open.
    function showLogTorrent(hash: string): void {
        root.setView("library");
        if (table.revealHash(hash)) {
            root.showDetailsTab(0);
            root.focusHome();
        }
    }

    /// The console's scope in words, from what the rail selects.
    readonly property string consoleScopeName: {
        const parts = [];
        const status = App.filtered.statusFilter;
        if (status !== TorrentFilterModel.All)
            parts.push(rail.statusRows[status].text);
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
            App.filtered.statusFilter = TorrentFilterModel.All;
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

    /// Which view holds the focus, for `HintModel`.
    readonly property string focusArea: {
        if (hero.searchField.editing || details.filterEditing || logView.filterEditing)
            return "search";
        if (root.logShown)
            return "log";
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

    // the footer's facts (left half of `.foot`)
    /// `[{value, label}]`, drawn value-in-foreground / label-in-dim and joined with `·`.
    readonly property var factParts: {
        if (root.serverLost)
            return [
                {
                    "value": "",
                    // docs/33 D9: never "last sync" with nothing after it.
                    "label": App.globalState.lastSyncText.length > 0 ? qsTr("last sync %1").arg(
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
        if (root.filterEmpty)
            return [
                {
                    "value": "0",
                    "label": qsTr("of %1").arg(App.counts.total)
                }
            ]; // Fig. 24
        // Fig. 1 (and Fig. 22, which simply has no downloading torrents to count). docs/10 §3:
        // on the hero's narrow steps the words shorten, then the least-read facts go.
        const step = hero.level;
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
        if (table.selectedCount > 0)
            parts.push({
                "value": "",
                "label": step >= 2 ? qsTr("%1 sel").arg(table.selectedCount)
                                   : qsTr("%1 selected").arg(table.selectedCount),
                "spoken": qsTr("%1 selected").arg(table.selectedCount)
            });
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

    /// `/`: put the caret in the hero's search field.
    function focusSearch(): void {
        hero.openSearch();
    }

    /// `esc` in the field, and the field's own clear: empty the filter and go back to the
    /// table.
    function clearFilter(): void {
        hero.searchField.text = "";
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

    /// The rail's own word for a status enum, for Fig. 24's `in <status>`.
    function statusName(status: int): string {
        const names = [qsTr("All"), qsTr("Downloading"), qsTr("Seeding"), qsTr("Completed"),
            qsTr("Stopped"), qsTr("Active"), qsTr("Stalled"), qsTr("Checking"), qsTr("Errored")];
        return status >= 0 && status < names.length ? names[status] : "";
    }

    /// `0`-`8`: a status row of the rail, so the rail's own selection follows the key.
    function selectStatus(index: int): void {
        rail.selectStatus(index);
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
        const count = table.selectedCount;
        if (count === 0)
            return "";
        if (count === 1 && table.selectedHashes()[0] === hash)
            return "";
        return qsTr("%n selected", "", count);
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
                // The jump palette moves the library's cursor: from the log, the library first.
                if (root.logShown)
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
                root.openMoreMenu();
                break;
            case Qt.Key_B:
                if (!root.logShown) // the log face has no rail (docs/32 L1)
                    root.toggleRail();
                break;
            case Qt.Key_S: // docs/33 D2: the server palette, app-level like ctrl+k
                root.openServerPalette();
                break;
            default:
                return;
            }
            event.accepted = true;
            return;
        }

        // #24: the console's own keys, while it is the face on show and the rail is not
        // where the keyboard is.
        if (root.consoleShown && !rail.activeFocus && !hero.searchField.editing) {
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
            case Qt.Key_H:
                App.railHidden = false;
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
        // about the library's table, rail and details do nothing there.
        if (root.logShown) {
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
        case Qt.Key_6:
        case Qt.Key_7:
        case Qt.Key_8:
            root.selectStatus(event.key - Qt.Key_0);
            break;
        case Qt.Key_9:
            break; // there are only nine status rows; `9` is spare
        default:
            if (!table.activeFocus || !root._tableAction(event, shift))
                return;
        }
        event.accepted = true;
    }

    /// docs/30 D9 (DECISIONS C1): the one runner behind the `?` sheet's `enter` and the
    /// command palette, so the two can never disagree. Each id does what its key does, through
    /// the same functions the keys call; keys keep their own routing. Rows of the sheet that
    /// name a movement rather than a command (j k, g G, h l, 0–8, 1–4, space, v, esc) are not
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
        // app
        "settings": () => App.openSettings(),
        "switchServer": () => root.openServerPalette(),
        "qbtSettings": () => root.openQbtSettings(),
        "help": () => root.helpOpen = true,
        "commands": () => root.openCommandPalette(),
        "more": () => root.openMoreMenu(),
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
            area = ["general", "files", "trackers", "peers", "webseeds"][details.currentTab] || "general";
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
            // App.stopStart follows the first hash: `p` starts when that one is stopped.
            "firstStopped": targets.length > 0 && targets[0].stopped === true,
            "marked": marked,
            "row": row,
            "rowIsFolder": rowIsFolder,
            "serverLost": root.serverLost,
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
        if (root.runAction(action))
            App.noteCommandRun(action);
    }

    /// `ctrl+b` (docs/08 §5): hide or show the rail. Hiding it never hides a filter: while it
    /// is gone, each active filter shows as a chip in the hero.
    function toggleRail(): void {
        App.railHidden = !App.railHidden;
        if (App.railHidden && rail.activeFocus)
            table.forceActiveFocus();
    }

    /// The filters the rail would show as selected, as the hero's chips while it is hidden.
    readonly property var activeFilterChips: {
        if (!App.railHidden)
            return [];
        const chips = [];
        const status = App.filtered.statusFilter;
        if (status !== TorrentFilterModel.All)
            chips.push({
                           "key": "status",
                           "glyph": rail.statusRows[status].glyph,
                           "text": rail.statusRows[status].text
                       });
        const category = App.filtered.category;
        if (category !== undefined)
            chips.push({
                           "key": "category",
                           "glyph": Glyphs.category,
                           "text": category === "" ? qsTr("no category") : category
                       });
        const tag = App.filtered.tag;
        if (tag !== undefined)
            chips.push({
                           "key": "tag",
                           "glyph": Glyphs.tag,
                           "text": tag === "" ? qsTr("untagged") : tag
                       });
        const tracker = App.filtered.tracker;
        if (tracker !== undefined)
            chips.push({
                           "key": "tracker",
                           "glyph": Glyphs.tracker,
                           "text": rail.trackerText(tracker)
                       });
        const folder = App.filtered.folder;
        if (folder.length > 0)
            chips.push({
                           "key": "folder",
                           "glyph": Glyphs.folder,
                           "text": folder.substring(folder.lastIndexOf("/") + 1)
                       });
        return chips;
    }

    function clearOneFilter(key: string): void {
        switch (key) {
        case "status":
            App.filtered.statusFilter = TorrentFilterModel.All;
            break;
        case "category":
            App.filtered.clearCategory();
            break;
        case "tag":
            App.filtered.clearTag();
            break;
        case "tracker":
            App.filtered.clearTracker();
            break;
        case "folder":
            App.filtered.folder = "";
            break;
        }
    }

    /// Every filter off: the rail's (shown as chips) and the name filter.
    function clearAllFilters(): void {
        for (const chip of root.activeFilterChips)
            root.clearOneFilter(chip.key);
        root.clearFilter();
    }

    /// docs/15 Q1 A: the hero's filters on its last step, hung under its `/` button with
    /// their right edges flush.
    function openFilterMenu(): void {
        const button = hero.filterButton;
        const corner = button.mapToItem(root, button.width, button.height);
        filterMenu.openAt(Math.round(corner.x - filterMenu.menuWidth), Math.round(corner.y
                                                                                  + Theme.space(4)));
    }

    /// The hero's ⋯ menu, hung under its button with their right edges flush.
    function openMoreMenu(): void {
        const button = hero.moreButton;
        const corner = button.mapToItem(root, button.width, button.height);
        moreMenu.openAt(Math.round(corner.x - moreMenu.menuWidth), Math.round(corner.y
                                                                              + Theme.space(4)));
    }

    /// ctrl+shift+, (docs/16 §2): Settings on the qBittorrent group. First run holds it like
    /// `,` does.
    function openQbtSettings(): void {
        if (root.firstRun)
            return;
        settingsModal.openQbt();
    }

    /// Runs a row of the ⋯ menu (docs/08 §3) through the one runner. The menu has closed and
    /// given the focus back.
    function moreAction(action: string): void {
        moreMenu.close();
        root.runAction(action);
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
            // docs/32 D3: the failure block's `x`, only while the pane shows it on General.
            if (details.collapsed || details.currentTab !== 0 || root.cursorHash.length === 0
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
        // `--demo table` reproduces spec §1 Fig. 1 exactly: Downloading selected in the rail
        // with the cursor parked on "movies", the table's cursor on row 1, rows 1 and 2
        // selected, row 3 in inline rename, the details open on General for row 1.
        App.filtered.setSortByRoleName(table.initialSortRoleName);
        App.filtered.sortOrder = table.initialSortOrder;
        rail.selectStatus(1);
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
        /// `a`, the hero's `+`, the tray.
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

        /// `,`, the hero's gear, the tray's `settings…`, the disconnected banner.
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
            root.showDetailsTab(2);
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

    // hero (spec `.hero`)
    Hero {
        id: hero

        filterChips: root.activeFilterChips
        // docs/14 A1: setup has no library chrome; FirstRunPage draws its own footer.
        visible: !root.firstRun

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        altDownLimit: App.globalState.dlRateLimitText
        altDownLimitShort: App.limitShortText(App.globalState.dlRateLimit)
        altUpLimitShort: App.limitShortText(App.globalState.upRateLimit)
        altSpeedActive: App.globalState.useAltSpeedLimits
        // docs/31: burst, turtle or today's alt-speed, until when, and how it came about.
        altKind: App.altSpeed.kind
        altTooltip: App.altSpeed.chipTooltip
        altUntil: App.altSpeed.untilText
        altUpLimit: App.globalState.upRateLimitText
        appName: qsTr("omaseed")
        connected: !root.serverLost
        connectionStatus: App.globalState.connectionStatus
        // docs/16 §5: the port behind "not reachable", and anonymous mode with private
        // torrents in the library.
        listenPort: App.serverPrefs.values.listen_port ?? 0
        anonymousWarning: App.serverPrefs.values.anonymous_mode === true && App.counts.privateCount > 0
        serverConfigured: App.hasServer
        downSpeed: root.live ? App.globalState.dlSpeedText : "—"
        downSpeedShort: root.live ? App.speedShortText(App.globalState.dlInfoSpeed) : "—"
        searchCount: App.filtered.count
        searchCountText: App.filtered.text.length > 0 ? qsTr("%1 of %2").arg(
                                                            App.filtered.count).arg(
                                                            App.counts.total) : String(
                                                            App.filtered.count)
        serverLine: {
            if (!App.hasServer)
                return qsTr("no server");
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
            // The API answers "v5.2.3"; the figure prints "qBittorrent 5.2.3".
            const version = App.globalState.serverVersion.replace(/^v/, "");
            return version.length > 0 ? qsTr("%1 · qBittorrent %2").arg(App.serverName).arg(
                                            version) : App.serverName;
        }
        // docs/10 §2: from the first narrow step the line drops the version (the tooltip
        // keeps it); the states that matter more than the version keep their words.
        serverLineShort: {
            if (!App.hasServer || root.serverLost || root.firstSync)
                return "";
            return App.serverName;
        }
        speedsAlignment: App.speedsAlignment
        syncing: root.firstSync
        // docs/33 D3: with two servers the server line switches; docs/35 N3: its dot.
        serverSwitchable: root.multiServer
        retrying: root.serverRetrying
        upSpeed: root.live ? App.globalState.upSpeedText : "—"
        upSpeedShort: root.live ? App.speedShortText(App.globalState.upInfoSpeed) : "—"

        // #24: library | seed, and the hero's ↓ ↑ breathing (T2, when Appearance turns them on).
        views: [
            {
                "key": "library",
                "label": qsTr("library")
            },
            {
                "key": "seed",
                "label": qsTr("seed"),
                "hint": "`"
            },
            {
                "key": "log",
                "label": qsTr("log"),
                "hint": "~"
            }
        ]
        currentView: root.view
        upSpark: root.live && App.uploadTrend ? App.console.heroSpark : []
        downSpark: root.live && App.downloadTrend ? App.console.heroDownSpark : []
        upNote: root.live ? App.console.todayText : ""

        onViewSelected: key => root.setView(key)
        onUpClicked: root.setView("seed")
        onAddClicked: App.addRequested()
        onAltSpeedClicked: root.toggleAltSpeed()
        onHelpClicked: root.helpOpen = true
        onFilterChipCleared: key => root.clearOneFilter(key)
        onFiltersClicked: root.openFilterMenu()
        onMoreClicked: root.openMoreMenu()

        Rectangle {
            anchors.fill: parent
            color: Theme.fill(Theme.fillIdle)
            visible: App.surfaceTint === 8
            z: -1
        }
        onSearchChanged: text => App.filtered.text = text
        // Review RUN-12: enter keeps the filter and gives the table back its keys, as `/`
        // then enter does in vim or less.
        onSearchAccepted: root.focusHome()
        onSearchCleared: root.clearFilter()
        onSettingsClicked: App.openSettings()
        onServerClicked: root.openServerPalette()
    }

    // The lost server's notice (spec §7 Fig. 23; docs/33 D8, D9 in docs/35's look): a band
    // the full width of the window under the hero, and the body starts under its line
    // (docs/35 N1, N2). It grows in once the server is declared lost (N6).
    DisconnectedBanner {
        id: banner

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hero.bottom
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
    // through every view, until its summary is closed.
    MoveBand {
        id: moveBand

        anchors.left: parent.left
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

    // body: rail | table | details
    Item {
        id: body

        // The first-run page (T4.6) stands in for the whole body, not over it.
        visible: !root.firstRun
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: moveBand.bottom

        Rail {
            id: rail

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.top: parent.top
            categories: App.categories
            counts: App.counts
            filtered: App.filtered
            // Board 42 F1 (M1, M2): the library's folders, after the categories.
            folders: App.move.folders
            servers: App.servers
            // T4.5: the SERVERS group is off unless the General pane asks for it.
            serverCount: App.showServersGroup ? App.serverCount : 0
            // T4.8: the group draws the real names, and picking one switches server.
            serverList: App.serverList
            tags: App.tags
            trackers: App.trackers
            // docs/08 §5: `ctrl+b` hides it; the filters it holds show as chips in the hero.
            // The log face has none: the rail is about torrents (docs/32 L1). docs/33 D1, D7:
            // with two servers it shows in every state, only its servers block while there is
            // no list; with one, not without a list, as v2.
            visible: !App.railHidden && !root.logShown
                     && (root.railServers || (!root.libraryEmpty && !root.firstSync && !root.noList))
            serversOnly: root.libraryEmpty || root.firstSync || root.noList
            serversFolded: App.serversFolded
            serverRetrying: root.serverRetrying

            collapsedGroups: root.railFolded
            scrollHint: App.railScrollHint
            trackerHostStyle: App.trackerHostStyle

            // docs/08 §5: Settings -> Appearance may tint the side surfaces, in the plan's own
            // fill steps: 4 % puts the rail and details on one step, 8 % puts the rail on two.
            Rectangle {
                anchors.fill: parent
                color: Theme.fill(App.surfaceTint === 8 ? Theme.fillHover : Theme.fillIdle)
                visible: App.surfaceTint > 0
                z: -1
            }

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
        }

        SplitLayout {
            id: split

            anchors.bottom: parent.bottom
            anchors.left: rail.visible ? rail.right : parent.left
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
                     && !root.logShown

            onFoldToggled: root.saveTableState()
            onFoldedChanged: root._detailsFoldChanged()
            onResized: root.saveTableState()
        }

        // #24: the seed console, in the table's and details' place.
        Console {
            id: seedConsole

            anchors.bottom: parent.bottom
            anchors.left: rail.visible ? rail.right : parent.left
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

            // Board 42 F1, J1 (M1, M10): what the table is narrowed to, a folder picked in the
            // rail or a move's torrents, and the way on: move it, or back to all.
            ScopeLine {
                id: moveScope

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: parent.top
                anchors.topMargin: Theme.space(12)
                linkKey: root.moveShowing ? "esc" : "shift+m"
                oneLine: true
                linkText: root.moveShowingJob ? qsTr("all torrents") : root.moveShowing ? qsTr("back to the sheet")
                                                                                         : qsTr("move this folder…")
                objectName: "moveScope"
                text: {
                    if (root.moveShowingJob)
                        return qsTr("the move of %1 · %2 · %3 with qBittorrent").arg(App.move.root.substring(
                                                                                         App.move.root.lastIndexOf("/") + 1)).arg(
                                    App.move.jobHashes.length === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(
                                                                          App.move.jobHashes.length)).arg(
                                    App.move.revision >= 0 ? App.move.band.withServer ?? 0 : 0);
                    if (root.moveShowing)
                        return root.moveShowHashes.length === 1 ? qsTr("1 torrent from the move sheet") : qsTr(
                                                                      "%1 torrents from the move sheet").arg(root.moveShowHashes.length);
                    return App.move.folders.revision >= 0 ? App.move.folderLine(App.filtered.folder) : "";
                }
                visible: root.view === "library" && (root.moveShowing || App.filtered.folder.length > 0) && text.length > 0

                onLinkActivated: {
                    if (root.moveShowing)
                        root.leaveMoveShow();
                    else
                        root.openMoveFolder(App.filtered.folder, "");
                }
            }

            TorrentTable {
                id: table

                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space(12)
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(16)
                anchors.top: moveScope.visible ? moveScope.bottom : parent.top
                anchors.topMargin: Theme.space(12)
                // Board 42 J1: the job's step for each of its torrents.
                showMoveColumn: root.moveShowingJob
                moveRevision: App.move.revision
                moveStepFor: hash => App.move.stepFor(hash)
                // DECISIONS C18 D1: a quick action's mark in its rows' status cells.
                rowMarkRevision: App.outcomes.revision
                rowMarkFor: hash => App.outcomes.rowMark(hash)
                escapeTaken: outcomeStrip.visible
                // docs/17 M9.6: the last row can always scroll clear of the selection bar, and
                // of the outcome strip over it (C18 D1).
                bottomRoom: (selectionBar.visible ? selectionBar.height + Theme.space(12) : 0)
                            + (outcomeStrip.visible ? outcomeStrip.height + Theme.space(selectionBar.visible ? 6 : 12)
                                                    : 0)
                filtered: App.filtered
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
                    actionsMenuPanel.openAt(point.x, point.y);
                }
                onActivated: root.openDetails()
                onColumnsRequested: columnsPalette.open()
                onEscapePressed: root.handleEscape()
                // C18 D6: the selection bar's mouse hint, the first 3 times it opens after a
                // plain click; it goes with the bar.
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
                onFocusRail: {
                    App.railHidden = false; // `h` brings a hidden rail back
                    rail.forceActiveFocus();
                }
                // The inline editor is the primary rename; the sheet (`renameSheetPanel`) is
                // only the menu/multi-selection fallback.
                onRenameRequested: (hash, name) => App.rename(hash, name)
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
                statusName: root.statusName(App.filtered.statusFilter)
                text: App.filtered.text
                visible: root.filterEmpty

                onAllStatusesRequested: root.selectStatus(0)
                onClearRequested: root.handleEscape()
            }

            // docs/17 M9: over the table, centred, 12 u above its bottom edge, while one or
            // more torrents are selected. Never while the server is lost (Fig. 23 is
            // read-only) or with no rows on show.
            SelectionBar {
                id: selectionBar

                anchors.bottom: table.bottom
                anchors.bottomMargin: Theme.space(12)
                anchors.horizontalCenter: table.horizontalCenter
                availableWidth: table.width - Theme.space(16)
                count: table.selectedCount
                hint: root.selectionHint
                // #24 T1: re-read when the selection or the library moves.
                summary: {
                    App.torrents.count; // the dependency: a sync that adds or drops a row
                    return table.selectedCount > 0 ? App.console.selectionSummary(table.selectedHashes()) : ({});
                }
                height: implicitHeight
                visible: table.selectedCount > 0 && table.visible && !root.serverLost
                width: implicitWidth

                onClearRequested: table.clearSelection()
                onMoreRequested: (button, folded) => root.openSelectionMore(button, folded)
                onSummaryClicked: root.setView("seed")
                onTriggered: action => root.selectionBarAction(action)
            }

            // DECISIONS C18 D1–D3 (board 45 §2): what a quick action on torrents came to, in
            // the selection bar's place. While a selection is on it sits just above the bar,
            // so both read together and nothing moves under the pointer (O3).
            OutcomeStrip {
                id: outcomeStrip

                anchors.bottom: selectionBar.visible ? selectionBar.top : table.bottom
                anchors.bottomMargin: Theme.space(selectionBar.visible ? 6 : 12)
                anchors.horizontalCenter: table.horizontalCenter
                availableWidth: table.width - Theme.space(16)
                height: implicitHeight
                outcome: App.outcomes.current
                reducedMotion: App.reducedMotion
                visible: outcomeStrip.shown && table.visible && !root.serverLost
                width: implicitWidth

                onCloseRequested: App.outcomes.dismiss()
                onDetailsRequested: root.openOutcomeDetails()
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
            // Main's footer carries the hints for every tab (Fig. 1), so the pane does not
            // repeat them inside the window.
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
            onProblemActionRequested: action => {
                const hashes = root.cursorHash.length > 0 ? [root.cursorHash] : [];
                if (hashes.length === 0)
                    return;
                if (action === "log")
                    root.showTorrentLog(hashes[0]);
                // docs/44 D13: a stuck move's sheet is not aimed at the destination it never
                // reached: qBittorrent ignores a move to the save path it already has
                // (scripts/dev/research/121-stuck-move), so the sheet starts as for any move.
                else if (action === "location")
                    moveSheet.openSelection(hashes, "");
                else if (action === "bothFolders")
                    App.showBothFolders(hashes[0]);
                else if (action === "start") // DECISIONS C20: "retry", a start that forgets the stop
                    App.retryTorrents(hashes);
                else if (action === "recheck")
                    App.recheck(hashes);
                else if (action === "addAgain") // board 44 §2: libtorrent's hybrid error
                    addAgainConfirm.openFor(hashes[0], App.torrentRow(hashes[0]).name || "", 0);
            }
            // Board 44 F1 (C17 D3): the Files tab's band, or shift+f there.
            onFreeSpaceRequested: freeSpaceSheet.openFor(App.details.hash)
            // C17 D4: Settings › Servers, on this server, where its path maps are.
            onPathMapsRequested: settingsModal.showServer(App.serverId)
            // docs/32 F1–F3: the failure block, on the torrent it is about. `m` tries the
            // move again with the destination already in the sheet.
            onFailureActionRequested: action => {
                const hash = root.cursorHash;
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
            // Board 45 B1 (C18 D7): the last move's block.
            onMoveBackRequested: (id, only) => moveSheet.openBack(id, only)
            onMoveShowRequested: hashes => root.showMoveHashes(hashes, false)

            Rectangle {
                anchors.fill: parent
                color: Theme.fill(Theme.fillIdle)
                visible: App.surfaceTint > 0
                z: -1
            }
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
            // In the room the rail leaves (docs/33 E1).
            anchors.horizontalCenterOffset: rail.visible ? rail.width / 2 : 0
            multiServer: root.multiServer
            objectName: "emptyState"
            otherCount: emptyState.other.count !== undefined ? emptyState.other.count : -1
            otherName: emptyState.other.name || ""
            serverName: App.serverName
            // docs/33 D7: the same place for a server lost (or refused) before it gave a list.
            variant: !root.noList ? "empty" : App.serverRefused ? "refused" : "noList"
            visible: (root.libraryEmpty || root.noList) && !root.firstRun && !root.logShown // T4.6: first run replaces this state
            width: Math.min(parent.width, Theme.space(520))

            onAddRequested: App.addRequested()
            onHelpRequested: root.helpOpen = true
            onSettingsRequested: App.openSettings()
            onSwitchRequested: root.openServerPalette()
        }

        LoadingState {
            anchors.left: rail.visible ? rail.right : parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.top: parent.top
            anchors.topMargin: Theme.space(12)
            receivedText: App.sizeText(App.globalState.dlInfoData)
            torrentCount: App.torrents.count
            visible: root.firstSync && !root.logShown
        }
    }

    // docs/35 N8: an action's failure, its details docked above the footer over the table's
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

        anchors.bottom: footer.top
        // DECISIONS C18 D2 (changes docs/35 N8): a table action's details open above the
        // strip, the same band one line higher.
        anchors.bottomMargin: root.footerFailure.fromStrip === true && outcomeStrip.visible
                              ? root._outcomeBandLift : 0
        anchors.left: parent.left
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

    // footer (spec `.foot`)
    HintModel {
        id: hintModel

        connected: !root.serverLost
        refused: App.serverRefused
        serverCount: App.serverCount
        detailsFolded: split.folded
        detailsTab: details.currentTab
        filterText: App.filtered.text
        filteredCount: App.filtered.count
        focusArea: root.focusArea
        logFollowing: logView.following
        logMarked: App.logLines.markedCount
        // T4.8, batch-2 D3: while an overlay is up the footer keeps its facts and teaches
        // nothing; the overlay's own hint row is the only one.
        overlayOpen: root.overlayOpen || details.menuOpen
        // Fig. 1 keeps the table's line while row 3 is being renamed, because its cursor is
        // elsewhere; a rename the user started is always on the cursor row.
        renaming: table.renaming && table.renameIndex === table.cursorIndex
        torrentCount: App.torrents.count
    }

    Item {
        id: footer

        visible: !root.firstRun
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(Theme.font.baseSize * 2.2)

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space(16)
            anchors.rightMargin: Theme.space(16)
            spacing: Theme.space(16)

            Row {
                id: facts

                // #79: the status line, one label in whole words ("16 torrents, 7 downloading,
                // 1.80 TB free"); the texts it is drawn from publish nothing themselves.
                Accessible.name: root.factsSpoken
                Accessible.role: Accessible.StaticText
                Layout.alignment: Qt.AlignVCenter
                objectName: "footerFacts"
                spacing: Theme.space(10)

                Repeater {
                    model: root.factParts

                    Row {
                        id: factRow

                        required property int index
                        required property var modelData

                        spacing: Theme.space(10)

                        Text {
                            color: Theme.dimForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            text: "·"
                            visible: factRow.index > 0
                        }

                        Row {
                            spacing: Theme.space(4)

                            Text {
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                text: factRow.modelData.value
                                visible: factRow.modelData.value.length > 0
                            }

                            Text {
                                color: Theme.dimForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.small
                                text: factRow.modelData.label
                            }
                        }
                    }
                }
            }

            // The note sits in the footer's own line, after the facts (docs/DECISIONS.md M16): it
            // used to float above the footer and covered whatever was there, the details
            // pane's first column included. The hints after it drop what no longer fits.
            // docs/31 F1: the note after A leads with the set's mark, ▲ for burst in green and
            // ▼ for turtle in yellow, and reads in the foreground.
            Label {
                Layout.alignment: Qt.AlignVCenter
                color: root.footerNoteMark === "▲" || root.footerNoteMark === "✓" ? Theme.green
                                                                                   : root.footerNoteMark === "✕"
                                                                                     ? Theme.red : Theme.yellow
                objectName: "footerNoteMark"
                size: "caption"
                text: root.footerNoteMark
                visible: root.footerNote.length > 0 && root.footerNoteMark.length > 0
            }

            Label {
                id: footerNoteText

                // #79: the note comes on its own ("copied", a failure, the set after A): an
                // alert by its words. Its ▲ ▼ mark stays out; the words name the set.
                Accessible.name: text
                Accessible.role: Accessible.AlertMessage
                Layout.alignment: Qt.AlignVCenter
                // docs/32 N1: a failure the log told of is yellow, as its block is.
                color: root.footerNoteWarn ? Theme.yellow : root.footerNoteSticky ? Theme.foreground
                                                                                  : Theme.accent
                objectName: "footerNote"
                size: "small"
                text: root.footerNote
                visible: root.footerNote.length > 0
            }

            // docs/35 N8: the sentence's "details" open the band above the footer.
            Label {
                id: footerDetailsLink

                /// Open or close the band: a click, or a screen reader's press.
                function toggle(): void {
                    root.footerNoticeOpen = !root.footerNoticeOpen;
                }

                // #77: a "details" button; its press is the click.
                Accessible.name: qsTr("details")
                Accessible.role: Accessible.Button
                Accessible.onPressAction: if (footerDetailsLink.enabled)
                                              footerDetailsLink.toggle()
                Layout.alignment: Qt.AlignVCenter
                color: footerDetailsMouse.containsMouse ? Theme.accent : Theme.dimForeground
                objectName: "footerFailureDetails"
                size: "small"
                text: qsTr("details") + " " + (root.footerNoticeOpen ? Glyphs.chevronDown : Glyphs.chevronRight)
                visible: root.footerNote.length > 0 && root.footerFailure.headline !== undefined

                MouseArea {
                    id: footerDetailsMouse

                    anchors.fill: parent
                    anchors.margins: -Theme.space(4)
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: footerDetailsLink.toggle()
                }
            }

            // docs/32 N1: "· i details", while the pane that holds the block is hidden.
            Item {
                id: footerNoteHint

                // #77: it shows while the pane is hidden: a "show details" button with its
                // key; its press is the click.
                Accessible.description: qsTr("shortcut %1").arg("i")
                Accessible.name: qsTr("show details")
                Accessible.role: Accessible.Button
                Accessible.onPressAction: if (footerNoteHint.enabled)
                                              root.toggleDetails()
                Layout.alignment: Qt.AlignVCenter
                implicitHeight: noteHintRow.implicitHeight
                implicitWidth: noteHintRow.implicitWidth
                objectName: "footerNoteHint"
                visible: root.footerNote.length > 0 && root.footerNoteHint && details.collapsed

                Row {
                    id: noteHintRow

                    spacing: Theme.space(6)

                    Label {
                        dim: true
                        size: "small"
                        text: "·"
                    }

                    Label {
                        color: noteHintMouse.containsMouse ? Theme.accent : Theme.foreground
                        size: "small"
                        text: "i"
                    }

                    Label {
                        color: noteHintMouse.containsMouse ? Theme.accent : Theme.dimForeground
                        size: "small"
                        text: qsTr("details")
                    }
                }

                MouseArea {
                    id: noteHintMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: root.toggleDetails()
                }
            }

            // docs/32 D12 (L1, L2): whether the log follows new lines, on its face only.
            Label {
                // #79: the log's state by its word, without the ● or ○.
                Accessible.name: logView.following ? qsTr("following") : qsTr("paused")
                Accessible.role: Accessible.StaticText
                Layout.alignment: Qt.AlignVCenter
                color: logView.following ? Theme.green : Theme.dimForeground
                objectName: "logFollowState"
                size: "small"
                text: logView.following ? qsTr("● following") : qsTr("○ paused")
                visible: root.logShown && !root.overlayOpen
            }

            KeyHints {
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                // Given only what the facts leave, so it can drop the hints that don't fit
                // (docs/10 §3); a layout never sizes an item under its minimum otherwise.
                Layout.minimumWidth: 0
                model: hintModel.hints
                pinned: hintModel.pinned
                pinnedLead: hintModel.pinnedLead
            }
        }
    }

    // the `?` sheet (spec §7 Fig. 26: 760 wide, no buttons — a sheet may have none)
    Sheet {
        id: helpSheetPanel

        anchors.fill: parent
        glyph: Glyphs.help
        sheetWidth: Theme.space(760)
        // docs/30 D10: the sheet names the palette that runs them, on a line of its own: the
        // head's one line at 760 cut it off (#66).
        subtitle: qsTr("single letters act on the table \u00b7 capitals are the stronger variant \u00b7 ctrl is app-level\n: runs any of them by name")
        subtitleWraps: true
        title: qsTr("Shortcuts")
        z: 10

        onRejected: root.helpOpen = false

        hints: [
            {
                "key": "↑↓",
                "label": qsTr("move")
            },
            {
                "key": "enter",
                "label": qsTr("run")
            },
            {
                "key": "esc",
                "label": qsTr("clear, then close")
            }
        ]

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
        const top = outcomeStrip.mapToItem(footer.parent, 0, 0).y;
        root._outcomeBandLift = Math.max(0, footer.y - top + Theme.space(6));
        root.footerNoticeOpen = true;
    }

    /// The strip's "show" (O1, O3): the cursor on the one torrent, or the several selected.
    /// A move that took them out of the rail's folder switches the folder to their new
    /// place, as the jump palette's pick shows a torrent wherever it is.
    function showOutcome(hashes: var): void {
        const present = hashes.filter(hash => App.torrentRow(hash).name !== undefined);
        if (present.length === 0)
            return;
        root.setView("library");
        root.leaveMoveShow();
        const first = present[0];
        if (App.filtered.rowOfHash(first) < 0 && App.filtered.folder.length > 0)
            App.filtered.folder = App.torrentRow(first).savePath || "";
        if (present.length === 1) {
            root.showTorrent(first);
            return;
        }
        if (table.revealHash(first))
            table.selectHashes(present);
        root.focusHome();
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

    /// A, the hero's chip and the palette's row.
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

    MoreMenu {
        id: moreMenu

        anchors.fill: parent
        // docs/10 §2: once the hero is too narrow for them, settings and help are here.
        altKind: App.altSpeed.kind
        appRows: hero.compact
        railHidden: App.railHidden
        z: 9

        onActivated: action => root.moreAction(action)
    }

    FilterMenu {
        id: filterMenu

        anchors.fill: parent
        countText: hero.searchCountText
        filters: root.activeFilterChips
        text: App.filtered.text
        z: 9

        onAllCleared: root.clearAllFilters()
        onFilterCleared: key => root.clearOneFilter(key)
        // The hero's field stays the one source: it writes the model and keeps the text for
        // when the window widens again.
        onTextEdited: text => hero.searchField.text = text
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
        items: [
            {
                "text": qsTr("move this folder…"),
                "key": "M",
                "action": "moveFolder"
            }
        ]
        menuWidth: Theme.space(240)
        objectName: "folderMenu"
        z: 9

        onActivated: action => {
            if (action === "moveFolder")
                root.openMoveFolder(folderMenuPanel.folder, "");
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

        onAddAgainRequested: (hash, name, files) => addAgainConfirm.openFor(hash, name, files)
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
            const kept = outcome.kept > 0 ? " · " + qsTr("%n stay", "", outcome.kept) : "";
            root.flashFooterNote(qsTr("freed %1 · %n skipped files deleted", "", outcome.deleted).arg(
                                     outcome.freedText) + kept + (outcome.rechecked ? " · " + qsTr("rechecking") : ""));
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

    /// `ctrl+s`, the hero's server line, "switch server…", the rail's "… N more", the add
    /// sheet's chip. With one server there is nothing to switch to: the footer says so (D19).
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
    }

    Connections {
        function onSortOrderChanged() {
            root.saveTableState();
        }

        function onSortRoleChanged() {
            root.saveTableState();
        }

        // A filter cleared from elsewhere (the jump palette, a notice's "show") empties the
        // hero's field too, which would otherwise keep a word that no longer filters.
        // Only the emptying: the model trims, so mirroring every change would eat a space
        // the user is still typing.
        function onTextChanged() {
            if (App.filtered.text.length === 0 && hero.searchField.text.trim().length > 0)
                hero.searchField.text = "";
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
    // Every source of an add ends up in one sheet (spec §3 Fig. 5): `a` and the hero's `+`
    // arrive as `App.addRequested`, a drop as `Main.qml`'s `addDropped`, the browser's
    // `magnet:`/`.torrent` hand-off as `App.addUrisRequested`. An infohash the library
    // already holds opens Fig. 10 instead of being sent.
    AddSheet {
        id: addSheet

        anchors.fill: parent
        z: 12

        onAddRequested: request => App.addTorrent(request)
        onDuplicateFound: (duplicate, source) => duplicateSheet.openFor(duplicate, source)
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
    // hero's gear, the tray's `settings…` and the disconnected banner all arrive through
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

    // The Appearance pane's transparency override; 0 keeps the shell's `[launcher]
    // background-alpha` (docs/DECISIONS.md Q1), which a theme change can move under us.
    Binding {
        property: "windowAlpha"
        target: Theme
        value: App.windowAlphaOverride > 0 ? App.windowAlphaOverride / 100 : Theme.defaultWindowAlpha
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
