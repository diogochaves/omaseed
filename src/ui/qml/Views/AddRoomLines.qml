pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The add sheet's room under Save to (board 64 §5, DECISIONS C47 I8–I12). Without a temp
// folder it is today's line (`AddFreeLine`, docs/25 §4). With one, two stages, each with its
// own meter and colour (I8): where the torrent downloads first, then where it moves; on one
// drive the second is a dim rename. The first stage's switch (I9) sends this torrent straight
// to Save to; off, today's line again with the switch and a dim line saying what it skips.
// Transmission's temp folder is session-wide: a glyph, no switch. `room` is
// `App.roomFor(…)`'s map (a test sets its own); a batch keeps today's one line.
//
// Hand-formatted (qmlformat rewrites typed function bodies).
//
//     AddRoomLines { savePath: …; category: …; autoTMM: …; useTemp: sheet.useTemp
//                    onUseTempRequested: on => sheet.useTemp = on }
ColumnLayout {
    id: root

    /// What Save to holds ("" for the server's default).
    property string savePath: ""
    /// The category picked, and whether the add leaves its folders to it (I12).
    property string category: ""
    property bool autoTMM: true
    /// The switch: this torrent downloads to the temp folder first (I9).
    property bool useTemp: true
    /// A batch: today's one line, no switch (no per-torrent choice in a batch).
    property bool batch: false
    property real neededBytes: -1
    property real totalBytes: -1
    property bool alreadyThere: false
    property bool serverDown: false
    property real thresholdBytes: App.lowDiskThresholdGiB * 1024 * 1024 * 1024

    // Read again whenever the server's figure, its preferences, the path map, what downloads
    // in progress promise or a category moves (App's freeSpaceRevision).
    property var room: App.freeSpaceRevision >= 0 && !root.batch
                       ? App.roomFor(root.savePath, root.category, root.autoTMM, root.useTemp)
                       : ({})

    /// The switch was turned (or a link asked for it): the owner sets `useTemp`.
    signal useTempRequested(bool on)

    readonly property alias todayLine: todayLine
    readonly property alias tempLine: tempLine
    readonly property alias saveLine: saveLine

    /// A temp folder to speak of: not a batch, nor files already there, nor a server down.
    readonly property bool hasTemp: root.room.hasTemp === true && !root.batch && !root.alreadyThere
                                    && !root.serverDown
    readonly property bool twoStages: root.hasTemp && root.room.useTemp === true
    /// The switch is off: today's line, with the switch before it and what it skips.
    readonly property bool skipping: root.hasTemp && root.room.useTemp !== true
    readonly property string tempPath: root.room.tempPath || ""
    /// Where it ends: Save to, or what the server picks for an empty one.
    readonly property string endPath: root.room.savePath || root.savePath

    readonly property string engineWords: {
        if (root.room.engine === "transmission")
            return qsTr("Transmission's setting, for every torrent.");
        if (root.room.engine === "deluge")
            return qsTr("Deluge's download folder.");
        // C47 I12: the folder is the picked category's own, not the server's (#280).
        if (root.room.tempRule === "own" && root.category !== "")
            return qsTr("The category %1's own temp folder.").arg(root.category);
        return qsTr("qBittorrent's setting.");
    }

    spacing: Theme.space(7)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: !root.twoStages

        // I9: off, the switch stays where it was, in front of today's line.
        Toggle {
            id: skipSwitch

            Accessible.name: qsTr("download to the temp folder first")
            objectName: "addRoomSkipSwitch"
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Theme.space(28)
            checked: false
            visible: root.skipping

            onToggled: {
                skipSwitch.checked = false;
                root.useTempRequested(true);
            }
        }

        AddFreeLine {
            id: todayLine

            Layout.fillWidth: true
            alreadyThere: root.alreadyThere
            neededBytes: root.neededBytes
            savePath: root.savePath
            serverDown: root.serverDown
            thresholdBytes: root.thresholdBytes
            totalBytes: root.totalBytes
        }
    }

    AddRoomLine {
        id: skipLine

        objectName: "addRoomSkip"
        Layout.fillWidth: true
        linkText: qsTr("use the temp folder")
        note: qsTr("This torrent skips %1: it downloads straight here.").arg(root.tempPath)
        noteOnly: true
        visible: root.skipping

        onLinkActivated: root.useTempRequested(true)
    }

    // I8: where it downloads first.
    AddRoomLine {
        id: tempLine

        objectName: "addRoomTemp"
        Layout.fillWidth: true
        drive: root.room.temp || ({})
        glyph: "↓"
        head: root.room.switchable === true ? "switch" : "glyph"
        linkText: tempLine.level === "bad" && root.room.switchable === true
                  ? qsTr("download straight to %1").arg(root.endPath) : ""
        neededBytes: root.neededBytes
        note: tempLine.level === "bad" ? qsTr("Won't fit with what's still coming.")
            : tempLine.level === "warn"
              ? qsTr("Leaves less than your low-disk line (%1) while it downloads.").arg(
                    App.sizeText(root.thresholdBytes))
              : root.engineWords
        switchOn: true
        thresholdBytes: root.thresholdBytes
        visible: root.twoStages
        what: qsTr("downloads first to %1 · %2").arg(root.tempPath).arg(todayLine.needsText)

        onLinkActivated: root.useTempRequested(false)
        onSwitchToggled: on => root.useTempRequested(on)
    }

    // I8, I11: then where it ends; on the same drive a rename, dim.
    AddRoomLine {
        id: saveLine

        objectName: "addRoomSave"
        Layout.fillWidth: true
        drive: root.room.save || ({})
        glyph: "→"
        neededBytes: root.neededBytes
        note: saveLine.plain.length > 0 ? ""
            : saveLine.level !== "bad"
              ? qsTr("A copy to another drive when it finishes; the temp drive's room comes back after.")
              : root.room.engine === "qbittorrent" || root.room.engine === undefined
                ? qsTr("When it finishes, the move to %1 won't fit: qBittorrent leaves part of it there and keeps it in the temp folder.").arg(saveLine.whereText)
                : qsTr("When it finishes, the move to %1 won't fit: it stays in the temp folder, part of it copied.").arg(saveLine.whereText)
        plain: root.room.sameDrive === true
               ? qsTr("then moved to %1 on the same drive: a rename, no second room").arg(root.endPath) : ""
        thresholdBytes: root.thresholdBytes
        visible: root.twoStages
        what: qsTr("then moves to %1 · %2").arg(root.endPath).arg(todayLine.needsText)
    }
}
