import QtQuick
import Omaseed.Kit

// One headline number with a leading glyph, tracked label beneath (spec v2 §1 Fig. 1
// `.hero .stat`: "↓4.21 MB/s" over "download"). `glyphColor` defaults to `Theme.foreground`;
// `Hero` overrides it per stat to `Theme.stateDownloading`/`Theme.stateSeeding`.
Column {
    id: root

    property string glyph: ""
    property color glyphColor: Theme.foreground
    property string label: ""
    property string value: ""

    spacing: 0

    Row {
        spacing: Theme.space(4)

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: root.glyphColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: root.glyph
            visible: root.glyph.length > 0
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.heading
            font.weight: Font.Medium
            text: root.value
        }
    }

    Label {
        text: root.label
        tracked: true
        visible: root.label.length > 0
    }
}
