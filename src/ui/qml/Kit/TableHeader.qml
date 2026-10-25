import QtQuick
import Omaseed.Kit

// One header cell of the torrent table (spec v2 §2 Fig. 2 `.tbl > div.h`): caption size, dim,
// 0.04em letter-spacing, a hairline bottom border. `alignRight` matches the numeric columns
// (`.tbl .num`); `sorted` brightens the label to `foreground` and shows a `▾`/`▴` glyph,
// direction picked by `descending` (`.tbl > div.h.sort`). The row takes its width from the
// parent; only `implicitHeight` (2 × `font.body`, the spec's `calc(var(--base)*2)`) matters
// here. `dragging` is set on every cell while a column is being dragged along the header
// (N2, M13): the pointer then shows a closed hand wherever it is over the header, instead of
// the pointing hand that says "click to sort". `clickable: false` is a caption that does
// nothing on a click (the details lists that do not sort): the plain arrow, so it does not
// promise one. With `rightClickable`, a right-click is `rightClicked` (the torrent table opens
// its columns palette, #51 D6); without it the right button passes through, as before.
Item {
    id: root

    property string text: ""
    property bool alignRight: false
    property bool sorted: false
    property bool descending: false
    /// A column is being dragged along the header.
    property bool dragging: false
    /// A click does something (sorts): the pointing hand. False keeps the arrow.
    property bool clickable: true
    /// The cell takes the right button too, as `rightClicked`.
    property bool rightClickable: false
    /// The caption elides to the cell's width instead of running past it: the torrent table,
    /// whose columns can be dragged narrower than their captions (#52).
    property bool elides: false
    /// The ground the caption is on (a dragged column's selected fill, docs/DECISIONS.md C30
    /// F1); null is the window.
    property Ink ink: null
    /// How wide the cell must be to show its whole caption and a sort chevron (#52's fit).
    readonly property real fitWidth: 2 * Theme.space(8) + caption.implicitWidth + Theme.space(4)
                                     + chevron.implicitWidth
    /// The caption's whole width in the skin's caption voice, padding left out: a column at
    /// least this wide never elides it (the details lists size their fixed columns by it).
    readonly property real captionWidth: caption.implicitWidth

    signal clicked
    signal rightClicked

    readonly property int _pixelSize: Theme.font.caption

    Accessible.name: root.text
    Accessible.role: Accessible.ColumnHeader

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
            id: caption

            anchors.verticalCenter: parent.verticalCenter
            color: root.ink !== null ? (root.sorted ? root.ink.foreground : root.ink.dimForeground)
                                     : (root.sorted ? Theme.foreground : Theme.dimForeground)
            elide: root.elides ? Text.ElideRight : Text.ElideNone
            // Board 46 `.tbl th`: the skin's caption voice.
            font.capitalization: Theme.captionUpper ? Font.AllUppercase : Font.MixedCase
            font.family: Theme.labelFamily
            font.letterSpacing: root._pixelSize * Theme.captionTrack(0.04)
            font.pixelSize: root._pixelSize
            font.weight: Theme.captionWeight(Font.Normal)
            objectName: "caption"
            text: root.text
            width: root.elides ? Math.max(0, Math.min(caption.implicitWidth, root.width - 2
                                                      * Theme.space(8) - (root.sorted
                                                                          ? chevron.implicitWidth
                                                                            + Theme.space(4) : 0)))
                               : caption.implicitWidth
        }

        Text {
            id: chevron

            anchors.verticalCenter: parent.verticalCenter
            color: root.ink !== null ? root.ink.foreground : Theme.foreground
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

        acceptedButtons: root.rightClickable ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
        anchors.fill: parent
        cursorShape: root.dragging ? Qt.ClosedHandCursor : root.clickable ? Qt.PointingHandCursor
                                                                          : Qt.ArrowCursor
        hoverEnabled: true
        objectName: "headerMouse"

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                root.rightClicked();
            else
                root.clicked();
        }
    }
}
