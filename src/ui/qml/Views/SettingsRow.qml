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

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.leftMargin: Theme.space(12)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(12)

        Glyph {
            Layout.preferredHeight: Theme.font.small
            Layout.preferredWidth: Theme.space(16)
            color: Theme.dimForeground
            pixelSize: Theme.font.small
            text: root.glyph
            visible: root.glyph.length > 0
        }

        ColumnLayout {
            Layout.fillWidth: true
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

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: childrenRect.height
            Layout.preferredWidth: childrenRect.width
        }
    }
}
