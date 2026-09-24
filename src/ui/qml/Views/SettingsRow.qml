import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One behaviour row of the settings modal (spec v2 §5 Fig. 16 `.trow`): a dim glyph, a
// bright title over a dim description, and the caller's control pushed to the right. The
// whole row is outlined at the hairline alpha, and `hasCursor` fills it the way every other
// cursor in the app does. The description wraps rather than eliding, so a caption that has
// to say something (Q16's disk row) is read in full and the row grows to fit it.
//
// The control is the default content, so a row reads like the figure's markup:
//
//     SettingsRow {
//         glyph: Glyphs.mark; title: "…"; description: "…"
//         Toggle { checked: … }
//     }
Rectangle {
    id: root

    property string glyph: ""
    property string title: ""
    property string description: ""
    property bool hasCursor: false

    /// The control at the right of the row.
    default property alias control: controlHolder.data

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// docs/15 Q3 A: a row under about 380 px (base 14) has no room for the text beside the
    /// control, and the description broke one word per line; the control moves under the
    /// text instead, lined up with its left edge.
    readonly property bool stacked: root.width > 0 && root.width < Theme.space(326)

    border.color: root.hasCursor ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
    border.width: 1
    color: root.hot ? Theme.fill(Theme.fillHover) : "transparent"
    implicitHeight: Math.max(Theme.space(40), layout.implicitHeight + Theme.space(16))
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: root.clicked()
    }

    // A grid rather than a row so the control can move under the text (`stacked`) without
    // being re-parented: it changes cell, from the row's end to under the description.
    GridLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        columnSpacing: Theme.space(12)
        columns: 3
        rowSpacing: Theme.space(8)

        Glyph {
            Layout.column: 0
            Layout.row: 0
            Layout.preferredHeight: Theme.font.small
            Layout.preferredWidth: Theme.space(16)
            color: Theme.dimForeground
            pixelSize: Theme.font.small
            text: root.glyph
            visible: root.glyph.length > 0
        }

        ColumnLayout {
            Layout.column: 1
            Layout.fillWidth: true
            Layout.row: 0
            objectName: "settingsRowText"
            spacing: Theme.space(1)

            Label {
                Layout.fillWidth: true
                bright: true
                elide: Text.ElideRight
                font.weight: Font.Medium
                text: root.title
            }

            Label {
                Layout.fillWidth: true
                dim: true
                size: "small"
                text: root.description
                visible: root.description.length > 0
                wrapMode: Text.Wrap
            }
        }

        Item {
            id: controlHolder

            Layout.alignment: root.stacked ? Qt.AlignLeft | Qt.AlignVCenter : Qt.AlignVCenter
            Layout.column: root.stacked ? 1 : 2
            Layout.row: root.stacked ? 1 : 0
            objectName: "settingsRowControl"
            Layout.preferredHeight: childrenRect.height
            Layout.preferredWidth: childrenRect.width
        }
    }
}
