import QtQuick
import QtQuick.Window
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// The shell window (spec v2 §1 Fig. 1, `.win.main`: 1360 × 800 at base 12). It owns the
// window and nothing else — the translucent ground, the 1 px edge, the drop target, the
// close-hides-to-tray rule and the poll cadence that follows visibility. Everything drawn
// inside it is `MainView`, which the qmltestrunner can build on its own.
Window {
    id: window

    // First run floats (Diogo, 2026-09-23). A window whose minimum and maximum sizes are equal
    // is one Hyprland floats and centres by itself, with no window rule to install, and it
    // decides that once, when the window maps. So first run locks the window to Omarchy's own
    // floating size and `leaveSetup()` unlocks it and maps it again, which tiles it like any
    // other launch. Latched in Component.onCompleted, before the window first shows:
    // `App.hasServer` flips mid-setup, when step 2 saves.
    //
    // The size is Omarchy's 875 × 600 at base 14, the size the setup column was checked at,
    // and grows with the font like everything inside it (1125 × 771 at 18), so a bigger font
    // never pushes the column into the footer. Never larger than the screen's free area;
    // what still does not fit then scrolls inside FirstRunPage.
    readonly property real setupScale: Theme.font.baseSize / 14
    readonly property size setupSize: Qt.size(
                                          Math.min(Math.round(875 * window.setupScale),
                                                   Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : 1e6),
                                          Math.min(Math.round(600 * window.setupScale),
                                                   Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight : 1e6))
    property bool setup: !App.hasServer && !App.demoMode

    function leaveSetup(): void {
        if (!window.setup)
            return;
        window.hide();
        window.setup = false;
        window.width = Theme.space(1360);
        window.height = Theme.space(800);
        window.show();
        window.requestActivate();
    }

    Component.onCompleted: window.setup = window.setup // latch: drop the binding

    width: window.setup ? window.setupSize.width : Theme.space(1360)
    height: window.setup ? window.setupSize.height : Theme.space(800)
    // docs/10 §2: a floor at the narrowest width the hero was drawn for, 560 px at base 14.
    // Hyprland still sizes a tiled window to its tile; the hero's ladder is what covers that.
    minimumWidth: window.setup ? window.setupSize.width : Theme.space(480)
    minimumHeight: window.setup ? window.setupSize.height : 0
    maximumWidth: window.setup ? window.setupSize.width : 16777215
    maximumHeight: window.setup ? window.setupSize.height : 16777215
    // `--hidden` (Start at login, "in the tray") keeps the window down until it is asked
    // for; first run shows it anyway, since without a server there is nothing to wait for.
    visible: !(App.launchHidden && !window.setup)
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
        onSetupFinished: window.leaveSetup()
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
