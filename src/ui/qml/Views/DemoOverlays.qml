pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit
import Omaseed.Views

// `--demo overlays` host: the assembled main window with one of T4.1's overlay components
// open over it, so each of spec v2 §3-§4's figures can be screenshot through the real path
// (scrim, centring, focus trap) rather than inline in the gallery.
// `OMASEED_DEMO_OVERLAY=sheet|confirm|palette|menu` picks which one; the default is the
// sheet. The demo library is already in `App` (main.cpp loads it for every window demo).
Window {
    id: root

    readonly property string which: DemoEnv.value("OMASEED_DEMO_OVERLAY") || "sheet"

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)
    height: Theme.space(800)
    title: qsTr("omaseed — overlays demo")
    visible: true
    width: Theme.space(1360)

    Rectangle {
        anchors.fill: parent
        border.color: Theme.accent
        border.width: 1
        color: "transparent"
        z: 100
    }

    MainView {
        id: main

        anchors.fill: parent
        anchors.margins: 1
    }

    // Fig. 7 — Set location.
    Sheet {
        actions: [
            {
                "text": qsTr("cancel"),
                "cancel": true
            },
            {
                "text": qsTr("move"),
                "primary": true
            }
        ]
        anchors.fill: parent
        footerRight: qsTr("server: home")
        glyph: Glyphs.folder
        hints: [
            {
                "key": "tab",
                "label": qsTr("fields")
            },
            {
                "key": "enter",
                "label": qsTr("move")
            }
        ]
        sheetWidth: Theme.space(420)
        subtitle: qsTr("1 torrent · 1.31 GB moves with it")
        title: qsTr("Set location")
        z: 200

        FormRow {
            Layout.fillWidth: true
            label: qsTr("New path on home")

            Field {
                Layout.fillWidth: true
                text: "/data/torrents/archive/"
                trailing: "ctrl+o"
            }
        }

        Component.onCompleted: if (root.which === "sheet")
                                   open()
    }

    // Fig. 6 — the delete confirm.
    ConfirmDialog {
        anchors.fill: parent
        cancelText: qsTr("Cancel")
        confirmText: qsTr("Delete")
        destructive: true
        message: qsTr("Delete 2 torrents and their files?")
        subtitle: qsTr(
                      "archlinux-2026.09.01-x86_64.iso, debian-13.1.0-amd64-DVD-1.iso · 5.29 GB on disk")
        z: 200

        Toggle {
            checked: true
            text: qsTr("also delete files from disk")
        }

        Component.onCompleted: if (root.which === "confirm") {
                                   open();
                                   cursor = 1; // Fig. 6 shows the red outline on Delete
                               }
    }

    // Fig. 12 — jump to torrent.
    Palette {
        accessibleName: qsTr("jump to a torrent") // as JumpPalette's (#91)
        anchors.fill: parent
        counter: qsTr("2 of 142")
        footerRight: qsTr("also matches hash and tracker")
        glyph: Glyphs.search
        metaSpoken: true // as JumpPalette's (#79)
        hints: [
            {
                "key": "↑↓",
                "label": qsTr("move")
            },
            {
                "key": "enter",
                "label": qsTr("jump")
            },
            {
                "key": "tab",
                "label": qsTr("actions")
            }
        ]
        items: [
            {
                "title": "Fedora-Workstation-Live-44-1.6.x86_64.iso",
                "meta": qsTr("downloading"),
                "detail": "linux · 2.34 GB · 12.4 %"
            },
            {
                "title": "Fedora-Server-44-1.6.x86_64.iso",
                "meta": qsTr("seeding"),
                "detail": "linux · 2.9 GB · ratio 4.1"
            }
        ]
        query: "fed"
        z: 200

        Component.onCompleted: if (root.which === "palette")
                                   open()
    }

    // Fig. 11 — the actions list, anchored under the cursor row.
    Menu {
        id: menu

        anchors.fill: parent
        items: [
            {
                "text": qsTr("start"),
                "key": "p"
            },
            {
                "text": qsTr("stop"),
                "key": "p"
            },
            {
                "text": qsTr("force start"),
                "key": "P"
            },
            {
                "separator": true
            },
            {
                "text": qsTr("recheck"),
                "key": "c"
            },
            {
                "text": qsTr("reannounce"),
                "key": "R"
            },
            {
                "separator": true
            },
            {
                "text": qsTr("category"),
                "key": "C"
            },
            {
                "text": qsTr("tags"),
                "key": "t"
            },
            {
                "text": qsTr("move…"),
                "key": "m"
            },
            {
                "text": qsTr("limits"),
                "key": "L"
            },
            {
                "text": qsTr("rename"),
                "key": "r"
            },
            {
                "separator": true
            },
            {
                "text": qsTr("queue up · down"),
                "key": "[ ]"
            },
            {
                "text": qsTr("copy magnet"),
                "key": "y"
            },
            {
                "text": qsTr("open folder"),
                "key": "o"
            },
            {
                "separator": true
            },
            {
                "text": qsTr("delete"),
                "key": "d",
                "danger": true
            }
        ]
        z: 200

        Component.onCompleted: if (root.which === "menu") {
                                   openAt(Theme.space(430), Theme.space(150));
                                   cursorIndex = 4; // Fig. 11 puts the cursor on `recheck`
                               }
    }
}
