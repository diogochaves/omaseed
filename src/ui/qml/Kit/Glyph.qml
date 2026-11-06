import QtQuick
import Omaseed.Kit

// One glyph slot: text for a typographic symbol, `Icon` for a name written "icon:<name>", and
// the skin's icon font for one written "ms:<ligature>" (see `Glyphs.qml`, #171). Components
// that draw a caller-supplied glyph use this instead of a bare `Text`, so a glyph can move
// from the font to an SVG icon or a skin's icon font without its callers changing.
Item {
    id: root

    property string text: ""
    property color color: Theme.foreground
    property int pixelSize: Theme.font.body
    property int horizontalAlignment: Text.AlignHCenter

    readonly property bool isIcon: root.text.startsWith("icon:")
    readonly property string iconName: root.isIcon ? root.text.slice(5) : ""
    /// An icon-font ligature: Glyphs.qml hands one out only while `Theme.iconsFamily` is
    /// installed, so it never draws its name in the text font.
    readonly property bool isLigature: root.text.startsWith("ms:")

    implicitHeight: root.isIcon ? root.pixelSize : label.implicitHeight
    implicitWidth: root.isIcon ? root.pixelSize : label.implicitWidth

    Text {
        id: label

        anchors.fill: parent
        color: root.color
        font.family: root.isLigature ? Theme.iconsFamily : Theme.fontFamily
        font.pixelSize: root.pixelSize
        horizontalAlignment: root.horizontalAlignment
        text: root.isIcon ? "" : root.isLigature ? root.text.slice(3) : root.text
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
