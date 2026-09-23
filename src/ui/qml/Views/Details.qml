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
// below). Actions leave as signals; only the file priority goes straight to App, because it
// is the one thing the pane changes in Phase 3.
Item {
    id: root

    required property TorrentDetails details

    /// The cursor row of the table. Expected fields: name, category, tagsText, addedOnText,
    /// sizeText, progressText, ratioText, etaLongText, stateText, stateRole,
    /// hasTrackerError. Null means "nothing selected".
    property var torrent: null

    /// 0 General, 1 Files, 2 Trackers, 3 Peers — the `1`-`4` keys.
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
    /// docs/10 §5: under the table the pane is wide and short, so the head and the strip take
    /// a left column and the tabs sit beside them, General's rows in two columns.
    property bool wide: false
    /// The head and strip's width: all of it, or the left column.
    readonly property int headWidth: root.wide ? Theme.space(280) : root.width

    /// Where the cursor sits inside the current tab's list; General's rows are a list too (G5).
    readonly property int cursorIndex: root.currentTab === 1 ? root.filesCursor : root.currentTab
                                                               === 2 ? root.trackersCursor :
                                                                       root.currentTab === 3
                                                                       ? root.peersCursor : root.generalCursor
    readonly property int rowCount: root.currentTab === 1 ? filesTab.count : root.currentTab === 2 ? trackersTab.count :
                                                                                                     root.currentTab
                                                                                                     === 3 ? peersTab.count :
                                                                                                             generalTab.count

    // One cursor per list, so switching tabs and coming back keeps the place.
    property int filesCursor: 0
    property int trackersCursor: 0
    property int peersCursor: 0
    property int generalCursor: 0

    /// `o` on a file row: the path inside the torrent, for Xdg to reveal.
    signal openFolderRequested(string path)
    /// `O`, or a double click on a file: the path inside the torrent, to open.
    signal openFileRequested(string path)
    /// `h` on anything that is not an open folder hands the focus back to the table.
    signal focusTable
    /// `a` add, `d` remove, `R` reannounce (Phase 4 wires them; App only re-emits for now).
    signal trackerAction(string action, string url)
    /// `b` ban, `a` add peer.
    signal peerAction(string action, string address)
    /// `y`: the text the caller should put on the clipboard.
    signal copyRequested(string text)
    /// `y` or a click on a General row (docs/12 G5): the whole value, and the row's name for
    /// the footer's "copied hash v1".
    signal valueCopyRequested(string text, string name)

    /// Something inside the pane holds the keyboard — the Files tab's filter, its inline
    /// rename or one of its menus — so Main still counts the focus as the pane's.
    readonly property bool editing: filesTab.filterEditing || filesTab.renamingRow >= 0
                                    || fileMenu.opened || priorityMenu.opened || sortMenu.opened
    /// The caret is in the Files tab's filter field (Main's footer teaches enter/esc then).
    readonly property bool filterEditing: filesTab.filterEditing
    /// One of the Files tab's menus is up; Main's footer then keeps only its facts (D3).
    readonly property bool menuOpen: fileMenu.opened || priorityMenu.opened || sortMenu.opened
    /// `v` selects from here to the cursor; the last row `space` toggled.
    property int filesAnchor: -1

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

    /// `1`-`4`. Tells App whether the Peers tab is the one on show, so sync/torrentPeers is
    /// only polled while somebody is looking at it.
    function activateTab(index: int) {
        if (index < 0 || index > 3)
            return;
        root.currentTab = index;
        // Tabs writes its own currentIndex on a click, so the binding is set from here
        // rather than declared, and the keys and the mouse cannot drift apart.
        tabs.currentIndex = index;
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

    /// `y` on the current row: the announce URL, the peer address, or the file path.
    function copyCurrent() {
        if (root.currentTab === 2)
            root.copyRequested(root.details.trackers.urlAt(root.trackersCursor));
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
        root.filesAnchor = root.filesCursor;
    }

    /// `v`: select every row from the last one `space` toggled to the cursor.
    function selectFileRange() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const anchor = root.filesAnchor < 0 ? root.filesCursor : root.filesAnchor;
        root.details.files.selectRange(anchor, root.filesCursor);
    }

    /// `r` on a file or folder row: the inline editor, on the leaf name.
    function startRename() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        filesTab.renameText = root.details.files.nameAt(root.filesCursor);
        filesTab.renamingRow = root.filesCursor;
    }

    function commitRename(text: string) {
        const row = filesTab.renamingRow;
        filesTab.renamingRow = -1;
        root.forceActiveFocus();
        if (row < 0 || text.length === 0 || text === root.details.files.nameAt(row))
            return;
        App.renameTorrentFile(root.details.hash, root.details.files.pathAt(row), text,
                              root.details.files.isFolder(row));
    }

    function cancelRename() {
        if (filesTab.renamingRow < 0)
            return;
        filesTab.renamingRow = -1;
        root.forceActiveFocus();
    }

    /// Sorts the Files tab and keeps the cursor on the row it was on.
    function sortFilesBy(key: string) {
        const path = root.details.files.pathAt(root.filesCursor);
        root.details.files.sortBy(key);
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

    /// `enter` or a right click on a Files row: its menu. `x`, `y` in the pane's coordinates;
    /// the cursor row's own place when they are left out.
    function openFileMenu(x: real, y: real) {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const at = x < 0 ? filesTab.menuPoint(root.filesCursor, root) : Qt.point(x, y);
        fileMenu.openAt(at.x, at.y);
    }

    /// `p` on a Files row: the priority menu.
    function openPriorityMenu() {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const at = filesTab.menuPoint(root.filesCursor, root);
        priorityMenu.openAt(at.x, at.y);
    }

    /// `s` in the Files tab: the sort menu, under the header.
    function openSortMenu() {
        if (root.currentTab !== 1)
            return;
        const at = filesTab.mapToItem(root, Theme.space(16), Theme.font.body * 2);
        sortMenu.openAt(at.x, at.y);
    }

    /// `/` in the Files tab.
    function focusFileFilter() {
        if (root.currentTab === 1)
            filesTab.focusFilter();
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
        case Qt.Key_Escape:
            if (root.details.files.selectedCount > 0) {
                root.details.files.clearSelection();
                root.filesAnchor = -1;
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
        if ((fileMenu.opened || priorityMenu.opened || sortMenu.opened)
                && (event.modifiers & Qt.ControlModifier) === 0) {
            event.accepted = true;
            return;
        }
        // The inline rename's editor keeps every key it does not use itself.
        if (filesTab.renamingRow >= 0) {
            event.accepted = true;
            return;
        }
        if (root.currentTab === 1 && (event.modifiers & Qt.ControlModifier) === 0
                && root._filesKey(event)) {
            event.accepted = true;
            return;
        }
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
            root.setPriority("skip");
            break;
        case Qt.Key_Y:
            root.copyCurrent();
            break;
        case Qt.Key_A:
            if (root.currentTab === 2)
                root.trackerAction("add", "");
            else if (root.currentTab === 3)
                root.peerAction("add", "");
            break;
        case Qt.Key_D:
            if (root.currentTab === 2)
                root.trackerAction("remove", root.details.trackers.urlAt(root.trackersCursor));
            break;
        case Qt.Key_R:
            if (root.currentTab === 2 && (event.modifiers & Qt.ShiftModifier))
                root.trackerAction("reannounce", root.details.trackers.urlAt(root.trackersCursor));
            break;
        case Qt.Key_B:
            if (root.currentTab === 3)
                root.peerAction("ban", root.details.peers.addressAt(root.peersCursor));
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
        width: root.headWidth - Theme.space(32)
        height: headColumn.implicitHeight + Theme.space(12) + Theme.space(8)

        Column {
            id: headColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.space(12)
            spacing: Theme.space(2)

            Text {
                color: Theme.brightForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Font.DemiBold
                text: root.torrent ? root.torrent.name : qsTr("no torrent selected")
                width: parent.width
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

                height: addedText.implicitHeight
                width: parent.width

                Text {
                    id: labelsText

                    color: Theme.dimForeground
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "headLabels"
                    text: !root.torrent ? qsTr("pick a row in the table") : metaLine.labels
                    width: Math.min(implicitWidth, metaLine.width - addedText.implicitWidth)
                }

                Text {
                    id: addedText

                    anchors.left: labelsText.right
                    color: Theme.dimForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.small
                    objectName: "headAdded"
                    // The separator travels with the date, so it survives the labels' cut.
                    text: !root.torrent || metaLine.added.length === 0 ? "" : metaLine.labels.length
                                                                             > 0 ? " · " + metaLine.added
                                                                                 : metaLine.added
                }
            }
        }
    }

    Strip {
        id: strip

        anchors.left: parent.left
        anchors.top: head.bottom
        width: root.headWidth
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

    // The rule between the left column and the tabs, while wide.
    Hairline {
        id: columnRule

        anchors.bottom: parent.bottom
        anchors.top: parent.top
        vertical: true
        visible: root.wide
        x: root.headWidth
    }

    Tabs {
        id: tabs

        anchors.left: root.wide ? columnRule.right : parent.left
        anchors.right: parent.right
        anchors.top: root.wide ? parent.top : strip.bottom
        currentIndex: root.currentTab
        // Set once; activateTab() keeps it in step afterwards.
        model: [
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

        onActivated: index => root.activateTab(index)
    }

    Item {
        id: body

        anchors.bottom: footer.top
        anchors.left: root.wide ? columnRule.right : parent.left
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

            // Another torrent may have fewer rows: keep the cursor on one.
            onCountChanged: root.generalCursor = Math.max(0, Math.min(root.generalCursor,
                                                                      generalTab.count - 1))
            onRowClicked: row => {
                root.generalCursor = row;
                root.copyGeneralRow(row);
            }
        }

        DetailsFilesTab {
            id: filesTab

            anchors.fill: parent
            cursorIndex: root.filesCursor
            model: root.details.files
            visible: root.currentTab === 1

            onFilterDone: root.forceActiveFocus()
            onRenameCancelled: root.cancelRename()
            onRenameCommitted: text => root.commitRename(text)
            onRowActivated: row => {
                if (!root.details.files.isFolder(row))
                    root.openFileRequested(root.details.files.pathAt(row));
            }
            onRowClicked: row => {
                root.filesCursor = row;
                if (root.details.files.isFolder(row))
                    root.details.files.toggle(row);
            }
            onRowMenuRequested: (row, x, y) => {
                root.filesCursor = row;
                const at = filesTab.mapToItem(root, x, y);
                root.openFileMenu(at.x, at.y);
            }
        }

        DetailsTrackersTab {
            id: trackersTab

            anchors.fill: parent
            cursorIndex: root.trackersCursor
            model: root.details.trackers
            visible: root.currentTab === 2

            onRowClicked: row => root.trackersCursor = row
        }

        DetailsPeersTab {
            id: peersTab

            anchors.fill: parent
            cursorIndex: root.peersCursor
            model: root.details.peers
            visible: root.currentTab === 3

            onRowClicked: row => root.peersCursor = row
        }
    }

    // The tab's own hints (spec §2 Fig. 2-4 `.foot .hints`), and since docs/12 G5 General's
    // (`j k` move, `y` copy value). Inside the window Main's footer teaches
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
                            }
                        ];
            }
        }
    }

    // --- the Files tab's menus (N1) -------------------------------------------------------
    // Kit/Menu over the whole pane, opened at the cursor row: `enter` or a right click for the
    // row's actions, `p` for its priority, `s` for the sort. Each row's key is the same key
    // the list itself answers to, where it has one.

    Menu {
        id: fileMenu

        readonly property bool onFolder: root.details.files.isFolder(root.filesCursor)
        readonly property int selected: root.details.files.selectedCount

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
                "text": fileMenu.onFolder ? qsTr("rename folder") : qsTr("rename"),
                "key": "r",
                "action": "rename"
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

    // A filter or a fold that shrinks the list pulls the cursor back onto a row.
    Connections {
        function onCountChanged(): void {
            if (root.filesCursor >= root.details.files.count)
                root.filesCursor = Math.max(0, root.details.files.count - 1);
        }

        target: root.details.files
    }

    // The Peers tab is the only one whose poll costs the server real work, so App is told
    // when it comes and goes (spec §2: peers refresh every 2 s while the tab is on show).
    // The pane's overall visibility is Main's to report, through App.setDetailsVisible().
    onCurrentTabChanged: {
        App.setPeersTabActive(!root.collapsed && root.currentTab === 3);
        App.setGeneralTabActive(!root.collapsed && root.currentTab === 0); // G3's pieces bar
    }
    onCollapsedChanged: {
        App.setPeersTabActive(!root.collapsed && root.currentTab === 3);
        App.setGeneralTabActive(!root.collapsed && root.currentTab === 0);
    }
}
