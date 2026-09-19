pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

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

    /// Everything on the table that Phase 4 will really perform. It leaves as one App signal
    /// so the sheets, the confirm dialog and the tray can all hang off the same place.
    function torrentAction(action: string): void {
        const hashes = root.actionHashes();
        if (hashes.length === 0)
            return;
        console.log("action:", action, hashes.length, "torrents");
        App.requestTorrentAction(action, hashes);
    }

    /// The three palettes Phase 4 owns (`ctrl+k` jump, `s` sort, `ctrl+,` columns).
    function notYet(action: string): void {
        console.log("not yet:", action);
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
                root.notYet("jump");
                break;
            case Qt.Key_Comma:
                root.notYet("columns");
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
            root.notYet("sort");
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
            serverCount: App.serverCount
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

                onActionsRequested: (hash, x, y) => console.log("actions:", hash, x, y)
                onActivated: hash => console.log("activated:", hash)
                onEscapePressed: root.handleEscape()
                onFocusDetails: {
                    if (!details.collapsed)
                        details.forceActiveFocus();
                }
                onFocusRail: rail.forceActiveFocus()
                onRenameRequested: (hash, name) => {
                    console.log("rename:", hash, "->", name);
                    App.requestTorrentAction("rename", [hash]);
                }
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
}
