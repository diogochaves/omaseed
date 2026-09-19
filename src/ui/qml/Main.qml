import QtQuick
import Omaseed
import Omaseed.Kit

// The shell window. Phase 1 shows only the identity half of the hero row (spec v2 §1, Fig. 1);
// speeds, search, actions, rail, table, details and footer arrive in phase 3.
Window {
    id: window

    width: 1360
    height: 800
    visible: true
    title: qsTr("omaseed")
    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.windowAlpha)

    // T1.5: closing the window hides it instead of quitting (charter §2, "one process is
    // window + tray-resident"). Only the tray menu's "quit" (main.cpp, QCoreApplication::quit)
    // really ends the process.
    onClosing: close => {
        close.accepted = false;
        window.hide();
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

    Item {
        id: hero

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.space(28) + 2 * Theme.space(12)

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(16)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(14)

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                border.color: Theme.accent
                border.width: 1
                color: "transparent"
                height: Theme.space(28)
                radius: Theme.cornerRadius
                width: Theme.space(28)

                Text {
                    anchors.centerIn: parent
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.title
                    text: "◆"
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(1)

                Text {
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.subtitle
                    font.weight: Font.DemiBold
                    text: qsTr("omaseed")
                }

                Row {
                    spacing: Theme.space(6)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.green
                        height: Theme.space(7)
                        radius: height / 2
                        width: Theme.space(7)
                    }

                    Text {
                        color: Theme.dimForeground
                        font.capitalization: Font.AllUppercase
                        font.family: Theme.fontFamily
                        font.letterSpacing: Theme.font.caption * 0.1
                        font.pixelSize: Theme.font.caption
                        text: qsTr("home · qBittorrent")
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: hero.bottom
        color: Theme.fill(Theme.hairlineAlpha)
        height: 1
    }

    // Magnet text and .torrent files dropped anywhere on the window. T4.2 routes these into
    // the add sheet; for now they are only logged (charter §2 "Drag and drop").
    DropArea {
        anchors.fill: parent

        onDropped: drop => {
            const addables = Xdg.addablesFromDrop(drop.urls, drop.text);
            for (const addable of addables)
                console.log("drop:", addable.kind, addable.value);
        }
    }
}
