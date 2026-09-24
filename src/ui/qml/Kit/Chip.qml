import QtQuick
import Omaseed.Kit

// Small pill-less rectangle for category/tag chips (spec v2 Fig. 5 `.chip`). Bordered idle
// (4 %/40 %), hover/cursor fill (8 %/25 %), selected fill (18 %) with no border, matching
// `.chip.on { background:var(--fill-selected); border-color:transparent }`.
Rectangle {
    id: root

    property string text: ""
    /// Optional glyph before the text (a `Glyphs.*` value: text or "icon:<name>").
    property string glyph: ""
    /// A state worth noticing (the hero's alt-speed while it throttles): the border and text
    /// take the warning role instead of the foreground. The fill stays foreground-based.
    property bool warn: false
    property bool selected: false
    property bool hasCursor: false
    /// Opt into the keyboard: the chip joins the tab order and `space`/`enter` picks it
    /// (spec §3 Fig. 9's auth row, where the three chips are the only way to choose). Off by
    /// default, so a chip that is only ever clicked stays out of everyone's tab order.
    property bool focusable: false

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse

    Accessible.name: root.text
    Accessible.role: Accessible.Button

    activeFocusOnTab: root.focusable && root.enabled
    border.color: root.warn ? Theme.yellow : root.hot ? Theme.fill(Theme.borderHover) : Theme.fill(
                                                            Theme.borderIdle)
    border.width: root.selected ? 0 : 1
    color: root.selected ? Theme.fill(Theme.fillSelected) : root.hot ? Theme.fill(Theme.fillHover) :
                                                                       Theme.fill(Theme.fillIdle)
    height: Theme.space(22)
    implicitWidth: content.implicitWidth + Theme.space(16)
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: 120
        }
    }

    Row {
        id: content

        anchors.centerIn: parent
        spacing: Theme.space(5)

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            color: label.color
            height: Theme.font.small
            pixelSize: Theme.font.small
            text: root.glyph
            visible: root.glyph.length > 0
            width: Theme.font.small
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            color: root.warn ? Theme.yellow : root.selected ? Theme.brightForeground :
                                                              Theme.foreground

            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.text
        }
    }

    // Keyboard focus rings the chip from outside (review Q6), selected or not.
    FocusRing {}

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: {
            if (root.focusable)
                root.forceActiveFocus();
            root.clicked();
        }
    }

    // Keypad `enter` too (KIT-7).
    Keys.onEnterPressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
