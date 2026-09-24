import QtQuick
import Omaseed.Kit

// The filter-with-no-result state (spec v2 §7 Fig. 24): `Nothing matches "<text>" in
// <status>` over the note `esc clears the filter · 0 shows all statuses` (small dim, `esc`
// and `0` set as key chips). Three shapes, chosen from the properties the caller passes:
//   - `text` set, `statusName` a real status (not "All"/empty): `Nothing matches "<text>" in
//     <statusName>`.
//   - `text` set, no narrowing status: `Nothing matches "<text>"`.
//   - `text` empty, a category/tag/tracker filter narrowed the table to nothing:
//     `Nothing in <scopeName>`.
Item {
    id: root

    property string scopeName: ""
    property string statusName: ""
    property string text: ""

    readonly property bool _hasStatus: root.statusName.length > 0 && root.statusName !== qsTr("All")
    readonly property string message: root._computeMessage()

    function _computeMessage() {
        if (root.text.length > 0) {
            return root._hasStatus ? qsTr("Nothing matches \"%1\" in %2").arg(root.text).arg(
                                         root.statusName) : qsTr("Nothing matches \"%1\"").arg(
                                         root.text);
        }
        return root.scopeName.length > 0 ? qsTr("Nothing in %1").arg(root.scopeName) : "";
    }

    implicitHeight: column.implicitHeight
    implicitWidth: column.implicitWidth

    Column {
        id: column

        anchors.centerIn: parent
        spacing: Theme.space(6)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: root.message
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space(4)

            Text {
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                font.pixelSize: Theme.font.caption
                text: "esc"
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: qsTr("clears the filter")
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: "·"
                visible: root._hasStatus
            }

            Text {
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                font.pixelSize: Theme.font.caption
                text: "0"
                visible: root._hasStatus
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                objectName: "allStatusesHint"
                text: qsTr("shows all statuses")
                // Nothing to widen when the status is already "all" (review RUN-13).
                visible: root._hasStatus
            }
        }
    }
}
