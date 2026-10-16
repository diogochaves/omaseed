import QtQuick
import Omaseed.Kit

// The filter-with-no-result state (spec v2 §7 Fig. 24): `Nothing matches "<text>" in
// <status>` over the note `esc clears the filter · 0 shows all statuses` (small dim, `esc`
// and `0` set as key chips, each half also clickable). Each half shows only when its key does
// something: `esc` while there is filter text, `0` while a status narrows the table (#51
// D13). Three shapes, chosen from the properties the caller passes:
//   - `text` set, `statusName` a real status (not "All"/empty): `Nothing matches "<text>" in
//     <statusName>`.
//   - `text` set, no narrowing status: `Nothing matches "<text>"`.
//   - `text` empty, a category/tag/tracker filter (or only a status) narrowed the table to
//     nothing: `Nothing in <scopeName>` (or `<statusName>`).
Item {
    id: root

    property string scopeName: ""
    property string statusName: ""
    property string text: ""

    readonly property bool _hasStatus: root.statusName.length > 0 && root.statusName !== qsTr("All")
    readonly property bool _hasText: root.text.length > 0
    readonly property string message: root._computeMessage()

    /// A click on `esc clears the filter`: the host runs what `esc` runs.
    signal clearRequested
    /// A click on `0 shows all statuses`: the host runs what `0` runs.
    signal allStatusesRequested

    function _computeMessage() {
        if (root.text.length > 0) {
            return root._hasStatus ? qsTr("Nothing matches \"%1\" in %2").arg(root.text).arg(
                                         root.statusName) : qsTr("Nothing matches \"%1\"").arg(
                                         root.text);
        }
        if (root.scopeName.length > 0)
            return qsTr("Nothing in %1").arg(root.scopeName);
        return root._hasStatus ? qsTr("Nothing in %1").arg(root.statusName) : "";
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

            // Each half of the note is also a link that does what its key does.
            Item {
                id: clearLink

                // #77: a link named by what it does, its key the description; its press is
                // the click.
                Accessible.description: qsTr("shortcut %1").arg("esc")
                Accessible.name: qsTr("clear the filter")
                Accessible.role: Accessible.Link
                Accessible.onPressAction: if (clearLink.enabled)
                                              root.clearRequested()
                height: clearRow.implicitHeight
                objectName: "clearFilterLink"
                // Without filter text, `esc` has no filter to clear (#51 D13).
                visible: root._hasText
                width: clearRow.implicitWidth

                Row {
                    id: clearRow

                    spacing: Theme.space(4)

                    Text {
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                        font.pixelSize: Theme.font.caption
                        text: "esc"
                    }

                    Text {
                        color: clearArea.containsMouse ? Theme.foreground : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        text: qsTr("clears the filter")
                    }
                }

                MouseArea {
                    id: clearArea

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: root.clearRequested()
                }
            }

            Text {
                color: Theme.dimForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.small
                text: "·"
                visible: root._hasText && root._hasStatus
            }

            Item {
                id: allLink

                // #77: as the other link.
                Accessible.description: qsTr("shortcut %1").arg("0")
                Accessible.name: qsTr("show all statuses")
                Accessible.role: Accessible.Link
                Accessible.onPressAction: if (allLink.enabled)
                                              root.allStatusesRequested()
                height: allRow.implicitHeight
                objectName: "allStatusesLink"
                // Nothing to widen when the status is already "all" (review RUN-13).
                visible: root._hasStatus
                width: allRow.implicitWidth

                Row {
                    id: allRow

                    spacing: Theme.space(4)

                    Text {
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.letterSpacing: Math.max(1, Math.round(Theme.font.caption * 0.08))
                        font.pixelSize: Theme.font.caption
                        text: "0"
                    }

                    Text {
                        color: allArea.containsMouse ? Theme.foreground : Theme.dimForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.small
                        objectName: "allStatusesHint"
                        text: qsTr("shows all statuses")
                    }
                }

                MouseArea {
                    id: allArea

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: root.allStatusesRequested()
                }
            }
        }
    }
}
