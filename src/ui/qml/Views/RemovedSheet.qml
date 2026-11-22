pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// DECISIONS C50 GT6 (board 67 §5, #295): what an agent removed through `omaseed do remove`,
// kept 30 days to add back. Opened from the Agents row's last line (add back…). Each row is a
// torrent: its name, where it was, when it was removed and how long it is kept, and "add back",
// which adds it back paused at that place. One kept from another server is listed with its
// button off: it comes back only there, never by switching (PLAN Q12). Adding back is the
// person's: `do` never adds.
//
//     RemovedSheet { anchors.fill: parent; entries: App.removedKept; busy: …; onAddBackRequested: … }
Sheet {
    id: root

    /// App.removedKept: [{id, hash, name, savePath, server, here, removed, until}].
    property var entries: []
    /// An export or import is running (App.torrentsBackupRunning): one add back at a time.
    property bool busy: false

    /// A row's add back (the host calls App.addBack).
    signal addBackRequested(string id)

    actions: [
        {
            "text": qsTr("close"),
            "cancel": true
        }
    ]
    glyph: Glyphs.backup
    sheetWidth: Theme.space(600)
    subtitle: root.entries.length === 1 ? qsTr("1 torrent an agent removed · kept 30 days") : qsTr(
                                              "%1 torrents an agent removed · each kept 30 days").arg(root.entries.length)
    title: qsTr("Add back")

    onRejected: root.close()
    onActionTriggered: root.close()
    // Every one added back: nothing left to show.
    onEntriesChanged: if (root.opened && root.entries.length === 0)
                          root.close()

    Label {
        Layout.fillWidth: true
        dim: true
        size: "small"
        text: qsTr("Each comes back paused, at the place it had, unchecked: start or recheck it once its files are there again.")
        wrapMode: Text.Wrap
    }

    Repeater {
        model: root.entries

        RowLayout {
            id: keptRow

            required property var modelData
            readonly property bool here: keptRow.modelData.here === true

            Layout.fillWidth: true
            Layout.topMargin: Theme.space(4)
            objectName: "removedRow-" + keptRow.modelData.id
            spacing: Theme.space(10)

            ColumnLayout {
                // One line for a screen reader: the name, where it was, when, and until when.
                Accessible.name: qsTr("%1, %2").arg(keptRow.modelData.name).arg(lineText.text)
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                spacing: Theme.space(2)

                Label {
                    Layout.fillWidth: true
                    bright: true
                    elide: Text.ElideRight
                    objectName: "name"
                    text: keptRow.modelData.name
                }

                Label {
                    id: lineText

                    Layout.fillWidth: true
                    dim: true
                    elide: Text.ElideMiddle
                    objectName: "line"
                    size: "small"
                    text: [qsTr("was in %1").arg(keptRow.modelData.savePath), qsTr("removed %1").arg(
                               keptRow.modelData.removed), qsTr("kept until %1").arg(keptRow.modelData.until)].join(
                              " · ")
                }

                // Q12: back only to the server it came from.
                Label {
                    Layout.fillWidth: true
                    color: Theme.yellow
                    objectName: "elsewhere"
                    size: "small"
                    text: qsTr("on %1: switch to it first").arg(keptRow.modelData.server)
                    visible: !keptRow.here
                    wrapMode: Text.Wrap
                }
            }

            Button {
                id: addBackButton

                Accessible.name: qsTr("add %1 back").arg(keptRow.modelData.name)
                enabled: keptRow.here && !root.busy
                fontSize: Theme.font.small
                objectName: "addBack"
                opacity: addBackButton.enabled ? 1 : 0.45
                text: qsTr("add back")

                onClicked: root.addBackRequested(keptRow.modelData.id)
            }
        }
    }
}
