pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed
import Omaseed.Kit

// One stage of the add's room (board 64 §5, DECISIONS C47 I8–I11): a head (the temp-folder
// switch, or a glyph), what the stage does with its drive's room, a small meter, and one dim
// line under it. `drive` is one of `App.roomFor`'s lines (`temp` or `save`): its free bytes,
// and what downloads in progress still promise there (I10), which the colour takes from free.
// `plain` draws a whole dim line instead (the same drive's "a rename, no second room").
//
// Hand-formatted (qmlformat rewrites typed function bodies).
//
//     AddRoomLine { what: "downloads first to /t · needs 8 GB"; drive: room.temp; neededBytes: 8e9 }
ColumnLayout {
    id: root

    /// "switch" (the temp-folder switch, I9) or "glyph".
    property string head: "glyph"
    property string glyph: ""
    /// The switch's state, when `head` is "switch".
    property bool switchOn: true
    /// The stage's words before its free words ("downloads first to F · needs X").
    property string what: ""
    /// A whole dim line instead of `what` and the drive's words, with no meter.
    property string plain: ""
    /// `App.roomFor`'s line: source, bytesFree, bytesTotal, where, promised, promisedCount.
    property var drive: ({})
    property real neededBytes: -1
    /// Below this much left over, the line turns yellow (Settings › Notifications).
    property real thresholdBytes: App.lowDiskThresholdGiB * 1024 * 1024 * 1024
    /// The dim line under the stage, in plain words; "" for none.
    property string note: ""
    /// A link after the note (I9's "download straight to …"); "" for none.
    property string linkText: ""
    /// Only the note, under a row drawn by someone else (the switch off: today's line).
    property bool noteOnly: false

    signal switchToggled(bool on)
    signal linkActivated

    readonly property bool known: root.drive.source !== undefined && root.drive.source !== "unknown"
    readonly property real free: root.known ? root.drive.bytesFree : 0
    /// C47 I10: room the downloads in progress still take there.
    readonly property real promised: root.known ? (root.drive.promised || 0) : 0
    readonly property int promisedCount: root.drive.promisedCount || 0
    readonly property bool sizeKnown: root.neededBytes >= 0

    /// "bad" | "warn" | "normal" | "dim"
    readonly property string level: {
        if (root.plain.length > 0)
            return "dim";
        if (!root.known || !root.sizeKnown)
            return "normal";
        const room = root.free - root.promised;
        if (root.neededBytes > room)
            return "bad";
        if (room - root.neededBytes < root.thresholdBytes)
            return "warn";
        return "normal";
    }
    readonly property color tone: root.level === "bad" ? Theme.red
                                  : root.level === "warn" ? Theme.yellow
                                  : root.level === "dim" ? Theme.dimForeground
                                  : Theme.foreground

    /// The mount point where this machine sees the drive, else "this drive" (C17 D9 a).
    readonly property string whereText: root.drive.source === "local" && (root.drive.where || "").length > 0
                                        ? root.drive.where : qsTr("this drive")

    readonly property string freeText: {
        if (!root.known)
            return qsTr("free space unknown");
        const words = qsTr("%1 free on %2").arg(App.sizeText(root.free)).arg(root.whereText);
        if (root.promised <= 0)
            return words;
        return words + " · " + qsTr("%1 of it still coming for %n downloads", "",
                                    root.promisedCount).arg(App.sizeText(root.promised));
    }

    readonly property string whatText: root.plain.length > 0 ? root.plain
                                                              : root.what + " · " + root.freeText

    /// The meter: the drive's used part, what is still coming (I10), then this torrent.
    readonly property bool showMeter: root.plain.length === 0 && root.known && root.sizeKnown
    readonly property real _span: root.drive.bytesTotal > 0 ? root.drive.bytesTotal
                                                            : Math.max(1, root.free)
    readonly property real usedShare: root.drive.bytesTotal > 0
                                      ? Math.max(0, Math.min(1, (root.drive.bytesTotal - root.free) / root._span))
                                      : 0
    readonly property real promisedShare: Math.max(0, Math.min(1 - root.usedShare,
                                                               root.promised / root._span))
    readonly property real needShare: Math.max(0, Math.min(1 - root.usedShare - root.promisedShare,
                                                           root.neededBytes / root._span))

    readonly property int _headWidth: Theme.space(28)

    /// The note's link, as a click on it (tests and the accessible press take this way).
    function activateLink(): void {
        if (root.linkText.length > 0)
            root.linkActivated();
    }

    function _escaped(text: string): string {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    spacing: Theme.space(1)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space(8)
        visible: !root.noteOnly

        Item {
            Layout.alignment: Qt.AlignTop
            implicitHeight: Math.max(stageSwitch.implicitHeight, stageGlyph.implicitHeight)
            implicitWidth: root._headWidth

            Toggle {
                id: stageSwitch

                Accessible.name: qsTr("download to the temp folder first")
                objectName: "addRoomSwitch"
                anchors.verticalCenter: parent.verticalCenter
                checked: root.switchOn
                visible: root.head === "switch"

                onToggled: {
                    const on = stageSwitch.checked;
                    // The owner says what the switch shows: keep it bound to `switchOn`.
                    stageSwitch.checked = Qt.binding(() => root.switchOn);
                    root.switchToggled(on);
                }
            }

            Glyph {
                id: stageGlyph

                objectName: "addRoomGlyph"
                anchors.fill: parent
                color: Theme.dimForeground
                pixelSize: Theme.font.caption
                text: root.glyph
                visible: root.head !== "switch"
            }
        }

        Label {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            objectName: "addRoomWhat"
            Layout.fillWidth: true
            color: root.tone
            size: "caption"
            text: root.whatText
            wrapMode: Text.Wrap
        }

        Item {
            objectName: "addRoomMeter"
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: Theme.space(4)
            implicitWidth: Theme.space(70)
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
                objectName: "addRoomPromised"
                anchors.bottom: parent.bottom
                anchors.top: parent.top
                color: Qt.alpha(Theme.yellow, Theme.fillSelected)
                width: parent.width * root.promisedShare
                x: parent.width * root.usedShare
            }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.top: parent.top
                color: root.level === "normal" ? Theme.accent : root.tone
                // Never thinner than a hairline, so a small torrent still shows where it goes.
                width: Math.max(1, parent.width * root.needShare)
                x: parent.width * (root.usedShare + root.promisedShare)
            }
        }
    }

    Label {
        Accessible.name: root.note + (root.linkText.length > 0 ? " " + root.linkText : "")
        Accessible.role: Accessible.StaticText
        Accessible.onPressAction: root.activateLink()
        objectName: "addRoomNote"
        Layout.fillWidth: true
        Layout.leftMargin: root._headWidth + Theme.space(8)
        color: Theme.dimForeground
        size: "caption"
        text: {
            const body = root._escaped(root.note);
            if (root.linkText.length === 0)
                return body;
            return body + " <a href='room'><font color='%1'>%2</font></a>".arg(Theme.accent).arg(
                        root._escaped(root.linkText));
        }
        textFormat: Text.StyledText
        visible: root.note.length > 0
        wrapMode: Text.Wrap

        onLinkActivated: root.activateLink()

        HoverHandler {
            cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }
}
