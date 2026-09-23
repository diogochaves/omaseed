import QtQuick
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The shell window (spec v2 §1 Fig. 1, `.win.main`: 1360 × 800 at base 12). It owns the
// window and nothing else — the translucent ground, the 1 px edge, the drop target, the
// close-hides-to-tray rule and the poll cadence that follows visibility. Everything drawn
// inside it is `MainView`, which the qmltestrunner can build on its own.
Window {
    id: window

    width: Theme.space(1360)
    height: Theme.space(800)
    // docs/10 §2: a floor at the narrowest width the hero was drawn for, 560 px at base 14.
    // Hyprland still sizes a tiled window to its tile; the hero's ladder is what covers that.
    minimumWidth: Theme.space(480)
    visible: true
    title: qsTr("omaseed")
    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)

    // T1.5: closing the window hides it instead of quitting (charter §2, "one process is
    // window + tray-resident"). Only the tray menu's "quit" and `ctrl+q` (main.cpp,
    // QCoreApplication::quit) really end the process.
    onClosing: close => {
        close.accepted = false;
        window.hide();
    }

    // The poll cadence follows the window: 1.5 s while it is on screen, 5 s while it is not.
    onVisibleChanged: App.setVisible(window.visible)

    MainView {
        id: view

        anchors.fill: parent
        focus: true

        onHideRequested: window.hide()
        onQuitRequested: Qt.quit()
    }

    // The 1 px window edge: accent while active, muted otherwise.
    Rectangle {
        anchors.fill: parent
        border.color: window.active ? Theme.accent : Theme.muted
        border.width: 1
        color: "transparent"
        radius: Theme.cornerRadius
        z: 1
    }

    // --- T4.2: add flow ---
    // Magnet text and .torrent files dropped anywhere on the window (charter §2 "Drag and
    // drop"): the first opens the add sheet, the rest queue behind it.
    DropArea {
        anchors.fill: parent

        onDropped: drop => {
            const addables = Xdg.addablesFromDrop(drop.urls, drop.text);
            const values = addables.map(addable => addable.value);
            if (values.length > 0)
                view.addDropped(values);
        }
    }
    // --- end T4.2 ---
}
