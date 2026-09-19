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
    readonly property alias renameSheetPanel: renameSheetPanel

    /// The `?` sheet.
    property bool helpOpen: false

    // the window's state (spec §7)
    /// A configured, non-demo server that stopped answering: Fig. 23.
    readonly property bool serverLost: App.hasServer && !App.demoMode
                                       && !App.globalState.connected && !root.firstSync
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
        if (hero.searchField.editing)
            return "search";
        if (rail.activeFocus)
            return "rail";
        if (details.activeFocus)
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
        // Fig. 1 (and Fig. 22, which simply has no downloading torrents to count).
        const parts = [
            {
                "value": String(App.counts.total),
                "label": qsTr("torrents")
            }
        ];
        if (App.counts.downloading > 0)
            parts.push({
                "value": String(App.counts.downloading),
                "label": qsTr("downloading")
            });
        parts.push({
            "value": App.globalState.freeSpaceText,
            "label": qsTr("free")
        });
        if (table.selectedCount > 0)
            parts.push({
                "value": "",
                "label": qsTr("%1 selected").arg(table.selectedCount)
            });
        return parts;
    }

    /// The same line as plain text, for the tests and for a log.
    readonly property string factsText: root.factParts.map(
                                            part => part.value.length > 0 ? part.value + " "
                                            + part.label : part.label).join(" · ")

    implicitHeight: Theme.space(800)
    implicitWidth: Theme.space(1360)

    // the API Main and the tests drive

    /// `/`: put the caret in the hero's search field.
    function focusSearch(): void {
        hero.searchField.focusInput();
    }

    /// `esc` in the field, and the field's own clear: empty the filter and go back to the
    /// table.
    function clearFilter(): void {
        hero.searchField.text = "";
        App.filtered.text = "";
        table.forceActiveFocus();
    }

    /// `i`: fold the details pane away (and stop its polls).
    function toggleDetails(): void {
        const wasFocused = details.activeFocus;
        details.collapsed = !details.collapsed;
        App.setDetailsVisible(!details.collapsed);
        if (details.collapsed && wasFocused)
            table.forceActiveFocus();
    }

    /// `esc`, in the order Fig. 26 promises: clear, then filter, then close.
    function handleEscape(): void {
        if (root.helpOpen) {
            root.helpOpen = false;
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
        case "openFolder":
            App.openFolder(root.cursorHash);
            break;
        default:
            console.log("action:", action, hashes.length, "torrents");
            break;
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
            helpSheetPanel.open(); // the sheet hands the keyboard to HelpSheet and remembers
        } else {                   // what had it, so `esc` lands back on the table
            helpSheetPanel.close();
            table.forceActiveFocus();
        }
    }

    // Key routing. Only what no focused view claimed arrives here.
    Keys.onPressed: event => root._route(event)

    function _route(event) {
        if (root.helpOpen)
            return; // the sheet traps `esc` and `?`; nothing else applies while it is up

        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;

        if (ctrl) {
            switch (event.key) {
            case Qt.Key_Q:
                root.quitRequested();
                break;
            case Qt.Key_K:
                jumpPalette.open();
                break;
            case Qt.Key_Comma:
                columnsPalette.open();
                break;
            default:
                return;
            }
            event.accepted = true;
            return;
        }

        switch (event.key) {
        case Qt.Key_Question:
            root.helpOpen = true;
            break;
        case Qt.Key_Slash:
            root.focusSearch();
            break;
        case Qt.Key_I:
            root.toggleDetails();
            break;
        case Qt.Key_A:
            if (shift)
                App.toggleAltSpeed();
            else
                App.addRequested();
            break;
        case Qt.Key_S:
            sortPalette.open();
            break;
        case Qt.Key_Comma:
            App.openSettings();
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
        default:
            return false;
        }
    }

    /// The startup state. `Component.onCompleted` of a Window runs before its composite
    /// children's, so this is deferred a frame (T3.3's finding).
    function _applyStartupState(): void {
        table.forceActiveFocus();
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

    Component.onCompleted: Qt.callLater(root._applyStartupState)

    // A sync brings new numbers for the row the details pane is showing.
    Connections {
        function onUpdated(): void {
            details.torrent = root.cursorHash.length > 0 ? App.torrentRow(root.cursorHash) : null;
        }

        target: App.torrents
    }

    // hero (spec `.hero`)
    Hero {
        id: hero

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        altSpeedActive: App.globalState.useAltSpeedLimits
        appName: qsTr("omaseed")
        connected: !root.serverLost
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
        syncing: root.firstSync
        upSpeed: root.live ? App.globalState.upSpeedText : "—"

        onAddClicked: App.addRequested()
        onAltSpeedClicked: App.toggleAltSpeed()
        onHelpClicked: root.helpOpen = true
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
        errorText: App.globalState.lastError.length > 0 ? App.globalState.lastError : qsTr(
                                                              "connection refused")
        retryInSeconds: App.globalState.retryInSeconds
        visible: root.serverLost

        onRetryRequested: App.retryNow()
        onSettingsRequested: App.openSettings()
    }

    // body: rail | table | details
    Item {
        id: body

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
            tags: App.tags
            trackers: App.trackers
            visible: !root.libraryEmpty && !root.firstSync

            onFocusTable: table.forceActiveFocus()
        }

        SplitLayout {
            id: split

            anchors.bottom: parent.bottom
            anchors.left: rail.visible ? rail.right : parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            main: tableHost
            side: details
            sideHeight: Theme.space(260)
            sideVisible: !details.collapsed
            sideWidth: Theme.space(340)
            visible: !root.libraryEmpty && !root.firstSync
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
                    actionsMenuPanel.openAt(point.x, point.y);
                }
                onActivated: hash => console.log("activated:", hash)
                onEscapePressed: root.handleEscape()
                onFocusDetails: {
                    if (!details.collapsed)
                        details.forceActiveFocus();
                }
                onFocusRail: rail.forceActiveFocus()
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
                onVisibleChanged: {
                    if (dimmed.visible)
                        dimmed.forceActiveFocus();
                    else
                        table.forceActiveFocus();
                }
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

            onFocusTable: table.forceActiveFocus()
            onCopyRequested: text => console.log("copy:", text)
            onOpenFolderRequested: path => console.log("open folder:", path)
            onPeerAction: (action, address) => console.log("peer:", action, address)
            onTrackerAction: (action, url) => console.log("tracker:", action, url)
        }

        // §7's whole-body states
        EmptyState {
            anchors.centerIn: parent
            visible: root.libraryEmpty

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
        // Fig. 1 keeps the table's line while row 3 is being renamed, because its cursor is
        // elsewhere; a rename the user started is always on the cursor row.
        renaming: table.renaming && table.renameIndex === table.cursorIndex
        torrentCount: App.torrents.count
    }

    Item {
        id: footer

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

        HelpSheet {
            id: helpSheet

            Layout.fillWidth: true

            onCloseRequested: root.helpOpen = false
        }
    }

    // --- T4.3: actions ---
    // The overlays `torrentAction()` opens, a short-lived footer note for "copied" and for
    // `App.actionFailed`, and the two notices `App.showTorrentRequested`/
    // `showTrackersRequested` (T4.7's seam) land on.

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

    Connections {
        function onActionFailed(action, message) {
            root.flashFooterNote(qsTr("%1 failed: %2").arg(action).arg(message));
        }
        function onShowTorrentRequested(hash) {
            let row = App.filtered.rowOfHash(hash);
            if (row < 0) {
                root.clearFilter(); // the row may be hidden behind the current filter
                row = App.filtered.rowOfHash(hash);
            }
            if (row >= 0) {
                table.moveCursorTo(row);
                table.forceActiveFocus();
            }
        }
        function onShowTrackersRequested(hash) {
            if (!details.collapsed)
                details.activateTab(2); // Trackers
        }

        target: App
    }

    Row {
        id: footerNoteRow

        anchors.bottom: footer.top
        anchors.bottomMargin: Theme.space(4)
        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        visible: root.footerNote.length > 0
        z: 8

        Text {
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.footerNote
        }
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
    // Jump (`ctrl+k`), category (`C`), tags (`t`), sort (`s`) and columns (`ctrl+,`), plus the
    // table-state round trip through `state.json`. `App.categoryPickerRequested` /
    // `tagPickerRequested` are T4.3's seam (its `C`/`t` actions, and the AddSheet's chips);
    // this block is what answers them.
    JumpPalette {
        id: jumpPalette

        anchors.fill: parent
        table: table
        z: 10
    }

    CategoryPalette {
        id: categoryPalette

        anchors.fill: parent
        z: 10
    }

    TagPalette {
        id: tagPalette

        anchors.fill: parent
        z: 10
    }

    SortPalette {
        id: sortPalette

        anchors.fill: parent
        table: table
        z: 10
    }

    ColumnsPalette {
        id: columnsPalette

        anchors.fill: parent
        table: table
        z: 10
    }

    Connections {
        function onCategoryPickerRequested(hashes) {
            categoryPalette.openFor(hashes);
        }

        function onTagPickerRequested(hashes) {
            tagPalette.openFor(hashes);
        }

        // Until T4.3 lands its own actions menu, the jump palette's `tab` has nowhere real
        // to go; this logs so the seam is visibly wired rather than silently dropped.
        function onShowActionsRequested(hash) {
            console.log("actions:", hash);
        }

        target: App
    }

    Connections {
        function onSortOrderChanged() {
            root.saveTableState();
        }

        function onSortRoleChanged() {
            root.saveTableState();
        }

        target: App.filtered
    }

    Connections {
        function onVisibleColumnsChanged() {
            root.saveTableState();
        }

        target: table
    }

    // The columns palette toggles `table.visibleColumns` live and the header (and every
    // other sort trigger, including a click) can change `App.filtered`'s sort at any time;
    // both round-trip through `state.json`, keyed by the chosen server (or "demo").
    function saveTableState(): void {
        const key = App.tableStateKey();
        if (key.length === 0)
            return;
        App.saveTableState(key, {
                               "columns": table.visibleColumns,
                               "sortRole": App.filtered.sortRoleName,
                               "sortOrder": App.filtered.sortOrder
                           });
    }

    function restoreTableState(): void {
        const state = App.tableState(App.tableStateKey());
        if (state.columns !== undefined && state.columns.length > 0)
            table.visibleColumns = state.columns;
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
            sortPalette.open();
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
        z: 11

        onAddRequested: request => App.addTorrent(request)
        onDuplicateFound: duplicate => duplicateSheet.openFor(duplicate)
    }

    DuplicateSheet {
        id: duplicateSheet

        anchors.fill: parent
        z: 12

        // `show it` gives up on the add: the sheet behind this one goes away with it.
        onShowRequested: hash => {
            addSheet.dismiss();
            root.showTorrent(hash);
        }
    }

    /// The drop target in `Main.qml` hands its addables over here.
    function addDropped(values: var): void {
        if (values.length === 1)
            addSheet.openWith(values[0], "drop");
        else
            addSheet.openList(values, "drop");
    }

    /// Move the table's cursor to `hash` and let the details pane follow. Used by the add
    /// flow once a new torrent turns up, and by T4.7's "show" notification action.
    function showTorrent(hash: string): void {
        const row = App.filtered.rowOfHash(hash);
        if (row < 0)
            return;
        table.moveCursorTo(row);
        table.forceActiveFocus();
    }

    Connections {
        function onAddRequested(): void {
            addSheet.openBlank();
        }

        function onAddUrisRequested(uris: list<string>): void {
            addSheet.openList(uris, "browser");
        }

        function onShowTorrentRequested(hash: string): void {
            root.showTorrent(hash);
        }

        target: App
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

        onClosed: table.forceActiveFocus()
    }

    Connections {
        function onOpenSettings(): void {
            settingsModal.open();
        }

        target: App
    }

    // The Appearance pane's transparency override; 0 keeps the default 0.90 of QUESTIONS Q1.
    Binding {
        property: "windowAlpha"
        target: Theme
        value: App.windowAlphaOverride > 0 ? App.windowAlphaOverride / 100 : 0.90
    }
    // --- end T4.5 ---
}
