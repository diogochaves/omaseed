import QtQuick
import Omaseed.Kit

// A group's head in the rows display (board 46 `.gh`, DECISIONS C24 H3): the question in small
// tracked capitals, its count in the list beside it, and a short line at the right
// (ScopeSentence::groupLine(), "next ready in 3 min"). Needs you is red, as the rail's row is.
// Properties only; its form is `Theme.libraryGroup*`.
Item {
    id: root

    property string title: ""
    property int count: 0
    property string line: ""
    property bool needsYou: false

    // #77: a heading for a screen reader, its count and line in the name.
    Accessible.name: [root.title, String(root.count), root.line].filter(part => part.length > 0).join(
                         " · ")
    Accessible.role: Accessible.Heading
    height: Theme.libraryGroupHeadHeight
    implicitHeight: Theme.libraryGroupHeadHeight

    Row {
        id: words

        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.libraryGroupHeadPaddingBottom
        anchors.left: parent.left
        anchors.leftMargin: Theme.libraryGroupHeadInset
        spacing: Theme.libraryRowGap

        Text {
            id: titleText

            color: root.needsYou ? Theme.red : Theme.dimForeground
            font.capitalization: Font.AllUppercase
            font.family: Theme.fontFamily
            font.letterSpacing: Theme.libraryGroupHeadTracking
            font.pixelSize: Theme.libraryGroupHeadSize
            objectName: "groupTitle"
            text: root.title
            textFormat: Text.PlainText
        }

        Text {
            anchors.baseline: titleText.baseline
            color: root.needsYou ? Theme.red : Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.libraryGroupHeadSize
            objectName: "groupCount"
            text: String(root.count)
            textFormat: Text.PlainText
        }
    }

    Text {
        id: lineText

        anchors.left: words.right
        anchors.leftMargin: Theme.libraryRowGap
        anchors.right: parent.right
        anchors.rightMargin: Theme.libraryRowInsetEnd
        color: Theme.dimForeground
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.libraryGroupLineSize
        horizontalAlignment: Text.AlignRight
        objectName: "groupLine"
        text: root.line
        textFormat: Text.PlainText
        // On the title's baseline.
        y: words.y + titleText.baselineOffset - lineText.baselineOffset
    }
}
