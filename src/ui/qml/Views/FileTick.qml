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

    signal clicked

    readonly property bool hovered: mouse.containsMouse

    implicitHeight: Theme.railRowHeight
    implicitWidth: Theme.space(14)

    // A click under the pointer changes what the next click does: the open tip follows.
    onTipChanged: {
        if (mouse.containsMouse && root.tooltip !== null)
            root.tooltip.showFor(root, root.tip);
    }

    Rectangle {
        anchors.centerIn: parent
        color: mouse.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
        height: Theme.railRowHeight - Theme.space(6)
        radius: Theme.cornerRadius
        width: root.width + Theme.space(6)
    }

    Text {
        anchors.centerIn: parent
        color: mouse.containsMouse ? Theme.brightForeground : root.wanted === 0 ? Theme.dimForeground
                                                                                : Theme.foreground
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
