import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One line of the Agents row's gate (board 67 §3 `.kl`, docs/DECISIONS.md C50 GT3, GT8): the
// kind's name over what it covers, and its levels as chips at the end. A level shown but never
// offered (allow, for moves and remove) is struck and does not click. The line is a cursor stop
// of the Settings pane: `hasCursor` outlines it as a row's cursor does, and the pane's enter
// moves its level on (SettingsTable's choice).
//
//     AgentGateLine {
//         title: "Fixes"; subtitle: "retry, …"; value: App.agentFixes
//         options: [["off", "off"], ["ask", "ask"], ["allow", "allow"]]
//         onChosen: level => App.agentFixes = level
//     }
Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    /// [[value, text], …], in the board's order.
    property var options: []
    /// value => whether the level can be had; a level that can't is struck.
    property var offered: value => true
    property string value: ""
    property bool hasCursor: false
    /// Reads is off (GT8): the line is dimmed and its chips don't click; the line itself still
    /// takes the click, so it never falls through to the row under it.
    property bool greyed: false
    /// Each level chip's objectName is this and its value ("agentFixesChip_" → "agentFixesChip_ask").
    property string chipPrefix: "level_"
    /// The ground the line lies on (the row's).
    property ThemeInk block: Theme.on.window
    property Ink ink: null

    /// A level was clicked.
    signal chosen(string value)
    /// The line itself was clicked (the pane's cursor goes to it).
    signal clicked

    Accessible.description: root.subtitle
    Accessible.name: root.title
    Accessible.role: Accessible.Grouping

    border.color: root.hasCursor ? Theme.accent : "transparent"
    border.width: Theme.hairWidth
    color: "transparent"
    // Lines sit edge to edge (their own padding is the gap), so no click lands between them.
    implicitHeight: layout.implicitHeight + Theme.space(6)
    opacity: root.greyed ? 0.4 : 1
    radius: Theme.cornerRadius

    MouseArea {
        anchors.fill: parent

        onClicked: root.clicked()
    }

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(4)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(2)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(12)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                bright: true
                elide: Text.ElideRight
                ink: root.ink
                objectName: "agentGateTitle"
                size: "small"
                text: root.title
                voice: "control"
            }

            Label {
                Layout.fillWidth: true
                dim: true
                ink: root.ink
                objectName: "agentGateSubtitle"
                size: "caption"
                text: root.subtitle
                wrapMode: Text.Wrap
            }
        }

        Row {
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: Theme.space(6)

            Repeater {
                model: root.options

                Chip {
                    id: levelChip

                    required property var modelData
                    readonly property bool never: !root.offered(levelChip.modelData[0])

                    // A struck level says why it is there to a screen reader.
                    Accessible.name: levelChip.never ? qsTr("%1, never offered").arg(levelChip.modelData[1])
                                                     : levelChip.modelData[1]
                    block: root.block
                    checkable: !levelChip.never
                    enabled: !levelChip.never && !root.greyed
                    objectName: root.chipPrefix + levelChip.modelData[0]
                    opacity: levelChip.never ? 0.35 : 1
                    selected: !levelChip.never && root.value === levelChip.modelData[0]
                    struck: levelChip.never
                    text: levelChip.modelData[1]

                    onClicked: root.chosen(levelChip.modelData[0])
                }
            }
        }
    }
}
