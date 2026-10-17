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
    /// What a screen reader calls the number when the label is cut or gone (the hero's
    /// narrow steps drop it): words, never the glyph.
    property string accessibleName: root.label

    // #79: a label named by what it counts ("download"), the number its description: a speed
    // changes every second and is not read out again each time.
    Accessible.description: root.value
    Accessible.name: root.accessibleName
    Accessible.role: Accessible.StaticText
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
