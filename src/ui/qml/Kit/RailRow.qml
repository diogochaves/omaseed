import QtQuick
import Omaseed.Kit

// One row of the borderless rail (spec v2 §1 Fig. 1 `.rail .row`): a dim glyph, the name, and
// a right-aligned tabular count. `count` of -1 hides the count text entirely (a group with no
// meaningful total). States mirror `.rail .row.sel`/`.rail .row.cur`: `selected` fills at the
// selected alpha and brightens both the name and the glyph (accent for the glyph, matching
// `.rail .row.sel .g { color:var(--accent) }`); `hasCursor` and plain hover both fill at the
// hover alpha. The row takes its width from the parent (it is anchored inside, not
// self-sizing), so only `implicitHeight` matters here. The name `Text` is given an explicit
// width (the label `Row`'s own width, itself anchored clear of the count column, minus the
// glyph column and the spacing between them) so `elide: Text.ElideRight` has something to
// elide against — a `Row` does not clip or shrink its children on its own, so without a width
// a long name would simply run into the count column instead of eliding.
Rectangle {
    id: root

    property string glyph: ""
    property string text: ""
    property int count: -1
    property bool selected: false
    property bool hasCursor: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// True once the name no longer fits its column and Text.ElideRight has cut it — for
    /// tests; the count column's own position (`countText`) is unaffected either way, since
    /// the name's width is computed clear of it rather than by clipping after the fact.
    readonly property bool truncated: nameText.truncated
    readonly property real countRight: countText.x + countText.width

    Accessible.name: root.text
    Accessible.role: Accessible.ListItem

    color: root.selected ? Theme.fill(Theme.fillSelected) : root.hot ? Theme.fill(Theme.fillHover) :
                                                                       "transparent"
    height: Theme.railRowHeight
    implicitHeight: Theme.railRowHeight

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Row {
        id: label

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16)
        anchors.right: countText.visible ? countText.left : parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(8)

        Text {
            id: glyphText

            anchors.verticalCenter: parent.verticalCenter
            color: root.selected ? Theme.accent : Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            horizontalAlignment: Text.AlignHCenter
            text: root.glyph
            width: Theme.space(14)
        }

        Text {
            id: nameText

            anchors.verticalCenter: parent.verticalCenter
            color: root.selected ? Theme.brightForeground : Theme.foreground
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.body
            text: root.text
            width: Math.max(0, label.width - glyphText.width - label.spacing)
        }
    }

    Text {
        id: countText

        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.dimForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: String(root.count)
        visible: root.count >= 0
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }
}
