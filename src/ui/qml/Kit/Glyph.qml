import QtQuick
import Omaseed.Kit

// One glyph slot: text for a typographic symbol, `Icon` for a name written "icon:<name>"
// (see `Glyphs.qml`). Components that draw a caller-supplied glyph use this instead of a bare
// `Text`, so a glyph can move from the font to an SVG icon without its callers changing.
Item {
    id: root

    property string text: ""
    property color color: Theme.foreground
    property int pixelSize: Theme.font.body
    property int horizontalAlignment: Text.AlignHCenter

    readonly property bool isIcon: root.text.startsWith("icon:")
    readonly property string iconName: root.isIcon ? root.text.slice(5) : ""

    implicitHeight: root.isIcon ? root.pixelSize : label.implicitHeight
    implicitWidth: root.isIcon ? root.pixelSize : label.implicitWidth

    Text {
        id: label

        anchors.fill: parent
        color: root.color
        font.family: Theme.fontFamily
        font.pixelSize: root.pixelSize
        horizontalAlignment: root.horizontalAlignment
        text: root.isIcon ? "" : root.text
        verticalAlignment: Text.AlignVCenter
        visible: !root.isIcon
    }

    Icon {
        anchors.centerIn: parent
        color: root.color
        height: root.pixelSize
        name: root.iconName
        visible: root.isIcon
        width: root.pixelSize
    }
}
