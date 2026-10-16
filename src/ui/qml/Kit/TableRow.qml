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
//
// A screen reader finds it as a row (#77): the caller names it with the row's identifying text
// (a torrent's name, a peer's address) through `Accessible.name`, never a live value; its
// state is `selected`, and its press is a plain left click, the same `clicked` signal.
Rectangle {
    id: root

    default property alias content: cells.data
    property bool selected: false
    property bool hasCursor: false

    /// `modifiers` is the click's Qt.KeyboardModifiers, for a list that selects on ctrl / shift.
    signal clicked(int modifiers)
    signal rightClicked
    signal doubleClicked

    readonly property bool both: root.selected && root.hasCursor
    readonly property color textColor: root.selected ? Theme.brightForeground : Theme.foreground

    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: root.selected
    // A screen reader's press is a plain click (#77); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked(Qt.NoModifier)

    border.color: root.hasCursor && !root.both ? Theme.fill(Theme.borderHover) : "transparent"
    border.width: root.hasCursor && !root.both ? 1 : 0
    color: root.both ? Theme.fill(Theme.fillSelectedCursor)
                     : root.selected ? Theme.fill(Theme.fillSelected)
                                     : root.hasCursor ? Theme.fill(Theme.fillHover) : "transparent"
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
        color: Theme.fill(Theme.rowRuleAlpha)
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
                root.clicked(mouse.modifiers);
        }
        onDoubleClicked: root.doubleClicked()
    }
}
