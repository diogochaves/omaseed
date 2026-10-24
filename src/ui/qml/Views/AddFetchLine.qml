pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The add sheet's Files row while a magnet's file list is not in yet (docs/29 §4, #44): one
// box where the tree will be, in the Kit Banner's language without its rule: a glyph, a
// headline, dim lines under it. It draws the lookup's phases (`App.addLookup.phase`):
//   waiting   F7  the server is not signed in yet; the lookup starts once it is
//   fetching  F1  from 0.5 s (D2 a); F8 for a link to a .torrent
//   slow      F3  from 10 s: names the two ways out
//   never     F4  from 60 s, yellow: and that closing leaves the lookup on the server (D6 a)
//   private   F5  the library's private tracker: nothing asked, use the site's .torrent (D7 a)
// Anything else draws nothing (`shown` false): "quiet" so a quick answer just shows the tree,
// "done" because the tree is there, "unsupported" and "failed" because the sheet is today's
// (D11 a). `summaryText` is what the Files head prints on its right, with the seconds.
//
// Hand-formatted (qmlformat rewrites typed function bodies).
//
//     AddFetchLine { phase: App.addLookup.phase; seconds: App.addLookup.seconds; kind: "magnet" }
Rectangle {
    id: root

    /// The lookup's phase by name (MetadataLookup::phaseName).
    property string phase: ""
    /// Whole seconds since the lookup started.
    property int seconds: 0
    /// "magnet" or "url": a link's wait is qBittorrent downloading its .torrent (F8).
    property string kind: "magnet"
    /// F5: the tracker host the magnet shares with the library's private torrents.
    property string privateHost: ""
    /// F7: the server it waits for.
    property string serverName: ""

    readonly property bool shown: ["waiting", "fetching", "slow", "never", "private"].indexOf(root.phase) >= 0
    readonly property bool isLink: root.kind === "url"
    /// F4's summary is yellow: the likely cause is now the magnet, not the network.
    readonly property bool summaryWarn: root.phase === "never"

    /// "3 s", "24 s", "1 min 12 s".
    function durationText(total: int): string {
        if (total < 60)
            return qsTr("%1 s").arg(total);
        const minutes = Math.floor(total / 60);
        const rest = total % 60;
        return rest === 0 ? qsTr("%1 min").arg(minutes) : qsTr("%1 min %2 s").arg(minutes).arg(rest);
    }

    /// A key or a name inside a dim line, in the foreground colour (a binding: it repaints).
    function keyText(text: string): string {
        return "<font color=\"" + root.ink.foreground + "\">" + text + "</font>";
    }

    readonly property string summaryText: {
        const time = root.durationText(root.seconds);
        switch (root.phase) {
        case "waiting":
            return qsTr("waiting for %1").arg(root.serverName);
        case "fetching":
            return root.isLink ? qsTr("downloading the .torrent · %1").arg(time)
                               : qsTr("fetching · %1").arg(time);
        case "slow":
            return root.isLink ? qsTr("no answer yet · %1").arg(time)
                               : qsTr("still looking · %1").arg(time);
        case "never":
            return root.isLink ? qsTr("no answer after %1").arg(time)
                               : qsTr("no list after %1").arg(time);
        case "private":
            return qsTr("private tracker");
        }
        return "";
    }

    readonly property string glyph: root.phase === "slow" ? Glyphs.lookupSlow
                                  : root.phase === "never" ? Glyphs.lookupNever
                                  : root.phase === "private" ? Glyphs.privateTracker
                                  : Glyphs.lookup
    /// docs/DECISIONS.md C30 F1: the line's words on its own 4 % fill.
    readonly property Ink ink: Ink {
        animated: false
        ground: Theme.on.idle
    }
    readonly property color glyphColor: root.phase === "slow" || root.phase === "never"
                                        ? root.ink.yellow
                                        : root.phase === "private" ? root.ink.magenta
                                        : root.ink.dimForeground

    readonly property string headline: {
        switch (root.phase) {
        case "waiting":
            return qsTr("Connecting to %1").arg(root.serverName);
        case "fetching":
            return root.isLink ? qsTr("qBittorrent is downloading the .torrent from the link")
                               : qsTr("Asking qBittorrent for the file list");
        case "slow":
            return root.isLink ? qsTr("The link hasn't answered") : qsTr("No peer has sent the file list yet");
        case "never":
            return root.isLink ? qsTr("The link hasn't answered") : qsTr("The file list may never come");
        case "private":
            return qsTr("A private tracker's magnet: no file list before adding");
        }
        return "";
    }

    /// The dim lines under the headline, each rich text.
    readonly property var lines: {
        const enter = root.keyText(qsTr("enter"));
        const shiftEnter = root.keyText(qsTr("shift+enter"));
        const add = root.keyText(qsTr("add"));
        const leftRunning = qsTr("qBittorrent goes on looking until it restarts, even after you close this sheet.");
        const noReport = qsTr("qBittorrent doesn't report a failed download here.");
        switch (root.phase) {
        case "waiting":
            return [qsTr("The file list is asked for as soon as it answers. %1 works now: it goes out once the server is up.").arg(add)];
        case "fetching":
            return root.isLink
                    ? [qsTr("Its files show here when it has it. If it's already in your library, the duplicate sheet opens instead.")]
                    : [qsTr("The tree shows here once it has it, usually in a few seconds. %1 takes every file meanwhile, as it does now.").arg(add)];
        case "slow":
            return root.isLink
                    ? [noReport + " " + qsTr("%1 adds it with every file. %2 adds it and stops it as soon as its list arrives.").arg(enter).arg(shiftEnter)]
                    : [qsTr("Some magnets take minutes. %1 adds it with every file. %2 adds it and stops it as soon as its list arrives, so you choose in the Files tab before anything downloads.").arg(enter).arg(shiftEnter)];
        case "never":
            return [(root.isLink ? noReport : qsTr("A magnet nobody seeds never gets one.")) + " "
                    + qsTr("The ways out are the same: %1 adds every file, %2 adds it stopped until its list arrives.").arg(enter).arg(shiftEnter),
                    leftRunning];
        case "private":
            return [qsTr("Your library's torrents from %1 are private, and qBittorrent can't fetch a private torrent's list from a magnet, so omaseed doesn't ask. To choose files first, add the site's %2 instead.").arg(root.keyText(root.privateHost)).arg(root.keyText(qsTr(".torrent"))),
                    qsTr("Added from this magnet, it may stay at \"metadata\" for good.")];
        }
        return [];
    }

    border.color: Theme.fill(Theme.hairlineAlpha)
    border.width: 1
    color: root.ink.fill
    implicitHeight: content.implicitHeight + 2 * Theme.space(9)
    visible: root.shown

    RowLayout {
        id: content

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(10)

        Label {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Theme.space(16)
            color: root.glyphColor
            objectName: "addFetchGlyph"
            size: "small"
            text: root.glyph
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space(4)

            Label {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                bright: true
                ink: root.ink
                objectName: "addFetchHeadline"
                size: "small"
                text: root.headline
                wrapMode: Text.Wrap
            }

            Repeater {
                model: root.lines

                Label {
                    id: lineLabel

                    required property string modelData
                    /// #79: the line's words for a screen reader, without its markup.
                    readonly property string spoken: lineLabel.modelData.replace(/<[^>]*>/g, "")

                    Accessible.name: lineLabel.spoken
                    Accessible.role: Accessible.StaticText
                    Layout.fillWidth: true
                    dim: true
                    ink: root.ink
                    lineHeight: 1.1
                    objectName: "addFetchLine"
                    size: "caption"
                    text: lineLabel.modelData
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
