pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// docs/28 G1–S5 (#54): why the torrent is in error, at the top of the General tab. The Kit
// Banner's language on several lines: a 1 px rule in the kind's colour (red for an error,
// yellow for missing files, as the Status row), the kind glyph, a headline in the bright
// foreground, qBittorrent's own words under it, the folder, when the log said so, and the
// actions that fit the cause as clickable key hints. `problem` is TorrentProblem::toMap().
//
// docs/44 D13 (#121): a stuck move (`problem.stuckMove`) is red in the same place, with
// "show both folders" and "set location…", and its partial copies named when they are seen.
//
// docs/32 F1–F3 (#55): the same block for a move, rename or part file that failed while the
// torrent kept working (`problem` is then TorrentFailure::toMap(), which has a `kind`): always
// yellow with "!", since nothing is broken, its own actions, and `x dismiss` last.
Rectangle {
    id: root

    property var problem: ({})
    /// docs/44 D13: a stuck move's partial copies, named where omaseed can see the destination
    /// (`App.stuckMoveCopies`); "" keeps the problem's own "may be left".
    property string partialText: ""
    /// "1.2 GB free" under the folder when the disk is full; empty leaves it out.
    property string freeText: ""

    /// One of the problem's actions: "location", "start", "recheck", "rename"; or "copy". A
    /// failure's: "location", "folder", "rename", "copy", "dismiss". Either's "log": its lines
    /// in the log (docs/32 D13).
    signal actionTriggered(string action)

    readonly property bool found: root.problem.found === true
    /// docs/32: a failure rather than an error.
    readonly property bool failure: (root.problem.kind || "").length > 0
    readonly property bool missingFiles: root.problem.missingFiles === true
    /// docs/44 D13 (#121): finished, but its files never left the incomplete folder. Red, in
    /// board 28's place, on a torrent that still seeds.
    readonly property bool stuckMove: root.problem.stuckMove === true
    readonly property color kindColor: root.missingFiles || root.failure ? Theme.yellow : Theme.red
    readonly property var hints: {
        const keys = {
            // docs/44 M1: board 43's sheet.
            "location": ["m", root.stuckMove ? qsTr("set location…") : qsTr("move…")],
            "bothFolders": ["", qsTr("show both folders")],
            // DECISIONS C20 (#107): `p` stops an errored torrent; "retry" has a key of its own.
            "start": ["e", qsTr("retry")],
            "recheck": ["c", qsTr("recheck")],
            "rename": ["2", qsTr("rename in Files")],
            "folder": ["o", qsTr("open folder")],
            // Board 44 §2 (C17 D3): libtorrent's hybrid error; only adding it again clears it.
            "addAgain": ["", qsTr("add it again…")],
            // #108: advice, not an action: the app can't check a disk, so no key, no click.
            "checkDisk": ["", qsTr("check the disk first")]
        };
        const list = [];
        for (const action of root.problem.actions || []) {
            const entry = keys[action];
            if (entry)
                list.push({
                              "key": entry[0],
                              "label": entry[1],
                              "action": action,
                              "clickable": action !== "checkDisk"
                          });
        }
        if (root.found)
            list.push({
                          "key": "",
                          "label": qsTr("copy the line"),
                          "action": "copy",
                          "clickable": true
                      });
        // docs/32 D13 (F1, L2): the log face, narrowed to this torrent's lines.
        if (root.found || root.failure)
            list.push({
                          "key": "",
                          "label": qsTr("its lines in the log"),
                          "action": "log",
                          "clickable": true
                      });
        // D3, D4: the last, and kept when the pane is too narrow for every hint.
        if (root.failure)
            list.push({
                          "key": "x",
                          "label": qsTr("dismiss"),
                          "action": "dismiss",
                          "clickable": true,
                          "keep": true
                      });
        return list;
    }
    readonly property string metaText: {
        // docs/44 D13: no line, no guess why; the log's limits are not the point here.
        if (!root.found && root.stuckMove)
            return "";
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

    // #79: the block is an alert named by its headline; its lines are text under it, and the
    // kind's glyph stays out.
    Accessible.name: root.problem.headline || ""
    Accessible.role: Accessible.AlertMessage
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
        text: root.missingFiles || root.failure ? Glyphs.statusErrored : Glyphs.close
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
            Accessible.name: text
            Accessible.role: Accessible.StaticText
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

        // #108: what the cause may mean, when the words alone don't say ("may be damaged").
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "errorAdvice"
            text: root.partialText.length > 0 ? root.partialText : root.problem.advice || ""
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }

        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
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
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "errorMeta"
            text: root.metaText
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }

        // A little more room above the actions than between the lines.
        Item {
            height: Theme.space(2)
            width: 1
        }

        KeyHints {
            id: actions

            model: root.hints
            objectName: "errorActions"
            size: "small"
            width: parent.width

            onHintClicked: (index, hint) => root.actionTriggered(hint.action)
        }

        // What the first line had no room for, on a second: in the pane's default width the
        // keyless "copy the line" and "its lines in the log" (docs/32 D13) would otherwise be
        // dropped, and the log link has no key to stand in for it.
        KeyHints {
            model: root.hints.filter((hint, index) => actions._shown[index] !== true)
            objectName: "errorActionsMore"
            size: "small"
            visible: actions.shownCount < root.hints.length
            width: parent.width

            onHintClicked: (index, hint) => root.actionTriggered(hint.action)
        }
    }
}
