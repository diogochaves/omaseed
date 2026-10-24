pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit

// The left rail, the window's full height (board 46 `rail()`, DECISIONS C24 H2): the omaseed
// mark on top (`markClicked`), the window's views (library, seed, log) under it, then,
// scrolling, the five questions under "now" and the tracked groups (categories, folders,
// tags, trackers), and the servers pinned at its foot when there is more than one. Every row is
// a Kit `RailRow`; the rail owns no filter state of its own, it only reads and writes
// `filtered`'s question, its three-valued category/tag/tracker properties and its folder.
//
// Keyboard: `j`/`k` move a cursor across every visible row of every group, the views first,
// `g`/`G` jump to the first/last row, `enter`/`space` select the cursor row, `1`-`5` pick a
// question and `0` the library (C24 H2; 6–9 do nothing), `l` hands focus to the table via
// `focusTable()`. Cursor and selection are independent: the keys move the cursor alone, and a
// click on a row selects it and moves the cursor there.
//
// #167 (assumed 4): `compact` folds the rail to board 46's narrow column of glyphs (ctrl+b):
// the mark, the views, the questions, one row standing for the other groups, the servers' dots.
// A row's name is then its tooltip. The views and the servers are pinned: they never scroll.
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
    /// Board 42 F1 (DECISIONS C15 M1, M2): the library's save paths as a tree, after the
    /// categories. Null or empty draws no group.
    property FolderModel folders: null
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
    /// The window's face, for the views' rows: "library", "seed" or "log".
    property string view: "library"
    /// #167 assumed 4: folded to its glyphs (ctrl+b).
    property bool compact: false
    /// DECISIONS C24 H10: alt is held: the mark, the views, the questions and the servers
    /// show their keys.
    property bool keysShown: false
    /// The mark's height: the host gives it the hero's, so their hairlines meet.
    property real headHeight: Theme.railHeadHeight
    /// The mark in red, as the hero's was: the server is lost and nothing retries.
    property bool markAlert: false
    readonly property alias markButton: markButton
    /// C24 H2: the library row is lit on the library face with nothing narrowing it.
    readonly property bool libraryShown: root.view === "library"
                                         && root.filtered.question === TorrentFilterModel.AnyQuestion
                                         && root.filtered.category === undefined
                                         && root.filtered.tag === undefined
                                         && root.filtered.tracker === undefined
                                         && root.filtered.folder.length === 0

    property int cursorIndex: 0
    /// DECISIONS C19 T2 (#135): the cursor's fill shows only while the rail has the keys. A
    /// click still moves `cursorIndex`, so `j`/`k` start from the row clicked, but until the
    /// rail gets the keys only the selection is painted.
    readonly property int shownCursor: root.activeFocus ? root.cursorIndex : -1
    /// The row or heading at cursor stop `index` shows the cursor. One that is no stop now (-1:
    /// an open group's heading, a row of the folded rail) never does: without the keys
    /// `shownCursor` is -1 too, and every open heading read as under the cursor (#187).
    function cursorOn(index: int): bool {
        return index >= 0 && root.shownCursor === index;
    }
    /// The folded groups: any of "status" (the questions' "now", which kept its key),
    /// "categories", "folders", "tags", "trackers", "servers".
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
    /// C24 H2: a view row: the host shows that face. "library" comes after the rail has
    /// cleared its filters.
    signal viewPicked(string view)
    /// The mark: the host opens the app's menu under it (`markButton`), as ctrl+. does.
    signal markClicked
    /// The folded rail's row for its other groups: the host unfolds the rail.
    signal unfoldRequested
    /// A heading was clicked, or `z` pressed on one of the group's rows.
    signal groupToggled(string group)
    /// `enter` or a click on a server row: that server should become the default one. The
    /// rail never switches servers itself — the host calls `App.setDefaultServer(id)`.
    signal serverActivated(string id)
    /// docs/33 D18: "… N more · ctrl+s": the host opens the server palette.
    signal moreServersRequested
    /// A folder row was folded or unfolded (F2): the host keeps `folders.folded`.
    signal folderFoldToggled
    /// DECISIONS C19 T3 (C15 M1's "the row's menu"): a right-click on a folder row, or the
    /// menu key / `shift+F10` on the cursor's: the host opens its menu ("move this folder…")
    /// for `path` at `x`, `y` (this item's coordinates).
    signal folderMenuRequested(string path, real x, real y)

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
            // serversOnly (D1, D7): the filter rows are hidden and take no stop; folded
            // (`compact`), only the pinned rows, the questions and the groups' row do.
            if (index < 0 || (root.serversOnly && entry.item.pinned !== true))
                continue;
            if (root.compact && entry.item.pinned !== true && entry.item.compactStop !== true)
                continue;
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
    readonly property bool foldersFolded: root.collapsedGroups.indexOf("folders") >= 0
    readonly property bool tagsFolded: root.collapsedGroups.indexOf("tags") >= 0
    readonly property bool trackersFolded: root.collapsedGroups.indexOf("trackers") >= 0
    /// D18: folded by itself while open would leave the filters fewer than 5 rows; `_serversForced`
    /// is a hand unfold then, which opens upwards over the filters.
    property bool _serversForced: false
    readonly property bool serversShort: !root.serversOnly && root.serversRowCount > 0
                                         && root.height - root._topHeight - root._serversOpenHeight
                                         < 5 * Theme.railRowHeight + Theme.space(30)
    readonly property bool serversShownFolded: root.serversFolded
                                               || (root.serversShort && !root._serversForced)
    /// Open by hand in a short rail: drawn over the filters.
    readonly property bool serversOverlay: root.serversShort && root._serversForced && !root.serversFolded

    /// The group the cursor position `index` falls in.
    function groupAt(index: int): string {
        if (index < root.statusBase)
            return "views";
        if (index >= root.serversBase && root.serversSpan > 0)
            return "servers";
        if (index >= root.trackersBase)
            return "trackers";
        if (index >= root.tagsBase)
            return "tags";
        if (index >= root.foldersBase && root.foldersSpan > 0)
            return "folders";
        if (index >= root.categoriesBase)
            return "categories";
        return "status";
    }

    function baseOf(group: string): int {
        switch (group) {
        case "categories":
            return root.categoriesBase;
        case "folders":
            return root.foldersBase;
        case "tags":
            return root.tagsBase;
        case "trackers":
            return root.trackersBase;
        case "servers":
            return root.serversBase;
        case "views":
            return root.viewsBase;
        default:
            return root.statusBase;
        }
    }

    /// Fold or unfold `group` and leave the cursor on its first stop.
    function toggleGroup(group: string): void {
        // The views never fold, and the folded rail has no headings to fold.
        if (group === "views" || root.compact)
            return;
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

    // ---- the views and the five questions (board 46 `rail()`, DECISIONS C24 H2) -------
    readonly property var viewRows: [
        {
            "key": "library",
            "text": qsTr("library"),
            "glyph": Glyphs.viewLibrary,
            "shortcut": "0"
        },
        {
            "key": "seed",
            "text": qsTr("seed"),
            "glyph": Glyphs.viewSeed,
            "shortcut": "`"
        },
        {
            "key": "log",
            "text": qsTr("log"),
            "glyph": Glyphs.viewLog,
            "shortcut": "~"
        }
    ]

    /// In the rail's order, which is `TorrentFilterModel.Question`'s (keys 1–5).
    readonly property var questionRows: [
        {
            "key": "needsYou",
            "question": TorrentFilterModel.NeedsYou,
            "text": qsTr("needs you"),
            "glyph": Glyphs.questionNeedsYou,
            "count": root.counts.needsYou
        },
        {
            "key": "arriving",
            "question": TorrentFilterModel.Arriving,
            "text": qsTr("arriving"),
            "glyph": Glyphs.questionArriving,
            "count": root.counts.arriving
        },
        {
            "key": "landedToday",
            "question": TorrentFilterModel.LandedToday,
            "text": qsTr("landed today"),
            "glyph": Glyphs.questionLanded,
            "count": root.counts.landedToday
        },
        {
            "key": "givingBack",
            "question": TorrentFilterModel.GivingBack,
            "text": qsTr("giving back"),
            "glyph": Glyphs.questionGivingBack,
            "count": root.counts.givingBack
        },
        {
            "key": "resting",
            "question": TorrentFilterModel.Resting,
            "text": qsTr("resting"),
            "glyph": Glyphs.questionResting,
            "count": root.counts.resting
        }
    ]

    /// A question's row, by `TorrentFilterModel.Question`; null for AnyQuestion.
    function questionRow(question: int): var {
        return root.questionRows.find(row => row.question === question) ?? null;
    }

    // ---- group sizes and the flat cursor space --------------------------------------
    // "none"/"untagged" are hidden when their count is 0 and the group has real entries;
    // the whole categories/tags group collapses to just "all" when its model is empty.
    // Trackers need no such row: TrackerModel only carries a "trackerless" member while
    // that count is non-zero (see TrackerModel::rebuild), so it is already excluded.
    readonly property int viewRowCount: root.viewRows.length
    readonly property int statusRowCount: root.questionRows.length
    readonly property int categoriesNamedCount: root.categories ? root.categories.count : 0
    readonly property bool categoriesNoneVisible: root.categoriesNamedCount > 0
                                                  && root.counts.uncategorised > 0
    readonly property int categoriesRowCount: 1 + root.categoriesNamedCount + (
                                                  root.categoriesNoneVisible ? 1 : 0)
    readonly property int foldersRowCount: root.folders ? root.folders.count : 0
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
    // filter groups have none; folded (`compact`), the questions and one row for the rest.
    readonly property int viewsSpan: root.viewRowCount
    readonly property int statusSpan: root.serversOnly ? 0 : root.statusFolded && !root.compact ? 1 :
                                                                                                  root.statusRowCount
    readonly property int groupsRowSpan: root.compact && !root.serversOnly ? 1 : 0
    readonly property bool _groupsHidden: root.serversOnly || root.compact
    readonly property int categoriesSpan: root._groupsHidden ? 0 : root.categoriesFolded ? 1 :
                                                                                           root.categoriesRowCount
    readonly property int foldersSpan: root._groupsHidden || root.foldersRowCount === 0 ? 0 : root.foldersFolded
                                                                                           ? 1 : root.foldersRowCount
    readonly property int tagsSpan: root._groupsHidden ? 0 : root.tagsFolded ? 1 : root.tagsRowCount
    readonly property int trackersSpan: root._groupsHidden ? 0 : root.trackersFolded ? 1 :
                                                                                       root.trackersRowCount
    // Folded, the block is its one line; open, its rows and "… N more".
    readonly property int serversSpan: root.serversRowCount === 0 ? 0 : root.serversShownFolded ? 1 :
                                                                                                root.serversListed
                                                                                                + (root.serversMore ? 1 : 0)

    readonly property int viewsBase: 0
    readonly property int statusBase: root.viewsBase + root.viewsSpan
    readonly property int groupsRowBase: root.statusBase + root.statusSpan
    readonly property int categoriesBase: root.groupsRowBase + root.groupsRowSpan
    readonly property int foldersBase: root.categoriesBase + root.categoriesSpan
    readonly property int tagsBase: root.foldersBase + root.foldersSpan
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
    /// the next `j`/`k` starts from the row clicked (its fill shows once the rail has the
    /// keys, C19 T2).
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

    /// F2's hover: "4 here, 405 with the folders below" on a save path with others under it.
    function _folderTip(row: RailRow, hovering: bool, here: int, all: int, full: string): void {
        if (hovering && here !== all) {
            const point = row.mapToItem(root, Theme.space(16), row.height);
            hostTip.showAt(point.x, point.y, qsTr("%1: %2 here, %3 with the folders below").arg(full).arg(here).arg(
                               all));
        } else {
            root._nameTip(row, hovering, full);
        }
    }

    /// A folder row picked (M1): the table shows it and everything below it; picked again, the
    /// filter goes. A drive's top folds instead (F2).
    function _pickFolder(path: string, heading: bool): void {
        if (heading) {
            root.folders.toggleFold(path);
            root.folderFoldToggled();
            return;
        }
        root.filtered.folder = root.filtered.folder === path ? "" : path;
    }

    /// The menu key / `shift+F10` (C19 T3): the cursor row's menu, under its start, when it
    /// has one (a folder's save path; a drive's top heading has none).
    function openCursorMenu(): bool {
        const entry = root._rowMap[root.cursorIndex];
        const path = entry && entry.item ? entry.item.menuFolder : undefined;
        if (typeof path !== "string" || path.length === 0)
            return false;
        root._scrollCursorIntoView();
        const point = entry.item.mapToItem(root, Theme.space(24), entry.item.height);
        root.folderMenuRequested(path, point.x, point.y);
        return true;
    }

    /// `1`–`5` and the question rows: that question (C24 H2); `0` and AnyQuestion, the
    /// library.
    function selectQuestion(question: int): void {
        if (question === TorrentFilterModel.AnyQuestion) {
            root.pickLibrary();
            return;
        }
        if (root.questionRow(question) === null)
            return;
        root.filtered.question = question;
    }

    /// The library row and `0`: every torrent, so each filter the rail sets goes (board 46's
    /// library is the scope "all"), then the host shows the library's face.
    function pickLibrary(): void {
        root.filtered.question = TorrentFilterModel.AnyQuestion;
        root.filtered.clearCategory();
        root.filtered.clearTag();
        root.filtered.clearTracker();
        root.filtered.folder = "";
        root.viewPicked("library");
    }

    function pickView(key: string): void {
        if (key === "library")
            root.pickLibrary();
        else
            root.viewPicked(key);
    }

    /// DECISIONS C24 H10: a view's or a question's tooltip names it and its key, beside the
    /// folded rail as its name, under the open one.
    function _keyTip(row: RailRow, hovering: bool): void {
        const text = qsTr("%1 · %2").arg(row.text).arg(row.keyText);
        if (root.compact)
            root._compactTip(row, hovering, text);
        else
            root._nameTip(row, hovering, text);
    }

    /// The mark's tooltip: what it opens and its key.
    function _markTip(hovering: bool): void {
        const text = qsTr("omaseed menu · %1").arg("ctrl+.");
        if (root.compact) {
            root._compactTip(markButton, hovering, text);
        } else if (hovering) {
            const point = markButton.mapToItem(root, Theme.railMarkInset, markButton.height);
            hostTip.showAt(point.x, point.y, text);
        } else {
            hostTip.hide();
        }
    }

    /// The folded rail's tooltip: a row's name beside it, since only its glyph shows.
    function _compactTip(row: Item, hovering: bool, text: string): void {
        if (hovering && root.compact) {
            // showAt() keeps the panel its gap right of and below the point: level with the row.
            const gap = compactTip.panelItem.gap;
            const point = row.mapToItem(compactTip, row.width + Theme.railTipGap - gap,
                                        (row.height - compactTip.panelItem.height) / 2 - gap);
            compactTip.showAt(point.x, point.y, text);
        } else {
            compactTip.hide();
        }
    }

    /// A server row's tooltip (board 46: its name and address).
    // TODO(engines, DECISIONS C25 K12): "NAME · ENGINE VERSION" here.
    function serverTipText(index: int): string {
        if (!root.serverList)
            return "";
        const map = root.serverList.serverMap(root.serverList.idAt(index));
        return [map.name || "", map.urlText || ""].filter(part => part.length > 0).join(" · ");
    }

    function _serverTip(row: RailRow, hovering: bool, index: int): void {
        if (root.compact)
            root._compactTip(row, hovering, root.serverTipText(index));
        else // never the row's own text, so always shown
            root._nameTip(row, hovering, root.serverTipText(index));
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

    // #91: the rail's rows, by what they do.
    Accessible.name: qsTr("filters")
    Accessible.role: Accessible.List

    activeFocusOnTab: true
    implicitHeight: root._topHeight + (root.serversOnly ? 0 : content.implicitHeight)
                    + (root.serversRowCount > 0 ? root._serversOpenHeight : 0)
    implicitWidth: root.compact ? Theme.railFoldedWidth : Theme.railWidth
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
        case Qt.Key_Menu:
            if (root.openCursorMenu())
                event.accepted = true;
            break;
        case Qt.Key_F10:
            if ((event.modifiers & Qt.ShiftModifier) && root.openCursorMenu())
                event.accepted = true;
            break;
        case Qt.Key_0:
        case Qt.Key_1:
        case Qt.Key_2:
        case Qt.Key_3:
        case Qt.Key_4:
        case Qt.Key_5:
            // The log face has no questions (docs/32 L1): its window swallows the digits.
            if (root.serversOnly)
                break;
            root.selectQuestion(event.key - Qt.Key_0);
            event.accepted = true;
            break;
        case Qt.Key_6:
        case Qt.Key_7:
        case Qt.Key_8:
        case Qt.Key_9:
            event.accepted = true; // C24 H2: nothing, as in the window
            break;
        }
    }

    // ---- the mark and the views (board 46 `.mark`, `.views`), pinned ----------------------
    /// What sits above the scroll: the mark, the views, their rule.
    readonly property real _topHeight: root.headHeight + viewsBlock.height + 1

    Rectangle {
        id: markButton

        // C24 H9: the omaseed menu, as ctrl+. opens it; its key badged while alt is held (H10).
        Accessible.description: qsTr("shortcut %1").arg("ctrl+.")
        Accessible.name: qsTr("omaseed menu")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: if (markButton.enabled)
                                      root.markClicked()
        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: parent.top
        color: markMouse.containsMouse ? markInk.fill : "transparent"
        height: root.headHeight
        objectName: "railMark"

        // docs/DECISIONS.md C30 F1: the mark's words on the hover fill while the pointer is on
        // it; the fill comes and goes at once, so do they.
        Ink {
            id: markInk

            animated: false
            ground: markMouse.containsMouse ? Theme.on.hover : Theme.on.window
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: root.compact ? Math.round((markButton.width - markGlyph.width) / 2)
                                             : Theme.railMarkInset
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.railMarkGap

            Glyph {
                id: markGlyph

                anchors.verticalCenter: parent.verticalCenter
                color: root.markAlert ? markInk.red : markInk.accent
                height: Theme.railMarkSize
                pixelSize: Theme.railMarkSize
                text: Glyphs.mark
                width: Theme.railMarkSize
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: markInk.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.body
                font.weight: Theme.railMarkWeight
                text: qsTr("omaseed")
                visible: !root.compact
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: markInk.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: Glyphs.chevronDown
                visible: !root.compact
            }
        }

        Hairline {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
        }

        MouseArea {
            id: markMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: root.markClicked()
            onContainsMouseChanged: root._markTip(markMouse.containsMouse)
        }

        KeyBadge {
            anchors.right: parent.right
            anchors.rightMargin: root.compact ? Theme.keyBadgeEdge : Theme.railMarkInset
            anchors.verticalCenter: parent.verticalCenter
            corner: false
            key: "ctrl+."
            objectName: "railMarkKey"
            shown: root.keysShown
        }
    }

    Column {
        id: viewsBlock

        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: markButton.bottom
        bottomPadding: Theme.railViewsPadding
        topPadding: Theme.railViewsPadding

        Repeater {
            model: root.viewRows

            RailRow {
                id: viewDelegate

                required property int index
                required property var modelData

                readonly property int globalIndex: root.viewsBase + viewDelegate.index
                readonly property bool pinned: true

                compact: root.compact
                count: viewDelegate.modelData.key === "library" ? root.counts.total : -1
                current: viewDelegate.modelData.key === "library" ? root.libraryShown :
                                                                    root.view === viewDelegate.modelData.key
                glyph: viewDelegate.modelData.glyph
                hasCursor: root.cursorOn(viewDelegate.globalIndex)
                keyText: viewDelegate.modelData.shortcut
                keysShown: root.keysShown
                objectName: "railView-" + viewDelegate.modelData.key
                text: viewDelegate.modelData.text
                width: viewsBlock.width

                onClicked: root.clickRow(viewDelegate.globalIndex)
                onHoverChanged: hovering => root._keyTip(viewDelegate, hovering)

                Component.onCompleted: root._registerRow(viewDelegate, () => root.pickView(
                                                             viewDelegate.modelData.key))
                Component.onDestruction: root._unregisterRow(viewDelegate)
            }
        }
    }

    Hairline {
        id: viewsRule

        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: viewsBlock.bottom
    }

    Flickable {
        id: flick

        // D12: the filters scroll above the servers block, never under it. Opened over the
        // filters (D18), the block keeps only its folded line's room.
        anchors.bottom: root.serversRowCount > 0 ? serversBlock.top : parent.bottom
        anchors.bottomMargin: root.serversOverlay ? -(serversBlock.height - root._serversFoldedHeight) : 0
        anchors.left: parent.left
        anchors.right: hairline.left
        anchors.top: viewsRule.bottom
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

            // ---- now: the five questions (DECISIONS C24 H2) ---------------------------
            RailGroup {
                id: statusGroup

                readonly property int globalIndex: root.statusFolded && !root.compact ? root.statusBase : -1

                collapsed: root.statusFolded && !root.compact
                count: root.counts.total
                hasCursor: root.cursorOn(statusGroup.globalIndex)
                headed: !root.compact
                label: qsTr("now")
                width: content.width

                onToggled: root.toggleGroup("status")

                Component.onCompleted: root._registerRow(statusGroup, () => root.toggleGroup(
                                                             "status"))
                Component.onDestruction: root._unregisterRow(statusGroup)

                Repeater {
                    model: root.questionRows

                    RailRow {
                        id: questionDelegate

                        required property int index
                        required property var modelData

                        readonly property bool shown: !root.statusFolded || root.compact
                        readonly property int globalIndex: questionDelegate.shown ? root.statusBase
                                                                                    + questionDelegate.index : -1
                        readonly property bool compactStop: true
                        /// Board 46 `.ri.needs`: red while something needs you.
                        readonly property bool alarm: questionDelegate.modelData.question
                                                      === TorrentFilterModel.NeedsYou
                                                      && questionDelegate.modelData.count > 0

                        compact: root.compact
                        count: questionDelegate.modelData.count
                        countColor: questionDelegate.alarm ? Theme.red : Theme.dimForeground
                        glyph: questionDelegate.modelData.glyph
                        glyphColor: questionDelegate.alarm ? Theme.red : questionDelegate.selected
                                                                         ? Theme.accent : Theme.dimForeground
                        hasCursor: root.cursorOn(questionDelegate.globalIndex)
                        // C24 H2: 1–5 in the rail's order.
                        keyText: String(questionDelegate.index + 1)
                        keysShown: root.keysShown
                        objectName: "railQuestion-" + questionDelegate.modelData.key
                        selected: root.filtered.question === questionDelegate.modelData.question
                        text: questionDelegate.modelData.text
                        visible: questionDelegate.shown
                        width: content.width

                        onClicked: root.clickRow(questionDelegate.globalIndex)
                        onHoverChanged: hovering => root._keyTip(questionDelegate, hovering)

                        Component.onCompleted: root._registerRow(questionDelegate, ()
                                                                 => root.selectQuestion(
                                                                        questionDelegate.modelData.question))
                        Component.onDestruction: root._unregisterRow(questionDelegate)
                    }
                }
            }

            // Folded: one row for the groups below, which unfolds the rail; lit while one of
            // them narrows the library.
            RailRow {
                id: groupsRow

                readonly property int globalIndex: root.groupsRowSpan > 0 ? root.groupsRowBase : -1
                readonly property bool compactStop: true

                compact: true
                glyph: Glyphs.railGroups
                hasCursor: root.cursorOn(groupsRow.globalIndex)
                objectName: "railGroupsRow"
                selected: root.filtered.category !== undefined || root.filtered.tag !== undefined
                          || root.filtered.tracker !== undefined || root.filtered.folder.length > 0
                text: qsTr("categories, folders, tags, trackers")
                visible: root.compact
                width: content.width

                onClicked: root.clickRow(groupsRow.globalIndex)
                onHoverChanged: hovering => root._compactTip(groupsRow, hovering, groupsRow.text)

                Component.onCompleted: root._registerRow(groupsRow, () => root.unfoldRequested())
                Component.onDestruction: root._unregisterRow(groupsRow)
            }

            // ---- categories ----------------------------------------------------------
            RailGroup {
                id: categoriesGroup

                readonly property int globalIndex: root.categoriesFolded ? root.categoriesBase : -1

                collapsed: root.categoriesFolded
                count: root.categoriesNamedCount
                hasCursor: root.cursorOn(categoriesGroup.globalIndex)
                label: qsTr("categories")
                visible: !root.compact
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
                    hasCursor: root.cursorOn(categoriesAll.globalIndex)
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
                        hasCursor: root.cursorOn(categoryDelegate.globalIndex)
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
                    hasCursor: root.cursorOn(categoriesNone.globalIndex)
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

            // ---- folders (board 42 F1–F3) -----------------------------------------------
            RailGroup {
                id: foldersGroup

                readonly property int globalIndex: root.foldersFolded ? root.foldersBase : -1

                collapsed: root.foldersFolded
                count: root.foldersRowCount
                hasCursor: root.cursorOn(foldersGroup.globalIndex)
                label: qsTr("folders")
                visible: root.foldersRowCount > 0 && !root.compact
                width: content.width

                onToggled: root.toggleGroup("folders")

                Component.onCompleted: root._registerRow(foldersGroup, () => root.toggleGroup("folders"))
                Component.onDestruction: root._unregisterRow(foldersGroup)

                Repeater {
                    model: root.folders

                    RailRow {
                        id: folderDelegate

                        required property int index
                        required property string path
                        required property string name
                        required property int depth
                        required property bool heading
                        required property bool hasChildren
                        required property bool folded
                        required property int folderCount
                        required property int folderCountHere
                        required property string mark

                        readonly property int globalIndex: root.foldersFolded ? -1 : root.foldersBase
                                                                                   + folderDelegate.index
                        /// C19 T3: what the row's menu moves; a drive's top heading has none.
                        readonly property string menuFolder: folderDelegate.heading ? "" : folderDelegate.path

                        // F1: a drive's top holds no torrents itself; a save path counts them and
                        // all below it. F3: a move's mark takes the count's place.
                        count: folderDelegate.heading ? -1 : folderDelegate.folderCount
                        countText: folderDelegate.mark
                        dimText: folderDelegate.heading
                        // A path below its parent row: the end tells two apart (M3).
                        elideLeft: true
                        glyph: folderDelegate.hasChildren ? (folderDelegate.folded ? Glyphs.chevronRight
                                                                                   : Glyphs.chevronDown)
                                                          : Glyphs.middot
                        glyphClickable: folderDelegate.hasChildren
                        hasCursor: root.cursorOn(folderDelegate.globalIndex)
                        hasMenu: folderDelegate.menuFolder.length > 0
                        indent: folderDelegate.depth
                        objectName: "folderRow-" + folderDelegate.path
                        selected: root.filtered.folder === folderDelegate.path
                        text: folderDelegate.name
                        visible: !root.foldersFolded
                        width: content.width

                        onClicked: root.clickRow(folderDelegate.globalIndex)
                        // C19 T3: the menu acts on this row's folder; the cursor goes there,
                        // as a right-click on a table row moves the table's.
                        onMenuRequested: (x, y) => {
                            root.cursorIndex = folderDelegate.globalIndex;
                            const point = folderDelegate.mapToItem(root, x, y);
                            root.folderMenuRequested(folderDelegate.path, point.x, point.y);
                        }
                        onGlyphClicked: {
                            root.folders.toggleFold(folderDelegate.path);
                            root.folderFoldToggled();
                        }
                        onHoverChanged: hovering => root._folderTip(folderDelegate, hovering,
                                                                    folderDelegate.folderCountHere,
                                                                    folderDelegate.folderCount,
                                                                    folderDelegate.path)

                        Component.onCompleted: root._registerRow(folderDelegate, () => root._pickFolder(
                                                                     folderDelegate.path,
                                                                     folderDelegate.heading))
                        Component.onDestruction: root._unregisterRow(folderDelegate)
                    }
                }
            }

            // ---- tags ------------------------------------------------------------------
            RailGroup {
                id: tagsGroup

                readonly property int globalIndex: root.tagsFolded ? root.tagsBase : -1

                collapsed: root.tagsFolded
                count: root.tagsNamedCount
                hasCursor: root.cursorOn(tagsGroup.globalIndex)
                label: qsTr("tags")
                visible: !root.compact
                width: content.width

                onToggled: root.toggleGroup("tags")

                Component.onCompleted: root._registerRow(tagsGroup, () => root.toggleGroup("tags"))
                Component.onDestruction: root._unregisterRow(tagsGroup)

                RailRow {
                    id: tagsAll

                    readonly property int globalIndex: root.tagsFolded ? -1 : root.tagsBase

                    count: root.counts.total
                    hasCursor: root.cursorOn(tagsAll.globalIndex)
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
                        hasCursor: root.cursorOn(tagDelegate.globalIndex)
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
                    hasCursor: root.cursorOn(tagsUntagged.globalIndex)
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
                hasCursor: root.cursorOn(trackersGroup.globalIndex)
                label: qsTr("trackers")
                visible: !root.compact
                width: content.width

                onToggled: root.toggleGroup("trackers")

                Component.onCompleted: root._registerRow(trackersGroup, () => root.toggleGroup(
                                                             "trackers"))
                Component.onDestruction: root._unregisterRow(trackersGroup)

                RailRow {
                    id: trackersAll

                    readonly property int globalIndex: root.trackersFolded ? -1 : root.trackersBase

                    count: root.counts.total
                    hasCursor: root.cursorOn(trackersAll.globalIndex)
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

                        // #78: the whole host, whichever form the row prints (a setting).
                        Accessible.name: trackerDelegate.host
                        count: trackerDelegate.torrentCount
                        glyph: trackerDelegate.isTrackerless ? "·" : Glyphs.tracker
                        hasCursor: root.cursorOn(trackerDelegate.globalIndex)
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
        anchors.top: flick.top
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

    // Folded, a row's name shows beside the 46 px column, over the window's content, so this
    // one lives on the window's content item (as the hero's server line's, KIT-8).
    Tooltip {
        id: compactTip

        objectName: "railCompactTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 5
    }

    // ---- the servers block (docs/33 §9: D12, D16–D18) ----------------------------------
    /// The block's height open and folded, for the short-window rule (D18).
    readonly property real _serversFoldedHeight: Theme.railRowHeight + Theme.space(12)
    readonly property real _serversOpenHeight: (root.compact ? 0 : serversHeading.implicitHeight) + Theme.space(12)
                                               + (root.serversListed + (root.serversMore ? 1 : 0))
                                               * Theme.railRowHeight

    /// A server row's dot (R2): the current one live, the others as last seen (D5).
    function _serverDotColor(state: string): color {
        switch (state) {
        case "live":
            return Theme.green;
        case "syncing":
            return Theme.yellow;
        case "waiting": // docs/44 D11: a timeout, there but not answering
            return Theme.yellow;
        case "down":
            return Theme.red; // docs/44 D11: nothing there
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
            return root.serverRetrying ? Theme.yellow : Theme.red; // "waiting" / "down", docs/44 D11
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

        // docs/DECISIONS.md C30 F1: the block's rows and heading are on this fill
        // (`block: Theme.on.idle`), hovered on the hover fill over it.
        Rectangle {
            anchors.fill: parent
            anchors.margins: parent.border.width
            color: Theme.on.idle.fill
        }

        Hairline {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }

        // C24 H10: ctrl+s, the server palette, on the block's top edge while alt is held.
        KeyBadge {
            anchors.right: parent.right
            anchors.rightMargin: root.compact ? Theme.keyBadgeEdge : Theme.space(16)
            anchors.verticalCenter: parent.top
            corner: false
            key: "ctrl+s"
            objectName: "serversKey"
            shown: root.keysShown
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.space(6)

            // The heading: "servers · 3" and the fold chevron (D16 a). A click folds.
            RailGroup {
                id: serversHeading

                // #78: "servers", the count in the description.
                accessibleDetail: qsTr("%n server(s)", "", root.serversRowCount)
                accessibleName: qsTr("servers")
                block: Theme.on.idle
                collapsed: false
                label: qsTr("servers · %1").arg(root.serversRowCount)
                objectName: "serversHeading"
                visible: !root.serversShownFolded && !root.compact
                width: parent.width

                onToggled: root.toggleGroup("servers")
            }

            // D17 a: folded, one line: the current server, its live dot, ▴, how many.
            RailRow {
                id: serversFoldedLine

                readonly property int globalIndex: root.serversShownFolded && root.serversRowCount > 0
                                                   ? root.serversBase : -1
                readonly property bool pinned: true

                // #78: the folded group is "servers", as its heading; the server it is on, how
                // many and the fold go in the description, and the ▴ in neither.
                Accessible.description: [root._currentServer.name || "",
                                         serversFoldedLine.countText, qsTr("folded")].filter(
                                            part => part.length > 0).join(" · ")
                Accessible.name: qsTr("servers")
                block: Theme.on.idle
                compact: root.compact
                countText: qsTr("%n servers", "", root.serversRowCount)
                glyph: Glyphs.dot
                glyphColor: root._serverDotColor(root._currentServer.serverState || "")
                hasCursor: root.cursorOn(serversFoldedLine.globalIndex)
                objectName: "serversFoldedLine"
                text: (root._currentServer.name || "") + " " + Glyphs.chevronUp
                visible: root.serversShownFolded
                width: parent.width

                onClicked: root.toggleGroup("servers")
                onHoverChanged: hovering => root._compactTip(serversFoldedLine, hovering,
                                                             root._currentServer.name || "")

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

                    block: Theme.on.idle
                    compact: root.compact
                    countColor: root._serverCountColor(serverDelegate.countLabel)
                    countText: serverDelegate.countLabel
                    glyph: Glyphs.dot
                    glyphColor: root._serverDotColor(serverDelegate.serverState)
                    hasCursor: root.cursorOn(serverDelegate.globalIndex)
                    objectName: "serverRow-" + serverDelegate.name
                    selected: serverDelegate.isDefault
                    text: serverDelegate.name
                    visible: !root.serversShownFolded && serverDelegate.listed
                    width: parent.width

                    onClicked: root.clickRow(serverDelegate.globalIndex)
                    onHoverChanged: hovering => root._serverTip(serverDelegate, hovering,
                                                                serverDelegate.index)

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

                // #78: named by what it opens; how many more and the key in the description.
                Accessible.description: qsTr("%1 more · shortcut %2").arg(
                                            root.serversRowCount - root.serversListed).arg("ctrl+s")
                Accessible.name: qsTr("more servers")
                block: Theme.on.idle
                compact: root.compact
                glyph: "…"
                hasCursor: root.cursorOn(serversMoreRow.globalIndex)
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
        if (top <= 0 || root.compact)
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
        anchors.top: flick.top
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

        // a11y-ignore A2: a copy of the heading scrolled under it, which is in the tree (#77)
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
