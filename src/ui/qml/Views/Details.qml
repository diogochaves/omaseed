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

    /// Where the cursor sits inside the current tab's list. General has no list, so it is -1.
    readonly property int cursorIndex: root.currentTab === 1 ? root.filesCursor : root.currentTab
                                                               === 2 ? root.trackersCursor :
                                                                       root.currentTab === 3
                                                                       ? root.peersCursor : -1
    readonly property int rowCount: root.currentTab === 1 ? filesTab.count : root.currentTab === 2 ? trackersTab.count :
                                                                                                     root.currentTab
                                                                                                     === 3 ? peersTab.count :
                                                                                                             0

    // One cursor per list, so switching tabs and coming back keeps the place.
    property int filesCursor: 0
    property int trackersCursor: 0
    property int peersCursor: 0

    /// `o` on a file row: the path inside the torrent, for Xdg to reveal.
    signal openFolderRequested(string path)
    /// `h` on anything that is not an open folder hands the focus back to the table.
    signal focusTable
    /// `a` add, `d` remove, `R` reannounce (Phase 4 wires them; App only re-emits for now).
    signal trackerAction(string action, string url)
    /// `b` ban, `a` add peer.
    signal peerAction(string action, string address)
    /// `y`: the text the caller should put on the clipboard.
    signal copyRequested(string text)

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

    /// `+`, `-`, `x`: "raise", "lower" or "skip" on the Files tab's cursor row.
    function setPriority(name: string) {
        if (root.currentTab !== 1 || root.details.files.count === 0)
            return;
        const current = root.details.files.priority(root.filesCursor);
        const wanted = name === "skip" ? 0 : name === "raise" ? root.details.files.raisedPriority(
                                                                    current) :
                                                                root.details.files.loweredPriority(
                                                                    current);
        App.setFilePriority(root.details.hash, root.details.files.fileIndexes(root.filesCursor),
                            wanted);
    }

    /// `y` on the current row: the announce URL, the peer address, or the file path.
    function copyCurrent() {
        if (root.currentTab === 2)
            root.copyRequested(root.details.trackers.urlAt(root.trackersCursor));
        else if (root.currentTab === 3)
            root.copyRequested(root.details.peers.addressAt(root.peersCursor));
        else if (root.currentTab === 1 && root.details.files.count > 0)
            root.copyRequested(root.details.files.pathAt(root.filesCursor));
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
        }
    }

    // --- layout --------------------------------------------------------------------------

    implicitWidth: root.collapsed ? 0 : Theme.space(340)
    implicitHeight: Theme.space(600)
    visible: !root.collapsed

    Keys.onPressed: event => {
        switch (event.key) {
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
        case Qt.Key_O:
            if (root.currentTab === 1 && root.details.files.count > 0)
                root.openFolderRequested(root.details.files.pathAt(root.filesCursor));
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
            Text {
                color: Theme.dimForeground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: {
                    if (!root.torrent)
                        return qsTr("pick a row in the table");
                    const parts = [];
                    if (root.torrent.category)
                        parts.push(root.torrent.category);
                    if (root.torrent.tagsText)
                        parts.push(root.torrent.tagsText);
                    if (root.torrent.addedOnText)
                        parts.push(root.torrent.addedOnText);
                    return parts.join(" · ");
                }
                width: parent.width
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
            anchors.fill: parent
            // Two columns only with room for them: narrower, the values would wrap instead.
            columns: root.wide && body.width >= Theme.space(620) ? 2 : 1
            properties: root.details.properties
            torrent: root.torrent
            visible: root.currentTab === 0
        }

        DetailsFilesTab {
            id: filesTab

            anchors.fill: parent
            cursorIndex: root.filesCursor
            model: root.details.files
            visible: root.currentTab === 1

            onRowActivated: row => root.openFolderRequested(root.details.files.pathAt(row))
            onRowClicked: row => {
                root.filesCursor = row;
                if (root.details.files.isFolder(row))
                    root.details.files.toggle(row);
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

    // The tab's own hints (spec §2 Fig. 2-4 `.foot .hints`). General has none: Fig. 1 shows
    // the window's footer there instead. T3.5's HintModel replaces the source; the component
    // and the rows stay.
    Item {
        id: footer

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: (root.currentTab === 0 || !root.showHints) ? 0 : Math.round(Theme.font.body * 2.2)
        visible: root.currentTab !== 0 && root.showHints

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
            model: {
                if (root.currentTab === 1)
                    return [
                                {
                                    "key": "+ -",
                                    "label": qsTr("priority")
                                },
                                {
                                    "key": "x",
                                    "label": qsTr("skip")
                                },
                                {
                                    "key": "h l",
                                    "label": qsTr("fold")
                                },
                                {
                                    "key": "o",
                                    "label": qsTr("open")
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

    // The Peers tab is the only one whose poll costs the server real work, so App is told
    // when it comes and goes (spec §2: peers refresh every 2 s while the tab is on show).
    // The pane's overall visibility is Main's to report, through App.setDetailsVisible().
    onCurrentTabChanged: App.setPeersTabActive(!root.collapsed && root.currentTab === 3)
    onCollapsedChanged: App.setPeersTabActive(!root.collapsed && root.currentTab === 3)
}
