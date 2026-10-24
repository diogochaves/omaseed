import QtQuick
import Omaseed.Kit

// A square icon-only control, `Theme.controlHeight` on each side (spec v2 §1 Fig. 1 `.acts`:
// add, alt-speed, settings, help). Same state machine as `Button`; see that file's header for
// the priority order. `tooltip` shows the kit's `Tooltip` under the button on hover or
// keyboard focus (over it with `tooltipAbove`).
Rectangle {
    id: root

    property string glyph: ""
    property string tooltip: ""
    /// The tip over the button rather than under it: a control at the bottom of the view
    /// (the selection bar, docs/17 M9).
    property bool tooltipAbove: false
    property bool primary: false
    property bool danger: false
    property bool hasCursor: false
    property bool selected: false
    property bool bordered: false
    /// Spec `.btn.icon.on`: the glyph alone turns accent, with no border or fill (the hero's
    /// `+`, and its alt-speed glyph while the server is on alternative limits).
    property bool accent: false
    // Debug-only override so the ThemeGallery can show the pressed fill without a held mouse
    // button; a real press (mouseArea.pressed) always wins over this.
    property bool pressed: false
    /// What the button sits on while it paints nothing (docs/DECISIONS.md C30 F1), as Button's.
    property ThemeInk block: Theme.on.window

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// The button's tooltip, which lives on the window's content item (the tests read it).
    readonly property alias tooltipItem: tip
    /// The ground under the glyph in each state, as `color` paints it (C30 F1, F4).
    readonly property ThemeInk ground: {
        if (mouseArea.pressed || root.pressed)
            return Theme.on.pressed;
        if (root.activeFocus || root.hot)
            return root.block === Theme.on.idle ? Theme.on.blockHover : Theme.on.hover;
        if (root.selected)
            return Theme.on.selected;
        return root.bordered && root.block === Theme.on.window ? Theme.on.idle : root.block;
    }
    /// The glyph's colours on that ground, eased with the fill.
    readonly property Ink ink: Ink {
        ground: root.ground
    }
    readonly property color _glyphColor: root.danger ? root.ink.red : (root.primary || root.selected
                                                                       || root.accent) ? root.ink.accent :
                                                                                         root.ink.dimForeground
    readonly property color _borderColor: (root.danger && (root.selected || root.primary))
                                          ? Theme.red : root.primary ? Theme.accent : root.hot
                                                                       ? Theme.fill(
                                                                             Theme.borderHover) : (
                                                                             root.bordered
                                                                             || root.selected)
                                                                         ? Theme.fill(
                                                                               Theme.borderIdle) :
                                                                           "transparent"

    // The tooltip, then nothing; never the glyph (#78), which reads as noise or nothing. A
    // tooltip with its key ("add · a") is no name either: such a use sets `Accessible.name`
    // to the action and `Accessible.description` to the key.
    Accessible.name: root.tooltip
    Accessible.role: Accessible.Button

    // A screen reader's press is the click (#75); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

    activeFocusOnTab: root.enabled
    border.color: root._borderColor
    border.width: (root.primary || root.hot || root.bordered || root.selected) ? Theme.hairWidth : 0
    // The ground's own fill; resting in a block, nothing: the block's is under it already.
    color: root.ground === root.block ? "transparent" : root.ground.fill
    // Implicit, not fixed: a layout (the hero's `room`) reads `implicitWidth`, and a fixed
    // `width` left it at 0.
    implicitHeight: Theme.controlHeight
    implicitWidth: Theme.controlHeight
    opacity: root.enabled ? 1.0 : 0.4
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }

    Glyph {
        anchors.centerIn: parent
        color: root._glyphColor
        pixelSize: Theme.font.icon
        text: root.glyph
    }

    FocusRing {}

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        enabled: root.enabled
        hoverEnabled: true

        // A click leaves the keyboard where it was (see Button's header).
        onClicked: root.clicked()
    }

    // On the window's content item, not in the button: clamped inside a button one control
    // tall, the panel would have nowhere to go (the hero's server tip does the same, KIT-8).
    Tooltip {
        id: tip

        readonly property bool wanted: root.visible && root.tooltip.length > 0 && (mouseArea.containsMouse
                                                                                   || root.activeFocus)

        objectName: "iconButtonTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000

        onWantedChanged: tip.wanted ? tip.showFor(root, root.tooltip, root.tooltipAbove) : tip.hide()
    }

    onTooltipChanged: tip.wanted ? tip.showFor(root, root.tooltip, root.tooltipAbove) : tip.hide()

    // Keypad `enter` is its own key (`Qt.Key_Enter`); without it the key went on to the
    // Sheet, which ran its primary action instead of this button (KIT-7).
    Keys.onEnterPressed: if (root.enabled)
                             root.clicked()
    Keys.onReturnPressed: if (root.enabled)
                              root.clicked()
    Keys.onSpacePressed: if (root.enabled)
                             root.clicked()
}
