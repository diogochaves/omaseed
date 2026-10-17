import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One block's heading in the seed console (docs/22 §4): the block's name as an accent tracked
// caption, a dim one-line summary that takes the rest, then anything the block puts on the
// right (the tide's range chips, the year's legend).
RowLayout {
    id: root

    property string title: ""
    property string summary: ""
    property color summaryColor: Theme.dimForeground
    default property alias trailing: trailingRow.data

    spacing: Theme.space(12)

    // #79: the section's title is a heading, the line after it text.
    Label {
        Accessible.name: text
        Accessible.role: Accessible.Heading
        Layout.alignment: Qt.AlignBaseline
        color: Theme.accent
        objectName: "consoleHeadTitle"
        text: root.title
        tracked: true
    }

    Text {
        Accessible.name: text
        Accessible.role: Accessible.StaticText
        Layout.alignment: Qt.AlignBaseline
        objectName: "consoleHeadSummary"
        Layout.fillWidth: true
        color: root.summaryColor
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.small
        text: root.summary
        textFormat: Text.PlainText
    }

    Row {
        id: trailingRow

        Layout.alignment: Qt.AlignVCenter
        spacing: Theme.space(3)
    }
}
