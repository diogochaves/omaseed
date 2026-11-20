pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The file pane's toolbar (board 64, DECISIONS C47 I4, I6): one row over the list, the same
// in the add sheet and details › Files.
//
//   filter      a field bound both ways to `files.filterText` (`/` focuses it), with
//               "N of M files" at its end while it narrows
//   sort ▾      a menu: name, size (biggest first), kind; then folders first, ticked by
//               default and off in flat (I4: the tree's own order)
//   tree · flat flat lists every file once, sorted across the torrent (I6); the view is kept
//               per window, through `App.setFilesView(scope, …)`
//   fold all    unfold all once every folder is shut; off in flat
//   hide skipped
//
// The list keeps the keyboard: its keys (`/`, `s`, `H`, `L`, `X`, `T`) come here through
// `run(action)`, with the Keymap's action names. `filterDone()` hands the keyboard back.
//
//     FilesToolbar { files: App.addFiles.tree; scope: "add"; onFilterDone: tree.forceActiveFocus() }
RowLayout {
    id: root

    /// The pane's model.
    property TorrentFilesModel files: null
    /// The window whose view (tree or flat) this is: "add" or "details" (C47 I6). "" sets
    /// the model's `flat` alone, kept nowhere.
    property string scope: ""

    /// `enter`, `esc` on an empty field, or up/down: the keyboard goes back to the list.
    signal filterDone

    readonly property alias filterField: filterInput
    readonly property alias sortButton: sortMenuButton
    /// The sort menu while it is open, else null (it lives on the window's content item).
    readonly property var sortMenu: root._menu
    readonly property bool flat: root.files !== null && root.files.flat
    /// Some folder is open: the button folds them all; none is, it opens them all. Read again
    /// whenever the rows change (a fold or an unfold changes how many there are).
    readonly property bool anyOpen: {
        const files = root.files;
        if (files === null || files.count < 0)
            return false;
        for (let row = 0; row < files.count; ++row) {
            if (files.isFolder(row) && files.isExpanded(row))
                return true;
        }
        return false;
    }

    property var _menu: null

    /// The button's words: "sort: size", or "sort" alone in the torrent's own order (no key
    /// picked), which keeps the row narrow enough for the add sheet.
    function sortText(key: string): string {
        switch (key) {
        case "name":
            return qsTr("sort: name");
        case "size":
            return qsTr("sort: size");
        case "kind":
            return qsTr("sort: kind");
        }
        return qsTr("sort");
    }

    /// The menu's rows: the keys with a ✓ on the current one, then folders first.
    function sortItems(): var {
        const files = root.files;
        const key = files !== null ? files.sortKey : "";
        return [
                    {
                        "text": qsTr("name"),
                        "key": "n",
                        "action": "name",
                        "checked": key === "name"
                    },
                    {
                        "text": qsTr("size, biggest first"),
                        "key": "s",
                        "action": "size",
                        "checked": key === "size"
                    },
                    {
                        "text": qsTr("kind"),
                        // `k` moves up in a Menu: the kind is `d`.
                        "key": "d",
                        "action": "kind",
                        "checked": key === "kind"
                    },
                    {
                        "separator": true
                    },
                    {
                        "text": qsTr("folders first"),
                        "key": "f",
                        "action": "foldersFirst",
                        "checked": files !== null && files.foldersFirst,
                        // C47 I4: folders first is the tree's order; flat has no folders.
                        "disabled": root.flat
                    }
                ];
    }

    /// `/`: the caret in the filter.
    function focusFilter(): void {
        filterInput.focusInput();
    }

    /// `s` or the button: the sort menu, under the button.
    function openSortMenu(): void {
        const host = root.Window.window ? root.Window.window.contentItem : null;
        if (root.files === null || host === null || root._menu !== null)
            return;
        const at = sortMenuButton.mapToItem(host, 0, sortMenuButton.height + Theme.space(3));
        root._menu = menuComponent.createObject(host, {
                                                    "items": root.sortItems()
                                                });
        root._menu.openAt(Math.round(at.x), Math.round(at.y));
    }

    /// A pick in the sort menu.
    function pickSort(action: string): void {
        if (root.files === null)
            return;
        if (action === "foldersFirst")
            root.files.foldersFirst = !root.files.foldersFirst;
        else
            root.files.sortBy(action);
    }

    /// Tree (false) or flat (true), kept for the window (C47 I6).
    function setFlat(flat: bool): void {
        if (root.files === null || root.files.flat === flat)
            return;
        if (root.scope.length > 0)
            App.setFilesView(root.scope, flat);
        else
            root.files.flat = flat;
    }

    /// Fold every folder (true), or open them all (false). Nothing in flat.
    function foldAll(folded: bool): void {
        if (root.files === null || root.flat)
            return;
        if (folded)
            root.files.collapseAll();
        else
            root.files.expandAll();
    }

    function toggleHideSkipped(): void {
        if (root.files !== null)
            root.files.hideSkipped = !root.files.hideSkipped;
    }

    /// A list's key, by its Keymap action: true when the toolbar took it.
    function run(action: string): bool {
        switch (action) {
        case "filterFiles":
            root.focusFilter();
            return true;
        case "sortFiles":
            root.openSortMenu();
            return true;
        case "collapseAll":
            root.foldAll(true);
            return true;
        case "expandAll":
            root.foldAll(false);
            return true;
        case "hideSkipped":
            root.toggleHideSkipped();
            return true;
        case "treeFlat":
            root.setFlat(!root.flat);
            return true;
        }
        return false;
    }

    function _dropMenu(): void {
        const menu = root._menu;
        root._menu = null;
        if (menu !== null)
            menu.destroy();
    }

    spacing: Theme.space(6)

    Component.onDestruction: root._dropMenu()

    Field {
        id: filterInput

        Layout.fillWidth: true
        Layout.minimumWidth: Theme.space(100)
        accessibleName: qsTr("filter files")
        hint: "/"
        objectName: "filesToolbarFilter"
        placeholder: qsTr("filter files")
        trailing: root.files !== null && root.files.filterText.length > 0 ? qsTr("%1 of %2 files").arg(
                                                                                root.files.matchCount).arg(
                                                                                root.files.fileCount) :
                                                                            ""

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                root.filterDone();
                event.accepted = true;
            } else if (event.key === Qt.Key_Escape) {
                // The Field passes on an `esc` it had nothing to clear: back to the list.
                root.filterDone();
                event.accepted = true;
            }
        }
        onAccepted: root.filterDone()
        onTextChanged: {
            if (root.files !== null && root.files.filterText !== filterInput.text)
                root.files.filterText = filterInput.text;
        }

        // × show all, or a new torrent, clears the model's filter: the field follows.
        Connections {
            function onFilterTextChanged(): void {
                if (filterInput.text !== root.files.filterText)
                    filterInput.text = root.files.filterText;
            }

            target: root.files
        }
    }

    Button {
        id: sortMenuButton

        Accessible.name: qsTr("sort files")
        bordered: true
        fontSize: Theme.font.small
        objectName: "filesToolbarSort"
        sidePadding: Theme.space(8)
        text: root.sortText(root.files !== null ? root.files.sortKey : "")
        trailingGlyph: Glyphs.chevronDownGlyph

        onClicked: root.openSortMenu()
    }

    // tree · flat: one segment of two.
    Row {
        Button {
            Accessible.checkable: true
            Accessible.checked: !root.flat
            bordered: true
            fontSize: Theme.font.small
            objectName: "filesToolbarTree"
            selected: !root.flat
            sidePadding: Theme.space(8)
            text: qsTr("tree")

            onClicked: root.setFlat(false)
        }

        Button {
            Accessible.checkable: true
            Accessible.checked: root.flat
            bordered: true
            fontSize: Theme.font.small
            objectName: "filesToolbarFlat"
            selected: root.flat
            sidePadding: Theme.space(8)
            text: qsTr("flat")

            onClicked: root.setFlat(true)
        }
    }

    Button {
        bordered: true
        enabled: !root.flat
        fontSize: Theme.font.small
        objectName: "filesToolbarFold"
        sidePadding: Theme.space(8)
        text: root.anyOpen ? qsTr("fold all") : qsTr("unfold all")

        onClicked: root.foldAll(root.anyOpen)
    }

    Button {
        Accessible.checkable: true
        Accessible.checked: root.files !== null && root.files.hideSkipped
        bordered: true
        fontSize: Theme.font.small
        objectName: "filesToolbarHideSkipped"
        selected: root.files !== null && root.files.hideSkipped
        sidePadding: Theme.space(8)
        text: qsTr("hide skipped")

        onClicked: root.toggleHideSkipped()
    }

    Component {
        id: menuComponent

        Menu {
            anchors.fill: parent
            dimmed: false
            menuWidth: Theme.space(200)
            objectName: "filesSortMenu"
            z: 1000

            onActivated: action => {
                root._dropMenu();
                root.pickSort(action);
            }
            onRejected: root._dropMenu()
        }
    }
}
