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
    /// Cut the start of a name that does not fit rather than its end (a tracker host, whose
    /// end is the part that tells two hosts apart).
    property bool elideLeft: false
    /// docs/33 R2: what the count column prints instead of `count` ("down", "!"); "" prints
    /// `count`. Its colour, and the glyph's, can be a state's (a server's dot).
    property string countText: ""
    property color countColor: Theme.dimForeground
    property color glyphColor: root.selected ? Theme.accent : Theme.dimForeground
    /// Board 42 F1 (DECISIONS C15 M2): a folder below another sits `indent` steps in.
    property int indent: 0
    /// A drive's top in the folders group (M2): the name dim, as a heading.
    property bool dimText: false
    /// The glyph is a fold switch of its own (a folder's ▾ / ▸): a click on it sends
    /// `glyphClicked`, not `clicked`.
    property bool glyphClickable: false

    signal clicked
    /// The glyph was clicked, while `glyphClickable`.
    signal glyphClicked
    /// The pointer came onto the row (true) or left it (false).
    signal hoverChanged(bool hovering)

    readonly property bool hovered: mouseArea.containsMouse

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// True once the name no longer fits its column and Text.ElideRight has cut it — for
    /// tests; the count column's own position (`countText`) is unaffected either way, since
    /// the name's width is computed clear of it rather than by clipping after the fact.
    readonly property bool truncated: nameText.truncated
    readonly property real countRight: countText.x + countText.width

    // #79: the count at the row's right ("8") is its description.
    Accessible.description: root.countText.length > 0 ? root.countText : root.count >= 0 ? String(
                                                                                               root.count) :
                                                                                           ""
    Accessible.name: root.text
    Accessible.role: Accessible.ListItem

    // A screen reader's press is the click (#75); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

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

        objectName: "railRowLabel"
        anchors.left: parent.left
        anchors.leftMargin: Theme.space(16) + root.indent * Theme.space(12)
        anchors.right: countText.visible ? countText.left : parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(8)

        Glyph {
            id: glyphText

            anchors.verticalCenter: parent.verticalCenter
            color: root.glyphColor
            height: Theme.font.small
            pixelSize: Theme.font.small
            text: root.glyph
            width: Theme.space(14)
        }

        Text {
            id: nameText

            anchors.verticalCenter: parent.verticalCenter
            color: root.selected ? Theme.brightForeground : root.dimText ? Theme.dimForeground :
                                                                           Theme.foreground

            elide: root.elideLeft ? Text.ElideLeft : Text.ElideRight
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
        color: root.countColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        objectName: "railRowCount"
        text: root.countText.length > 0 ? root.countText : String(root.count)
        visible: root.countText.length > 0 || root.count >= 0
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
        onContainsMouseChanged: root.hoverChanged(mouseArea.containsMouse)
    }

    MouseArea {
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        cursorShape: Qt.PointingHandCursor
        enabled: root.glyphClickable
        objectName: "railRowGlyphArea"
        visible: root.glyphClickable
        width: Theme.space(8) + glyphText.width + Theme.space(4)
        x: label.x - Theme.space(8)

        onClicked: root.glyphClicked()
    }
}
