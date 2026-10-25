import QtQuick
import Omaseed.Kit

// The row band of the torrent table (spec v2 §2 Fig. 2 `.tbl .r`): background, hairline and
// cursor/selection chrome only — the column cells themselves are the caller's content, added
// through the default `content` alias (`TorrentTable`, T3.3, puts a `Row`/`RowLayout` of cells
// there). `selected` fills at the selected alpha and switches `textColor` to
// `brightForeground` (`.tbl .r.sel > div`); `hasCursor` fills at the hover alpha and draws a
// 1 px inset border at `Theme.borderHover` on every side, since this row is one band rather
// than the spec's per-`div` box-shadow trick (`.tbl .r.cur > div`); both together use a single
// 24 % fill (`.tbl .r.sel.cur > div`) instead of stacking the two. `hovered` (the caller's
// pointer, `.tbl .r:hover`) fills at the hover alpha.
//
// A skin may mark the cursor instead (#171 step 6, `Theme.cursorMarked`: board 46 `.row.sel`,
// Console's brackets through `SelectionMark`) and show a pick by the `picked` ground alone
// (`Theme.rowGround`, `.row.picked`); Omarchy's face draws no mark, so there it is as above.
//
// Every word in a cell takes its colour from `ink` (docs/DECISIONS.md C30 F1–F4): the text
// roles on the ground this row paints, eased with its fill. A cell's caption is
// `band.ink.dimForeground`, its state colour `band.ink[stateRole]`, never `Theme.*`.
//
// A screen reader finds it as a row (#77): the caller names it with the row's identifying text
// (a torrent's name, a peer's address) through `Accessible.name`, never a live value; its
// state is `selected`, and its press is a plain left click, the same `clicked` signal.
Rectangle {
    id: root

    default property alias content: cells.data
    property bool selected: false
    property bool hasCursor: false
    /// #201 F16: the list this row is in has the keys. Without them the cursor keeps its fill
    /// and drops its edge (the hairline, or the skin's mark), so h / l show where they went.
    property bool keysHere: true
    property bool hovered: false

    /// `modifiers` is the click's Qt.KeyboardModifiers, for a list that selects on ctrl / shift.
    signal clicked(int modifiers)
    signal rightClicked
    signal doubleClicked

    readonly property bool both: root.selected && root.hasCursor
    /// The cursor's hairline, where the skin doesn't mark the cursor and the keys are here.
    readonly property bool _edged: root.hasCursor && root.keysHere && !root.both && !Theme.cursorMarked
    /// The text roles on this row's ground (C30 F7).
    readonly property Ink ink: Ink {
        ground: Theme.rowGround(root.selected, root.hasCursor, root.hovered, false)
    }
    readonly property color textColor: root.selected ? root.ink.brightForeground : root.ink.foreground

    Accessible.role: Accessible.ListItem
    Accessible.selectable: true
    Accessible.selected: root.selected
    // A screen reader's press is a plain click (#77); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked(Qt.NoModifier)

    border.color: root._edged ? Theme.fill(Theme.borderHover) : "transparent"
    border.width: root._edged ? Theme.hairWidth : 0
    color: root.ink.ground.fill
    height: Theme.rowHeight
    implicitHeight: Theme.rowHeight

    Behavior on color {
        ColorAnimation {
            duration: Theme.fillChangeDuration
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

    // The cursor's mark: nothing in Omarchy's face, Console's brackets.
    SelectionMark {
        anchors.fill: parent
        color: Theme.accent
        objectName: "tableRowCursor"
        visible: root.hasCursor && root.keysHere
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
