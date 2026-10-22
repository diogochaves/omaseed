pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// Under the add sheet's free-space line (board 44 §4 S1–S3, DECISIONS C17 D8–D9, #120): where
// this machine reads the save disk and its driver is ntfs3, exfat or vfat, every ticked file
// takes its full size at once, so the line says so, naming the driver ("ntfs3", not "NTFS"),
// and counts the full size of what is ticked: it fits, or "Select fewer files, or save to
// another disk". On vfat (FAT32) a file of 4 GiB or more is named in red with "Skip these";
// the add stays possible. The newer ntfs driver, Linux filesystems, unmeasured types and disks
// this machine can't see get nothing.
//
// Hand-formatted (qmlformat rewrites typed function bodies).
ColumnLayout {
    id: root

    property string savePath: ""
    /// The ticked files' bytes (their full size), or -1 while unknown.
    property real neededBytes: -1
    property bool serverDown: false
    /// D9 needs the files: off for a batch of several torrents.
    property bool perFile: true

    /// App.freeSpaceFor(savePath), read again as it moves; a test gives its own.
    property var room: App.freeSpaceRevision >= 0 ? App.freeSpaceFor(root.savePath) : ({})
    readonly property bool allocates: root.room.allocatesInFull === true && !root.serverDown
    readonly property bool fits: root.neededBytes <= root.room.bytesFree
    /// D9: the ticked files FAT32 can't hold; read again whenever the ticks change.
    property var tooBig: root.perFile && root.room.fat32 === true && App.addFiles.wantedSize >= 0
                                  ? App.addFiles.wantedFilesOfAtLeast(4 * 1024 * 1024 * 1024) : []

    spacing: Theme.space(4)
    visible: root.allocates && root.neededBytes >= 0

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)

        Label {
            Layout.alignment: Qt.AlignTop
            color: root.fits ? Theme.dimForeground : Theme.red
            objectName: "addDriverName"
            size: "caption"
            text: "[" + (root.room.fileSystem || "") + "]"
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: root.fits ? Theme.dimForeground : Theme.red
            objectName: "addDriverLine"
            size: "caption"
            text: root.fits ? qsTr("On this disk every file you tick takes its full size at once. Selected: %1, so it fits.").arg(
                                  App.sizeText(root.neededBytes))
                            : qsTr("Needs %1, %2 free: every ticked file takes its full size here at once, so qBittorrent would stop with \"No space left on device\" before downloading. Select fewer files, or save to another disk.").arg(
                                  App.sizeText(root.neededBytes)).arg(App.sizeText(root.room.bytesFree))
            wrapMode: Text.Wrap
        }
    }

    // D9: FAT32's 4 GiB limit.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: root.tooBig.length > 0

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: Theme.red
            objectName: "addFat32Line"
            size: "caption"
            text: qsTr("%n files are 4 GiB or more, past what FAT32 can hold: qBittorrent would stop with \"File too large\" within seconds of the tick.", "", root.tooBig.length)
                  + " " + root.tooBig.slice(0, 3).map(file => file.path).join(", ")
                  + (root.tooBig.length > 3 ? " …" : "")
            wrapMode: Text.Wrap
        }

        Button {
            Layout.alignment: Qt.AlignTop
            objectName: "addFat32Skip"
            text: qsTr("Skip these %n", "", root.tooBig.length)

            onClicked: App.addFiles.skipFilesOfAtLeast(4 * 1024 * 1024 * 1024)
        }
    }
}
