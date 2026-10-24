pragma ComponentBehavior: Bound
import QtQuick
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// `--demo details`: the details pane docked at its default width beside a placeholder for the
// table, with the fictional library already in App. The General tab shows spec §1 Fig. 1's
// torrent; `OMASEED_DEMO_TAB=files|trackers|peers` opens one of the other three for the
// screenshots of §2 Fig. 2-4, and picks the torrent that figure draws (Fig. 2 is Big Buck
// Bunny, Fig. 3 and Fig. 4 the Arch ISO).
Window {
    id: window

    readonly property int tabIndex: {
        const tab = App.demoTab();
        return tab === "files" ? 1 : tab === "trackers" ? 2 : tab === "peers" ? 3 : 0;
    }
    /// `OMASEED_DEMO_TORRENT=<name>` shows another torrent of the demo library (N1: "Sintel
    /// 1080p (CC-BY)" has a generated tree with extras, subtitles and a skipped sample), and
    /// `OMASEED_DEMO_DETAILS_WIDTH=<units>` docks the pane wider, as a drag would.
    readonly property string subjectName: {
        const asked = DemoEnv.value("OMASEED_DEMO_TORRENT");
        if (asked.length > 0)
            return asked;
        return window.tabIndex === 1 ? "Big Buck Bunny 4K (CC-BY)" : "archlinux-2026.09.01-x86_64.iso";
    }
    readonly property int paneUnits: {
        const asked = Number(DemoEnv.value("OMASEED_DEMO_DETAILS_WIDTH"));
        return asked > 0 ? asked : 400; // MainView's default width (docs/11 J1)
    }

    /// The demo library is small and its hashes are generated, so the figure's torrent is
    /// found by the name the figure prints.
    function hashOf(name: string): string {
        for (let row = 0; row < App.filtered.count; ++row) {
            const hash = App.filtered.hashAt(row);
            if (App.torrentRow(hash).name === name)
                return hash;
        }
        return App.filtered.count > 0 ? App.filtered.hashAt(0) : "";
    }

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(800)
    title: qsTr("omaseed — details")
    visible: true
    width: Theme.space(1360)

    // Opens at that size and keeps it, as Main.qml does: Theme.space() follows the base size
    // live (#156), and a demo window left bound resized itself on a change (#184).
    Component.onCompleted: {
        window.width = window.width;
        window.height = window.height;
        const hash = window.hashOf(window.subjectName);
        App.selectTorrent(hash);
        details.torrent = App.torrentRow(hash);
        details.activateTab(window.tabIndex);
    }

    Rectangle {
        anchors.fill: parent
        border.color: window.active ? Theme.accent : Theme.muted
        border.width: 1
        color: "transparent"
        radius: Theme.cornerRadius
        z: 1
    }

    // Where the rail and the table will be (T3.2, T3.3): a plain label, so the screenshot is
    // unmistakably about the right-hand column.
    Item {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: details.left
        anchors.top: parent.top

        Text {
            anchors.centerIn: parent
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: qsTr("rail + table")
        }
    }

    Details {
        id: details

        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.top: parent.top
        details: App.details
        focus: true
        width: Theme.space(window.paneUnits)
    }
}
