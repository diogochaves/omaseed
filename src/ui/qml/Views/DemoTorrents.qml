import QtQuick
import Omaseed
import Omaseed.Kit

// Screenshot host for `omaseed --demo torrents`: the table alone in the spec's window, fed
// from the demo library (`--rows` scales it). At the demo's 16 rows it also sets the exact
// state of spec v2 §1 Fig. 1 — cursor on row 1, rows 1 and 2 selected, row 3 in inline
// rename — so the shot can be compared with the figure column by column without driving the
// keyboard. With more rows it stays plain, which is what the 10 000-row scrolling test wants.
Window {
    id: root

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(800)
    title: qsTr("omaseed — table")
    visible: true
    width: Theme.space(1360)

    // The order of `Component.onCompleted` between a window and its children is undefined,
    // so this does not rely on `TorrentTable` having applied its own initial sort yet: it
    // sets the figure's sort itself, which is idempotent when the table repeats it.
    function applyFigureState() {
        table.forceActiveFocus();
        if (App.filtered.count !== 16)
            return;
        App.filtered.setSortByRoleName(table.initialSortRoleName);
        App.filtered.sortOrder = table.initialSortOrder;
        table.moveCursorTo(0);
        table.toggleCursorSelection();
        table.moveCursor(1);
        table.toggleCursorSelection();
        table.moveCursor(1);
        table.startRename();
        table.moveCursorTo(0);
    }

    // Opens at that size and keeps it, as Main.qml does: Theme.space() follows the base size
    // live (#156), and a demo window left bound resized itself on a change (#184).
    Component.onCompleted: {
        root.width = root.width;
        root.height = root.height;
        Qt.callLater(root.applyFigureState);
    }

    TorrentTable {
        id: table

        anchors.bottomMargin: Theme.space(12)
        anchors.fill: parent
        anchors.leftMargin: Theme.space(16)
        anchors.rightMargin: Theme.space(16)
        anchors.topMargin: Theme.space(12)
        filtered: App.filtered

        onActionsRequested: (hash, x, y) => console.log("actions:", hash, x, y)
        onActivated: hash => console.log("activated:", hash)
        onRenameRequested: (hash, name) => console.log("rename:", hash, "->", name)
    }
}
