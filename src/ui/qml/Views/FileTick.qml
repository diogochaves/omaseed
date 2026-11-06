import QtQuick
import Omaseed.Kit

// One cell of a file tree's tick column (docs/27, #39 #42): the add sheet's tree and the
// details Files tab draw the same one, and so do both trees' header rows, where it stands for
// every file. `✓` nothing under it skipped, `–` some, `·` all (the palettes' marks). A click
// asks the owner to tick or skip; the cell changes nothing itself.
//
// A Tooltip passed in `tooltip` shows `tip` under the cell while the pointer rests on it (the
// head's "tick every file · ctrl+a").
//
//     FileTick { wanted: row.wanted; onClicked: tree.tick(row.index) }
Item {
    id: root

    /// 2 nothing skipped, 1 some, 0 all.
    property int wanted: 2
    property string tip: ""
    property Tooltip tooltip: null
    /// The row the cell sits on (its TableRow's `ink`, docs/DECISIONS.md C30 F1); null is the
    /// window.
    property Ink ink: null

    signal clicked

    readonly property bool hovered: mouse.containsMouse
    /// The mark's colours: resting, the row's; under the pointer, on the cell's own hover fill:
    /// over the window the hover ground, over a 4 % block the hover in a block, over a filled
    /// row its fill (no ground of its own: the row's bright foreground).
    readonly property ThemeInk _rest: root.ink !== null ? root.ink.ground : Theme.on.window
    readonly property color _foreground: root.ink !== null ? root.ink.foreground : Theme.foreground
    readonly property color _dim: root.ink !== null ? root.ink.dimForeground : Theme.dimForeground
    readonly property color _hot: {
        if (root._rest === Theme.on.window)
            return Theme.on.hover.brightForeground;
        return root._rest === Theme.on.idle ? Theme.on.blockHover.brightForeground
                                            : root.ink.brightForeground;
    }

    // #77: a screen reader finds a check box, "download", checked when nothing under it is
    // skipped and mixed when some is; the tip (what a click does, and its key) is the
    // description. Its toggle and its press are both the click; disabled, neither does
    // anything, as the pointer.
    Accessible.checkStateMixed: root.wanted === 1
    Accessible.checkable: true
    Accessible.checked: root.wanted === 2
    Accessible.description: root.tip
    Accessible.name: qsTr("download")
    Accessible.role: Accessible.CheckBox
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()
    Accessible.onToggleAction: if (root.enabled)
                                   root.clicked()

    implicitHeight: Theme.railRowHeight
    implicitWidth: Theme.space(14)

    // A click under the pointer changes what the next click does: the open tip follows.
    onTipChanged: {
        if (mouse.containsMouse && root.tooltip !== null)
            root.tooltip.showFor(root, root.tip);
    }

    Rectangle {
        anchors.centerIn: parent
        color: mouse.containsMouse ? Theme.on.hover.fill : "transparent"
        height: Theme.railRowHeight - Theme.space(6)
        radius: Theme.cornerRadius
        width: root.width + Theme.space(6)
    }

    Text {
        anchors.centerIn: parent
        color: mouse.containsMouse ? root._hot : root.wanted === 0 ? root._dim : root._foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "fileTickGlyph"
        text: root.wanted === 2 ? Glyphs.check : root.wanted === 1 ? "–" : Glyphs.middot
    }

    MouseArea {
        id: mouse

        anchors.centerIn: parent
        cursorShape: Qt.PointingHandCursor
        height: Theme.railRowHeight
        hoverEnabled: true
        objectName: "fileTickMouse"
        width: root.width + Theme.space(6)

        // Two quick clicks are two ticks, not a row's double click.
        onClicked: root.clicked()
        onDoubleClicked: root.clicked()
        onContainsMouseChanged: {
            if (root.tooltip === null || root.tip.length === 0)
                return;
            if (mouse.containsMouse)
                root.tooltip.showFor(root, root.tip);
            else
                root.tooltip.hide();
        }
    }
}
