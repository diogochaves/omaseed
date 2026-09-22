import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// The hero row (spec v2 §1 Fig. 1 `.hero`): mark in an outlined square, app name with a
// tracked server caption and status dot beneath, two headline speed `Stat`s, a search `Field`
// with a `/` hint and live count, four glyph `IconButton`s, and a hairline rule underneath.
// Only `Theme` tokens; the caller owns state and reacts to the signals.
Item {
    id: root

    property string appName: "omaseed"
    property bool connected: true
    /// No server is configured at all. The dot then reads as absent — the spec's `.dot.off`,
    /// the dim foreground — instead of the green `connected: true` would otherwise draw.
    property bool serverConfigured: true
    /// The first sync has not landed yet (spec §7 Fig. 25): the mark stays accent, the dot
    /// goes yellow and the caller writes "… · first sync" into `serverLine`.
    property bool syncing: false
    /// The server is on its alternative speed limits: the glyph turns accent (`.btn.icon.on`).
    property bool altSpeedActive: false
    property string downSpeed: "—"
    // Exposed read-only so `tst_Hero.qml` can drive the inner Field directly instead of
    // simulating keystrokes.
    property alias searchField: searchField
    /// The connection dot (spec `.dot`), exposed so a test can read the colour it resolved to.
    readonly property alias statusDot: statusDot
    /// The download figure, so a test can see which side `speedsAlignment` put the pair on.
    readonly property alias downStat: downStat
    /// The `+`, so a test can see how it is drawn.
    readonly property alias addButton: addButton
    property int searchCount: 0
    /// What the field's trailing count prints. Fig. 1 shows the bare count, Fig. 24 shows
    /// "0 of 142"; the caller decides, and the plain count is the default.
    property string searchCountText: String(root.searchCount)
    property string searchText: ""
    property string serverLine: ""
    property string upSpeed: "—"

    /// "left" (beside the server name, the spec's placement) or "right" (against the search
    /// field). Anything else reads as "left".
    property string speedsAlignment: "left"

    signal addClicked
    signal altSpeedClicked
    signal helpClicked
    signal searchChanged(string text)
    signal searchCleared
    signal settingsClicked

    height: implicitHeight
    implicitHeight: heroRow.implicitHeight + Theme.space(24) + 1
    implicitWidth: Theme.space(600)
    width: implicitWidth

    RowLayout {
        id: heroRow

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.top: parent.top
        anchors.topMargin: Theme.space(12)
        spacing: Theme.space(14)

        Rectangle {
            id: mark

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: Theme.space(28)
            Layout.preferredWidth: Theme.space(28)
            border.color: (root.connected || root.syncing
                           || !root.serverConfigured) ? Theme.accent : Theme.red
            border.width: 1
            color: "transparent"

            Text {
                anchors.centerIn: parent
                color: mark.border.color
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.title
                text: Glyphs.mark
            }
        }

        Column {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space(1)

            Text {
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.subtitle
                font.weight: Font.DemiBold
                text: root.appName
            }

            Row {
                spacing: Theme.space(6)

                Rectangle {
                    id: statusDot

                    anchors.verticalCenter: parent.verticalCenter
                    color: !root.serverConfigured ? Theme.dimForeground : root.syncing ? Theme.yellow : root.connected ? Theme.green : Theme.red
                    height: Theme.space(7)
                    radius: width / 2
                    width: Theme.space(7)
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.serverLine
                    tracked: true
                }
            }
        }

        // The speed pair sits beside the server name (the spec's own placement) or against
        // the search field, per `speedsAlignment`. Exactly one of the two spacers takes the
        // slack, so the pair moves without the row being declared twice.
        Item {
            Layout.fillWidth: root.speedsAlignment === "right"
        }

        Stat {
            id: downStat

            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.space(6)
            glyph: Glyphs.arrowDown
            glyphColor: Theme.stateDownloading
            label: qsTr("download")
            value: root.downSpeed
        }

        Stat {
            Layout.alignment: Qt.AlignVCenter
            glyph: Glyphs.arrowUp
            glyphColor: Theme.stateSeeding
            label: qsTr("upload")
            value: root.upSpeed
        }

        Item {
            Layout.fillWidth: root.speedsAlignment !== "right"
        }

        Field {
            id: searchField

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Theme.space(220)
            hint: Glyphs.search
            placeholder: qsTr("filter torrents")
            text: root.searchText
            trailing: root.searchCountText

            onAccepted: root.searchChanged(searchField.text)
            onCleared: root.searchCleared()
            onTextChanged: root.searchChanged(searchField.text)
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space(2)

            IconButton {
                id: addButton

                // Fig. 1 draws `.btn.icon.on`, the bare accent glyph; beside the other icons it
                // did not read as a button. The kit's primary outline does (docs/08 §3, REVIEW §9).
                glyph: Glyphs.add
                primary: true
                tooltip: qsTr("add")

                onClicked: root.addClicked()
            }

            IconButton {
                accent: root.altSpeedActive
                glyph: Glyphs.altSpeed
                tooltip: qsTr("alt-speed")

                onClicked: root.altSpeedClicked()
            }

            IconButton {
                glyph: Glyphs.settings
                tooltip: qsTr("settings")

                onClicked: root.settingsClicked()
            }

            IconButton {
                glyph: Glyphs.help
                tooltip: qsTr("help")

                onClicked: root.helpClicked()
            }
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
