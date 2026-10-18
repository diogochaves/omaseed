import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Quit while a folder moves (board 42 J5, DECISIONS C15 M13): closing the window only hides
// it, and the job goes on; quit asks first. "pause and quit" lets the ones with qBittorrent
// finish and leaves the rest for the next start; "keep running" hides the window instead.
Sheet {
    id: root

    /// "keep running": the host hides the window.
    signal hideRequested

    actions: [
        {
            "text": qsTr("cancel"),
            "cancel": true
        },
        {
            "text": qsTr("keep running")
        },
        {
            "text": qsTr("pause and quit"),
            "primary": true
        }
    ]
    glyph: "↻"
    sheetWidth: Theme.space(560)
    title: qsTr("A move is running on %1").arg(App.serverName)

    onAccepted: {
        root.close();
        App.pauseMoveAndQuit();
    }
    onActionTriggered: text => {
        if (text === qsTr("keep running")) {
            root.close();
            root.hideRequested();
        }
    }

    Label {
        Layout.fillWidth: true
        objectName: "moveQuitLine"
        size: "small"
        text: App.move.quitLine
        wrapMode: Text.Wrap
    }
}
