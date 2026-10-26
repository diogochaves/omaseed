pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// Move back's loose files (DECISIONS C32 D19, board 51): each file the move carried, and any
// put there since, one line each: a sign in the line's colour (green goes back, yellow changed
// since the move and stays, dim stays), the file and a dim detail, and at the right what
// happens to it ("back", "stays").
ColumnLayout {
    id: root

    /// [{name, detail, verdict, kind}] from App.move.planBack()'s `carryBack.rows`; kind is
    /// "ok", "warn" or "fact".
    property var rows: []

    spacing: Theme.space(2)

    Repeater {
        model: root.rows

        RowLayout {
            id: fileRow

            required property var modelData
            readonly property color tone: fileRow.modelData.kind === "ok" ? Theme.green
                                        : fileRow.modelData.kind === "warn" ? Theme.yellow
                                        : Theme.dimForeground

            Layout.fillWidth: true
            objectName: "moveCarryBackRow"
            spacing: Theme.space(8)

            Label {
                Layout.preferredWidth: Theme.space(12)
                color: fileRow.tone
                horizontalAlignment: Text.AlignHCenter
                size: "small"
                text: fileRow.modelData.kind === "ok" ? "✓" : fileRow.modelData.kind === "warn" ? "!" : "·"
            }

            Label {
                Accessible.name: fileRow.modelData.detail.length > 0
                                 ? qsTr("%1, %2: %3").arg(fileRow.modelData.name).arg(fileRow.modelData.detail).arg(
                                       fileRow.modelData.verdict) : text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                elide: Text.ElideMiddle
                objectName: "moveCarryBackName"
                size: "small"
                text: fileRow.modelData.name
            }

            Label {
                Layout.fillWidth: true
                dim: true
                elide: Text.ElideRight
                size: "caption"
                text: fileRow.modelData.detail.length > 0 ? "· " + fileRow.modelData.detail : ""
            }

            Label {
                color: fileRow.tone
                objectName: "moveCarryBackVerdict"
                size: "small"
                text: fileRow.modelData.verdict
            }
        }
    }
}
