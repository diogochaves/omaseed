import QtQuick
import Omaseed.Kit

// The torrent table's progress cell (spec v2 §2 Fig. 2 `.tbl > div.pg`): a 2 px hairline
// track, a filled bar in `color` (defaults `Theme.accent`, the caller passes a state colour
// such as `Theme.stateSeeding`), and an optional right-aligned percent. `value` is 0..1;
// the percent reads "100%" at 1 and one decimal place otherwise (e.g. "48.9%"), matching the
// spec's percent column (`.tbl .pg span`).
Item {
    id: root

    property real value: 0
    property color color: Theme.accent
    property bool showText: true

    readonly property real _clamped: Math.max(0, Math.min(1, root.value))
    readonly property string _percentText: root._clamped >= 1 ? qsTr("100%") : qsTr("%1%").arg((root._clamped
                                                                                                * 100).toFixed(
                                                                                                   1))

    height: implicitHeight
    implicitHeight: Theme.space(2)
    implicitWidth: Theme.space(120)
    width: implicitWidth

    Rectangle {
        id: track

        anchors.left: parent.left
        anchors.right: percentText.visible ? percentText.left : parent.right
        anchors.rightMargin: percentText.visible ? Theme.space(8) : 0
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.fill(Theme.hairlineAlpha)
        height: Theme.space(2)

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.top: parent.top
            color: root.color
            width: track.width * root._clamped
        }
    }

    Text {
        id: percentText

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        horizontalAlignment: Text.AlignRight
        text: root._percentText
        visible: root.showText
        width: Theme.space(40)
    }
}
