pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// Board 45 B1 (D7, DECISIONS C18, #133): the torrent's last move, in General where board 28's
// error block sits for a failure, while the history keeps it. The error block's language in the
// accent: a 1 px rule, a headline (when), the paths, what else went with it, and the actions as
// clickable key hints. `move` is App.move.lastMoveOf(hash).
Rectangle {
    id: root

    property var move: ({})

    /// "back" (the whole move), "backThis" (this torrent only) or "show" (the move's torrents in
    /// the table).
    signal actionTriggered(string action)

    readonly property var hints: {
        const list = [];
        if (root.move.canBack === true)
            list.push({
                          "key": "",
                          "label": qsTr("move back…"),
                          "action": "back",
                          "clickable": true
                      });
        if (root.move.canBackThis === true && (root.move.others ?? 0) > 0)
            list.push({
                          "key": "",
                          "label": qsTr("this one only"),
                          "action": "backThis",
                          "clickable": true
                      });
        if ((root.move.others ?? 0) > 0)
            list.push({
                          "key": "",
                          "label": qsTr("show the move"),
                          "action": "show",
                          "clickable": true
                      });
        return list;
    }
    readonly property string headline: root.move.today === true ? qsTr("Moved today %1").arg(
                                                                      root.move.when ?? "") : qsTr(
                                                                      "Moved %1").arg(
                                                                      root.move.when ?? "")
    readonly property string metaText: {
        const parts = [];
        if ((root.move.others ?? 0) > 0)
            parts.push(qsTr("with %n other torrent(s)", "", root.move.others) + " · " + qsTr("%1 moved").arg(
                           root.move.title ?? ""));
        if ((root.move.carried ?? "").length > 0)
            parts.push(root.move.carried);
        // Nothing of it can go back: everything was moved since.
        if (root.move.canBack !== true)
            parts.push(qsTr("moved again since: nothing to move back"));
        return parts.join(", ");
    }

    // The block is a note named by its headline; its lines are text under it.
    Accessible.name: root.headline
    Accessible.role: Accessible.Note
    color: Theme.fill(Theme.fillIdle)
    implicitHeight: column.implicitHeight + Theme.space(12)
    objectName: "moveBlock"

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top
        color: Theme.accent
        width: 1
    }

    Column {
        id: column

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(3)

        Text {
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            font.weight: Font.Medium
            objectName: "moveHeadline"
            text: root.headline
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
            objectName: "movePaths"
            text: qsTr("from %1 to %2").arg(root.move.from ?? "").arg(root.move.to ?? "")
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.WrapAnywhere
        }

        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "moveMeta"
            text: root.metaText
            textFormat: Text.PlainText
            visible: text.length > 0
            width: parent.width
            wrapMode: Text.Wrap
        }

        Item {
            height: Theme.space(2)
            visible: root.hints.length > 0
            width: 1
        }

        KeyHints {
            model: root.hints
            objectName: "moveActions"
            size: "small"
            visible: root.hints.length > 0
            width: parent.width

            onHintClicked: (index, hint) => root.actionTriggered(hint.action)
        }
    }
}
