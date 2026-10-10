pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// docs/27 L2/L3 (#41): what a folder picked with `browse` means to the server, under the
// field it went into. Fed by `App.serverPathForPick(picked)`:
//   mapped  — one dim line, "picked <local> · the server sees it as <remote>";
//   outside — the yellow line of SharedDataLine's look: the server has a path map and the
//             pick lies outside it, so qBittorrent may not see it (the field keeps it);
//   neither — nothing (no map: the pick is the path).
//
//     BrowsedPathLine { pick: root._pick; serverName: App.serverName }
Item {
    id: root

    /// `App.serverPathForPick()`'s answer, or `{}` before a pick (or after typing over it).
    property var pick: ({})
    property string serverName: ""

    readonly property bool mapped: root.pick.mapped === true
    readonly property bool outside: root.pick.outside === true
    readonly property alias text: lineText.text

    function _escape(text: string): string {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    Layout.fillWidth: true
    implicitHeight: root.outside ? lineText.implicitHeight + Theme.space(12) : lineText.implicitHeight
    visible: root.mapped || root.outside

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Theme.yellow, 0.08)
        visible: root.outside

        Rectangle {
            color: Theme.yellow
            height: parent.height
            width: Theme.space(2)
        }
    }

    Text {
        id: lineText

        anchors.left: parent.left
        anchors.leftMargin: root.outside ? Theme.space(10) : 0
        anchors.right: parent.right
        anchors.rightMargin: root.outside ? Theme.space(10) : 0
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        lineHeight: 1.3
        objectName: "browsedPathText"
        text: {
            if (root.mapped)
                return qsTr("picked %1 · the server sees it as %2").arg(root.pick.local).arg(root.pick.path);
            if (!root.outside)
                return "";
            const head = qsTr("%1 is outside the folders mapped for %2.").arg(root.pick.local).arg(root.serverName);
            const tail = qsTr("qBittorrent may not see it. Mapped: %1.").arg(root.pick.mappings);
            return "<font color='%1'>%2</font><br><font color='%3'>%4</font>".arg(Theme.yellow).arg(root._escape(head)).arg(
                        Theme.dimForeground).arg(root._escape(tail));
        }
        textFormat: root.outside ? Text.StyledText : Text.PlainText
        wrapMode: Text.Wrap
    }
}
