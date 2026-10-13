pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// docs/28 G1–S5 (#54): why the torrent is in error, at the top of the General tab. The Kit
// Banner's language on several lines: a 1 px rule in the kind's colour (red for an error,
// yellow for missing files, as the Status row), the kind glyph, a headline in the bright
// foreground, qBittorrent's own words under it, the folder, when the log said so, and the
// actions that fit the cause as clickable key hints. `problem` is TorrentProblem::toMap().
Rectangle {
    id: root

    property var problem: ({})
    /// "1.2 GB free" under the folder when the disk is full; empty leaves it out.
    property string freeText: ""

    /// One of the problem's actions: "location", "start", "recheck", "rename"; or "copy".
    signal actionTriggered(string action)

    readonly property bool found: root.problem.found === true
    readonly property bool missingFiles: root.problem.missingFiles === true
    readonly property color kindColor: root.missingFiles ? Theme.yellow : Theme.red
    readonly property var hints: {
        const keys = {
            "location": ["m", qsTr("set location")],
            "start": ["p", qsTr("try again")],
            "recheck": ["c", qsTr("recheck")],
            "rename": ["2", qsTr("rename in Files")]
        };
        const list = [];
        for (const action of root.problem.actions || []) {
            const entry = keys[action];
            if (entry)
                list.push({
                              "key": entry[0],
                              "label": entry[1],
                              "action": action,
                              "clickable": true
                          });
        }
        if (root.found)
            list.push({
                          "key": "",
                          "label": qsTr("copy the line"),
                          "action": "copy",
                          "clickable": true
                      });
        return list;
    }
    readonly property string metaText: {
        if (!root.found)
            return qsTr(
                        "Its log starts at its last restart and keeps 20 000 lines. Trying again usually logs the reason again.");
        const parts = [qsTr("qBittorrent's log"), root.problem.loggedAtText];
        if (root.problem.count > 1)
            parts.push(qsTr("%n times", "", root.problem.count));
        if (root.problem.ambiguous === true)
            parts.push(qsTr("may be about the other torrent named this"));
        return parts.filter(part => part && part.length > 0).join(" · ");
    }
    readonly property string folderText: {
        const folder = root.problem.folder || "";
        if (folder.length === 0)
            return "";
        return root.freeText.length > 0 ? folder + " · " + root.freeText : folder;
    }

    color: Theme.fill(Theme.fillIdle)
    implicitHeight: column.implicitHeight + Theme.space(12)
    objectName: "errorBlock"

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: root.kindColor
        width: 1
    }

    Text {
        id: glyph

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.top: column.top
        color: root.kindColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.body
        text: root.missingFiles ? Glyphs.statusErrored : Glyphs.close
    }

    Column {
        id: column

        anchors.left: glyph.right
        anchors.leftMargin: Theme.space(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(3)

        Text {
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            font.weight: Font.Medium
            objectName: "errorHeadline"
            text: root.problem.headline || ""
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        Text {
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "errorDetail"
            text: root.problem.detail || ""
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.WrapAnywhere
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "errorFolder"
            text: root.folderText
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.WrapAnywhere
        }

        Text {
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "errorMeta"
            text: root.metaText
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }

        // A little more room above the actions than between the lines.
        Item {
            height: Theme.space(2)
            width: 1
        }

        KeyHints {
            model: root.hints
            objectName: "errorActions"
            size: "small"
            width: parent.width

            onHintClicked: (index, hint) => root.actionTriggered(hint.action)
        }
    }
}
