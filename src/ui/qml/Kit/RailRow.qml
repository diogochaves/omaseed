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
    /// The row has a menu (DECISIONS C19 T3: a folder row's "move this folder…"): a
    /// right-click sends `menuRequested` instead of selecting it.
    property bool hasMenu: false
    /// DECISIONS C24 H2 (board 46 `.views .ri.on`): the view on show, the hover fill with an
    /// accent edge on the left, so a view and the filter it shows never look alike.
    property bool current: false
    /// The folded rail (board 46's narrow `rail()`): the glyph alone; the name goes into the
    /// tooltip the rail shows.
    property bool compact: false
    /// DECISIONS C24 H10: the row's key ("1", "`"), badged while `keysShown` (alt held), just
    /// before the count, or at the folded row's edge. "" for a row with no key of its own.
    property string keyText: ""
    property bool keysShown: false
    /// What the row sits on while it paints nothing (docs/DECISIONS.md C30 F1): the window,
    /// or the block it is in (the rail's servers, `Theme.on.idle`).
    property ThemeInk block: Theme.on.window

    signal clicked
    /// A right-click on the row, while `hasMenu`, at `x`, `y` in the row's coordinates.
    signal menuRequested(real x, real y)
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
    /// The ground under the row's words, as `color` paints it (C30 F1, F4).
    readonly property ThemeInk ground: Theme.groundIn(root.block, root.selected, false,
                                                      root.hot || root.current, false)
    /// The words' colours on that ground, eased with the fill. `countColor` and `glyphColor`
    /// are window roles; the row paints that ground's of the same (C30 F3).
    readonly property Ink ink: Ink {
        ground: root.ground
    }

    // #79: the count at the row's right ("8") is its description.
    Accessible.description: root.countText.length > 0 ? root.countText : root.count >= 0 ? String(
                                                                                               root.count) :
                                                                                           ""
    Accessible.name: root.text
    Accessible.role: Accessible.ListItem
    // #91: the row the rail (or Settings' nav) is on, as the fill shows it.
    Accessible.selectable: true
    Accessible.selected: root.selected

    // A screen reader's press is the click (#75); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

    // The ground's own fill; resting in a block, nothing: the block's is under it already.
    color: root.ground === root.block ? "transparent" : root.ground.fill
    height: Theme.railRowHeight
    implicitHeight: Theme.railRowHeight

    Behavior on color {
        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }

    // The current row's mark (the skin's SelectionMark face): Omarchy's leading rule,
    // Console's brackets (board 46 `.ri.on::after`).
    SelectionMark {
        anchors.fill: parent
        color: Theme.accent
        objectName: "railRowCurrentEdge"
        rule: true
        ruleWidth: Theme.railCurrentEdge
        visible: root.current
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
            color: root.ink.of(root.glyphColor)
            height: Theme.font.small
            pixelSize: Theme.font.small
            text: root.glyph
            width: Theme.space(14)
        }

        Text {
            id: nameText

            anchors.verticalCenter: parent.verticalCenter
            color: root.selected || root.current ? root.ink.brightForeground : root.dimText
                                                                               ? root.ink.dimForeground
                                                                               : root.ink.foreground

            elide: root.elideLeft ? Text.ElideLeft : Text.ElideRight
            // Board 46 `.ri`: a place's name in the label family, its count a value.
            font.family: Theme.labelFamily
            font.pixelSize: Theme.font.body
            text: root.text
            visible: !root.compact
            width: Math.max(0, label.width - glyphText.width - label.spacing)
        }
    }

    Text {
        id: countText

        anchors.right: parent.right
        anchors.rightMargin: Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        color: root.ink.of(root.countColor)
        font.family: Theme.valueFamily
        font.pixelSize: Theme.font.small
        objectName: "railRowCount"
        text: root.countText.length > 0 ? root.countText : Theme.numeral(root.count)
        visible: !root.compact && (root.countText.length > 0 || root.count >= 0)
    }

    KeyBadge {
        anchors.right: countText.visible ? countText.left : parent.right
        anchors.rightMargin: countText.visible ? Theme.keyBadgeRowGap : root.compact ? Theme.keyBadgeEdge
                                                                                     : Theme.space(16)
        anchors.verticalCenter: parent.verticalCenter
        corner: false
        key: root.keyText
        objectName: "railRowKey"
        shown: root.keysShown
    }

    MouseArea {
        id: mouseArea

        acceptedButtons: root.hasMenu ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                root.menuRequested(mouse.x, mouse.y);
            else
                root.clicked();
        }
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
