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
// (the spec figure shows the cursor on "movies" while "downloading" is the selected status):
// the keys move the cursor alone, and a click on a row selects it and moves the cursor there.
//
// docs/08 §5: groups fold. `collapsedGroups` names the folded ones (the host owns and persists
// it, per server); a click on a heading or `z` on any row asks for a toggle through
// `groupToggled`. A folded group leaves one cursor stop, its heading, where `enter`/`space`
// unfolds it. While the rail is scrolled, the heading of the group the top row belongs to
// sticks to the top edge, so a scrolled row always says which group it is in.
//
// docs/33 §9 (D12, D16–D18, DECISIONS C12): the servers are not a group of the scroll but a
// block pinned to the rail's bottom, outside it, on a band of their own (the idle fill under a
// hairline). Each row is a state dot, the name and what omaseed saw last (D5); the current
// one is selected. It folds to one line ("● private ▴ 3 servers"), app-wide (`serversFolded`,
// the host keeps it). Open, it lists 5 servers at most, then "… N more · ctrl+s"
// (`moreServersRequested`). When open it would leave the filters fewer than 5 rows, it shows
// folded by itself (not remembered); unfolded by hand then, it opens upwards over the filters,
// opaque with an accent edge, and folds again on esc or a pick. `serversOnly` (D1, D7) leaves
// the block alone: the empty library's rail, and a server lost before it gave a list.
Item {
    id: root

    required property TorrentFilterModel filtered
    required property FilterCounts counts
    required property CategoryModel categories
    required property TagModel tags
    required property TrackerModel trackers
    property int serverCount: 1
    property ServerStore servers: null
    /// The rows of the servers block (roles `name`, `isDefault`, `serverState`, `countLabel`). The
    /// host gates the block with `serverCount`, which it sets to 0 when the General pane hides
    /// it.
    property ServerListModel serverList: null
    /// docs/33 D17: the servers block folded to one line (the host keeps it, app-wide).
    property bool serversFolded: false
    /// docs/33 D1, D7: only the servers block; the filter groups wait for a list.
    property bool serversOnly: false
    /// docs/35 N3: the current server is gone and omaseed retries on its own (its count is
    /// yellow, not red).
    property bool serverRetrying: false
    /// docs/33 D18: at most this many servers open, then "… N more".
    readonly property int serversShownMax: 5

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

    // ---- per-name counts ------------------------------------------------------------
    /// FilterCounts' lookups by name are methods, so a binding calling one directly never
    /// re-runs: a category filled after launch read 0 for good. Every count binding passes
    /// `counts.revision`, which moves whenever the counts do.
    function _categoryCount(name: string, revision: int): int {
        return revision < 0 ? 0 : root.counts.categoryCount(name);
    }

    function _tagCount(name: string, revision: int): int {
        return revision < 0 ? 0 : root.counts.tagCount(name);
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
            if (item.globalIndex < 0 || !item.visible || item.pinned === true)
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
    /// docs/33 D18: "… N more · ctrl+s": the host opens the server palette.
    signal moreServersRequested

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
            // serversOnly (D1, D7): the filter rows are hidden and take no stop.
            if (index >= 0 && (!root.serversOnly || entry.item.pinned === true))
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
    /// D18: folded by itself while open would leave the filters fewer than 5 rows; `_serversForced`
    /// is a hand unfold then, which opens upwards over the filters.
    property bool _serversForced: false
    readonly property bool serversShort: !root.serversOnly && root.serversRowCount > 0
                                         && root.height - root._serversOpenHeight
                                         < 5 * Theme.railRowHeight + Theme.space(30)
    readonly property bool serversShownFolded: root.serversFolded
                                               || (root.serversShort && !root._serversForced)
    /// Open by hand in a short rail: drawn over the filters.
    readonly property bool serversOverlay: root.serversShort && root._serversForced && !root.serversFolded

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
        if (group === "servers" && root.serversShort && !root.serversFolded) {
            // D18: a short rail's hand unfold is for now, not remembered.
            root._serversForced = !root._serversForced;
        } else {
            root.groupToggled(group);
        }
        root.cursorIndex = root.baseOf(group);
        root._scrollCursorIntoView();
    }

    /// esc on the servers block opened over the filters folds it again (D18).
    function foldServersOverlay(): bool {
        if (!root.serversOverlay)
            return false;
        root._serversForced = false;
        return true;
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
    /// The rows the open block lists (D18), and whether "… N more" follows them.
    readonly property int serversListed: Math.min(root.serversRowCount, root.serversShownMax)
    readonly property bool serversMore: root.serversRowCount > root.serversShownMax

    // A folded group is one stop (its heading) in the cursor space. With `serversOnly` the
    // filter groups have none.
    readonly property int statusSpan: root.serversOnly ? 0 : root.statusFolded ? 1 : root.statusRowCount
    readonly property int categoriesSpan: root.serversOnly ? 0 : root.categoriesFolded ? 1 :
                                                                                         root.categoriesRowCount
    readonly property int tagsSpan: root.serversOnly ? 0 : root.tagsFolded ? 1 : root.tagsRowCount
    readonly property int trackersSpan: root.serversOnly ? 0 : root.trackersFolded ? 1 :
                                                                                     root.trackersRowCount
    // Folded, the block is its one line; open, its rows and "… N more".
    readonly property int serversSpan: root.serversRowCount === 0 ? 0 : root.serversShownFolded ? 1 :
                                                                                                root.serversListed
                                                                                                + (root.serversMore ? 1 : 0)

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

    /// A click on a row: the cursor goes there too, so the old cursor row loses its fill and
    /// the next `j`/`k` starts from the row clicked.
    function clickRow(index: int): void {
        if (index < 0)
            return;
        root.cursorIndex = index;
        root.selectCursor();
    }

    /// The rail's hover label: `full` under `row` while the pointer is on it and the row
    /// shows less (elided, or a tracker host shortened), hidden otherwise.
    function _nameTip(row: RailRow, hovering: bool, full: string): void {
        if (hovering && (row.truncated || row.text !== full)) {
            const point = row.mapToItem(root, Theme.space(16), row.height);
            hostTip.showAt(point.x, point.y, full);
        } else {
            hostTip.hide();
        }
    }

    function selectStatus(index) {
        if (index < 0 || index >= root.statusRowCount)
            return;
        root.filtered.statusFilter = index;
    }

    function _scrollCursorIntoView() {
        const entry = root._rowMap[root.cursorIndex];
        if (!entry || !entry.item || entry.item.pinned === true)
            return; // the servers block never scrolls (D12)
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
    implicitHeight: (root.serversOnly ? 0 : content.implicitHeight)
                    + (root.serversRowCount > 0 ? root._serversOpenHeight : 0)
    implicitWidth: Theme.space(200)
    width: root.implicitWidth

    Keys.onPressed: event => {
        // ctrl and alt chords belong to the window (`ctrl+k` jumps, `ctrl+b` hides this
        // rail), as in the table and the details pane.
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
            return;
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
        case Qt.Key_Escape:
            if (root.foldServersOverlay())
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

        // D12: the filters scroll above the servers block, never under it. Opened over the
        // filters (D18), the block keeps only its folded line's room.
        anchors.bottom: root.serversRowCount > 0 ? serversBlock.top : parent.bottom
        anchors.bottomMargin: root.serversOverlay ? -(serversBlock.height - root._serversFoldedHeight) : 0
        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: parent.top
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: content.implicitHeight
        contentWidth: width
        interactive: content.implicitHeight > height
        visible: !root.serversOnly

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

                        onClicked: root.clickRow(statusDelegate.globalIndex)

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

                    onClicked: root.clickRow(categoriesAll.globalIndex)

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

                        count: root._categoryCount(categoryDelegate.name, root.counts.revision)
                        glyph: Glyphs.category
                        hasCursor: root.cursorIndex === categoryDelegate.globalIndex
                        selected: root.filtered.category === categoryDelegate.name
                        text: categoryDelegate.name
                        visible: !root.categoriesFolded
                        width: content.width

                        onClicked: root.clickRow(categoryDelegate.globalIndex)
                        // The whole name, when the row elides it.
                        onHoverChanged: hovering => root._nameTip(categoryDelegate, hovering, categoryDelegate.name)

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

                    onClicked: root.clickRow(categoriesNone.globalIndex)

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

                    onClicked: root.clickRow(tagsAll.globalIndex)

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

                        count: root._tagCount(tagDelegate.name, root.counts.revision)
                        glyph: Glyphs.tag
                        hasCursor: root.cursorIndex === tagDelegate.globalIndex
                        selected: root.filtered.tag === tagDelegate.name
                        text: tagDelegate.name
                        visible: !root.tagsFolded
                        width: content.width

                        onClicked: root.clickRow(tagDelegate.globalIndex)
                        // The whole name, when the row elides it.
                        onHoverChanged: hovering => root._nameTip(tagDelegate, hovering, tagDelegate.name)

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

                    onClicked: root.clickRow(tagsUntagged.globalIndex)

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

                    onClicked: root.clickRow(trackersAll.globalIndex)

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

                        width: content.width

                        // The full host, whenever the row shows less of it.
                        onHoverChanged: hovering => root._nameTip(trackerDelegate, hovering,
                                                                  trackerDelegate.host)

                        onClicked: root.clickRow(trackerDelegate.globalIndex)

                        Component.onCompleted: root._registerRow(trackerDelegate, () => {
                                                                     root.filtered.tracker
                                                                             = trackerDelegate.host;
                                                                 })
                        Component.onDestruction: root._unregisterRow(trackerDelegate)
                    }
                }
            }
        }
    }

    // A wheel notch moves three rail rows, and quick notches add up (Kit/WheelScroll).
    WheelScroll {
        anchors.fill: flick
        flickable: flick
        objectName: "railWheel"
        rowHeight: Theme.railRowHeight
        z: 2
    }

    // ---- the scroll hint (docs/08 §5, the Kit look every list shares) ----------------
    ScrollHint {
        id: scrollHintOverlay

        anchors.bottom: flick.bottom
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

        objectName: "railTip"
        z: 4
    }

    // ---- the servers block (docs/33 §9: D12, D16–D18) ----------------------------------
    /// The block's height open and folded, for the short-window rule (D18).
    readonly property real _serversFoldedHeight: Theme.railRowHeight + Theme.space(12)
    readonly property real _serversOpenHeight: serversHeading.implicitHeight + Theme.space(12)
                                               + (root.serversListed + (root.serversMore ? 1 : 0))
                                               * Theme.railRowHeight

    /// A server row's dot (R2): the current one live, the others as last seen (D5).
    function _serverDotColor(state: string): color {
        switch (state) {
        case "live":
            return Theme.green;
        case "syncing":
            return Theme.yellow;
        case "down":
            return root.serverRetrying ? Theme.yellow : Theme.red; // docs/35 N3
        case "refused":
        case "certificate":
        case "failed":
            return Theme.red;
        default:
            return Theme.fill(Theme.faintAlpha);
        }
    }

    function _serverCountColor(text: string): color {
        if (text === "!")
            return Theme.red;
        if (text.length > 0 && isNaN(Number(text)))
            return root.serverRetrying ? Theme.yellow : Theme.red; // "down", docs/35 N3
        return Theme.dimForeground;
    }

    /// The current server's row, for the folded line: `{name, state}`.
    readonly property var _currentServer: {
        root._serversRevision;
        const list = root.serverList;
        if (!list)
            return {
                "name": "",
                "state": ""
            };
        for (let i = 0; i < list.count; ++i) {
            const map = list.serverMap(list.idAt(i));
            if (map.isCurrent === true)
                return map;
        }
        for (let i = 0; i < list.count; ++i) {
            const map = list.serverMap(list.idAt(i));
            if (map.isDefault === true)
                return map;
        }
        return {
            "name": "",
            "state": ""
        };
    }
    property int _serversRevision: 0

    Connections {
        function onDataChanged(): void {
            root._serversRevision++;
        }

        function onModelReset(): void {
            root._serversRevision++;
        }

        target: root.serverList
    }

    Rectangle {
        id: serversBlock

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: hairline.left
        // D16 a: a band of its own, the idle fill under a hairline. Opened over the filters
        // (D18) it is opaque, with an accent edge, like a menu.
        border.color: Theme.accent
        border.width: root.serversOverlay ? 1 : 0
        color: root.serversOverlay ? Theme.background : "transparent"
        height: root.serversShownFolded ? root._serversFoldedHeight : root._serversOpenHeight
        objectName: "serversBlock"
        visible: root.serverCount > 1 && root.serversRowCount > 0
        z: 5

        Rectangle {
            anchors.fill: parent
            anchors.margins: parent.border.width
            color: Theme.fill(Theme.fillIdle)
        }

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.space(6)

            // The heading: "servers · 3" and the fold chevron (D16 a). A click folds.
            RailGroup {
                id: serversHeading

                collapsed: false
                label: qsTr("servers · %1").arg(root.serversRowCount)
                objectName: "serversHeading"
                visible: !root.serversShownFolded
                width: parent.width

                onToggled: root.toggleGroup("servers")
            }

            // D17 a: folded, one line: the current server, its live dot, ▴, how many.
            RailRow {
                id: serversFoldedLine

                readonly property int globalIndex: root.serversShownFolded && root.serversRowCount > 0
                                                   ? root.serversBase : -1
                readonly property bool pinned: true

                countText: qsTr("%n servers", "", root.serversRowCount)
                glyph: Glyphs.dot
                glyphColor: root._serverDotColor(root._currentServer.serverState || "")
                hasCursor: root.cursorIndex === serversFoldedLine.globalIndex
                objectName: "serversFoldedLine"
                text: (root._currentServer.name || "") + " " + Glyphs.chevronUp
                visible: root.serversShownFolded
                width: parent.width

                onClicked: root.toggleGroup("servers")

                Component.onCompleted: root._registerRow(serversFoldedLine, () => root.toggleGroup("servers"))
                Component.onDestruction: root._unregisterRow(serversFoldedLine)
            }

            // One row per server, in Settings' order, 5 at most (D18). `id` is a QML keyword,
            // so the row's id comes from `idAt(index)` rather than from a required property.
            Repeater {
                model: root.serverList

                RailRow {
                    id: serverDelegate

                    required property int index
                    required property string name
                    required property bool isDefault
                    required property string serverState
                    required property string countLabel

                    readonly property bool listed: serverDelegate.index < root.serversShownMax
                    readonly property int globalIndex: root.serversShownFolded || root.serversRowCount === 0
                                                       || !serverDelegate.listed ? -1 : root.serversBase
                                                                                        + serverDelegate.index
                    readonly property bool pinned: true

                    countColor: root._serverCountColor(serverDelegate.countLabel)
                    countText: serverDelegate.countLabel
                    glyph: Glyphs.dot
                    glyphColor: root._serverDotColor(serverDelegate.serverState)
                    hasCursor: root.cursorIndex === serverDelegate.globalIndex
                    objectName: "serverRow-" + serverDelegate.name
                    selected: serverDelegate.isDefault
                    text: serverDelegate.name
                    visible: !root.serversShownFolded && serverDelegate.listed
                    width: parent.width

                    onClicked: root.clickRow(serverDelegate.globalIndex)

                    Component.onCompleted: root._registerRow(serverDelegate, () => {
                                                                 root._serversForced = false;
                                                                 root.serverActivated(root.serverList.idAt(
                                                                                          serverDelegate.index));
                                                             })
                    Component.onDestruction: root._unregisterRow(serverDelegate)
                }
            }

            // D18: past 5, one row that opens the server palette.
            RailRow {
                id: serversMoreRow

                readonly property int globalIndex: !root.serversShownFolded && root.serversMore
                                                   ? root.serversBase + root.serversListed : -1
                readonly property bool pinned: true

                glyph: "…"
                hasCursor: root.cursorIndex === serversMoreRow.globalIndex
                objectName: "serversMoreRow"
                text: qsTr("%1 more · ctrl+s").arg(root.serversRowCount - root.serversListed)
                visible: !root.serversShownFolded && root.serversMore
                width: parent.width

                onClicked: root.clickRow(serversMoreRow.globalIndex)

                Component.onCompleted: root._registerRow(serversMoreRow, () => root.moreServersRequested())
                Component.onDestruction: root._unregisterRow(serversMoreRow)
            }
        }
    }

    // docs/08 §5: while the rail is scrolled, the heading of the group the top row belongs to
    // sticks to the top edge. It is the real group's heading drawn again, so it folds too.
    readonly property var _groups: [statusGroup, categoriesGroup, tagsGroup, trackersGroup]
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
