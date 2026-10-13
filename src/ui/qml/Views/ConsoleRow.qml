import QtQuick
import QtQuick.Layouts
import Omaseed.Kit

// One row of a console list (keeping alive, earning, holding, on air, lately): an optional
// glyph, the name (elided), an optional bar, and up to two right-hand notes. A click asks for
// the torrent in the library.
Item {
    id: root

    property string glyph: ""
    property color glyphColor: Theme.stateSeeding
    property string name: ""
    property string note: ""
    property string trailing: ""
    property color noteColor: Theme.dimForeground
    property color trailingColor: Theme.dimForeground
    /// 0..1 draws a bar before the notes; below 0 draws none.
    property real fraction: -1
    property color barColor: Theme.stateSeeding
    property real barWidth: Theme.space(96)
    property real noteWidth: Theme.space(84)
    property real trailingWidth: Theme.space(84)
    property bool current: false
    property bool clickable: true

    signal clicked

    implicitHeight: Theme.space(30)

    Rectangle {
        anchors.fill: parent
        border.color: root.current ? Theme.fill(0.25) : "transparent"
        border.width: 1
        color: hover.hovered && root.clickable ? Theme.fill(Theme.fillHover)
                                               : root.current ? Theme.fill(Theme.fillHover) : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space(8)
        anchors.rightMargin: Theme.space(8)
        spacing: Theme.space(12)

        Text {
            Layout.preferredWidth: Theme.space(14)
            color: root.glyphColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            horizontalAlignment: Text.AlignHCenter
            text: root.glyph
            visible: root.glyph.length > 0
        }

        Text {
            id: nameText

            Layout.fillWidth: true
            color: Theme.foreground
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "consoleRowName"
            text: root.name
            textFormat: Text.PlainText

            HoverHandler {
                id: nameHover
            }

            // The kit's one tooltip, as KeyValue's: the full text
            // while it is cut off and the pointer is on it.
            Tooltip {
                id: nameTip

                readonly property bool wanted: nameText.truncated && nameHover.hovered

                objectName: "consoleRowTip"
                parent: nameText.Window.window ? nameText.Window.window.contentItem : nameText
                z: 1000

                onWantedChanged: nameTip.wanted ? nameTip.showFor(nameText, nameText.text, false)
                                               : nameTip.hide()
            }
        }

        Rectangle {
            Layout.preferredHeight: Theme.space(4)
            Layout.preferredWidth: root.barWidth
            color: Theme.fill(Theme.fillHover)
            visible: root.fraction >= 0

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.top: parent.top
                color: root.barColor
                width: parent.width * Math.max(0, Math.min(1, root.fraction))

                Behavior on width {
                    NumberAnimation {
                        duration: 600
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        Text {
            Layout.preferredWidth: root.noteWidth
            color: root.noteColor
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            horizontalAlignment: Text.AlignRight
            text: root.note
            visible: root.note.length > 0
        }

        Text {
            Layout.preferredWidth: root.trailingWidth
            color: root.trailingColor
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            horizontalAlignment: Text.AlignRight
            text: root.trailing
            visible: root.trailing.length > 0
        }
    }

    HoverHandler {
        id: hover

        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    TapHandler {
        enabled: root.clickable
        onTapped: root.clicked()
    }
}
