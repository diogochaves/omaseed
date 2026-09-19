import QtQuick
import Omaseed.Kit

// The row band of the torrent table (spec v2 §2 Fig. 2 `.tbl .r`): background, hairline and
// cursor/selection chrome only — the column cells themselves are the caller's content, added
// through the default `content` alias (`TorrentTable`, T3.3, puts a `Row`/`RowLayout` of cells
// there). `selected` fills at the selected alpha and switches `textColor` to
// `brightForeground` (`.tbl .r.sel > div`); `hasCursor` fills at the hover alpha and draws a
// 1 px inset border at `Theme.borderHover` on every side, since this row is one band rather
// than the spec's per-`div` box-shadow trick (`.tbl .r.cur > div`); both together use a single
// 24 % fill (`.tbl .r.sel.cur > div`) instead of stacking the two.
Rectangle {
    id: root

    default property alias content: cells.data
    property bool selected: false
    property bool hasCursor: false

    signal clicked
    signal rightClicked
    signal doubleClicked

    // The spec derives this from `color-mix(in srgb, var(--fg) 5%, transparent)`; it has no
    // named Theme role of its own, so it is kept here as the one place that alpha is spelled
    // out.
    readonly property real rowHairlineAlpha: 0.05
    readonly property bool both: root.selected && root.hasCursor
    readonly property color textColor: root.selected ? Theme.brightForeground : Theme.foreground

    border.color: root.hasCursor && !root.both ? Theme.fill(Theme.borderHover) : "transparent"
    border.width: root.hasCursor && !root.both ? 1 : 0
    color: root.both ? Theme.fill(0.24) : root.selected ? Theme.fill(Theme.fillSelected) :
                                                          root.hasCursor ? Theme.fill(
                                                                               Theme.fillHover) :
                                                                           "transparent"
    height: Theme.rowHeight
    implicitHeight: Theme.rowHeight

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Item {
        id: cells

        anchors.fill: parent
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: Theme.fill(root.rowHairlineAlpha)
        height: 1
    }

    MouseArea {
        id: mouseArea

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                root.rightClicked();
            else
                root.clicked();
        }
        onDoubleClicked: root.doubleClicked()
    }
}
