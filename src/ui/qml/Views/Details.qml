pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The details pane (spec v2 §1 Fig. 1's right column and §2 Fig. 2-4): head, four-up strip,
// tab strip, the current tab's body, and the tab's own key hints along the foot.
//
// It fills whatever size it is given; the responsive move (docked right above 1100 px,
// below the table under it) is SplitLayout's job in Main. `collapsed` is the `i` key: the
// pane goes to zero width and stops emitting anything.
//
// Everything on screen comes from two properties: `details` (the controller and its three
// models) and `torrent` (the table's cursor row, as a plain object with the roles named
// below). Most actions leave as signals for MainView; the Files tab's priority and rename,
// and General's option rows, call App directly.
Item {
    id: root

    required property TorrentDetails details

    /// The cursor row of the table. Expected fields: name, category, tagsText, addedOnText,
    /// sizeText, progressText, ratioText, etaLongText, stateText, stateRole,
    /// hasTrackerError. Null means "nothing selected".
    property var torrent: null

    /// 0 General, 1 Files, 2 Trackers, 3 Peers — the `1`-`4` keys — and 4 HTTP sources
    /// (#10), `5`, only while the torrent has web seeds.
    property int currentTab: 0
    /// `i` in Main: the pane folds away entirely.
    property bool collapsed: false

    /// The pane's own left border. Inside the main window `SplitLayout` already draws the
    /// divider between the table and the pane, so Main turns this off to avoid a 2 px rule.
    property bool showEdge: true
    /// The tab's key hints along the pane's foot (Fig. 2-4). The main window teaches every
    /// tab from its own footer (`HintModel`), so Main turns this off too; the standalone
    /// figures keep it.
    property bool showHints: true
    /// docs/10 §5, reworked by docs/13: under the table the pane is wide and short, so the
    /// head and the strip share one band across the top (name on the left, the four facts on
    /// the right) and the tabs get the whole width below it, General's rows in two columns.
    property bool wide: false
    /// #65: General's "Auto manage" row raises `autoManagementRequested` instead of applying,
    /// so Main can ask first when turning it on moves files another torrent uses.
    property bool asksBeforeAutoManagement: false
    /// docs/13: the strip's width in the band. Fixed, so the name does not shift as the ETA
    /// changes length; never more than half the pane, so the name keeps room.
    readonly property int bandStripWidth: Math.min(Theme.space(440), Math.floor(root.width / 2))
    /// The head and the strip share one line: `wide`, and at least 700 px at base 14
    /// (docs/15 Q2 A). Narrower, half the pane left every fact eliding, so the band stops
    /// sharing its line: the name on top, the four facts across the whole width under it,
    /// then the tabs, as when the pane is docked at the side.
    readonly property bool band: root.wide && root.width >= Theme.space(600)

    /// Where the cursor sits inside the current tab's list; General's rows are a list too (G5).
    readonly property int cursorIndex: root.currentTab === 1 ? root.filesCursor : root.currentTab
                                                               === 2 ? root.trackersCursor :
                                                                       root.currentTab === 3
                                                                       ? root.peersCursor : root.currentTab === 4
                                                                         ? root.webSeedsCursor : root.generalCursor
    readonly property int rowCount: root.currentTab === 1 ? filesTab.count : root.currentTab === 2 ? trackersTab.count :
                                                                                                     root.currentTab
                                                                                                     === 3 ? peersTab.count :
                                                                                                             root.currentTab === 4
                                                                                                             ? webSeedsTab.count :
                                                                                                               generalTab.count
    /// #10: the HTTP sources tab exists while the torrent has web seeds, and also while the
    /// next torrent's list is still on its way and that tab is the one on show, so moving the
    /// cursor between two web-seeded torrents does not drop it (the model clears on every
    /// selection change; `loaded` says when it has answered).
    readonly property bool hasWebSeeds: root.details.webSeeds.count > 0
                                        || (!root.details.webSeeds.loaded && root.currentTab === 4)

    // One cursor per list, so switching tabs and coming back keeps the place.
    property int filesCursor: 0
    property int trackersCursor: 0
    property int peersCursor: 0
    property int webSeedsCursor: 0
    /// Every poll re-sorts the peers by speed, so the cursor also holds its peer's address
    /// and follows it to the new row: `y` copies, and `b` bans, the peer it was left on.
    property string peersCursorAddress: ""
    property int generalCursor: 0
    /// The file the Files cursor is on, and the one being renamed. A sync that re-sorts the
    /// tab (by progress, remaining, availability) rebuilds the rows, so the index alone would
    /// land on another file; these find the row again (as the table follows its hash).
    property string filesCursorPath: ""
    property string _renamePath: ""

    onFilesCursorChanged: root.filesCursorPath = root.details.files.pathAt(root.filesCursor)

    /// `o` on a file row: the path inside the torrent, for Xdg to reveal.
    signal openFolderRequested(string path)
    /// `O`, or a double click on a file: the path inside the torrent, to open.
    signal openFileRequested(string path)
    /// `h` on anything that is not an open folder hands the focus back to the table.
    signal focusTable
    /// `a` add, `d` remove, `R` reannounce. `urls`: what `d` removes, the tab's selection or
    /// else the cursor's row (#51 D15), never DHT, PeX or LSD; empty for add and reannounce.
    signal trackerAction(string action, var urls)
    /// `b` ban, `a` add peer. `addresses`: the `ip:port` peers `b` bans, the selection or
    /// else the cursor's row; empty for add.
    signal peerAction(string action, var addresses)
    /// The HTTP sources tab (#10): `a` add, `d` remove, as the Trackers tab.
    signal webSeedAction(string action, var urls)
    /// `y`: the text the caller should put on the clipboard.
    /// #11: `r` on two or more selected files, or the file menu's "rename files…": the files
    /// to rename (TorrentFilesModel::renameTargets()) and whether they are the selection.
    signal renameFilesRequested(var paths, bool fromSelection)
    signal copyRequested(string text)
    /// docs/28: an action of General's error block on this torrent: "location", "start",
    /// "recheck" (MainView runs them); "rename" and "copy" are handled here.
    signal problemActionRequested(string action)
    /// docs/32 (#55): an action of General's failure block on this torrent: "location",
    /// "folder", "dismiss" (MainView runs them); "rename" and "copy" are handled here. `x` on
    /// General sends "dismiss" too.
    signal failureActionRequested(string action)
    /// Board 45 B1 (C18 D7): General's last-move block wants a move back of finished move `id`,
    /// `only` this torrent when given (MainView opens the Move sheet).
    signal moveBackRequested(string id, string only)
    /// Board 45 B1: "show the move", its torrents in the table.
    signal moveShowRequested(var hashes)
    /// A few words for the window's footer note ("one file, nothing to filter").
    signal noteRequested(string text)
    /// Board 44 F1 (C17 D3): the Files tab's "Free the space…", or shift+f there.
    signal freeSpaceRequested
    /// C17 D4: the hint's "Path maps…".
    signal pathMapsRequested

    /// `y` or a click on a General row (docs/12 G5): the whole value, and the row's name for
    /// the footer's "copied hash v1".
    signal valueCopyRequested(string text, string name)
    /// #65: `enter` on General's "Auto manage" row, when `asksBeforeAutoManagement`.
    signal autoManagementRequested(var hashes, bool on)

    /// Something inside the pane holds the keyboard — the Files tab's filter, its inline
    /// rename or one of its menus — so Main still counts the focus as the pane's.
    readonly property bool editing: filesTab.filterEditing || filesTab.renamingRow >= 0
                                    || root.menuOpen
    /// The caret is in the Files tab's filter field (Main's footer teaches enter/esc then).
    readonly property bool filterEditing: filesTab.filterEditing
    /// One of the pane's menus is up; Main's footer then keeps only its facts (D3).
    readonly property bool menuOpen: fileMenu.opened || priorityMenu.opened || sortMenu.opened
                                     || trackerMenu.opened || peerMenu.opened || webSeedMenu.opened
    /// The Files tab's range anchor: `v` and shift+click select from it to the cursor. The
    /// last row `space`, a plain click or a ctrl+click left it on, by path, so a fold, a sort
    /// or a filter that rebuilds the rows does not move it onto another file. Empty, or
    /// folded out of sight, the cursor stands in for it.
    property string filesAnchorPath: ""

    /// The Trackers, Peers and HTTP sources selections (#51 D15), by the click rules and
    /// the Files tab's keys (`space`, `v`, `ctrl+a`, `esc`). Kept by URL or address, since
    /// the peers move on every poll, and let go on another torrent. DHT, PeX and LSD are
    /// rows of the Trackers list, not trackers of the torrent: they never join it.
    readonly property DetailsSelection trackersSelection: DetailsSelection {
        keyAt: row => root.details.trackers.urlAt(row)
        rowCount: root.details.trackers.count
        selectable: row => !root.details.trackers.urlAt(row).startsWith("** [")
    }
    readonly property DetailsSelection peersSelection: DetailsSelection {
        keyAt: row => root.details.peers.addressAt(row)
        rowCount: root.details.peers.count
    }
    readonly property DetailsSelection webSeedsSelection: DetailsSelection {
        keyAt: row => root.details.webSeeds.urlAt(row)
        rowCount: root.details.webSeeds.count
    }

    readonly property bool hasTrackerError: (root.torrent && root.torrent.hasTrackerError === true)
                                            || root.details.hasTrackerError

    // --- the API Main drives -------------------------------------------------------------

    /// Moves the cursor inside the current tab's list, clamped to its ends.
    function moveCursor(delta: int) {
        if (root.rowCount <= 0)
            return;
        const next = Math.max(0, Math.min(root.rowCount - 1, root.cursorIndex + delta));
        root._setCursor(next);
    }

    /// `1`-`4`, and `5` for HTTP sources while there are any. Tells App whether the Peers tab
    /// is the one on show, so sync/torrentPeers is only polled while somebody is looking at it.
    function activateTab(index: int) {
        if (index < 0 || index > 4 || (index === 4 && !root.hasWebSeeds))
            return;
        root.currentTab = index;
        // Tabs writes its own currentIndex on a click, so the binding is set from here
        // rather than declared, and the keys and the mouse cannot drift apart.
        tabs.currentIndex = index;
    }

    /// docs/32 F2: the Files tab, with the cursor on `path` (a path inside the torrent). The
    /// list may still be on its way: the cursor's path is set either way, and the rows find
    /// it when they come (the Files model's reset follows `filesCursorPath`).
    function showFile(path: string): void {
        root.activateTab(1);
        if (path.length === 0)
            return;
        root.filesCursorPath = path;
        const row = root.details.files.rowOfPath(path);
        if (row >= 0)
            root.filesCursor = row;
    }

    /// docs/32 D3: `x` on General, or the block's "dismiss": the failure goes for good.
    function dismissFailure(): bool {
        if (root.currentTab !== 0 || !generalTab._hasFailure)
            return false;
        root.failureActionRequested("dismiss");
        return true;
    }

    /// `l` on a folder row of the Files tab.
    function unfold() {
        if (root.currentTab === 1 && root.details.files.isFolder(root.filesCursor))
            root.details.files.setExpanded(root.filesCursor, true);
    }

    /// `h`: closes an open folder, and otherwise gives the focus back to the table.
    function fold() {
        if (root.currentTab === 1 && root.details.files.isFolder(root.filesCursor)
                && root.details.files.isExpanded(root.filesCursor)) {
            root.details.files.setExpanded(root.filesCursor, false);
            return;
        }
        root.focusTable();
    }

    /// `+`, `-`, `x`: "raise", "lower" or "skip" on the Files tab's selection, or on its
    /// cursor row when nothing is selected. A mixed set raises to high and lowers to normal.
    function setPriority(name: string) {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const files = root.details.files;
        const current = files.targetPriority(root.filesCursor);
        const wanted = name === "skip" ? 0 : name === "raise" ? files.raisedPriority(current) :
                                                                files.loweredPriority(current);
        root.applyPriority(wanted);
    }

    /// `x` (#42, DECISIONS C19 T1): skips the selection, or the cursor row when nothing is
    /// selected, mixed ones included; when none of those files is wanted, ticks back what was
    /// wanted before (App.toggleFilesWanted).
    function toggleSkip() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const ids = root.details.files.targetFileIndexes(root.filesCursor);
        if (ids.length > 0)
            App.toggleFilesWanted(root.details.hash, ids);
    }

    /// The priority menu's rows: one API priority (0 skip, 1 normal, 6 high, 7 maximal) on the
    /// selection or the cursor row.
    function applyPriority(priority: int) {
        const ids = root.details.files.targetFileIndexes(root.filesCursor);
        if (ids.length > 0)
            App.setFilePriority(root.details.hash, ids, priority);
    }

    /// qBittorrent's "by shown file order": the first third maximal, the next high, the rest
    /// normal, in the order the list shows them.
    function applyShownOrder() {
        App.setFilePriorityGroups(root.details.hash,
                                  root.details.files.shownOrderGroups(root.filesCursor));
    }

    /// `y` on the current row: the announce URL, the peer address, the web seed, or the file
    /// path. With rows selected in Trackers, Peers or HTTP sources, all of theirs, one a line.
    function copyCurrent() {
        const selection = root._listSelection();
        if (selection !== null && selection.count > 0)
            root.copyRequested(selection.targets(root.cursorIndex).join("\n"));
        else if (root.currentTab === 2)
            root.copyRequested(root.details.trackers.urlAt(root.trackersCursor));
        else if (root.currentTab === 4)
            root.copyRequested(root.details.webSeeds.urlAt(root.webSeedsCursor));
        else if (root.currentTab === 3)
            root.copyRequested(root.details.peers.addressAt(root.peersCursor));
        else if (root.currentTab === 1 && root.details.files.count > 0)
            root.copyRequested(root.details.files.pathAt(root.filesCursor));
        else if (root.currentTab === 0)
            root.copyGeneralRow(root.generalCursor);
    }

    // --- the General tab (docs/12 G5) ------------------------------------------------------

    /// `y`, or a click on a General row: its whole value (the full magnet or hash, not the
    /// shortened text), named for the footer.
    function copyGeneralRow(row: int) {
        const entry = generalTab.rows[row];
        if (entry && entry.copy.length > 0)
            root.valueCopyRequested(entry.copy, entry.name);
    }

    /// `enter`, or a click, on one of General's option rows (docs/12 G4): turns it the other
    /// way for this torrent. False when the row is not an option.
    function toggleGeneralOption(row: int): bool {
        const entry = generalTab.rows[row];
        if (!entry || !entry.option || root.details.hash.length === 0)
            return false;
        const hashes = [root.details.hash];
        switch (entry.option) {
        case "sequential":
            App.setSequentialDownload(hashes, !entry.on);
            break;
        case "firstLast":
            App.setFirstLastPiecePrio(hashes, !entry.on);
            break;
        case "superSeeding":
            App.setSuperSeeding(hashes, !entry.on);
            break;
        case "autoManagement":
            if (root.asksBeforeAutoManagement)
                root.autoManagementRequested(hashes, !entry.on);
            else
                App.setAutoManagement(hashes, !entry.on);
            break;
        }
        return true;
    }

    /// `Y` on a file row: where the file is on this machine, through the path map.
    function copyLocalPath() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const local = App.torrentFileLocalPath(root.details.hash,
                                               root.details.files.pathAt(root.filesCursor));
        if (local.length > 0)
            root.copyRequested(local);
    }

    /// `n` on a file row: the name alone.
    function copyName() {
        if (root.currentTab === 1 && root.details.files.count > 0)
            root.copyRequested(root.details.files.nameAt(root.filesCursor));
    }

    /// `space`: select or unselect the Files tab's cursor row.
    function toggleFileSelection() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        root.details.files.toggleSelected(root.filesCursor);
        root.filesAnchorPath = root.details.files.pathAt(root.filesCursor);
    }

    /// `v`: add every row from the anchor to the cursor to the selection.
    function selectFileRange() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        root.details.files.selectRange(root.filesAnchorRow(), root.filesCursor);
    }

    /// The anchor's row now, or the cursor's when there is none on show.
    function filesAnchorRow(): int {
        const row = root.filesAnchorPath.length > 0 ? root.details.files.rowOfPath(root.filesAnchorPath)
                                                    : -1;
        return row >= 0 ? row : root.filesCursor;
    }

    /// A click on Files row `row`, by the pane's click rules: a plain click puts the cursor
    /// and the anchor there and lets the selection go (on a folder too: its arrow or a double
    /// click folds it, #51 D5); ctrl+click, or a middle click, toggles the row; shift+click
    /// makes the selection exactly anchor..row, and ctrl+shift+click adds that range. Shift
    /// leaves the anchor where it was.
    function clickFile(row: int, modifiers: int): void {
        const files = root.details.files;
        if (row < 0 || row >= files.count)
            return;
        const ctrl = (modifiers & Qt.ControlModifier) !== 0;
        if (modifiers & Qt.ShiftModifier) {
            // Read before the cursor moves: with no anchor, the cursor it had stands in.
            const anchor = root.filesAnchorRow();
            if (!ctrl)
                files.clearSelection();
            files.selectRange(anchor, row);
            root.filesAnchorPath = files.pathAt(anchor);
            root.filesCursor = row;
            return;
        }
        root.filesCursor = row;
        root.filesAnchorPath = files.pathAt(row);
        if (ctrl) {
            files.toggleSelected(row);
            return;
        }
        files.clearSelection();
    }

    /// A click on a folder's fold arrow, or a double click on it: the cursor goes there and
    /// the folder folds or opens. The selection stays as it was.
    function foldFileRow(row: int): void {
        const files = root.details.files;
        if (row < 0 || row >= files.count || !files.isFolder(row))
            return;
        root.filesCursor = row;
        files.toggle(row);
    }

    /// A right click on Files row `row`: a row outside the selection becomes the menu's
    /// target alone (the selection goes, the cursor and anchor move there); a row inside it
    /// keeps the selection, so the menu's priority and rename still act on all of it.
    function fileMenuAt(row: int, x: real, y: real): void {
        const files = root.details.files;
        if (row < 0 || row >= files.count)
            return;
        if (!files.isSelected(row)) {
            files.clearSelection();
            root.filesAnchorPath = files.pathAt(row);
        }
        root.filesCursor = row;
        root.openFileMenu(x, y);
    }

    /// `ctrl+a` (docs/12 F3): every file the filter shows, so `p` / `+` `-` `x` then act on
    /// all of them; with no filter, every file.
    function selectAllFiles() {
        if (root.currentTab === 1)
            root.details.files.selectAllFiles();
    }

    /// `r` on a file or folder row: the inline editor, on the leaf name.
    function startRename() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        // docs/18 A1: several files selected, so `r` means all of them.
        if (root.details.files.selectedFileCount() >= 2) {
            root.renameFilesRequested(root.details.files.renameTargets(), true);
            return;
        }
        root._renamePath = root.details.files.pathAt(root.filesCursor);
        filesTab.renameText = root.details.files.nameAt(root.filesCursor);
        filesTab.renameFresh = true;
        filesTab.renamingRow = root.filesCursor;
    }

    function commitRename(text: string) {
        root._endRename(text, true);
    }

    /// Closes the inline rename and sends `text` as the new name. `refocus` hands the keys
    /// back to the pane, as `enter` in the editor does; a rename committed because the focus
    /// went elsewhere (a click on the table) leaves it there.
    function _endRename(text: string, refocus: bool) {
        const open = filesTab.renamingRow >= 0;
        const path = root._renamePath;
        filesTab.renamingRow = -1;
        root._renamePath = "";
        if (refocus)
            root.forceActiveFocus();
        // By the path the rename started on: a re-sort may have moved the rows since, so
        // the name and the file-or-folder kind come from the row that path is on now.
        const row = path.length > 0 ? root.details.files.rowOfPath(path) : -1;
        if (!open || row < 0 || text.length === 0
                || text === root.details.files.nameAt(row))
            return;
        App.renameTorrentFile(root.details.hash, path, text, root.details.files.isFolder(row));
    }

    /// The rows moved or were rebuilt: an open rename follows its file, or is dropped when
    /// that file is gone.
    function _followRename() {
        if (filesTab.renamingRow < 0)
            return;
        const row = root.details.files.rowOfPath(root._renamePath);
        if (row < 0) {
            root.cancelRename();
        } else if (row !== filesTab.renamingRow) {
            filesTab.renamingRow = row;
            Qt.callLater(filesTab.focusRenameEditor);
        }
    }

    /// A click in the pane gives it the keyboard, as `l` from the table does: without it a
    /// click on a file left the keys with the table, and `ctrl+a` selected every torrent.
    /// A click away from an open inline rename commits it first, as a file manager does.
    function takeKeys() {
        if (filesTab.renamingRow >= 0)
            root.commitRename(filesTab.renameText);
        if (!root.activeFocus)
            root.forceActiveFocus();
    }

    /// takeKeys() for a click on Files row `row`. The rename it may commit can rebuild the
    /// rows (a local rename, a sort by name), so the clicked row is found again by its path.
    function _takeKeysAt(row: int): int {
        const path = root.details.files.pathAt(row);
        root.takeKeys();
        const now = path.length > 0 ? root.details.files.rowOfPath(path) : -1;
        return now >= 0 ? now : row;
    }

    /// The window's keyboard moved while a rename was open. Anywhere but the editor, it
    /// commits: a click on the table, the filter, another tab. Not when it went nowhere (the
    /// window lost the keyboard to another app, and the rename waits for it to come back), nor
    /// to the list or the window's root, where a row rebuilt by a reset or a scroll leaves it
    /// for a moment before `_followRename` gives it back to the new editor.
    function _focusMoved(): void {
        if (filesTab.renamingRow < 0)
            return;
        const item = root.Window.activeFocusItem;
        if (item === null || item === root.Window.contentItem || item.objectName === "fileRenameEditor"
                || filesTab.holdsListFocus(item))
            return;
        root._endRename(filesTab.renameText, item === root);
    }

    function cancelRename() {
        if (filesTab.renamingRow < 0)
            return;
        filesTab.renamingRow = -1;
        root._renamePath = "";
        root.forceActiveFocus();
    }

    /// Sorts the Files tab and keeps the cursor on the row it was on.
    function sortFilesBy(key: string) {
        const path = root.details.files.pathAt(root.filesCursor);
        root.details.files.sortBy(key);
        root._followPath(path);
    }

    /// A click on a Files column header (#51 D14): its first direction, the other, then the
    /// torrent's own order again; the cursor stays on its row throughout.
    function cycleFilesSort(key: string): void {
        const path = root.details.files.pathAt(root.filesCursor);
        root.details.files.cycleSort(key);
        root._followPath(path);
    }

    /// `H` / `L`: fold or open every folder.
    function foldAll(folded: bool) {
        const path = root.details.files.pathAt(root.filesCursor);
        if (folded)
            root.details.files.collapseAll();
        else
            root.details.files.expandAll();
        root._followPath(path);
    }

    /// `X`, the file menu's row or a click on "N skipped" (#9): hide the skipped files, or
    /// show them again. The cursor stays on its file; on a file that just went, at the top.
    function toggleHideSkipped() {
        const files = root.details.files;
        const path = files.pathAt(root.filesCursor);
        files.hideSkipped = !files.hideSkipped;
        const row = files.rowOfPath(path);
        root._setCursor(row >= 0 ? row : 0);
    }

    /// `enter` or a right click on a Files row: its menu. `x`, `y` in the pane's coordinates;
    /// the cursor row's own place when they are left out.
    function openFileMenu(x: real, y: real) {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const at = x < 0 ? filesTab.menuPoint(root.filesCursor, menuHost) : root.mapToItem(menuHost, x, y);
        fileMenu.openAt(at.x, at.y);
    }

    /// `p` on a Files row: the priority menu.
    function openPriorityMenu() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const at = filesTab.menuPoint(root.filesCursor, menuHost);
        priorityMenu.openAt(at.x, at.y);
    }

    /// `s` in the Files tab: the sort menu, under the header.
    function openSortMenu() {
        if (root.currentTab !== 1)
            return;
        const at = filesTab.mapToItem(menuHost, Theme.space(16), Theme.font.body * 2);
        sortMenu.openAt(at.x, at.y);
    }

    /// A right click on a Trackers, Peers or HTTP sources row (the cursor already moved
    /// there): that tab's menu, at `x`, `y` in the pane's coordinates.
    function openRowMenu(x: real, y: real): void {
        const menu = root.currentTab === 2 ? trackerMenu : root.currentTab === 3
                                                           ? peerMenu : root.currentTab === 4 ? webSeedMenu : null;
        if (menu === null || root.rowCount === 0)
            return;
        const at = root.mapToItem(menuHost, x, y);
        menu.openAt(at.x, at.y);
    }

    /// A row of the Trackers, Peers or HTTP sources menu: the same as its key on the list.
    function rowMenuAction(action: string): void {
        if (action === "copy") {
            root.copyCurrent();
            return;
        }
        const selection = root._listSelection();
        if (selection === null)
            return;
        // Remove and ban act on the selection, or on the cursor's row; with neither (the
        // cursor on DHT) there is nothing to send.
        const values = action === "add" || action === "reannounce" ? [] : selection.targets(root.cursorIndex);
        if ((action === "remove" || action === "ban") && values.length === 0)
            return;
        if (root.currentTab === 2)
            root.trackerAction(action, values);
        else if (root.currentTab === 3)
            root.peerAction(action, values);
        else
            root.webSeedAction(action, values);
    }

    /// The selection of the tab on show, when it is Trackers, Peers or HTTP sources.
    function _listSelection(): DetailsSelection {
        return root.currentTab === 2 ? root.trackersSelection : root.currentTab === 3
                                                                ? root.peersSelection : root.currentTab === 4
                                                                  ? root.webSeedsSelection : null;
    }

    /// A click on Trackers, Peers or HTTP sources row `row`, by the click rules the table
    /// follows: a plain click puts the cursor there and selects that row alone; ctrl+click
    /// (or a middle click) toggles it; shift+click makes the selection anchor..row and
    /// ctrl+shift+click adds that range, leaving the anchor where it was.
    function clickListRow(row: int, modifiers: int): void {
        const selection = root._listSelection();
        if (selection === null || row < 0 || row >= root.rowCount)
            return;
        const ctrl = (modifiers & Qt.ControlModifier) !== 0;
        if (modifiers & Qt.ShiftModifier) {
            const anchor = selection.anchorRow(root.cursorIndex);
            selection.range(anchor, row, ctrl);
            selection.anchorKey = selection.keyOf(anchor);
        } else if (ctrl) {
            selection.toggle(row);
        } else {
            selection.only(row);
        }
        root._setCursor(row);
    }

    /// A right click on one of those rows: one outside the selection becomes the menu's
    /// only target, one inside it keeps the selection; then the menu, at `x`, `y` in the
    /// pane's coordinates.
    function listMenuAt(row: int, x: real, y: real): void {
        const selection = root._listSelection();
        if (selection === null || row < 0 || row >= root.rowCount)
            return;
        if (!selection.has(selection.keyOf(row)))
            selection.only(row);
        root._setCursor(row);
        root.openRowMenu(x, y);
    }

    /// `space` in those tabs: the cursor's row in or out of the selection.
    function toggleListSelection(): bool {
        const selection = root._listSelection();
        if (selection === null || root.rowCount === 0)
            return false;
        selection.toggle(root.cursorIndex);
        return true;
    }

    /// `v`: the rows from the anchor to the cursor join the selection.
    function selectListRange(): bool {
        const selection = root._listSelection();
        if (selection === null || root.rowCount === 0)
            return false;
        selection.range(selection.anchorRow(root.cursorIndex), root.cursorIndex, true);
        return true;
    }

    /// `/` in the Files tab. A single file has no filter (docs/12 F2): the footer says so,
    /// so the key does not seem broken.
    function focusFileFilter() {
        if (root.currentTab === 1 && !filesTab.focusFilter())
            root.noteRequested(qsTr("one file, nothing to filter"));
    }

    /// A row of the file menu, or the same key pressed on the list.
    function fileAction(action: string) {
        const files = root.details.files;
        const path = files.pathAt(root.filesCursor);
        switch (action) {
        case "openFile":
            if (!files.isFolder(root.filesCursor))
                root.openFileRequested(path);
            break;
        case "reveal":
            root.openFolderRequested(path);
            break;
        case "priority":
            root.openPriorityMenu();
            break;
        case "rename":
            root.startRename();
            break;
        case "renameMany":
            root.renameFilesRequested(files.renameTargets(), files.selectedFileCount() > 0);
            break;
        case "copyPath":
            root.copyCurrent();
            break;
        case "copyLocalPath":
            root.copyLocalPath();
            break;
        case "copyName":
            root.copyName();
            break;
        case "expandAll":
            root.foldAll(false);
            break;
        case "collapseAll":
            root.foldAll(true);
            break;
        case "hideSkipped":
            root.toggleHideSkipped();
            break;
        case "freeSpace":
            if (filesTab.bandShown)
                root.freeSpaceRequested();
            break;
        }
    }

    function _followPath(path: string) {
        const row = root.details.files.rowOfPath(path);
        if (row >= 0)
            root._setCursor(row);
    }

    function _filesKey(event: var): bool {
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
        switch (event.key) {
        case Qt.Key_Slash:
            root.focusFileFilter();
            return true;
        case Qt.Key_S:
            root.openSortMenu();
            return true;
        case Qt.Key_P:
            root.openPriorityMenu();
            return true;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.openFileMenu(-1, -1);
            return true;
        case Qt.Key_Space:
            root.toggleFileSelection();
            return true;
        case Qt.Key_V:
            root.selectFileRange();
            return true;
        case Qt.Key_R:
            root.startRename();
            return true;
        case Qt.Key_N:
            root.copyName();
            return true;
        case Qt.Key_O:
            if (root.details.files.count > 0)
                root.fileAction(shift ? "openFile" : "reveal");
            return true;
        case Qt.Key_Y:
            if (shift)
                root.copyLocalPath();
            else
                root.copyCurrent();
            return true;
        case Qt.Key_H:
            if (!shift)
                return false;
            root.foldAll(true);
            return true;
        case Qt.Key_L:
            if (!shift)
                return false;
            root.foldAll(false);
            return true;
        case Qt.Key_X:
            if (!shift)
                return false;
            root.toggleHideSkipped();
            return true;
        case Qt.Key_F:
            // Board 44 F1: shift+f frees what skipped files hold, while the band shows.
            if (!shift || !filesTab.bandShown)
                return false;
            root.freeSpaceRequested();
            return true;
        case Qt.Key_Escape:
            if (root.details.files.selectedCount > 0) {
                root.details.files.clearSelection();
                root.filesAnchorPath = "";
                return true;
            }
            if (root.details.files.filterText.length > 0) {
                root.details.files.filterText = "";
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    function _setCursor(row: int) {
        if (root.currentTab === 1) {
            root.filesCursor = row;
            filesTab.ensureVisible();
        } else if (root.currentTab === 2) {
            root.trackersCursor = row;
            trackersTab.ensureVisible();
        } else if (root.currentTab === 3) {
            root.peersCursor = row;
            peersTab.ensureVisible();
        } else if (root.currentTab === 4) {
            root.webSeedsCursor = row;
            webSeedsTab.ensureVisible();
        } else if (root.currentTab === 0) {
            root.generalCursor = row;
            generalTab.ensureVisible();
        }
    }

    // --- layout --------------------------------------------------------------------------

    implicitWidth: root.collapsed ? 0 : Theme.space(340)
    implicitHeight: Theme.space(600)
    visible: !root.collapsed

    Keys.onPressed: event => {
        // A menu of the pane is open: what it did not take stops here, so a letter it has no
        // row for cannot run the list's own key underneath it.
        if (root.menuOpen && (event.modifiers & Qt.ControlModifier) === 0) {
            event.accepted = true;
            return;
        }
        // The inline rename's editor keeps every key it does not use itself. A key that
        // reaches the pane while the editor no longer holds the keyboard (its row scrolled
        // away) ends the rename instead, so the pane is never left deaf to its own keys.
        if (filesTab.renamingRow >= 0) {
            const focused = root.Window.activeFocusItem;
            if (focused !== null && focused.objectName === "fileRenameEditor") {
                event.accepted = true;
                return;
            }
            root.commitRename(filesTab.renameText);
        }
        // The one ctrl key of the Files tab; the rest of ctrl belongs to the window.
        if (root.currentTab === 1 && !root.menuOpen && event.key === Qt.Key_A
                && event.modifiers === Qt.ControlModifier) {
            root.selectAllFiles();
            event.accepted = true;
            return;
        }
        if (root.currentTab === 1 && (event.modifiers & Qt.ControlModifier) === 0
                && root._filesKey(event)) {
            event.accepted = true;
            return;
        }
        // #51 D15: `ctrl+a` selects every row of Trackers, Peers or HTTP sources too.
        if (root._listSelection() !== null && !root.menuOpen && event.key === Qt.Key_A
                && event.modifiers === Qt.ControlModifier) {
            root._listSelection().all();
            event.accepted = true;
            return;
        }
        // The rest of ctrl belongs to the window: `ctrl+k` is jump, not `k`, and `ctrl+b`
        // hides the rail rather than doing whatever `b` does here.
        if (event.modifiers & Qt.ControlModifier)
            return;
        switch (event.key) {
        case Qt.Key_G:
            if (root.rowCount > 0)
                root._setCursor((event.modifiers & Qt.ShiftModifier) ? root.rowCount - 1 : 0);
            break;
        case Qt.Key_1:
            root.activateTab(0);
            break;
        case Qt.Key_2:
            root.activateTab(1);
            break;
        case Qt.Key_3:
            root.activateTab(2);
            break;
        case Qt.Key_4:
            root.activateTab(3);
            break;
        // #10: `5` is the HTTP sources tab while the torrent has web seeds; otherwise it goes
        // on to the window, which picks status 5 as it does from anywhere.
        case Qt.Key_5:
            if (!root.hasWebSeeds)
                return;
            root.activateTab(4);
            break;
        case Qt.Key_J:
        case Qt.Key_Down:
            root.moveCursor(1);
            break;
        case Qt.Key_K:
        case Qt.Key_Up:
            root.moveCursor(-1);
            break;
        case Qt.Key_H:
        case Qt.Key_Left:
            root.fold();
            break;
        case Qt.Key_L:
        case Qt.Key_Right:
            root.unfold();
            break;
        case Qt.Key_Plus:
        case Qt.Key_Equal:
            root.setPriority("raise");
            break;
        case Qt.Key_Minus:
            root.setPriority("lower");
            break;
        case Qt.Key_X:
            // docs/32: on General, `x` dismisses the failure; on Files it still skips a file.
            if (!root.dismissFailure())
                root.toggleSkip();
            break;
        case Qt.Key_Y:
            root.copyCurrent();
            break;
        // #51 D15: Trackers, Peers and HTTP sources select as the Files tab does.
        case Qt.Key_Space:
            if (!root.toggleListSelection())
                return;
            break;
        case Qt.Key_V:
            if (!root.selectListRange())
                return;
            break;
        case Qt.Key_Escape:
            if (root._listSelection() === null || root._listSelection().count === 0)
                return;
            root._listSelection().clear();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            // G4: General's option rows; anything else goes on to the window.
            if (root.currentTab !== 0 || !root.toggleGeneralOption(root.generalCursor))
                return;
            break;
        // The Trackers and Peers keys (Fig. 3/4) belong to those tabs only: anywhere else the
        // key goes on to the window, so `a` still adds a torrent from General or Files.
        case Qt.Key_A:
            if (root._listSelection() === null)
                return;
            root.rowMenuAction("add");
            break;
        case Qt.Key_D:
            if (root.currentTab !== 2 && root.currentTab !== 4)
                return;
            root.rowMenuAction("remove");
            break;
        case Qt.Key_R:
            if (root.currentTab !== 2 || !(event.modifiers & Qt.ShiftModifier))
                return;
            root.rowMenuAction("reannounce");
            break;
        case Qt.Key_B:
            if (root.currentTab !== 3)
                return;
            root.rowMenuAction("ban");
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // The pane's own left edge (spec §1 `.details { border-left: 1px solid hairline }`).
    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        vertical: true
        visible: root.showEdge
    }

    Item {
        id: head

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.top: parent.top
        // Never negative: at the window's narrowest a negative width let the texts draw
        // unclipped over the rail (review RUN-6).
        width: Math.max(0, (root.band ? root.width - root.bandStripWidth : root.width) - Theme.space(32))
        height: headColumn.implicitHeight + Theme.space(12) + Theme.space(8)
        clip: true

        Column {
            id: headColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.space(12)
            spacing: Theme.space(2)

            // #79: the pane's heading is the torrent's name, whole when it is cut.
            Text {
                id: headName

                Accessible.name: text
                Accessible.role: Accessible.Heading
                color: Theme.brightForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Font.DemiBold
                objectName: "headName"
                text: root.torrent ? root.torrent.name : qsTr("no torrent selected")
                width: parent.width

                // A name the pane had to cut shows whole under it while the pointer rests.
                HoverHandler {
                    id: headNameHover

                    onHoveredChanged: {
                        if (headNameHover.hovered && headName.truncated)
                            headTip.showFor(headName, headName.text);
                        else
                            headTip.hide();
                    }
                }
            }

            // Spec §1 Fig. 1: "linux · keep · added today 10:41" — category, tags, added.
            // docs/11 J8: when it does not fit, the category and tags give way and the date
            // stays whole, so it is two texts, the first one eliding.
            Item {
                id: metaLine

                readonly property string labels: {
                    if (!root.torrent)
                        return "";
                    const parts = [];
                    if (root.torrent.category)
                        parts.push(root.torrent.category);
                    if (root.torrent.tagsText)
                        parts.push(root.torrent.tagsText);
                    return parts.join(" · ");
                }
                readonly property string added: root.torrent && root.torrent.addedOnText
                                                ? root.torrent.addedOnText : ""
                /// The labels show only while a few characters of them fit next to the date;
                /// below that they go, and the ` · ` that joined them to the date goes too.
                readonly property bool labelsFit: metaLine.labels.length > 0
                                                  && metaLine.width - joinedMetrics.advanceWidth
                                                  >= Theme.space(40)
                /// #79: the whole line, whatever the width drops ("linux · private · added 18 Sep
                /// 10:41"), for a screen reader; its two texts publish nothing themselves.
                readonly property string spoken: !root.torrent ? qsTr("pick a row in the table")
                                                               : [metaLine.labels, metaLine.added].filter(
                                                                     part => part.length > 0).join(" · ")

                Accessible.name: metaLine.spoken
                Accessible.role: Accessible.StaticText
                height: addedText.implicitHeight
                objectName: "headMeta"
                width: parent.width

                Text {
                    id: labelsText

                    color: Theme.dimForeground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "headLabels"
                    text: !root.torrent ? qsTr("pick a row in the table") : metaLine.labels
                    visible: !root.torrent || metaLine.labelsFit
                    // Measured with a TextMetrics, not implicitWidth: an eliding Text handed
                    // its text while its width was 0 kept an implicitWidth of 0 once shown,
                    // so the head read " · added …" with no labels (review VER-2).
                    width: !root.torrent ? Math.min(Math.ceil(labelsMetrics.advanceWidth), metaLine.width)
                                         : Math.max(0, Math.min(Math.ceil(labelsMetrics.advanceWidth),
                                                                metaLine.width - Math.ceil(
                                                                    joinedMetrics.advanceWidth)))
                }

                TextMetrics {
                    id: labelsMetrics

                    font: labelsText.font
                    text: labelsText.text
                }

                // The date with its separator, measured apart from `addedText` (whose text
                // depends on whether the labels fit, which depends on this width).
                TextMetrics {
                    id: joinedMetrics

                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    text: metaLine.added.length > 0 ? " · " + metaLine.added : ""
                }

                Text {
                    id: addedText

                    anchors.left: labelsText.visible ? labelsText.right : parent.left
                    color: Theme.dimForeground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "headAdded"
                    // The separator travels with the date, so it survives the labels' cut,
                    // and leaves with the labels when they no longer fit at all.
                    text: !root.torrent || metaLine.added.length === 0 ? "" : metaLine.labelsFit
                                                                             ? " · " + metaLine.added
                                                                             : metaLine.added
                    width: Math.max(0, Math.min(Math.ceil(addedMetrics.advanceWidth),
                                                metaLine.width - (labelsText.visible ? labelsText.width : 0)))
                }

                TextMetrics {
                    id: addedMetrics

                    font: addedText.font
                    text: addedText.text
                }
            }
        }
    }

    Strip {
        id: strip

        // docs/13: in the band the strip sits beside the head, centred on its two lines, and
        // the band's own rule closes it.
        anchors.right: parent.right
        objectName: "strip"
        rule: !root.band
        width: root.band ? root.bandStripWidth : root.width
        // A y binding rather than anchors: swapping anchors.top for anchors.verticalCenter
        // when `wide` flips leaves the strip where it was. Centred, the head's text sits 2
        // below its middle (12 over, 8 under) and the strip's 2 above its own (6 over, 10
        // under).
        y: root.band ? head.y + (head.height - height) / 2 + Theme.space(2) + Theme.space(2)
                     : head.y + head.height
        items: [
            {
                "label": qsTr("size"),
                "value": root.torrent ? root.torrent.sizeText : "—"
            },
            {
                "label": qsTr("done"),
                "value": root.torrent ? root.torrent.progressText : "—"
            },
            {
                "label": qsTr("ratio"),
                "value": root.torrent ? root.torrent.ratioText : "—"
            },
            {
                "label": qsTr("eta"),
                "value": root.torrent ? root.torrent.etaLongText : "—"
            }
        ]
    }

    // docs/13: the rule under the band, across the whole pane, while wide.
    Hairline {
        id: bandRule

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: head.bottom
        visible: root.band
    }

    Tabs {
        id: tabs

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.band ? bandRule.bottom : strip.bottom
        currentIndex: root.currentTab
        objectName: "detailsTabs"
        // activateTab() keeps currentIndex in step. HTTP sources comes and goes with the
        // torrent's web seeds; it is last, so the other tabs keep their indexes.
        model: root.hasWebSeeds ? root._tabs.concat([
                                                       {
                                                           "text": qsTr("HTTP sources"),
                                                           "key": "5"
                                                       }
                                                   ]) : root._tabs

        onActivated: index => {
            root.takeKeys();
            root.activateTab(index);
        }
    }

    // The four tabs every torrent has (spec §2 Fig. 1).
    readonly property var _tabs: [
        {
            "text": qsTr("General"),
            "key": "1"
        },
        {
            "text": qsTr("Files"),
            "key": "2"
        },
        {
            "text": qsTr("Trackers"),
            "key": "3",
            "badge": root.hasTrackerError
        },
        {
            "text": qsTr("Peers"),
            "key": "4"
        }
    ]

    Item {
        id: body

        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        clip: true

        DetailsGeneralTab {
            id: generalTab

            anchors.fill: parent
            details: root.details
            objectName: "generalTab"
            // Two columns only with room for them: narrower, the values would wrap instead.
            columns: root.wide && body.width >= Theme.space(620) ? 2 : 1
            cursorIndex: root.generalCursor
            properties: root.details.properties
            torrent: root.torrent
            visible: root.currentTab === 0
            // docs/28 G2: how much room the full disk has left, read as the add sheet does.
            freeText: {
                const problem = root.torrent && root.torrent.problem ? root.torrent.problem : ({});
                if (problem.causeKey !== "diskFull" || App.freeSpaceRevision < 0)
                    return "";
                const room = App.freeSpaceFor(root.torrent.savePath || "");
                return room.source === "unknown" ? "" : qsTr("%1 free").arg(App.sizeText(room.bytesFree));
            }

            // docs/44 D13: a stuck move's partial copies, read on this machine when it can
            // see the destination; again when the free space is read again.
            partialText: {
                const problem = root.torrent && root.torrent.problem ? root.torrent.problem : ({});
                if (problem.stuckMove !== true || App.freeSpaceRevision < 0)
                    return "";
                return App.stuckMoveCopies(root.torrent.hash);
            }

            // Board 45 B1: read again when the history or the torrent's place changes.
            lastMove: {
                const revision = App.move.historyRevision;
                const savePath = root.torrent ? root.torrent.savePath : "";
                return root.torrent && revision >= 0 && savePath !== undefined
                       ? App.move.lastMoveOf(root.torrent.hash) : ({});
            }

            onMoveAction: action => {
                const move = generalTab.lastMove;
                if (action === "show")
                    root.moveShowRequested(move.hashes ?? []);
                else
                    root.moveBackRequested(move.id ?? "", action === "backThis" ? root.torrent.hash : "");
            }
            onProblemAction: action => {
                if (action === "rename") {
                    root.currentTab = 1;
                } else if (action === "copy") {
                    root.valueCopyRequested(generalTab.problem.line || "", qsTr("the log line"));
                } else {
                    root.problemActionRequested(action);
                }
            }
            // docs/32 F2: `2` opens Files with the cursor on the file that kept its name.
            onFailureAction: action => {
                if (action === "rename") {
                    root.showFile(generalTab.failure.torrentFile || "");
                } else if (action === "copy") {
                    root.valueCopyRequested(generalTab.failure.line || "", qsTr("the log line"));
                } else {
                    root.failureActionRequested(action);
                }
            }

            // Another torrent may have fewer rows: keep the cursor on one.
            onCountChanged: root.generalCursor = Math.max(0, Math.min(root.generalCursor,
                                                                      generalTab.count - 1))
            onRowClicked: row => {
                root.takeKeys();
                root.generalCursor = row;
                if (!root.toggleGeneralOption(row))
                    root.copyGeneralRow(row);
            }
        }

        DetailsFilesTab {
            id: filesTab

            objectName: "filesTab"
            anchors.fill: parent
            cursorIndex: root.filesCursor
            model: root.details.files
            serverName: App.serverName
            // C17 D1–D4: the reading, while it is this torrent's.
            skipped: App.skippedOnDisk.hash === root.details.hash ? App.skippedOnDisk : ({})
            visible: root.currentTab === 1

            onFreeSpaceRequested: root.freeSpaceRequested()
            onPathMapsRequested: root.pathMapsRequested()

            onFilterDone: root.forceActiveFocus()
            onHeaderClicked: key => {
                root.takeKeys();
                root.cycleFilesSort(key);
            }
            onHeadTickClicked: {
                root.takeKeys();
                // DECISIONS C19 T1: a half tick unticks; the next click brings it back.
                App.toggleFilesWanted(root.details.hash, root.details.files.headFileIndexes());
            }
            onHideSkippedRequested: root.toggleHideSkipped()
            onRenameCancelled: root.cancelRename()
            onRenameCommitted: text => root.commitRename(text)
            onFoldClicked: row => root.foldFileRow(root._takeKeysAt(row))
            // A double click opens a file and folds a folder (the add sheet's tree, D5).
            onRowActivated: row => {
                if (root.details.files.isFolder(row))
                    root.foldFileRow(row);
                else
                    root.openFileRequested(root.details.files.pathAt(row));
            }
            onRowClicked: (row, modifiers) => root.clickFile(root._takeKeysAt(row), modifiers)
            onRowMiddleClicked: row => root.clickFile(root._takeKeysAt(row), Qt.ControlModifier)
            onEmptyClicked: {
                root.takeKeys();
                root.details.files.clearSelection();
                root.filesAnchorPath = "";
            }
            onRowMenuRequested: (row, x, y) => {
                const at = filesTab.mapToItem(root, x, y);
                root.fileMenuAt(root._takeKeysAt(row), at.x, at.y);
            }
            onTickClicked: row => {
                row = root._takeKeysAt(row);
                root.filesCursor = row;
                App.toggleFilesWanted(root.details.hash, root.details.files.fileIndexes(row));
            }
        }

        DetailsTrackersTab {
            id: trackersTab

            anchors.fill: parent
            cursorIndex: root.trackersCursor
            model: root.details.trackers
            visible: root.currentTab === 2

            selectedKeys: root.trackersSelection.keys

            onEmptyClicked: {
                root.takeKeys();
                root.trackersSelection.clear();
            }
            onRowClicked: (row, modifiers) => {
                root.takeKeys();
                root.clickListRow(row, modifiers);
            }
            onRowMenuRequested: (row, x, y) => {
                root.takeKeys();
                const at = trackersTab.mapToItem(root, x, y);
                root.listMenuAt(row, at.x, at.y);
            }
            onRowMiddleClicked: row => {
                root.takeKeys();
                root.clickListRow(row, Qt.ControlModifier);
            }
        }

        DetailsPeersTab {
            id: peersTab

            anchors.fill: parent
            cursorIndex: root.peersCursor
            model: root.details.peers
            visible: root.currentTab === 3

            selectedKeys: root.peersSelection.keys

            onEmptyClicked: {
                root.takeKeys();
                root.peersSelection.clear();
            }
            onRowClicked: (row, modifiers) => {
                root.takeKeys();
                root.clickListRow(row, modifiers);
            }
            onRowMenuRequested: (row, x, y) => {
                root.takeKeys();
                const at = peersTab.mapToItem(root, x, y);
                root.listMenuAt(row, at.x, at.y);
            }
            onRowMiddleClicked: row => {
                root.takeKeys();
                root.clickListRow(row, Qt.ControlModifier);
            }
        }

        DetailsWebSeedsTab {
            id: webSeedsTab

            anchors.fill: parent
            cursorIndex: root.webSeedsCursor
            model: root.details.webSeeds
            objectName: "webSeedsTab"
            visible: root.currentTab === 4

            selectedKeys: root.webSeedsSelection.keys

            onEmptyClicked: {
                root.takeKeys();
                root.webSeedsSelection.clear();
            }
            onRowClicked: (row, modifiers) => {
                root.takeKeys();
                root.clickListRow(row, modifiers);
            }
            onRowMenuRequested: (row, x, y) => {
                root.takeKeys();
                const at = webSeedsTab.mapToItem(root, x, y);
                root.listMenuAt(row, at.x, at.y);
            }
            onRowMiddleClicked: row => {
                root.takeKeys();
                root.clickListRow(row, Qt.ControlModifier);
            }
        }
    }

    // The tab's own hints (spec §2 Fig. 2-4 `.foot .hints`), and since docs/12 G5 General's
    // (`j k` move, `y` copy value, `enter` change an option). Inside the window Main's footer teaches
    // them instead (HintModel), so this row only shows in the standalone figures.
    Item {
        id: footer

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: !root.showHints ? 0 : Math.round(Theme.font.body * 2.2)
        visible: root.showHints

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }

        KeyHints {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(16)
            anchors.verticalCenter: parent.verticalCenter
            objectName: "detailsHints"
            model: {
                if (root.currentTab === 0)
                    return [
                                {
                                    "key": "j k",
                                    "label": Keymap.labelOf("details", "j")
                                },
                                {
                                    "key": "y",
                                    "label": qsTr("copy value")
                                },
                                {
                                    "key": "enter",
                                    "label": qsTr("change")
                                }
                            ];
                if (root.currentTab === 1)
                    return [
                                {
                                    "key": "p",
                                    "label": Keymap.labelOf("details", "p")
                                },
                                {
                                    "key": "space",
                                    "label": Keymap.labelOf("details", "space")
                                },
                                {
                                    "key": "enter",
                                    "label": Keymap.labelOf("details", "enter")
                                },
                                {
                                    "key": "/",
                                    "label": Keymap.labelOf("details", "/")
                                }
                            ];
                if (root.currentTab === 2)
                    return [
                                {
                                    "key": "a",
                                    "label": qsTr("add")
                                },
                                {
                                    "key": "d",
                                    "label": qsTr("remove")
                                },
                                {
                                    "key": "R",
                                    "label": qsTr("reannounce")
                                },
                                {
                                    "key": "y",
                                    "label": qsTr("copy")
                                },
                                {
                                    "key": "space",
                                    "label": Keymap.labelOf("details", "space")
                                },
                                {
                                    "key": "ctrl+a",
                                    "label": Keymap.labelOf("details", "ctrl+a")
                                }
                            ];
                if (root.currentTab === 4)
                    return [
                                {
                                    "key": "a",
                                    "label": qsTr("add")
                                },
                                {
                                    "key": "d",
                                    "label": qsTr("remove")
                                },
                                {
                                    "key": "y",
                                    "label": qsTr("copy")
                                },
                                {
                                    "key": "space",
                                    "label": Keymap.labelOf("details", "space")
                                },
                                {
                                    "key": "ctrl+a",
                                    "label": Keymap.labelOf("details", "ctrl+a")
                                }
                            ];
                return [
                            {
                                "key": "b",
                                "label": qsTr("ban")
                            },
                            {
                                "key": "a",
                                "label": qsTr("add peer")
                            },
                            {
                                "key": "y",
                                "label": qsTr("copy")
                            },
                            {
                                "key": "space",
                                "label": Keymap.labelOf("details", "space")
                            },
                            {
                                "key": "ctrl+a",
                                "label": Keymap.labelOf("details", "ctrl+a")
                            }
                        ];
            }
        }
    }

    // The head's whole name, over the pane (the head itself is clipped to its two lines).
    Tooltip {
        id: headTip

        objectName: "headTip"
    }

    // --- the pane's menus (N1) -----------------------------------------------------------
    // Kit/Menu, opened at the cursor row: in Files `enter` or a right click for the row's
    // actions, `p` for its priority, `s` for the sort; in Trackers, Peers and HTTP sources a
    // right click. Each row's key is the same key the list itself answers to.
    //
    // They cover the whole window, not the pane: under the table the pane can be a few rows
    // tall, and a menu clamped inside it ran past its bottom edge and off the window.
    Item {
        id: menuHost

        anchors.fill: parent
        objectName: "detailsMenuHost"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 50

        Menu {
            id: fileMenu

            readonly property bool onFolder: root.details.files.isFolder(root.filesCursor)
            readonly property int selected: root.details.files.selectedCount
            // Read again whenever the selection count moves (selectedFileCount() is a call).
            readonly property int selectedFiles: fileMenu.selected >= 0 ? root.details.files.selectedFileCount() : 0

            anchors.fill: parent
            items: [
                {
                    "text": qsTr("open"),
                    "key": "O",
                    "action": "openFile",
                    "disabled": fileMenu.onFolder
                },
                {
                    "text": qsTr("open folder"),
                    "key": "o",
                    "action": "reveal"
                },
                {
                    "separator": true
                },
                {
                    "text": fileMenu.selected > 0 ? qsTr("priority of %n selected…", "", fileMenu.selected)
                                                  : qsTr("priority…"),
                    "key": "p",
                    "action": "priority"
                },
                {
                    "text": fileMenu.selectedFiles >= 2 ? qsTr("rename %n files…", "", fileMenu.selectedFiles)
                            : fileMenu.onFolder ? qsTr("rename folder") : qsTr("rename"),
                    "key": "r",
                    "action": "rename"
                },
                {
                    "text": qsTr("rename files…"),
                    "action": "renameMany",
                    "disabled": root.details.files.fileCount < 2
                },
                {
                    "separator": true
                },
                {
                    "text": qsTr("copy path"),
                    "key": "y",
                    "action": "copyPath"
                },
                {
                    "text": qsTr("copy local path"),
                    "key": "Y",
                    "action": "copyLocalPath"
                },
                {
                    "text": qsTr("copy name"),
                    "key": "n",
                    "action": "copyName"
                },
                {
                    "separator": true
                },
                {
                    "text": qsTr("expand all"),
                    "key": "L",
                    "action": "expandAll"
                },
                {
                    "text": qsTr("collapse all"),
                    "key": "H",
                    "action": "collapseAll"
                },
                {
                    "text": root.details.files.hideSkipped ? qsTr("show skipped files")
                                                          : qsTr("hide skipped files"),
                    "key": "X",
                    "action": "hideSkipped"
                }
            ]
            menuWidth: Math.min(Theme.space(260), root.width - Theme.space(16))
            z: 20

            onActivated: action => root.fileAction(action)
        }

        Menu {
            id: priorityMenu

            /// The one priority every target shares, -1 when they differ.
            readonly property int current: priorityMenu.opened
                                           ? root.details.files.targetPriority(root.filesCursor) : -1

            function row(text: string, key: string, priority: int): var {
                return {
                    "text": text,
                    "key": key,
                    "action": String(priority),
                    "glyph": priorityMenu.current === priority ? Glyphs.check : ""
                };
            }

            anchors.fill: parent
            items: [priorityMenu.row(qsTr("maximal"), "m", 7), priorityMenu.row(qsTr("high"), "h", 6),
                priorityMenu.row(qsTr("normal"), "n", 1), priorityMenu.row(qsTr("skip"), "x", 0), {
                    "separator": true
                }, {
                    "text": qsTr("by shown file order"),
                    "key": "b",
                    "action": "shownOrder"
                }]
            menuWidth: Math.min(Theme.space(240), root.width - Theme.space(16))
            z: 20

            onActivated: action => {
                if (action === "shownOrder")
                    root.applyShownOrder();
                else
                    root.applyPriority(Number(action));
            }
        }

        Menu {
            id: sortMenu

            function row(text: string, key: string, sortKey: string): var {
                const files = root.details.files;
                const current = files.sortKey === sortKey;
                return {
                    "text": text,
                    "key": key,
                    "action": sortKey,
                    "glyph": !current ? "" : sortKey === "" ? Glyphs.check : files.sortDescending
                                                                   ? Glyphs.chevronDown : Glyphs.chevronUp
                };
            }

            anchors.fill: parent
            items: [sortMenu.row(qsTr("torrent order"), "t", ""), sortMenu.row(qsTr("name"), "n",
                                                                                   "name"),
                sortMenu.row(qsTr("size"), "s", "size"), sortMenu.row(qsTr("progress"), "p",
                                                                      "progress"),
                sortMenu.row(qsTr("remaining"), "r", "remaining"), sortMenu.row(qsTr("priority"), "i",
                                                                                "priority"),
                sortMenu.row(qsTr("availability"), "a", "availability"), sortMenu.row(qsTr(
                                                                                         "pieces"),
                                                                                     "c", "pieces")]
            menuWidth: Math.min(Theme.space(220), root.width - Theme.space(16))
            z: 20

            onActivated: action => root.sortFilesBy(action)
        }

        // Trackers: DHT, PeX and LSD are rows of the list, not trackers of the torrent, so they
        // have nothing to remove.
        Menu {
            id: trackerMenu

            readonly property bool onPseudo: root.details.trackers.urlAt(root.trackersCursor).startsWith("** [")
            /// How many trackers remove and copy act on: the selection, or the cursor's row.
            readonly property int targets: root.trackersSelection.count > 0 ? root.trackersSelection.count
                                                                            : trackerMenu.onPseudo ? 0 : 1

            anchors.fill: parent
            items: [
                {
                    "text": qsTr("add…"),
                    "key": "a",
                    "action": "add"
                },
                {
                    "text": trackerMenu.targets > 1 ? qsTr("remove %n trackers", "", trackerMenu.targets)
                                                    : qsTr("remove"),
                    "key": "d",
                    "action": "remove",
                    "disabled": trackerMenu.targets === 0
                },
                {
                    "text": qsTr("reannounce"),
                    "key": "R",
                    "action": "reannounce"
                },
                {
                    "separator": true
                },
                {
                    "text": trackerMenu.targets > 1 ? qsTr("copy %n URLs", "", trackerMenu.targets)
                                                    : qsTr("copy URL"),
                    "key": "y",
                    "action": "copy",
                    "disabled": trackerMenu.targets === 0
                }
            ]
            menuWidth: Math.min(Theme.space(220), root.width - Theme.space(16))
            objectName: "trackerMenu"
            z: 20

            onActivated: action => root.rowMenuAction(action)
        }

        Menu {
            id: peerMenu

            readonly property int selected: root.peersSelection.count

            anchors.fill: parent
            items: [
                {
                    "text": qsTr("add peer…"),
                    "key": "a",
                    "action": "add"
                },
                {
                    "text": peerMenu.selected > 1 ? qsTr("ban %n peers…", "", peerMenu.selected) : qsTr("ban…"),
                    "key": "b",
                    "action": "ban"
                },
                {
                    "separator": true
                },
                {
                    "text": peerMenu.selected > 1 ? qsTr("copy %n addresses", "", peerMenu.selected)
                                                  : qsTr("copy address"),
                    "key": "y",
                    "action": "copy"
                }
            ]
            menuWidth: Math.min(Theme.space(220), root.width - Theme.space(16))
            objectName: "peerMenu"
            z: 20

            onActivated: action => root.rowMenuAction(action)
        }

        Menu {
            id: webSeedMenu

            readonly property int selected: root.webSeedsSelection.count

            anchors.fill: parent
            items: [
                {
                    "text": qsTr("add…"),
                    "key": "a",
                    "action": "add"
                },
                {
                    "text": webSeedMenu.selected > 1 ? qsTr("remove %n sources", "", webSeedMenu.selected)
                                                     : qsTr("remove"),
                    "key": "d",
                    "action": "remove"
                },
                {
                    "separator": true
                },
                {
                    "text": webSeedMenu.selected > 1 ? qsTr("copy %n URLs", "", webSeedMenu.selected)
                                                     : qsTr("copy URL"),
                    "key": "y",
                    "action": "copy"
                }
            ]
            menuWidth: Math.min(Theme.space(220), root.width - Theme.space(16))
            objectName: "webSeedMenu"
            z: 20

            onActivated: action => root.rowMenuAction(action)
        }
    }

    // A click away from an open rename commits it, wherever the keyboard went.
    Connections {
        function onActiveFocusItemChanged(): void {
            root._focusMoved();
        }

        target: root.Window.window
    }

    // Another torrent: the Files anchor, and the other lists' selections, belonged to the
    // last one's rows.
    Connections {
        function onHashChanged(): void {
            root.filesAnchorPath = "";
            root.trackersSelection.clear();
            root.peersSelection.clear();
            root.webSeedsSelection.clear();
        }

        target: root.details
    }

    // A filter or a fold that shrinks the list pulls the cursor back onto a row.
    Connections {
        function onCountChanged(): void {
            if (root.filesCursor >= root.details.files.count)
                root.filesCursor = Math.max(0, root.details.files.count - 1);
            root.filesCursorPath = root.details.files.pathAt(root.filesCursor);
        }

        // Sorted by progress or remaining, a poll moves the rows: the cursor and an open
        // rename keep their file (review REG-1).
        function onReordered(): void {
            const row = root.details.files.rowOfPath(root.filesCursorPath);
            if (row >= 0)
                root.filesCursor = row;
            root.filesCursorPath = root.details.files.pathAt(root.filesCursor);
            root._followRename();
        }

        // Rows rebuilt (a live re-sort, a filter, a fold): the cursor and an open rename
        // follow their file, and a rename whose file is gone is dropped.
        function onModelReset(): void {
            const files = root.details.files;
            const path = root.filesCursorPath;
            const row = path.length > 0 ? files.rowOfPath(path) : -1;
            if (row >= 0 && row !== root.filesCursor)
                root.filesCursor = row;
            else
                root.filesCursorPath = files.pathAt(root.filesCursor);
            root._followRename();
        }

        target: root.details.files
    }

    // Another torrent may have fewer trackers: keep the cursor on a row, so `y` never
    // copies nothing (review VIEW-13).
    Connections {
        function onCountChanged(): void {
            if (root.trackersCursor >= root.details.trackers.count)
                root.trackersCursor = Math.max(0, root.details.trackers.count - 1);
            root.trackersSelection.prune(); // a removed tracker leaves the selection
        }

        target: root.details.trackers
    }

    // The same for the web seeds (#10).
    Connections {
        function onCountChanged(): void {
            if (root.webSeedsCursor >= root.details.webSeeds.count)
                root.webSeedsCursor = Math.max(0, root.details.webSeeds.count - 1);
            root.webSeedsSelection.prune();
        }

        target: root.details.webSeeds
    }

    // A poll has moved the peers: put the cursor back on the one it was on, or keep it on
    // a row when that peer is gone.
    Connections {
        function onApplied(): void {
            const peers = root.details.peers;
            const row = peers.rowOfAddress(root.peersCursorAddress);
            root.peersCursor = row >= 0 ? row : Math.max(0, Math.min(root.peersCursor,
                                                                     peers.count - 1));
            root.peersCursorAddress = peers.addressAt(root.peersCursor);
            root.peersSelection.prune(); // a peer that left leaves the selection
        }

        target: root.details.peers
    }

    // The Peers tab is the only one whose poll costs the server real work, so App is told
    // when it comes and goes (spec §2: peers refresh every 2 s while the tab is on show).
    // The pane's overall visibility is Main's to report, through App.setDetailsVisible().
    onCurrentTabChanged: {
        App.setPeersTabActive(!root.collapsed && root.currentTab === 3);
        App.setGeneralTabActive(!root.collapsed && root.currentTab === 0); // G3's pieces bar
        App.setWebSeedsTabActive(!root.collapsed && root.currentTab === 4); // #10
        App.setFilesTabActive(!root.collapsed && root.currentTab === 1); // C17 D1
    }
    onCollapsedChanged: {
        App.setPeersTabActive(!root.collapsed && root.currentTab === 3);
        App.setGeneralTabActive(!root.collapsed && root.currentTab === 0);
        App.setWebSeedsTabActive(!root.collapsed && root.currentTab === 4);
        App.setFilesTabActive(!root.collapsed && root.currentTab === 1);
    }
    // #10: the torrent has no web seeds (another torrent, or the last one removed) while
    // their tab is on show: Trackers, its neighbour, takes over.
    onHasWebSeedsChanged: {
        if (!root.hasWebSeeds && root.currentTab === 4)
            root.activateTab(2);
    }
    onPeersCursorChanged: root.peersCursorAddress = root.details.peers.addressAt(root.peersCursor)
}
