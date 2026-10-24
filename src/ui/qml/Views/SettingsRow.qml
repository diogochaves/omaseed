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
//
// `warning` (docs/32 E1, #55) is a fault in what the row sets, found elsewhere (qBittorrent's
// log): a yellow line under the text and the control, behind a 1 px yellow rule.
Rectangle {
    id: root

    property string glyph: ""
    property string title: ""
    property string description: ""
    property string warning: ""
    property bool hasCursor: false

    /// The control at the right of the row.
    default property alias control: controlHolder.data

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// The row's words on its ground, the hover fill while it is hot, eased with the fill
    /// (docs/DECISIONS.md C30 F1, F4).
    readonly property Ink ink: Ink {
        ground: Theme.ground(false, false, root.hot, false)
    }
    /// docs/15 Q3 A: a row under about 380 px (base 14) has no room for the text beside the
    /// control, and the description broke one word per line; the control moves under the
    /// text instead, lined up with its left edge.
    readonly property bool stacked: root.width > 0 && root.width < Theme.space(326)

    // #77: a group named by its title around its control, so the switch or field a screen
    // reader lands on says what it is for. The row's click does what its control does, and
    // the control takes the screen reader's press, so the row has no press of its own.
    Accessible.description: root.description
    Accessible.name: root.title
    Accessible.role: Accessible.Grouping

    border.color: root.hasCursor ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
    border.width: 1
    color: root.hot ? root.ink.fill : "transparent"
    implicitHeight: Math.max(Theme.space(40), layout.implicitHeight + Theme.space(16))
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: Theme.fillChangeDuration
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
            color: root.ink.dimForeground
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
                id: titleText

                Layout.fillWidth: true
                bright: true
                elide: Text.ElideRight
                font.weight: Font.Medium
                ink: root.ink
                objectName: "settingsRowTitle"
                text: root.title

                HoverHandler {
                    id: titleHover
                }

                // The kit's one tooltip, as KeyValue's: the full text
                // while it is cut off and the pointer is on it.
                Tooltip {
                    id: titleTip

                    readonly property bool wanted: titleText.truncated && titleHover.hovered

                    objectName: "settingsRowTip"
                    parent: titleText.Window.window ? titleText.Window.window.contentItem : titleText
                    z: 1000

                    onWantedChanged: titleTip.wanted ? titleTip.showFor(titleText, titleText.text, false)
                                                    : titleTip.hide()
                }
            }

            Label {
                Layout.fillWidth: true
                dim: true
                ink: root.ink
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

        Item {
            Layout.column: 1
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.preferredHeight: warningText.implicitHeight
            Layout.row: 2
            objectName: "settingsRowWarning"
            visible: root.warning.length > 0

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.top: parent.top
                color: root.ink.yellow
                width: 1
            }

            Label {
                id: warningText

                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.right: parent.right
                color: root.ink.yellow
                size: "small"
                text: root.warning
                wrapMode: Text.Wrap
            }
        }
    }
}
