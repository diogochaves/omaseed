import QtQuick
import Omaseed.Kit

// The torrent table's progress cell (spec v2 §2 Fig. 2 `.tbl > div.pg`): a 2 px hairline
// track, a filled bar in `color` (defaults `Theme.accent`, the caller passes a state colour
// such as `Theme.stateSeeding`), and an optional right-aligned percent. `value` is 0..1; the
// percent reads "100%" only when complete and one decimal place, truncated, below it (e.g.
// "48.9%"), as the spec's percent column does (`.tbl .pg span`).
Item {
    id: root

    property real value: 0
    property color color: Theme.accent
    property bool showText: true

    readonly property real _clamped: Math.max(0, Math.min(1, root.value))
    // The rule of `Format::percentCompact` (review REG-3): truncated, as qBittorrent does, so
    // an unfinished torrent never reads "100%"; 0.9996 is "99.9%". Complete is "100%", never
    // "100.0%" (KIT-15). The epsilon keeps 0.489 * 1000 (488.99999…) at 48.9.
    readonly property real _tenths: Math.min(99.9, Math.floor(root._clamped * 1000 + 1e-6) / 10)
    readonly property string _percentText: root._clamped >= 1 ? qsTr("100%")
                                                              : qsTr("%1%").arg(root._tenths.toFixed(1))

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
