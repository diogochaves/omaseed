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

    signal focusTable
    /// `enter` or a click on a server row: that server should become the default one. The
    /// rail never switches servers itself — the host calls `App.setDefaultServer(id)`.
    signal serverActivated(string id)

    // ---- row bookkeeping -----------------------------------------------------------
    // Each visible row registers itself here (global cursor index -> {item, select}) so
    // moveCursor/selectCursor and the "keep the cursor row visible" scroll can find it
    // without the rail owning a duplicate copy of every model's rows.
    property var _rowMap: ({})

    function _registerRow(globalIndex, item, selectFn) {
        root._rowMap[globalIndex] = {
            "item": item,
            "select": selectFn
        };
    }
    function _unregisterRow(globalIndex) {
        delete root._rowMap[globalIndex];
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

    readonly property int statusBase: 0
    readonly property int categoriesBase: root.statusBase + root.statusRowCount
    readonly property int tagsBase: root.categoriesBase + root.categoriesRowCount
    readonly property int trackersBase: root.tagsBase + root.tagsRowCount
    readonly property int serversBase: root.trackersBase + root.trackersRowCount
    readonly property int totalRowCount: root.serversBase + root.serversRowCount

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
                label: qsTr("status")
                width: content.width

                Repeater {
                    model: root.statusRows

                    RailRow {
                        id: statusDelegate

                        required property int index
                        required property var modelData

                        count: statusDelegate.modelData.count
                        glyph: statusDelegate.modelData.glyph
                        hasCursor: root.cursorIndex === (root.statusBase + statusDelegate.index)
                        // "all" (index 0) is never a real narrowing filter — the figure's
                        // RAIL[0][0] carries no "sel" class — so it stays plain even while it
                        // is logically the active status filter.
                        selected: statusDelegate.index !== 0 && root.filtered.statusFilter
                                  === statusDelegate.index
                        text: statusDelegate.modelData.text
                        width: content.width

                        onClicked: root.selectStatus(statusDelegate.index)

                        Component.onCompleted: root._registerRow(root.statusBase
                                                                 + statusDelegate.index,
                                                                 statusDelegate, ()
                                                                 => root.selectStatus(
                                                                        statusDelegate.index))
                        Component.onDestruction: root._unregisterRow(root.statusBase
                                                                     + statusDelegate.index)
                    }
                }
            }

            // ---- categories ----------------------------------------------------------
            RailGroup {
                label: qsTr("categories")
                width: content.width

                RailRow {
                    id: categoriesAll

                    count: root.counts.total
                    hasCursor: root.cursorIndex === root.categoriesBase
                    // "all" is never a real narrowing filter (spec RAIL's "all" rows carry no
                    // "sel" class), so it stays plain even while it is the active filter.
                    selected: false
                    text: qsTr("all")
                    width: content.width

                    onClicked: root.filtered.clearCategory()

                    Component.onCompleted: root._registerRow(root.categoriesBase, categoriesAll, ()
                                                             => root.filtered.clearCategory())
                    Component.onDestruction: root._unregisterRow(root.categoriesBase)
                }

                Repeater {
                    model: root.categories

                    RailRow {
                        id: categoryDelegate

                        required property int index
                        required property string name

                        readonly property int globalIndex: root.categoriesBase + 1
                                                           + categoryDelegate.index

                        count: root.counts.categoryCount(categoryDelegate.name)
                        glyph: Glyphs.category
                        hasCursor: root.cursorIndex === categoryDelegate.globalIndex
                        selected: root.filtered.category === categoryDelegate.name
                        text: categoryDelegate.name
                        width: content.width

                        onClicked: root.filtered.category = categoryDelegate.name

                        Component.onCompleted: root._registerRow(categoryDelegate.globalIndex,
                                                                 categoryDelegate, () => {
                                                                     root.filtered.category
                                                                             = categoryDelegate.name;
                                                                 })
                        Component.onDestruction: root._unregisterRow(categoryDelegate.globalIndex)
                    }
                }

                RailRow {
                    id: categoriesNone

                    readonly property int globalIndex: root.categoriesBase + 1
                                                       + root.categoriesNamedCount

                    count: root.counts.uncategorised
                    glyph: "·"
                    hasCursor: root.cursorIndex === categoriesNone.globalIndex
                    selected: root.filtered.category === ""
                    text: qsTr("none")
                    visible: root.categoriesNoneVisible
                    width: content.width

                    onClicked: root.filtered.category = ""

                    Component.onCompleted: root._registerRow(categoriesNone.globalIndex,
                                                             categoriesNone, () => {
                                                                 root.filtered.category = "";
                                                             })
                    Component.onDestruction: root._unregisterRow(categoriesNone.globalIndex)
                }
            }

            // ---- tags ------------------------------------------------------------------
            RailGroup {
                label: qsTr("tags")
                width: content.width

                RailRow {
                    id: tagsAll

                    count: root.counts.total
                    hasCursor: root.cursorIndex === root.tagsBase
                    // "all" is never a real narrowing filter — see the categories "all" row.
                    selected: false
                    text: qsTr("all")
                    width: content.width

                    onClicked: root.filtered.clearTag()

                    Component.onCompleted: root._registerRow(root.tagsBase, tagsAll, ()
                                                             => root.filtered.clearTag())
                    Component.onDestruction: root._unregisterRow(root.tagsBase)
                }

                Repeater {
                    model: root.tags

                    RailRow {
                        id: tagDelegate

                        required property int index
                        required property string name

                        readonly property int globalIndex: root.tagsBase + 1 + tagDelegate.index

                        count: root.counts.tagCount(tagDelegate.name)
                        glyph: Glyphs.tag
                        hasCursor: root.cursorIndex === tagDelegate.globalIndex
                        selected: root.filtered.tag === tagDelegate.name
                        text: tagDelegate.name
                        width: content.width

                        onClicked: root.filtered.tag = tagDelegate.name

                        Component.onCompleted: root._registerRow(tagDelegate.globalIndex,
                                                                 tagDelegate, () => {
                                                                     root.filtered.tag
                                                                             = tagDelegate.name;
                                                                 })
                        Component.onDestruction: root._unregisterRow(tagDelegate.globalIndex)
                    }
                }

                RailRow {
                    id: tagsUntagged

                    readonly property int globalIndex: root.tagsBase + 1 + root.tagsNamedCount

                    count: root.counts.untagged
                    glyph: "·"
                    hasCursor: root.cursorIndex === tagsUntagged.globalIndex
                    selected: root.filtered.tag === ""
                    text: qsTr("untagged")
                    visible: root.tagsUntaggedVisible
                    width: content.width

                    onClicked: root.filtered.tag = ""

                    Component.onCompleted: root._registerRow(tagsUntagged.globalIndex, tagsUntagged,
                                                             () => {
                                                                 root.filtered.tag = "";
                                                             })
                    Component.onDestruction: root._unregisterRow(tagsUntagged.globalIndex)
                }
            }

            // ---- trackers ----------------------------------------------------------------
            RailGroup {
                label: qsTr("trackers")
                width: content.width

                RailRow {
                    id: trackersAll

                    count: root.counts.total
                    hasCursor: root.cursorIndex === root.trackersBase
                    // "all" is never a real narrowing filter — see the categories "all" row.
                    selected: false
                    text: qsTr("all")
                    width: content.width

                    onClicked: root.filtered.clearTracker()

                    Component.onCompleted: root._registerRow(root.trackersBase, trackersAll, ()
                                                             => root.filtered.clearTracker())
                    Component.onDestruction: root._unregisterRow(root.trackersBase)
                }

                Repeater {
                    model: root.trackers

                    RailRow {
                        id: trackerDelegate

                        required property int index
                        required property bool isTrackerless
                        required property string host
                        required property int torrentCount

                        readonly property int globalIndex: root.trackersBase + 1
                                                           + trackerDelegate.index

                        count: trackerDelegate.torrentCount
                        glyph: trackerDelegate.isTrackerless ? "·" : Glyphs.tracker
                        hasCursor: root.cursorIndex === trackerDelegate.globalIndex
                        selected: root.filtered.tracker === trackerDelegate.host
                        text: trackerDelegate.host
                        width: content.width

                        onClicked: root.filtered.tracker = trackerDelegate.host

                        Component.onCompleted: root._registerRow(trackerDelegate.globalIndex,
                                                                 trackerDelegate, () => {
                                                                     root.filtered.tracker
                                                                             = trackerDelegate.host;
                                                                 })
                        Component.onDestruction: root._unregisterRow(trackerDelegate.globalIndex)
                    }
                }
            }

            // ---- servers -------------------------------------------------------------
            // One row per configured server, named, with the default one drawn selected the
            // way a status or category row is. `id` is a QML keyword, so the row's id comes
            // from `idAt(index)` rather than from a required property.
            RailGroup {
                label: qsTr("servers")
                visible: root.serversRowCount > 0
                width: content.width

                Repeater {
                    model: root.serverList

                    RailRow {
                        id: serverDelegate

                        required property int index
                        required property string name
                        required property bool isDefault

                        readonly property int globalIndex: root.serversBase + serverDelegate.index

                        count: -1
                        glyph: Glyphs.server
                        hasCursor: root.cursorIndex === serverDelegate.globalIndex
                        selected: serverDelegate.isDefault
                        text: serverDelegate.name
                        width: content.width

                        onClicked: root.serverActivated(root.serverList.idAt(
                                                            serverDelegate.index))

                        Component.onCompleted: root._registerRow(serverDelegate.globalIndex,
                                                                 serverDelegate, () => {
                                                                     root.serverActivated(
                                                                         root.serverList.idAt(
                                                                             serverDelegate.index));
                                                                 })
                        Component.onDestruction: root._unregisterRow(serverDelegate.globalIndex)
                    }
                }
            }
        }
    }

    Hairline {
        id: hairline

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        vertical: true
    }
}
