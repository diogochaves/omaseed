pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// docs/26 S1/S3/L1: the yellow line that says which torrents outside the selection use the
// same data, so a delete with files or a move would take it from under them. Fed by
// `App.sharedData(hashes)`; shown only when that found any (`outside` not empty).
//
// One selected torrent lists its sharers, each with tracker, state and ratio. A selection
// lists the selected torrents that share (up to three, then "and N more"), each with how many
// sharers it has and where they seed (D5 a).
//
//     SharedDataLine { shared: App.sharedData(hashes); selectionCount: hashes.length; mode: "delete" }
Rectangle {
    id: root

    /// `App.sharedData()`'s answer.
    property var shared: ({})
    /// How many torrents the question is about (one reads "Its files…", more "N of them…").
    property int selectionCount: 1
    /// "delete" or "move": the verb in the first line.
    property string mode: "delete"

    /// How many names are spelled out before "and N more".
    readonly property int listed: 3

    readonly property var _sharing: root.shared.sharing ?? []
    readonly property var _outside: root.shared.outside ?? []

    function _headline(): string {
        const count = root._outside.length;
        if (root.selectionCount === 1) {
            if (root.mode === "move")
                return count === 1 ? qsTr(
                                         "Its files are also seeded by 1 torrent that isn't moving.") :
                                     qsTr("Its files are also seeded by %1 torrents that aren't moving.").arg(
                                         count);
            return count === 1 ? qsTr(
                                     "Its files are also seeded by 1 torrent you're not deleting.") :
                                 qsTr("Its files are also seeded by %1 torrents you're not deleting.").arg(
                                     count);
        }
        const torrents = count === 1 ? qsTr("1 torrent") : qsTr("%1 torrents").arg(count);
        const who = root._sharing.length === 1 ? qsTr("1 of them shares") : qsTr("%1 of them share").arg(
                                                     root._sharing.length);
        if (root.mode === "move")
            return qsTr("%1 files with %2 that %3 moving.").arg(who).arg(torrents).arg(count === 1
                                                                                       ? qsTr("isn't") :
                                                                                         qsTr("aren't"));
        return qsTr("%1 files with %2 you're not deleting.").arg(who).arg(torrents);
    }

    /// The lines under the headline: one per sharer (one torrent selected) or one per
    /// selected torrent that shares (a selection), each as StyledText.
    function _lines(): var {
    // On the band's warning tint (docs/DECISIONS.md C30 F5).
    const dim = String(Theme.on.warning.dimForeground);
    const out = [];
    if (root.selectionCount === 1) {
        const sharers = root._sharing.length > 0 ? root._sharing[0].sharers : [];
        for (const sharer of sharers.slice(0, root.listed)) {
            const parts = [];
            if (sharer.tracker.length > 0)
            parts.push(sharer.tracker);
            parts.push(sharer.stateText);
            if (!sharer.missingFiles)
            parts.push(qsTr("ratio %1").arg(sharer.ratioText));
            out.push("<b>%1</b> <font color='%2'>· %3</font>".arg(root._escape(sharer.name)).arg(
            dim).arg(root._escape(parts.join(" · "))));
        }
            if (sharers.length > root.listed)
            out.push("<font color='%1'>%2</font>".arg(dim).arg(qsTr("and %1 more").arg(
            sharers.length - root.listed)));
            return out;
        }
            for (const entry of root._sharing.slice(0, root.listed)) {
                const trackers = [];
                for (const sharer of entry.sharers) {
                    if (sharer.tracker.length > 0 && trackers.indexOf(sharer.tracker) < 0)
                    trackers.push(sharer.tracker);
                }
                    let where = "";
                    if (trackers.length === 1)
                    where = ", " + trackers[0];
                    else if (trackers.length > 1)
                    where = ", " + trackers.slice(0, -1).join(", ") + qsTr(" and ")
                    + trackers[trackers.length - 1];
                    out.push("<b>%1</b> <font color='%2'>· %3</font>".arg(root._escape(
                    entry.name)).arg(dim).arg(root._escape(qsTr("with %1").arg(
                    entry.sharers.length) + where)));
                }
                    if (root._sharing.length > root.listed)
                    out.push("<font color='%1'>%2</font>".arg(dim).arg(qsTr("and %1 more").arg(
                    root._sharing.length - root.listed)));
                    return out;
                }

                    function _escape(text: string): string {
                        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(
                        />/g, "&gt;");
                    }

                        readonly property alias text: lineText.text

                        Layout.fillWidth: true
                        // The warning tint; its words lifted against it (docs/DECISIONS.md C30 F5).
                        color: Theme.on.warning.fill
                        implicitHeight: lineText.implicitHeight + Theme.space(12)
                        visible: root._outside.length > 0

                        Rectangle {
                            color: Theme.yellow
                            height: parent.height
                            width: Theme.space(2)
                        }

                        Text {
                            id: lineText

                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(10)
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(10)
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.on.warning.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.small
                            lineHeight: 1.3
                            text: {
                                void root.shared;
                                return ["<font color='%1'>%2</font>".arg(Theme.on.warning.yellow).arg(
                                            root._headline())].concat(root._lines()).join("<br>");
                            }
                            textFormat: Text.StyledText
                            wrapMode: Text.Wrap
                        }
                    }
