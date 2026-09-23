pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The left rail (spec v2 §1 Fig. 1's left column, CSS `.rail`/`.rail .grp`/`.rail .grp
// .lbl`/`.rail .row`, and the `RAIL` array in the page script): four tracked groups — status,
// categories, tags, trackers — plus a fifth, servers, only when there is more than one server
// configured. Every row is a Kit `RailRow`; the rail owns no filter state of its own, it only
// reads and writes `filtered`'s three-valued category/tag/tracker properties and its
// statusFilter enum.
//
// Keyboard (spec v2 §9 Fig. 26): `j`/`k` move a cursor across every visible row of every
// group, `g`/`G` jump to the first/last row, `enter`/`space` select the cursor row, `0`-`9`
// pick a status row directly (0 = all .. 8 = errored, 9 is unused — there are only nine status
// rows), `l` hands focus to the table via `focusTable()`. Cursor and selection are independent
// (the spec figure shows the cursor on "movies" while "downloading" is the selected status),
// so clicking a row only ever changes selection.
//
// docs/08 §5: groups fold. `collapsedGroups` names the folded ones (the host owns and persists
// it, per server); a click on a heading or `z` on any row asks for a toggle through
// `groupToggled`. A folded group leaves one cursor stop, its heading, where `enter`/`space`
// unfolds it. While the rail is scrolled, the heading of the group the top row belongs to
// sticks to the top edge, so a scrolled row always says which group it is in.
Item {
    id: root

    required property TorrentFilterModel filtered
    required property FilterCounts counts
    required property CategoryModel categories
    required property TagModel tags
    required property TrackerModel trackers
    property int serverCount: 1
    property ServerStore servers: null
    /// The rows of the SERVERS group (roles `name`, `isDefault`). The host gates the group
    /// with `serverCount`, which it sets to 0 when the General pane hides it.
    property ServerListModel serverList: null

    property int cursorIndex: 0
    /// The folded groups: any of "status", "categories", "tags", "trackers", "servers".
    property var collapsedGroups: []
    /// How the rail says it scrolls (Settings -> Appearance, docs/08 §5): "chevrons" (a
    /// counted line at each end), "fade" or "scrollbar".
    property string scrollHint: "chevrons"
    /// How a tracker host is fitted: "prefix" (drop a leading tracker./announce./open.),
    /// "left" (elide the start) or "right" (elide the end).
    property string trackerHostStyle: "prefix"

    /// The name a tracker row prints for `host` under `trackerHostStyle`.
    function trackerText(host: string): string {
        if (root.trackerHostStyle !== "prefix")
            return host;
        const short = host.replace(/^(tracker|announce|open)\./, "");
        // Never shorten a host down to a bare top-level domain.
        return short.indexOf(".") > 0 ? short : host;
    }

    // ---- what is scrolled out of view, for the hints -------------------------------
    /// Rows (and folded headings) wholly above / below the visible part of the rail.
    readonly property int rowsAbove: root._countOutside(true)
    readonly property int rowsBelow: root._countOutside(false)

    function _countOutside(above: bool): int {
        root._rowsRevision;
        const top = flick.contentY;
        const bottom = top + flick.height;
        let n = 0;
        for (const entry of root._rows) {
            const item = entry.item;
            if (item.globalIndex < 0 || !item.visible)
                continue;
            const y = item.mapToItem(content, 0, 0).y;
            if (above ? y + item.height <= top + 1 : y >= bottom - 1)
                ++n;
        }
        return n;
    }

    signal focusTable
    /// A heading was clicked, or `z` pressed on one of the group's rows.
    signal groupToggled(string group)
    /// `enter` or a click on a server row: that server should become the default one. The
    /// rail never switches servers itself — the host calls `App.setDefaultServer(id)`.
    signal serverActivated(string id)

    // ---- row bookkeeping -----------------------------------------------------------
    // Every row (and every group heading) registers itself with a function that selects it;
    // `_rowMap` maps each one's live `globalIndex` (the cursor position, -1 while it is not
    // a stop) to `{item, select}`, so moveCursor/selectCursor and the "keep the cursor row
    // visible" scroll can find it without the rail owning a copy of every model's rows. It is
    // rebuilt whenever a row comes or goes or an index moves (a group folds, a category is
    // added above).
    property var _rows: []
    property int _rowsRevision: 0
    readonly property var _rowMap: {
        root._rowsRevision;
        const map = {};
        for (const entry of root._rows) {
            const index = entry.item.globalIndex;
            if (index >= 0)
                map[index] = entry;
        }
        return map;
    }

    function _registerRow(item, selectFn) {
        root._rows.push({
                            "item": item,
                            "select": selectFn
                        });
        root._rowsRevision++;
    }
    function _unregisterRow(item) {
        root._rows = root._rows.filter(entry => entry.item !== item);
        root._rowsRevision++;
    }

    // ---- folding -------------------------------------------------------------------
    readonly property bool statusFolded: root.collapsedGroups.indexOf("status") >= 0
    readonly property bool categoriesFolded: root.collapsedGroups.indexOf("categories") >= 0
    readonly property bool tagsFolded: root.collapsedGroups.indexOf("tags") >= 0
    readonly property bool trackersFolded: root.collapsedGroups.indexOf("trackers") >= 0
    readonly property bool serversFolded: root.collapsedGroups.indexOf("servers") >= 0

    /// The group the cursor position `index` falls in.
    function groupAt(index: int): string {
        if (index >= root.serversBase && root.serversSpan > 0)
            return "servers";
        if (index >= root.trackersBase)
            return "trackers";
        if (index >= root.tagsBase)
            return "tags";
        if (index >= root.categoriesBase)
            return "categories";
        return "status";
    }

    function baseOf(group: string): int {
        switch (group) {
        case "categories":
            return root.categoriesBase;
        case "tags":
            return root.tagsBase;
        case "trackers":
            return root.trackersBase;
        case "servers":
            return root.serversBase;
        default:
            return root.statusBase;
        }
    }

    /// Fold or unfold `group` and leave the cursor on its first stop.
    function toggleGroup(group: string): void {
        root.groupToggled(group);
        root.cursorIndex = root.baseOf(group);
        root._scrollCursorIntoView();
    }

    // ---- the status group: nine fixed rows, spec v2 §9 `RAIL[0]` -------------------
    readonly property var statusRows: [
        {
            "text": qsTr("all"),
            "glyph": Glyphs.statusAll,
            "count": root.counts.total
        },
        {
            "text": qsTr("downloading"),
            "glyph": Glyphs.statusDownloading,
            "count": root.counts.downloading
        },
        {
            "text": qsTr("seeding"),
            "glyph": Glyphs.statusSeeding,
            "count": root.counts.seeding
        },
        {
            "text": qsTr("completed"),
            "glyph": Glyphs.statusCompleted,
            "count": root.counts.completed
        },
        {
            "text": qsTr("stopped"),
            "glyph": Glyphs.statusStopped,
            "count": root.counts.stopped
        },
        {
            "text": qsTr("active"),
            "glyph": Glyphs.statusActive,
            "count": root.counts.active
        },
        {
            "text": qsTr("stalled"),
            "glyph": Glyphs.statusStalled,
            "count": root.counts.stalled
        },
        {
            "text": qsTr("checking"),
            "glyph": Glyphs.statusChecking,
            "count": root.counts.checking
        },
        {
            "text": qsTr("errored"),
            "glyph": Glyphs.statusErrored,
            "count": root.counts.errored
        }
    ]

    // ---- group sizes and the flat cursor space --------------------------------------
    // "none"/"untagged" are hidden when their count is 0 and the group has real entries;
    // the whole categories/tags group collapses to just "all" when its model is empty.
    // Trackers need no such row: TrackerModel only carries a "trackerless" member while
    // that count is non-zero (see TrackerModel::rebuild), so it is already excluded.
    readonly property int statusRowCount: root.statusRows.length
    readonly property int categoriesNamedCount: root.categories ? root.categories.count : 0
    readonly property bool categoriesNoneVisible: root.categoriesNamedCount > 0
                                                  && root.counts.uncategorised > 0
    readonly property int categoriesRowCount: 1 + root.categoriesNamedCount + (
                                                  root.categoriesNoneVisible ? 1 : 0)
    readonly property int tagsNamedCount: root.tags ? root.tags.count : 0
    readonly property bool tagsUntaggedVisible: root.tagsNamedCount > 0 && root.counts.untagged > 0
    readonly property int tagsRowCount: 1 + root.tagsNamedCount + (root.tagsUntaggedVisible ? 1 : 0)
    readonly property int trackersNamedCount: root.trackers ? root.trackers.count : 0
    readonly property int trackersRowCount: 1 + root.trackersNamedCount
    readonly property int serversRowCount: root.serverCount > 1
                                           && root.serverList ? root.serverList.count : 0

    // A folded group is one stop (its heading) in the cursor space.
    readonly property int statusSpan: root.statusFolded ? 1 : root.statusRowCount
    readonly property int categoriesSpan: root.categoriesFolded ? 1 : root.categoriesRowCount
    readonly property int tagsSpan: root.tagsFolded ? 1 : root.tagsRowCount
    readonly property int trackersSpan: root.trackersFolded ? 1 : root.trackersRowCount
    readonly property int serversSpan: root.serversRowCount > 0 && root.serversFolded ? 1 :
                                                                                     root.serversRowCount

    readonly property int statusBase: 0
    readonly property int categoriesBase: root.statusBase + root.statusSpan
    readonly property int tagsBase: root.categoriesBase + root.categoriesSpan
    readonly property int trackersBase: root.tagsBase + root.tagsSpan
    readonly property int serversBase: root.trackersBase + root.trackersSpan
    readonly property int totalRowCount: root.serversBase + root.serversSpan

    onTotalRowCountChanged: if (root.cursorIndex >= root.totalRowCount)
                                root.cursorIndex = Math.max(0, root.totalRowCount - 1)

    // ---- public keyboard/selection surface ------------------------------------------
    function moveCursor(delta) {
        if (root.totalRowCount <= 0)
            return;
        const next = Math.max(0, Math.min(root.totalRowCount - 1, root.cursorIndex + delta));
        root.cursorIndex = next;
        root._scrollCursorIntoView();
    }

    function selectCursor() {
        const entry = root._rowMap[root.cursorIndex];
        if (entry && entry.select)
            entry.select();
    }

    function selectStatus(index) {
        if (index < 0 || index >= root.statusRowCount)
            return;
        root.filtered.statusFilter = index;
    }

    function _scrollCursorIntoView() {
        const entry = root._rowMap[root.cursorIndex];
        if (!entry || !entry.item)
            return;
        const point = entry.item.mapToItem(content, 0, 0);
        const top = point.y;
        const bottom = top + entry.item.height;
        if (top < flick.contentY)
            flick.contentY = top;
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = bottom - flick.height;
    }

    Accessible.role: Accessible.List

    activeFocusOnTab: true
    implicitHeight: content.implicitHeight
    implicitWidth: Theme.space(200)
    width: root.implicitWidth

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_J:
            root.moveCursor(1);
            event.accepted = true;
            break;
        case Qt.Key_K:
            root.moveCursor(-1);
            event.accepted = true;
            break;
        case Qt.Key_G:
            root.cursorIndex = (event.modifiers & Qt.ShiftModifier) ? root.totalRowCount - 1 : 0;
            root._scrollCursorIntoView();
            event.accepted = true;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            root.selectCursor();
            event.accepted = true;
            break;
        case Qt.Key_L:
            root.focusTable();
            event.accepted = true;
            break;
        case Qt.Key_Z:
            root.toggleGroup(root.groupAt(root.cursorIndex));
            event.accepted = true;
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
        case Qt.Key_9:
            root.selectStatus(event.key - Qt.Key_0);
            event.accepted = true;
            break;
        }
    }

    Flickable {
        id: flick

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: parent.top
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: content.implicitHeight
        contentWidth: width
        interactive: content.implicitHeight > height

        Column {
            id: content

            bottomPadding: Theme.space(10)
            spacing: Theme.space(14)
            topPadding: Theme.space(10)
            width: flick.width

            // ---- status ------------------------------------------------------------
            RailGroup {
                id: statusGroup

                readonly property int globalIndex: root.statusFolded ? root.statusBase : -1

                collapsed: root.statusFolded
                count: root.counts.total
                hasCursor: root.cursorIndex === statusGroup.globalIndex
                label: qsTr("status")
                width: content.width

                onToggled: root.toggleGroup("status")

                Component.onCompleted: root._registerRow(statusGroup, () => root.toggleGroup(
                                                             "status"))
                Component.onDestruction: root._unregisterRow(statusGroup)

                Repeater {
                    model: root.statusRows

                    RailRow {
                        id: statusDelegate

                        required property int index
                        required property var modelData

                        readonly property int globalIndex: root.statusFolded ? -1 : root.statusBase
                                                                                  + statusDelegate.index

                        count: statusDelegate.modelData.count
                        glyph: statusDelegate.modelData.glyph
                        hasCursor: root.cursorIndex === statusDelegate.globalIndex
                        // Every group always shows its answer: with no status filter, "all"
                        // is the selected row (docs/08 §5; the spec's RAIL array never
                        // selected it, REVIEW §9).
                        selected: root.filtered.statusFilter === statusDelegate.index
                        text: statusDelegate.modelData.text
                        visible: !root.statusFolded
                        width: content.width

                        onClicked: root.selectStatus(statusDelegate.index)

                        Component.onCompleted: root._registerRow(statusDelegate, ()
                                                                 => root.selectStatus(
                                                                        statusDelegate.index))
                        Component.onDestruction: root._unregisterRow(statusDelegate)
                    }
                }
            }

            // ---- categories ----------------------------------------------------------
            RailGroup {
                id: categoriesGroup

                readonly property int globalIndex: root.categoriesFolded ? root.categoriesBase : -1

                collapsed: root.categoriesFolded
                count: root.categoriesNamedCount
                hasCursor: root.cursorIndex === categoriesGroup.globalIndex
                label: qsTr("categories")
                width: content.width

                onToggled: root.toggleGroup("categories")

                Component.onCompleted: root._registerRow(categoriesGroup, () => root.toggleGroup(
                                                             "categories"))
                Component.onDestruction: root._unregisterRow(categoriesGroup)

                RailRow {
                    id: categoriesAll

                    readonly property int globalIndex: root.categoriesFolded ? -1 :
                                                                               root.categoriesBase

                    count: root.counts.total
                    hasCursor: root.cursorIndex === categoriesAll.globalIndex
                    // Selected while the group has no filter, like the status "all" row.
                    selected: root.filtered.category === undefined
                    text: qsTr("all")
                    visible: !root.categoriesFolded
                    width: content.width

                    onClicked: root.filtered.clearCategory()

                    Component.onCompleted: root._registerRow(categoriesAll, ()
                                                             => root.filtered.clearCategory())
                    Component.onDestruction: root._unregisterRow(categoriesAll)
                }

                Repeater {
                    model: root.categories

                    RailRow {
                        id: categoryDelegate

                        required property int index
                        required property string name

                        readonly property int globalIndex: root.categoriesFolded ? -1 :
                                                                                   root.categoriesBase
                                                                                   + 1
                                                                                   + categoryDelegate.index

                        count: root.counts.categoryCount(categoryDelegate.name)
                        glyph: Glyphs.category
                        hasCursor: root.cursorIndex === categoryDelegate.globalIndex
                        selected: root.filtered.category === categoryDelegate.name
                        text: categoryDelegate.name
                        visible: !root.categoriesFolded
                        width: content.width

                        onClicked: root.filtered.category = categoryDelegate.name

                        Component.onCompleted: root._registerRow(categoryDelegate, () => {
                                                                     root.filtered.category
                                                                             = categoryDelegate.name;
                                                                 })
                        Component.onDestruction: root._unregisterRow(categoryDelegate)
                    }
                }

                RailRow {
                    id: categoriesNone

                    readonly property int globalIndex: root.categoriesFolded
                                                       || !root.categoriesNoneVisible ? -1 :
                                                                                        root.categoriesBase
                                                                                        + 1
                                                                                        + root.categoriesNamedCount

                    count: root.counts.uncategorised
                    glyph: "·"
                    hasCursor: root.cursorIndex === categoriesNone.globalIndex
                    selected: root.filtered.category === ""
                    text: qsTr("none")
                    visible: root.categoriesNoneVisible && !root.categoriesFolded
                    width: content.width

                    onClicked: root.filtered.category = ""

                    Component.onCompleted: root._registerRow(categoriesNone, () => {
                                                                 root.filtered.category = "";
                                                             })
                    Component.onDestruction: root._unregisterRow(categoriesNone)
                }
            }

            // ---- tags ------------------------------------------------------------------
            RailGroup {
                id: tagsGroup

                readonly property int globalIndex: root.tagsFolded ? root.tagsBase : -1

                collapsed: root.tagsFolded
                count: root.tagsNamedCount
                hasCursor: root.cursorIndex === tagsGroup.globalIndex
                label: qsTr("tags")
                width: content.width

                onToggled: root.toggleGroup("tags")

                Component.onCompleted: root._registerRow(tagsGroup, () => root.toggleGroup("tags"))
                Component.onDestruction: root._unregisterRow(tagsGroup)

                RailRow {
                    id: tagsAll

                    readonly property int globalIndex: root.tagsFolded ? -1 : root.tagsBase

                    count: root.counts.total
                    hasCursor: root.cursorIndex === tagsAll.globalIndex
                    selected: root.filtered.tag === undefined
                    text: qsTr("all")
                    visible: !root.tagsFolded
                    width: content.width

                    onClicked: root.filtered.clearTag()

                    Component.onCompleted: root._registerRow(tagsAll, () => root.filtered.clearTag())
                    Component.onDestruction: root._unregisterRow(tagsAll)
                }

                Repeater {
                    model: root.tags

                    RailRow {
                        id: tagDelegate

                        required property int index
                        required property string name

                        readonly property int globalIndex: root.tagsFolded ? -1 : root.tagsBase + 1
                                                                                + tagDelegate.index

                        count: root.counts.tagCount(tagDelegate.name)
                        glyph: Glyphs.tag
                        hasCursor: root.cursorIndex === tagDelegate.globalIndex
                        selected: root.filtered.tag === tagDelegate.name
                        text: tagDelegate.name
                        visible: !root.tagsFolded
                        width: content.width

                        onClicked: root.filtered.tag = tagDelegate.name

                        Component.onCompleted: root._registerRow(tagDelegate, () => {
                                                                     root.filtered.tag
                                                                             = tagDelegate.name;
                                                                 })
                        Component.onDestruction: root._unregisterRow(tagDelegate)
                    }
                }

                RailRow {
                    id: tagsUntagged

                    readonly property int globalIndex: root.tagsFolded || !root.tagsUntaggedVisible
                                                       ? -1 : root.tagsBase + 1 + root.tagsNamedCount

                    count: root.counts.untagged
                    glyph: "·"
                    hasCursor: root.cursorIndex === tagsUntagged.globalIndex
                    selected: root.filtered.tag === ""
                    text: qsTr("untagged")
                    visible: root.tagsUntaggedVisible && !root.tagsFolded
                    width: content.width

                    onClicked: root.filtered.tag = ""

                    Component.onCompleted: root._registerRow(tagsUntagged, () => {
                                                                 root.filtered.tag = "";
                                                             })
                    Component.onDestruction: root._unregisterRow(tagsUntagged)
                }
            }

            // ---- trackers ----------------------------------------------------------------
            RailGroup {
                id: trackersGroup

                readonly property int globalIndex: root.trackersFolded ? root.trackersBase : -1

                collapsed: root.trackersFolded
                count: root.trackersNamedCount
                hasCursor: root.cursorIndex === trackersGroup.globalIndex
                label: qsTr("trackers")
                width: content.width

                onToggled: root.toggleGroup("trackers")

                Component.onCompleted: root._registerRow(trackersGroup, () => root.toggleGroup(
                                                             "trackers"))
                Component.onDestruction: root._unregisterRow(trackersGroup)

                RailRow {
                    id: trackersAll

                    readonly property int globalIndex: root.trackersFolded ? -1 : root.trackersBase

                    count: root.counts.total
                    hasCursor: root.cursorIndex === trackersAll.globalIndex
                    selected: root.filtered.tracker === undefined
                    text: qsTr("all")
                    visible: !root.trackersFolded
                    width: content.width

                    onClicked: root.filtered.clearTracker()

                    Component.onCompleted: root._registerRow(trackersAll, ()
                                                             => root.filtered.clearTracker())
                    Component.onDestruction: root._unregisterRow(trackersAll)
                }

                Repeater {
                    model: root.trackers

                    RailRow {
                        id: trackerDelegate

                        required property int index
                        required property bool isTrackerless
                        required property string host
                        required property int torrentCount

                        readonly property int globalIndex: root.trackersFolded ? -1 :
                                                                                 root.trackersBase
                                                                                 + 1
                                                                                 + trackerDelegate.index

                        count: trackerDelegate.torrentCount
                        glyph: trackerDelegate.isTrackerless ? "·" : Glyphs.tracker
                        hasCursor: root.cursorIndex === trackerDelegate.globalIndex
                        elideLeft: root.trackerHostStyle === "left"
                        selected: root.filtered.tracker === trackerDelegate.host
                        text: trackerDelegate.isTrackerless ? trackerDelegate.host :
                                                              root.trackerText(trackerDelegate.host)
                        visible: !root.trackersFolded

                        // The full host, whenever the row shows less of it.
                        onHoverChanged: hovering => {
                            if (hovering && (trackerDelegate.truncated || trackerDelegate.text
                                             !== trackerDelegate.host)) {
                                const point = trackerDelegate.mapToItem(root, Theme.space(16),
                                                                        trackerDelegate.height);
                                hostTip.showAt(point.x, point.y, trackerDelegate.host);
                            } else {
                                hostTip.hide();
                            }
                        }
                        width: content.width

                        onClicked: root.filtered.tracker = trackerDelegate.host

                        Component.onCompleted: root._registerRow(trackerDelegate, () => {
                                                                     root.filtered.tracker
                                                                             = trackerDelegate.host;
                                                                 })
                        Component.onDestruction: root._unregisterRow(trackerDelegate)
                    }
                }
            }

            // ---- servers -------------------------------------------------------------
            // One row per configured server, named, with the default one drawn selected the
            // way a status or category row is. `id` is a QML keyword, so the row's id comes
            // from `idAt(index)` rather than from a required property.
            RailGroup {
                id: serversGroup

                readonly property int globalIndex: root.serversFolded && root.serversRowCount > 0
                                                   ? root.serversBase : -1

                collapsed: root.serversFolded
                count: root.serversRowCount
                hasCursor: root.cursorIndex === serversGroup.globalIndex
                label: qsTr("servers")
                visible: root.serversRowCount > 0
                width: content.width

                onToggled: root.toggleGroup("servers")

                Component.onCompleted: root._registerRow(serversGroup, () => root.toggleGroup(
                                                             "servers"))
                Component.onDestruction: root._unregisterRow(serversGroup)

                Repeater {
                    model: root.serverList

                    RailRow {
                        id: serverDelegate

                        required property int index
                        required property string name
                        required property bool isDefault

                        readonly property int globalIndex: root.serversFolded
                                                           || root.serversRowCount === 0 ? -1 :
                                                                                           root.serversBase
                                                                                           + serverDelegate.index

                        count: -1
                        glyph: Glyphs.server
                        hasCursor: root.cursorIndex === serverDelegate.globalIndex
                        selected: serverDelegate.isDefault
                        text: serverDelegate.name
                        visible: !root.serversFolded
                        width: content.width

                        onClicked: root.serverActivated(root.serverList.idAt(
                                                            serverDelegate.index))

                        Component.onCompleted: root._registerRow(serverDelegate, () => {
                                                                     root.serverActivated(
                                                                         root.serverList.idAt(
                                                                             serverDelegate.index));
                                                                 })
                        Component.onDestruction: root._unregisterRow(serverDelegate)
                    }
                }
            }
        }
    }

    // ---- the scroll hint (docs/08 §5, the Kit look every list shares) ----------------
    ScrollHint {
        id: scrollHintOverlay

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: parent.top
        countAbove: root.rowsAbove
        countBelow: root.rowsBelow
        flickable: flick
        revealed: root.activeFocus
        style: root.scrollHint
        z: 3
    }

    Tooltip {
        id: hostTip

        z: 4
    }

    // docs/08 §5: while the rail is scrolled, the heading of the group the top row belongs to
    // sticks to the top edge. It is the real group's heading drawn again, so it folds too.
    readonly property var _groups: [statusGroup, categoriesGroup, tagsGroup, trackersGroup,
        serversGroup]
    /// The group whose heading has scrolled off while some of its rows are still in view.
    readonly property RailGroup stuckGroup: {
        const top = flick.contentY;
        if (top <= 0)
            return null;
        for (const group of root._groups) {
            if (!group.visible || group.collapsed)
                continue;
            if (group.y < top && group.y + group.height - group.header.height > top)
                return group;
        }
        return null;
    }

    Rectangle {
        id: stuckHeader

        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: parent.top
        anchors.topMargin: scrollHintOverlay.topInset
        color: Theme.background
        height: stuckLabel.implicitHeight + Theme.space(10)
        visible: root.stuckGroup !== null
        z: 2

        Label {
            id: stuckLabel

            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.verticalCenter: parent.verticalCenter
            text: root.stuckGroup ? root.stuckGroup.label : ""
            tracked: true
        }

        Hairline {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor

            onClicked: root.toggleGroup(root._groupKey(root.stuckGroup))
        }
    }

    function _groupKey(group: RailGroup): string {
        return group === statusGroup ? "status" : group === categoriesGroup ? "categories" :
                                                                              group === tagsGroup
                                                                              ? "tags" : group
                                                                                         === trackersGroup
                                                                                         ? "trackers" :
                                                                                           "servers";
    }

    Hairline {
        id: hairline

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        vertical: true
    }
}
