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
    /// The role `warn` draws in: the warning colour, or the hero's burst green (docs/31 D3).
    property color warnColor: Theme.yellow
    /// Words after the text in the same colour, a little fainter ("· until 07:00", docs/31
    /// D5); "" draws none.
    property string suffix: ""
    property bool selected: false
    property bool hasCursor: false
    /// A switch rather than a label (docs/32 L1's level chips): off, its text reads dim.
    property bool toggle: false
    /// A key drawn faint before the text (the level chips' `1`–`4`); "" draws none.
    property string keyText: ""
    /// A number drawn dim after the text (a level's lines); "" draws none.
    property string count: ""
    /// The text's colour while selected, a Theme role (a level's: warning `Theme.yellow`).
    /// Left transparent, a selected chip's text is the bright foreground.
    property color tone: "transparent"
    /// An accent edge, for a filter that narrows what is shown (the log's torrent chip).
    property bool accent: false
    /// Opt into the keyboard: the chip joins the tab order and `space`/`enter` picks it
    /// (spec §3 Fig. 9's auth row, where the three chips are the only way to choose). Off by
    /// default, so a chip that is only ever clicked stays out of everyone's tab order.
    property bool focusable: false
    /// Shown under the chip on hover or keyboard focus, as IconButton's (the hero's alt-speed,
    /// which is its glyph alone while off). "" shows nothing.
    property string tooltip: ""
    /// The chip's tooltip, which lives on the window's content item (the tests read it).
    readonly property alias tooltipItem: tip

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse

    // #79: the count it shows ("19") and its key are the description, as a rail row's count.
    Accessible.description: [root.count, root.keyText.length > 0 ? qsTr("shortcut %1").arg(root.keyText) : ""].filter(
                                part => part.length > 0).join(" · ")
    Accessible.name: root.suffix.length > 0 ? root.text + " " + root.suffix : root.text
    Accessible.role: Accessible.Button

    // A screen reader's press is the click (#75); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

    activeFocusOnTab: root.focusable && root.enabled
    border.color: root.warn ? root.warnColor : root.accent ? Theme.accent : root.hot ? Theme.fill(
                                                                                          Theme.borderHover)
                                                                                    : Theme.fill(Theme.borderIdle)
    border.width: root.selected && !root.accent ? 0 : 1
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
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.caption
            objectName: "chipKey"
            opacity: 0.8
            text: root.keyText
            visible: root.keyText.length > 0
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            color: {
                if (root.warn)
                    return root.warnColor;
                if (root.selected)
                    return root.tone.a > 0 ? root.tone : Theme.brightForeground;
                return root.toggle ? Theme.dimForeground : Theme.foreground;
            }

            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "chipLabel"
            text: root.text
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: label.color
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            objectName: "chipSuffix"
            opacity: 0.72
            text: root.suffix
            visible: root.suffix.length > 0
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.dimForeground
            font.family: Theme.fontFamily
            font.features: {
                "tnum": 1
            }
            font.pixelSize: Theme.font.small
            objectName: "chipCount"
            text: root.count
            visible: root.count.length > 0
        }
    }

    // Keyboard focus rings the chip from outside (review Q6), selected or not.
    FocusRing {}

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        // A click leaves the keyboard where it was, as Button's does.
        onClicked: root.clicked()
    }

    // On the window's content item, not in the chip, as IconButton's tip (KIT-8).
    Tooltip {
        id: tip

        readonly property bool wanted: root.visible && root.tooltip.length > 0
                                       && (mouseArea.containsMouse || root.activeFocus)

        objectName: "chipTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000

        onWantedChanged: tip.wanted ? tip.showFor(root, root.tooltip, false) : tip.hide()
    }

    onTooltipChanged: tip.wanted ? tip.showFor(root, root.tooltip, false) : tip.hide()

    // Keypad `enter` too (KIT-7).
    Keys.onEnterPressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
