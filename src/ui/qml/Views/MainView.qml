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
    // T4.3
    readonly property alias actionsMenuPanel: actionsMenuPanel
    readonly property alias deleteConfirmPanel: deleteConfirmPanel
    readonly property alias locationSheetPanel: locationSheetPanel
    readonly property alias limitsSheetPanel: limitsSheetPanel
    readonly property alias moreMenu: moreMenu
    readonly property alias globalLimitsSheet: globalLimitsSheet
    readonly property alias renameSheetPanel: renameSheetPanel
    // T4.8: the rest of the overlays, for the stacking and focus cases of `tst_Main`.
    readonly property alias addSheet: addSheet
    readonly property alias duplicateSheet: duplicateSheet
    readonly property alias settingsModal: settingsModal
    readonly property alias jumpPalette: jumpPalette
    readonly property alias categoryPalette: categoryPalette
    readonly property alias tagPalette: tagPalette
    readonly property alias sortPalette: sortPalette
    readonly property alias columnsPalette: columnsPalette

    /// The `?` sheet.
    property bool helpOpen: false

    // ---- overlays (T4.8) -----------------------------------------------------------------
    // Sheets, palettes and the actions menu are all modal: while one is up it owns the
    // keyboard, `esc` closes that one and nothing else, and the footer teaches its keys
    // instead of the table's. They are listed topmost-first, by the `z` each one is given
    // further down, so the one the user is actually looking at is the one that answers.
    /// The overlay on top, or null.
    readonly property var activeOverlay: {
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
        if (limitsSheetPanel.opened)
            return limitsSheetPanel;
        if (globalLimitsSheet.opened)
            return globalLimitsSheet;
        if (locationSheetPanel.opened)
            return locationSheetPanel;
        if (deleteConfirmPanel.opened)
            return deleteConfirmPanel;
        if (actionsMenuPanel.opened)
            return actionsMenuPanel;
        if (moreMenu.opened)
            return moreMenu;
        return null;
    }

    /// The rail groups folded on this server (docs/08 §5), saved with the table's state.
    property var railFolded: []

    /// True while anything is over the window.
    readonly property bool overlayOpen: root.activeOverlay !== null

    // the window's state (spec §7)
    /// A configured, non-demo server that stopped answering: Fig. 23.
    readonly property bool serverLost: root._testServerLost
                                       || (App.hasServer && !App.demoMode
                                           && !App.globalState.connected && !root.firstSync)
    /// A test seam: the QML tests run on the demo library, which never loses its server.
    property bool _testServerLost: false
    /// A server that has not finished its first sync yet: Fig. 25.
    readonly property bool firstSync: App.hasServer && !App.demoMode
                                      && App.globalState.syncing && App.torrents.count === 0
    /// Nothing to show at all: Fig. 22, which doubles as the first-run screen.
    readonly property bool libraryEmpty: App.torrents.count === 0 && !root.firstSync
                                         && !root.serverLost

    /// A filter that matches nothing: Fig. 24.
    readonly property bool filterEmpty: App.torrents.count > 0 && App.filtered.count === 0
    /// There are numbers worth printing in the hero.
    readonly property bool live: App.demoMode || App.globalState.connected

    /// Which view holds the focus, for `HintModel`.
    readonly property string focusArea: {
        if (hero.searchField.editing || details.filterEditing)
            return "search";
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
                    "label": qsTr("last sync %1").arg(App.globalState.lastSyncText)
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
        const parts = [
            {
                "value": String(App.counts.total),
                "label": step >= 3 ? "" : qsTr("torrents")
            }
        ];
        // Not the rail's count: that one takes stopped downloads too (Q16), which a status
        // line would misread as "downloading now".
        if (App.counts.inProgress > 0 && step < 4)
            parts.push({
                "value": String(App.counts.inProgress),
                "label": step >= 2 ? Glyphs.arrowDown : qsTr("downloading")
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
                                   : qsTr("%1 selected").arg(table.selectedCount)
            });
        // docs/12 F3: the Files tab's own selection (`space`, `v`, `ctrl+a`), while it shows.
        const files = App.details.files.selectedCount;
        if (files > 0 && details.currentTab === 1 && !details.collapsed)
            parts.push({
                "value": "",
                "label": step >= 2 ? qsTr("%1 files sel").arg(files) : files === 1
                                                                       ? qsTr("1 file selected")
                                                                       : qsTr("%1 files selected").arg(files)
            });
        return parts;
    }

    /// The same line as plain text, for the tests and for a log.
    readonly property string factsText: root.factParts.map(
                                            part => (part.value + " " + part.label).trim()).join(" · ")

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
        if (root.serverLost)
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
        const wrong = root.serverLost ? !dimmed.activeFocus
                                      : (dimmed.activeFocus || !item || !item.visible);
        if (wrong)
            root.focusHome();
    }

    onOverlayOpenChanged: {
        if (!root.overlayOpen)
            Qt.callLater(root._settleFocus);
    }

    /// `i`: fold the details pane away (and stop its polls).
    function toggleDetails(): void {
        const wasFocused = details.activeFocus;
        details.collapsed = !details.collapsed;
        App.setDetailsVisible(!details.collapsed);
        if (details.collapsed && wasFocused)
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

    /// `esc`, in the order Fig. 26 promises: clear, then filter, then close.
    function handleEscape(): void {
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

    /// The hashes a table action applies to: the selection, or the cursor row alone.
    function actionHashes(): var {
        if (table.selectedCount > 0)
            return table.selectedHashes();
        return root.cursorHash.length > 0 ? [root.cursorHash] : [];
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
        case "location":
            locationSheetPanel.openFor(hashes);
            break;
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
            App.copyToClipboard(App.torrentRow(root.cursorHash).magnetUri || "");
            root.flashFooterNote(qsTr("copied"));
            break;
        case "copyHash":
            App.copyToClipboard(App.torrentRow(root.cursorHash).infohashV1 || "");
            root.flashFooterNote(qsTr("copied"));
            break;
        case "copyName":
            App.copyToClipboard(App.torrentRow(root.cursorHash).name || "");
            root.flashFooterNote(qsTr("copied"));
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
            App.setAutoManagement(hashes, App.torrentRow(root.cursorHash).autoTmm !== true);
            break;
        default:
            console.log("action:", action, hashes.length, "torrents");
            break;
        }
    }

    /// The Trackers and Peers tabs' keys (Fig. 3/4). `R` reannounces the torrent (the API has
    /// no per-tracker reannounce) and `d` removes the tracker under the cursor. Adding a
    /// tracker or a peer needs an input sheet and banning a peer is a permanent IP ban; both
    /// wait for a design (review VIEW-6), so they say so instead of doing nothing.
    function detailsRowAction(kind: string, action: string, value: string): void {
        const hash = App.details.hash;
        if (hash.length === 0)
            return;
        if (kind === "tracker" && action === "reannounce") {
            App.reannounce([hash]);
            root.flashFooterNote(qsTr("reannounced"));
        } else if (kind === "tracker" && action === "remove") {
            // DHT, PeX and LSD are rows of the list but not trackers of the torrent.
            if (value.length === 0 || value.startsWith("** ["))
                return;
            App.removeTracker(hash, value);
            root.flashFooterNote(qsTr("removed %1").arg(value));
        } else {
            root.flashFooterNote(qsTr("not in this version yet"));
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
                root.quitRequested();
            return;
        }

        if (ctrl) {
            switch (event.key) {
            case Qt.Key_Q:
                root.quitRequested();
                break;
            case Qt.Key_K:
                jumpPalette.open();
                break;
            case Qt.Key_Comma: // the desktop's settings key, beside bare `,` (Keymap "ctrl+,")
                App.openSettings();
                break;
            case Qt.Key_Period:
                root.openMoreMenu();
                break;
            case Qt.Key_B:
                root.toggleRail();
                break;
            default:
                return;
            }
            event.accepted = true;
            return;
        }

        switch (event.key) {
        case Qt.Key_Question:
        case Qt.Key_F1: // the desktop's help key, same sheet (Keymap's "f1" entry)
            root.helpOpen = true;
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
                App.toggleAltSpeed();
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

    /// Runs a Keymap row's action from the help sheet's `enter` (docs/08 §4). Rows that name a
    /// movement rather than a command (j k, g G, h l, 0–8, 1–4, space, v, esc) do nothing.
    function runKeymapAction(action: string): void {
        // Fig. 23's read-only guard: from the `?` sheet of a lost server, only what the
        // guard itself still lets through runs.
        if (root.serverLost && ["settings", "hide", "quit"].indexOf(action) < 0)
            return;
        switch (action) {
        case "filter":
            root.focusSearch();
            return;
        case "jump":
            jumpPalette.open();
            return;
        case "selectAll":
            table.forceActiveFocus();
            table.selectAll();
            return;
        case "add":
            App.addRequested();
            return;
        case "actions":
            table.forceActiveFocus();
            table.requestActions();
            return;
        case "toggleDetails":
            root.toggleDetails();
            return;
        case "sort":
            sortPalette.openOnCurrent();
            return;
        case "columns":
            columnsPalette.open();
            return;
        case "altSpeed":
            App.toggleAltSpeed();
            return;
        case "settings":
            App.openSettings();
            return;
        case "more":
            root.openMoreMenu();
            return;
        case "toggleRail":
            root.toggleRail();
            return;
        case "hide":
            root.hideRequested();
            return;
        case "quit":
            root.quitRequested();
            return;
        case "stopStart":
        case "delete":
        case "recheck":
        case "category":
        case "queueUp":
        case "openFolder":
        case "copyMagnet":
            table.forceActiveFocus();
            root.torrentAction(action);
            return;
        default:
            return;
        }
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
        }
    }

    /// The hero's ⋯ menu, hung under its button with their right edges flush.
    function openMoreMenu(): void {
        const button = hero.moreButton;
        const corner = button.mapToItem(root, button.width, button.height);
        moreMenu.openAt(Math.round(corner.x - moreMenu.menuWidth), Math.round(corner.y
                                                                              + Theme.space(4)));
    }

    /// Runs a row of the ⋯ menu (docs/08 §3). The menu has closed and given the focus back.
    function moreAction(action: string): void {
        moreMenu.close();
        switch (action) {
        case "settings":
            App.openSettings();
            break;
        case "help":
            root.helpOpen = true;
            break;
        case "globalLimits":
            globalLimitsSheet.openFor(false);
            break;
        case "altLimits":
            globalLimitsSheet.openFor(true);
            break;
        case "columns":
            columnsPalette.open();
            break;
        case "selectAll":
            table.forceActiveFocus();
            table.selectAll();
            break;
        case "toggleRail":
            root.toggleRail();
            break;
        }
    }

    /// Fig. 26's "act" column, for the table only: every one of these needs Phase 4, so they
    /// travel as `App.torrentActionRequested` and log.
    function _tableAction(event, shift) {
        switch (event.key) {
        case Qt.Key_P:
            root.torrentAction(shift ? "forceStart" : "stopStart");
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

    // A sync brings new numbers for the row the details pane is showing.
    Connections {
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
            addSheet.openBlank();
        }

        /// The browser's `magnet:` / `.torrent` hand-off (T1.4's second instance).
        function onAddUrisRequested(uris: list<string>): void {
            if (root.firstRun) // held until there is a server to add them to
                root._pendingAdds = root._pendingAdds.concat(uris);
            else
                addSheet.openList(uris, "browser");
        }

        /// `,`, the hero's gear, the tray's `settings…`, the disconnected banner.
        function onOpenSettings(): void {
            settingsModal.open();
        }

        /// T4.3's seam: an action the server refused, on the footer for a few seconds.
        function onActionFailed(action: string, message: string): void {
            root.flashFooterNote(qsTr("%1 failed: %2").arg(action).arg(message));
        }

        /// T4.7's "show" notification action, and the duplicate sheet's `show it`.
        function onShowTorrentRequested(hash: string): void {
            root.showTorrent(hash);
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
        altUpLimit: App.globalState.upRateLimitText
        appName: qsTr("omaseed")
        connected: !root.serverLost
        connectionStatus: App.globalState.connectionStatus
        serverConfigured: App.hasServer
        downSpeed: root.live ? App.globalState.dlSpeedText : "—"
        searchCount: App.filtered.count
        searchCountText: App.filtered.text.length > 0 ? qsTr("%1 of %2").arg(
                                                            App.filtered.count).arg(
                                                            App.counts.total) : String(
                                                            App.filtered.count)
        serverLine: {
            if (!App.hasServer)
                return qsTr("no server");
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
        upSpeed: root.live ? App.globalState.upSpeedText : "—"

        onAddClicked: App.addRequested()
        onAltSpeedClicked: App.toggleAltSpeed()
        onHelpClicked: root.helpOpen = true
        onFilterChipCleared: key => root.clearOneFilter(key)
        onMoreClicked: root.openMoreMenu()

        Rectangle {
            anchors.fill: parent
            color: Theme.fill(Theme.fillIdle)
            visible: App.surfaceTint === 8
            z: -1
        }
        onSearchChanged: text => App.filtered.text = text
        onSearchCleared: root.clearFilter()
        onSettingsClicked: App.openSettings()
    }

    // the lost-server banner (spec §7 Fig. 23, margins are the caller's)
    DisconnectedBanner {
        id: banner

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: hero.bottom
        anchors.topMargin: Theme.space(10)
        errorText: App.globalState.lastError.length > 0 ? App.errorReasonText(
                                                              App.globalState.lastError) : qsTr(
                                                              "connection refused")
        retryInSeconds: App.globalState.retryInSeconds
        visible: root.serverLost

        onRetryRequested: App.retryNow()
        onSettingsRequested: App.openSettings()
    }

    // body: rail | table | details
    Item {
        id: body

        // The first-run page (T4.6) stands in for the whole body, not over it.
        visible: !root.firstRun
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: banner.visible ? banner.bottom : hero.bottom
        anchors.topMargin: banner.visible ? Theme.space(10) : 0

        Rail {
            id: rail

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.top: parent.top
            categories: App.categories
            counts: App.counts
            filtered: App.filtered
            servers: App.servers
            // T4.5: the SERVERS group is off unless the General pane asks for it.
            serverCount: App.showServersGroup ? App.serverCount : 0
            // T4.8: the group draws the real names, and picking one switches server.
            serverList: App.serverList
            tags: App.tags
            trackers: App.trackers
            // docs/08 §5: `ctrl+b` hides it; the filters it holds show as chips in the hero.
            visible: !root.libraryEmpty && !root.firstSync && !App.railHidden

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
                root.railFolded = root.railFolded.indexOf(group) >= 0 ? root.railFolded.filter(
                                                                            g => g !== group) :
                                                                        root.railFolded.concat(
                                                                            [group]);
                root.saveTableState();
            }
            onServerActivated: id => App.setDefaultServer(id)
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
            side: details
            // Two columns under the table (Details.wide) fit the General tab in less height;
            // docs/13's band over the tabs costs about 40, so 220 became 260.
            defaultSideHeight: Theme.space(260)
            // docs/11 J1: wide enough for the Files tab's four columns and a name that can be
            // told apart (it was 340, which cut every list in the pane).
            defaultSideWidth: Theme.space(400)
            // docs/10 §5: Settings -> Appearance, or `I`; the sizes are the table state's.
            placement: App.detailsPlacement
            sideVisible: !details.collapsed
            visible: !root.libraryEmpty && !root.firstSync

            onResized: root.saveTableState()
        }

        // `main` and `side` are reparented into the split; they are declared here so the
        // table can keep the figure's 16 px gutter without the split knowing about it.
        Item {
            id: tableHost

            TorrentTable {
                id: table

                anchors.bottomMargin: Theme.space(12)
                anchors.fill: parent
                anchors.leftMargin: Theme.space(16)
                anchors.rightMargin: Theme.space(16)
                anchors.topMargin: Theme.space(12)
                filtered: App.filtered
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
                    actionsMenuPanel.openAt(point.x, point.y);
                }
                onActivated: hash => console.log("activated:", hash)
                onEscapePressed: root.handleEscape()
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
                scopeName: App.filtered.category !== undefined
                           && App.filtered.category !== null ? String(
                                                                   App.filtered.category) : ""
                statusName: root.statusName(App.filtered.statusFilter)
                text: App.filtered.text
                visible: root.filterEmpty
            }

            // Fig. 23's read-only guard: it eats every click and every key but the five the
            // figure's footer still promises.
            DimmedOverlay {
                id: dimmed

                visible: root.serverLost

                onHelpRequested: root.helpOpen = true
                onHideRequested: root.hideRequested()
                onQuitRequested: root.quitRequested()
                onRetryRequested: App.retryNow()
                onSettingsRequested: App.openSettings()
                // Not under an open overlay or the first-run page: `focusHome` knows.
                onVisibleChanged: root.focusHome()
            }
        }

        Details {
            id: details

            details: App.details
            // Main's footer carries the hints for every tab (Fig. 1), so the pane does not
            // repeat them inside the window.
            showHints: false
            // SplitLayout draws the divider between the table and the pane.
            showEdge: false
            // docs/10 §5 and docs/13: under the table, the head band over full-width tabs.
            wide: !split.docked

            onFocusTable: table.forceActiveFocus()
            onCopyRequested: text => {
                App.copyToClipboard(text);
                root.flashFooterNote(qsTr("copied"));
            }
            onOpenFileRequested: path => App.openTorrentFile(App.details.hash, path)
            onNoteRequested: text => root.flashFooterNote(text)
            // docs/12 G5: "copied hash v1".
            onValueCopyRequested: (text, name) => {
                App.copyToClipboard(text);
                root.flashFooterNote(qsTr("copied %1").arg(name));
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.fill(Theme.fillIdle)
                visible: App.surfaceTint > 0
                z: -1
            }
            onOpenFolderRequested: path => App.revealTorrentFile(App.details.hash, path)
            onPeerAction: (action, address) => root.detailsRowAction("peer", action, address)
            onTrackerAction: (action, url) => root.detailsRowAction("tracker", action, url)
        }

        // §7's whole-body states
        EmptyState {
            anchors.centerIn: parent
            visible: root.libraryEmpty && !root.firstRun // T4.6: first run replaces this state

            onAddRequested: App.addRequested()
            onHelpRequested: root.helpOpen = true
            onSettingsRequested: App.openSettings()
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
            visible: root.firstSync
        }
    }

    // footer (spec `.foot`)
    HintModel {
        id: hintModel

        connected: !root.serverLost
        detailsTab: details.currentTab
        filterText: App.filtered.text
        filteredCount: App.filtered.count
        focusArea: root.focusArea
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

                Layout.alignment: Qt.AlignVCenter
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

            KeyHints {
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                // Given only what the facts leave, so it can drop the hints that don't fit
                // (docs/10 §3); a layout never sizes an item under its minimum otherwise.
                Layout.minimumWidth: 0
                model: hintModel.hints
                pinned: hintModel.pinned
            }
        }
    }

    // the `?` sheet (spec §7 Fig. 26: 760 wide, no buttons — a sheet may have none)
    Sheet {
        id: helpSheetPanel

        anchors.fill: parent
        glyph: Glyphs.help
        sheetWidth: Theme.space(760)
        subtitle: qsTr("single letters act on the table \u00b7 capitals are the stronger variant \u00b7 ctrl is app-level")
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

            onCloseRequested: root.helpOpen = false
            onRunRequested: action => {
                root.helpOpen = false;
                root.runKeymapAction(action);
            }
        }
    }

    // --- T4.3: actions ---
    // The overlays `torrentAction()` opens, a short-lived footer note for "copied" and for
    // `App.actionFailed`, and the notice `App.showTorrentRequested` (T4.7's seam) lands on.

    /// A few words shown next to the footer's facts for `_footerNoteSeconds`, then cleared.
    property string footerNote: ""
    readonly property int _footerNoteSeconds: 5

    /// `y`/`Y`'s "copied", and (through `Connections` below) `App.actionFailed`.
    function flashFooterNote(text: string): void {
        root.footerNote = text;
        footerNoteTimer.restart();
    }

    Timer {
        id: footerNoteTimer

        interval: root._footerNoteSeconds * 1000

        onTriggered: root.footerNote = ""
    }

    Row {
        id: footerNoteRow

        anchors.bottom: footer.top
        anchors.bottomMargin: Theme.space(4)
        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        visible: root.footerNote.length > 0
        z: 8

        // Backed by the ground and a hairline, so the note never reads through whatever it
        // sits on (the rail's "▾ 9 below", a table row).
        Rectangle {
            border.color: Theme.fill(Theme.hairlineAlpha)
            border.width: 1
            color: Theme.background
            height: noteText.implicitHeight + Theme.space(6)
            width: noteText.implicitWidth + Theme.space(16)

            Text {
                id: noteText

                anchors.centerIn: parent
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: root.footerNote
            }
        }
    }

    MoreMenu {
        id: moreMenu

        anchors.fill: parent
        // docs/10 §2: once the hero is too narrow for them, settings and help are here.
        appRows: hero.compact
        railHidden: App.railHidden
        z: 9

        onActivated: action => root.moreAction(action)
    }

    GlobalLimitsSheet {
        id: globalLimitsSheet

        anchors.fill: parent
        z: 9
    }

    ActionsMenu {
        id: actionsMenuPanel

        anchors.fill: parent
        z: 9

        onActivated: action => root.torrentAction(action)
    }

    DeleteConfirm {
        id: deleteConfirmPanel

        anchors.fill: parent
        z: 9
    }

    LocationSheet {
        id: locationSheetPanel

        anchors.fill: parent
        z: 9
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

        target: table
    }

    // The columns palette toggles `table.visibleColumns` and moves `table.columnOrder` live
    // (a header drag moves it too), and the header (and every other sort trigger, including
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
                               "sortRole": App.filtered.sortRoleName,
                               "sortOrder": App.filtered.sortOrder,
                               "railFolded": root.railFolded,
                               // In base-12 units, so a base size change keeps the proportion.
                               // 0 while it is the default, so a later default reaches it.
                               "detailsWidth": split.sideWidth === split.defaultSideWidth ? 0
                                                                                          : Math.round(
                                                                                                split.sideWidth
                                                                                                * 12
                                                                                                / Theme.font.baseSize),
                               "detailsHeight": Math.round(split.sideHeight * 12
                                                           / Theme.font.baseSize)
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

    function _applyTableState(state: var): void {
        root.railFolded = state.railFolded !== undefined ? state.railFolded : [];
        // 340 was the default before docs/11 J1, saved by every earlier version whether or
        // not anybody dragged it; it reads as "the default" so the wider one arrives.
        split.sideWidth = state.detailsWidth > 0 && state.detailsWidth !== 340
                ? Theme.space(state.detailsWidth) : split.defaultSideWidth;
        // Likewise 220 for the height, the default before docs/13's band.
        split.sideHeight = state.detailsHeight > 0 && state.detailsHeight !== 220
                ? Theme.space(state.detailsHeight) : split.defaultSideHeight;
        if (state.columns !== undefined && state.columns.length > 0)
            table.visibleColumns = state.columns;
        table.setColumnOrder(state.columnOrder !== undefined ? state.columnOrder
                                                             : table.defaultColumnOrder);
        if (state.sortRole !== undefined && String(state.sortRole).length > 0) {
            App.filtered.setSortByRoleName(state.sortRole);
            App.filtered.sortOrder = state.sortOrder !== undefined ? state.sortOrder : Qt.DescendingOrder;
        }
    }

    /// `OMASEED_DEMO_PALETTE=jump|category|tags|sort|columns` opens one palette on top of
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
        onDuplicateFound: duplicate => duplicateSheet.openFor(duplicate)
        // The `…` chips: T4.4's palettes, asked to answer instead of to act.
        onCategoryPaletteRequested: categoryPalette.openForChoice()
        onTagPaletteRequested: tagPalette.openForChoice(addSheet.tagChips.selected)
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
    }

    /// The drop target in `Main.qml` hands its addables over here.
    function addDropped(values: var): void {
        if (root.firstRun)
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
    }

    // The Appearance pane's transparency override; 0 keeps the shell's `[launcher]
    // background-alpha` (QUESTIONS Q1), which a theme change can move under us.
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
    /// The page, for the tests; `_firstRunDetect: false` keeps them off port 8080's probe.
    readonly property alias firstRunLoader: firstRunLoader
    property bool _firstRunDetect: true

    function _openPendingAdds(): void {
        const pending = root._pendingAdds;
        root._pendingAdds = [];
        if (pending.length > 0)
            addSheet.openList(pending, "browser");
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
