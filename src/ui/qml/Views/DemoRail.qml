import QtQuick
import Omaseed
import Omaseed.Kit

// `--demo rail` host: the demo library is already loaded into `App` by main.cpp, so this
// just places the rail against the spec's window (v2 §1 Fig. 1, 1360x800 at base 12) and
// puts it in the figure's state — "downloading" selected, the cursor on "movies" — so a
// screenshot can be compared straight against the figure.
Window {
    id: root

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(800)
    title: qsTr("omaseed — rail demo")
    visible: true
    width: Theme.space(1360)

    // Opens at that size and keeps it, as Main.qml does: Theme.space() follows the base size
    // live (#156), and a demo window left bound resized itself on a change (#184).
    Component.onCompleted: {
        root.width = root.width;
        root.height = root.height;
    }

    Rectangle {
        anchors.fill: parent
        border.color: root.active ? Theme.accent : Theme.muted
        border.width: 1
        color: "transparent"
        radius: Theme.cornerRadius
        z: 1
    }

    Rail {
        id: rail

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        categories: App.categories
        counts: App.counts
        filtered: App.filtered
        serverCount: App.serverCount
        serverList: App.serverList
        tags: App.tags
        trackers: App.trackers

        Component.onCompleted: {
            App.filtered.statusFilter = TorrentFilterModel.Downloading;
            // Demo categories sort alphabetically: linux, movies, music, tv — "movies" is
            // model index 1, one row after the categories group's "all".
            rail.cursorIndex = rail.categoriesBase + 2;
            rail.forceActiveFocus();
        }
    }
}
