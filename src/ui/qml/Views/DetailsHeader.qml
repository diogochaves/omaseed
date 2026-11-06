import QtQuick
import Omaseed.Kit

// One column header of a details list (Files, Trackers, Peers), lined up with the text of the
// rows under it (docs/11 J2).
//
// `Kit/TableHeader` pads its caption by `Theme.space(8)` on both sides, because in the main
// table the cells carry that padding themselves. The details rows put their text straight at
// the cell's edge instead, so a TableHeader given the cell's own width sat 8 u to one side of
// the text below it. This cell has exactly the row cell's geometry and hangs its TableHeader
// 8 u past each edge, so the caption starts (or, `alignRight`, ends) where the row text does.
// Neighbouring headers overlap by the padding, which keeps the hairline under them unbroken.
Item {
    id: root

    property string text: ""
    property bool alignRight: false
    property bool sorted: false
    property bool descending: false
    /// False on a list that does not sort (Trackers, Peers, HTTP sources): no pointing hand.
    property bool clickable: true
    /// What a screen reader calls the column when `text` is a glyph (the Peers tab's ↓ and
    /// ↑): words, never the arrow (#78).
    property string accessibleName: root.text
    /// How wide the column must be to show its whole caption: the lists take the larger of
    /// this and their own width, so a skin's wider caption voice (upper case, tracked) gets
    /// the room it needs instead of eliding or running into the next caption.
    readonly property int fitWidth: Math.ceil(header.captionWidth)

    /// A click on the caption: the Files tab sorts by the column.
    signal clicked

    implicitHeight: header.implicitHeight
    implicitWidth: Theme.space(40)

    TableHeader {
        id: header

        Accessible.name: root.accessibleName
        alignRight: root.alignRight
        clickable: root.clickable
        descending: root.descending
        // A skin's wider caption voice (upper case, tracked) elides rather than running into
        // the next column's.
        elides: true
        height: parent.height
        sorted: root.sorted
        text: root.text
        width: parent.width + 2 * Theme.space(8)
        x: -Theme.space(8)

        onClicked: root.clicked()
    }
}
