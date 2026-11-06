pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Search's bar (board 48 `sbar()`, DECISIONS C26 SR6): the field, then plugins ▾ (every plugin
// that is on, or a pick), type ▾ (any, or one of the types those plugins know), hide dead (off)
// and sort ▾ (most seeds, newest, size, name). Plugins and type ask again; hide dead and sort
// don't. Narrower than 720 px the three filters fold into "filters ▾". While the window's rail
// is open on search (`railHolds`, DECISIONS C34 D5s a) it holds the plugins and the type, so
// the bar is the field, hide dead and sort ▾.
//
// Keys (SR14): enter in the field asks (`accepted`), ↓ goes to the rows (`downPressed`), tab
// walks the filters (`stop` 1–4, then the plugins' line), where space or enter opens one.
// While the field has words or there are results it ends in "× esc", as the title bar's
// filter does (C32 D18): a click on it, or esc in the field, clears both (`clearRequested`,
// DECISIONS C34 D21 a); esc in the field with neither leaves (`emptied`). Keys are taught on
// demand (C24 H10): each control's tooltip names its key, and nothing in the bar prints one.
Item {
    id: root

    property var plugins: null
    property var pluginPick: []
    property string category: "all"
    property bool hideDead: false
    property string order: "seeds"
    property bool narrow: false
    /// DECISIONS C34 D5s a (amending SR6): the rail is open and holds the plugins and the
    /// type, so plugins ▾ and type ▾ (or filters ▾, narrow) leave the bar; hide dead and sort ▾
    /// stay.
    property bool railHolds: false
    /// A search has been asked: its results (or its "0 results") are on show.
    property bool hasResults: false
    /// C34 D21 a: there is something to clear, so the field ends in "× esc".
    readonly property bool clearable: root.field.text.length > 0 || root.hasResults
    /// The keyboard's stop: 0 the field, 1 plugins ▾ (filters ▾ when narrow), 2 type ▾, 3 hide
    /// dead, 4 sort ▾; narrow, 2 is sort ▾; while the rail holds the plugins (C34 D5s a), 1 is
    /// hide dead and 2 sort ▾.
    property int stop: 0
    readonly property int sortStop: root.narrow || root.railHolds ? 2 : 4
    readonly property int hideDeadStop: root.railHolds ? 1 : root.narrow ? -1 : 3
    readonly property alias field: field

    signal accepted
    signal downPressed
    signal emptied
    /// C34 D21 a: "× esc", clicked, or esc in the field while it has words or results.
    signal clearRequested
    signal hideDeadToggled
    /// A menu to open under (`x`, `y`): "plugins", "type", "sort", "filters".
    signal menuRequested(string which, real x, real y)
    signal answersRequested(real x, real y)

    readonly property string pluginsText: {
        const on = root.plugins ? root.plugins.enabledCount : 0;
        if (root.pluginPick.length > 0 && root.pluginPick.length < on)
            return root.pluginPick.length === 1 ? qsTr("1 plugin ▾") : qsTr("%1 plugins ▾").arg(
                                                      root.pluginPick.length);
        return on === 1 ? qsTr("1 plugin ▾") : qsTr("%1 plugins ▾").arg(on);
    }
    readonly property string typeText: {
        if (root.category === "all")
            return qsTr("any type ▾");
        const types = root.plugins ? root.plugins.types : [];
        const found = types.find(type => type.id === root.category);
        return qsTr("%1 ▾").arg(found ? found.name : root.category);
    }
    readonly property string orderText: {
        switch (root.order) {
        case "newest":
            return qsTr("newest ▾");
        case "size":
            return qsTr("size ▾");
        case "name":
            return qsTr("name ▾");
        }
        return qsTr("most seeds ▾");
    }
    /// The filters narrowed while folded: how many are not their default.
    readonly property int _filtersOn: (root.pluginPick.length > 0 ? 1 : 0) + (root.category
                                                                              !== "all" ? 1 : 0) + (
                                          root.hideDead ? 1 : 0)
    /// SR14: tab's last stop is the plugins' line's "N of M plugins didn't answer ›", while
    /// it shows.
    property bool answersShown: false
    readonly property int answersStop: root.sortStop + 1
    readonly property bool answersHaveCursor: root.answersShown && root.stop === root.answersStop
    readonly property int _lastStop: root.answersShown ? root.answersStop : root.sortStop

    function _open(which: string, item: Item): void {
        root.menuRequested(which, item.x, item.y + item.height);
    }

    function _activate(): void {
        if (root.answersHaveCursor) {
            root.answersRequested(sortChip.x + sortChip.width, sortChip.y + sortChip.height);
            return;
        }
        if (root.stop === root.sortStop)
            root._open("sort", sortChip);
        else if (root.stop === root.hideDeadStop)
            root.hideDeadToggled();
        else if (root.stop === 1)
            root._open(root.narrow ? "filters" : "plugins", root.narrow ? filtersChip : pluginsChip);
        else if (root.stop === 2)
            root._open("type", typeChip);
    }

    implicitHeight: bar.implicitHeight + Theme.searchBarInsetTop + Theme.searchBarInsetBottom
    height: implicitHeight

    // The filters' keys while one of them has the cursor.
    Item {
        id: stopKeys

        focus: root.stop > 0

        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Tab:
                root.stop = root.stop >= root._lastStop ? 0 : root.stop + 1;
                break;
            case Qt.Key_Backtab:
                root.stop = Math.max(0, root.stop - 1);
                break;
            case Qt.Key_Space:
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root._activate();
                break;
            case Qt.Key_Down:
                root.downPressed();
                break;
            default:
                return;
            }
            if (root.stop === 0)
                field.focusInput();
            event.accepted = true;
        }
    }

    onStopChanged: {
        if (root.stop > 0)
            stopKeys.forceActiveFocus();
    }

    RowLayout {
        id: bar

        anchors.left: parent.left
        anchors.leftMargin: Theme.searchBarInset
        anchors.right: parent.right
        anchors.rightMargin: Theme.searchBarInset
        anchors.top: parent.top
        anchors.topMargin: Theme.searchBarInsetTop
        spacing: Theme.searchBarGap

        Field {
            id: field

            Layout.fillWidth: true
            // Grows from its least width, so the chips always fit beside it (narrower still
            // once the filters fold).
            Layout.minimumWidth: root.narrow ? Theme.searchFieldNarrowWidth : Theme.searchFieldMinWidth
            Layout.preferredWidth: Layout.minimumWidth
            accessibleName: qsTr("search your plugins")
            hint: Glyphs.viewSearch
            objectName: "searchField"
            placeholder: qsTr("search your plugins")
            // C34 D21 a: esc is the bar's, to clear the words and the results together.
            escClears: false
            // C34 D21 a: "× esc" at the field's end, as the title bar's filter (C32 D18).
            trailingKey: root.clearable ? qsTr("× esc") : ""
            trailingClickable: root.clearable
            // #78: the key's glyph is no name for what its click does.
            trailingName: qsTr("clear the search")
            shortcuts: [
                {
                    "key": Qt.Key_Down,
                    "modifiers": Qt.NoModifier
                },
                {
                    "key": Qt.Key_Tab,
                    "modifiers": Qt.NoModifier
                }
            ]

            Keys.onEscapePressed: event => {
                // C34 D21 a (amends SR14): words or results, both go; neither, it leaves.
                if (root.clearable)
                    root.clearRequested();
                else
                    root.emptied();
                event.accepted = true;
            }
            onAccepted: root.accepted()
            onTrailingClicked: root.clearRequested()
            onShortcutPressed: key => {
                if (key === Qt.Key_Down)
                    root.downPressed();
                else
                    root.stop = 1;
            }
        }

        Chip {
            id: pluginsChip

            hasCursor: root.stop === 1
            objectName: "searchPluginsChip"
            text: root.pluginsText
            tooltip: qsTr("which plugins to ask · tab")
            visible: !root.narrow && !root.railHolds

            onClicked: root._open("plugins", pluginsChip)
        }

        Chip {
            id: typeChip

            hasCursor: root.stop === 2
            objectName: "searchTypeChip"
            selected: root.category !== "all"
            text: root.typeText
            tooltip: qsTr("one type of torrent, or any")
            visible: !root.narrow && !root.railHolds

            onClicked: root._open("type", typeChip)
        }

        Chip {
            id: hideDeadChip

            checkable: true
            hasCursor: root.stop === root.hideDeadStop
            objectName: "searchHideDeadChip"
            selected: root.hideDead
            text: root.hideDead ? qsTr("✓ hide dead") : qsTr("hide dead")
            tooltip: qsTr("hide rows the plugins saw no seeds for; unknown counts stay")
            visible: !root.narrow || root.railHolds

            onClicked: root.hideDeadToggled()
        }

        Chip {
            id: filtersChip

            hasCursor: root.stop === 1
            objectName: "searchFiltersChip"
            selected: root._filtersOn > 0
            text: root._filtersOn > 0 ? qsTr("filters ▾ · %1").arg(root._filtersOn) : qsTr(
                                            "filters ▾")
            tooltip: qsTr("plugins, type and hide dead · tab")
            // C34 D5s a: with the plugins and the type in the rail, hide dead is all it held.
            visible: root.narrow && !root.railHolds

            onClicked: root._open("filters", filtersChip)
        }

        Chip {
            id: sortChip

            hasCursor: root.stop === root.sortStop
            objectName: "searchSortChip"
            text: root.orderText
            tooltip: qsTr("the rows' order")

            onClicked: root._open("sort", sortChip)
        }
    }
}
