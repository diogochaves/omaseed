import QtQuick
import Omaseed.Kit

// One header cell of the torrent table (spec v2 §2 Fig. 2 `.tbl > div.h`): caption size, dim,
// 0.04em letter-spacing, a hairline bottom border. `alignRight` matches the numeric columns
// (`.tbl .num`); `sorted` brightens the label to `foreground` and shows a `▾`/`▴` glyph,
// direction picked by `descending` (`.tbl > div.h.sort`). The row takes its width from the
// parent; only `implicitHeight` (2 × `font.body`, the spec's `calc(var(--base)*2)`) matters
// here.
Item {
    id: root

    property string text: ""
    property bool alignRight: false
    property bool sorted: false
    property bool descending: false

    signal clicked

    readonly property int _pixelSize: Theme.font.caption

    height: root.implicitHeight
    implicitHeight: Theme.font.body * 2

    // T3.3: the row is anchored to one side rather than stretched with a reversed
    // `layoutDirection`. Reversing it also reversed the two children, so a sorted numeric
    // header read `▾ ↓` where the spec's Fig. 1 header reads `↓ ▾`.
    Row {
        anchors.left: root.alignRight ? undefined : parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.right: root.alignRight ? parent.right : undefined
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(4)

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: root.sorted ? Theme.foreground : Theme.dimForeground
            font.family: Theme.fontFamily
            font.letterSpacing: root._pixelSize * 0.04
            font.pixelSize: root._pixelSize
            text: root.text
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: root._pixelSize
            text: root.descending ? Glyphs.chevronDown : Glyphs.chevronUp
            visible: root.sorted
        }
    }

    Hairline {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }
}
