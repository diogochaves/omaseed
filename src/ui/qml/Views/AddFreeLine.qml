pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// The add sheet's line under Save to (docs/25 §4, #29): what the torrent needs, a small
// meter, and how much room is left where it will go. Yellow when the add would leave less
// than the low-disk threshold, red when it does not fit (D7 a: the add still works), green
// when the files are already there (#27's "save there"). The room is `App.freeSpaceFor`'s:
// the server's own figure for its default folder, this machine's for any other folder it can
// see, and "unknown" otherwise (D8 a), with the default folder's figure for scale.
//
// Hand-formatted (qmlformat rewrites typed function bodies).
//
//     AddFreeLine { savePath: savePathField.text; neededBytes: 48e9; totalBytes: 48e9 }
RowLayout {
    id: root

    /// What Save to holds ("" for the server's default).
    property string savePath: ""
    /// Bytes this add writes: the ticked files' size, or -1 while unknown (a magnet).
    property real neededBytes: -1
    /// The whole torrent's size, so an add with files unticked reads "needs X of Y".
    property real totalBytes: -1
    /// The files are already in Save to (#27's files match): nothing new is written.
    property bool alreadyThere: false
    /// Below this much left over, the line turns yellow (Settings › Notifications).
    property real thresholdBytes: App.lowDiskThresholdGiB * 1024 * 1024 * 1024

    // Read again whenever the server's figure, its preferences or the path map move. The
    // revision is part of the expression: a bare read of it can be compiled away.
    readonly property var room: App.freeSpaceRevision >= 0 ? App.freeSpaceFor(root.savePath)
                                                           : ({})
    readonly property bool known: root.room.source !== "unknown"
    readonly property real free: root.room.bytesFree
    readonly property bool sizeKnown: root.neededBytes >= 0

    /// "good" | "bad" | "warn" | "normal"
    readonly property string level: {
        if (root.alreadyThere)
            return "good";
        if (!root.known || !root.sizeKnown)
            return "normal";
        if (root.neededBytes > root.free)
            return "bad";
        if (root.free - root.neededBytes < root.thresholdBytes)
            return "warn";
        return "normal";
    }
    readonly property color tone: root.level === "good" ? Theme.green
                                  : root.level === "bad" ? Theme.red
                                  : root.level === "warn" ? Theme.yellow
                                  : Theme.dimForeground

    /// "this drive" for the default folder, the mount point for any other (D9 a).
    readonly property string whereText: root.room.source === "local" && root.room.where.length > 0
                                        ? root.room.where : qsTr("this drive")

    readonly property string needsText: {
        if (root.alreadyThere)
            return qsTr("needs nothing new");
        if (!root.sizeKnown)
            return qsTr("size known once the metadata arrives");
        if (root.totalBytes > root.neededBytes)
            return qsTr("needs %1 of %2").arg(App.sizeText(root.neededBytes)).arg(
                        App.sizeText(root.totalBytes));
        return qsTr("needs %1").arg(App.sizeText(root.neededBytes));
    }

    readonly property string roomText: {
        if (root.alreadyThere)
            return root.known ? qsTr("the files are already there · %1 free").arg(
                                    App.sizeText(root.free))
                              : qsTr("the files are already there");
        if (!root.known) {
            const scale = root.room.defaultFree > 0
                    ? qsTr("%1 reports only its default folder: %2").arg(App.serverName).arg(
                          App.sizeText(root.room.defaultFree))
                    : "";
            return scale.length > 0 ? qsTr("free space here unknown · %1").arg(scale)
                                    : qsTr("free space here unknown");
        }
        const freeText = App.sizeText(root.free);
        if (root.level === "bad")
            return qsTr("only %1 free on %2 · untick files or pick another folder").arg(
                        freeText).arg(root.whereText);
        if (root.level === "warn")
            return qsTr("%1 free on %2 · leaves %3").arg(freeText).arg(root.whereText).arg(
                        App.sizeText(root.free - root.neededBytes));
        return qsTr("%1 free on %2").arg(freeText).arg(root.whereText);
    }

    /// The meter: the drive's used part (when its size is known), then this torrent.
    readonly property bool showMeter: root.known && root.sizeKnown && !root.alreadyThere
    readonly property real _span: root.room.bytesTotal > 0 ? root.room.bytesTotal
                                                           : Math.max(1, root.free)
    readonly property real usedShare: root.room.bytesTotal > 0
                                      ? Math.max(0, Math.min(1, (root.room.bytesTotal - root.free) / root._span))
                                      : 0
    readonly property real needShare: Math.max(0, Math.min(1 - root.usedShare,
                                                            root.neededBytes / root._span))

    spacing: Theme.space(8)

    Label {
        objectName: "addFreeNeeds"
        color: root.tone
        size: "caption"
        text: root.needsText
    }

    Item {
        objectName: "addFreeMeter"
        Layout.alignment: Qt.AlignVCenter
        implicitHeight: Theme.space(4)
        implicitWidth: Theme.space(96)
        visible: root.showMeter

        Rectangle {
            anchors.fill: parent
            color: Theme.fill(Theme.fillHover)
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.top: parent.top
            color: Theme.fill(Theme.fillPressed)
            width: parent.width * root.usedShare
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.top: parent.top
            color: root.level === "normal" ? Theme.accent : root.tone
            // Never thinner than a hairline, so a small torrent still shows where it goes.
            width: Math.max(1, parent.width * root.needShare)
            x: parent.width * root.usedShare
        }
    }

    Label {
        objectName: "addFreeRoom"
        Layout.fillWidth: true
        color: root.tone
        elide: Text.ElideRight
        size: "caption"
        // Without the meter between them, the two halves need a separator.
        text: (root.showMeter ? "" : "· ") + root.roomText
    }
}
