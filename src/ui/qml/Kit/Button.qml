import QtQuick
import Omaseed.Kit

// A themed clickable control — one component for every button-shaped thing in the kit (spec
// v2 §8 tokens page `.btn`, and §9 item 10: "the kit has no filled button"). States compose in
// priority order, mirroring the shell's own Button
// (/usr/share/omarchy/shell/Ui/Button.qml):
//
//   pressed         fillPressed (22 %)
//   activeFocus     the hover fill, plus the kit's FocusRing: 1 px accent, `Theme.focusGap`
//                   outside the edge, so it never reads as the primary's outline (review Q6)
//   hasCursor/hover fillHover (8 %) + borderHover (25 %)
//   selected        fillSelected (18 %), no border unless `bordered`
//   idle            no chrome, unless `bordered`: fillIdle (4 %) + borderIdle (40 %)
//
// `primary` is an accent outline with accent text (never a fill — see §9 item 10). `danger`
// is red text; when `danger` is also `primary` or `selected` the outline turns red too (spec
// `.btn.danger.sel`). Every colour comes from `Theme`.
//
// A click runs `clicked()` and does not take the focus (IconButton and Chip do the same): the
// keyboard stays where it was, so a mouse click never leaves the focus ring (or IconButton's
// tooltip) behind, and an overlay the click opens gives the keyboard back to what had it —
// the table, not the hero's gear (docs/DECISIONS.md M8). `tab` still focuses and rings it.
Rectangle {
    id: root

    property string text: ""
    property string glyph: ""
    property bool primary: false
    property bool danger: false
    property bool hasCursor: false
    property bool selected: false
    property bool bordered: false
    /// A `primary` drawn filled with the accent, its words in the window's ground (board 46
    /// `.btn.pri`, a sheet's main button: `Theme.sheetPrimaryFilled`). Background on accent is
    /// the key badge's pair, held at 4.5:1 by tst_themecontrast (C30 F1). Not for `danger`.
    property bool filled: false
    /// The room either side of the words (spec v2 `.btn`: 10 px; a sheet's foot, 12 px).
    property int sidePadding: Theme.space(10)
    // The label's type size. Body everywhere (spec `.btn`), except the confirm dialog's two
    // buttons, which the spec prints at caption (`.confirm .bts .btn`).
    property int fontSize: Theme.font.body
    // Debug-only override so the ThemeGallery can show the pressed fill without a held mouse
    // button; a real press (mouseArea.pressed) always wins over this.
    property bool pressed: false
    /// What the button sits on while it paints nothing (docs/DECISIONS.md C30 F1): the window,
    /// or the block it is in (`Theme.on.idle` for a notice's or a sheet block's 4 %).
    property ThemeInk block: Theme.on.window

    signal clicked

    readonly property bool hot: root.hasCursor || mouseArea.containsMouse
    /// The ground under the label in each state, as `color` paints it (C30 F1, F4).
    readonly property ThemeInk ground: {
        if (mouseArea.pressed || root.pressed)
            return Theme.on.pressed;
        if (root.activeFocus || root.hot)
            return root.block === Theme.on.idle ? Theme.on.blockHover : Theme.on.hover;
        if (root.selected)
            return root.danger ? Theme.on.danger : Theme.on.selected;
        return root.bordered && root.block === Theme.on.window ? Theme.on.idle : root.block;
    }
    /// The label's colours on that ground, eased with the fill.
    readonly property Ink ink: Ink {
        ground: root.ground
    }
    readonly property bool _filled: root.filled && root.primary && !root.danger
    readonly property color _textColor: root._filled ? Theme.background : root.danger ? root.ink.red
                                                                                     : (root.primary ? root.ink.accent
                                                                                                     : root.ink.foreground)
    readonly property color _borderColor: (root.danger && (root.selected || root.primary))
                                          ? Theme.red : root.primary ? Theme.accent : root.hot
                                                                       ? Theme.fill(
                                                                             Theme.borderHover) : (
                                                                             root.bordered
                                                                             || root.selected)
                                                                         ? Theme.fill(
                                                                               Theme.borderIdle) :
                                                                           "transparent"

    // The text, never the glyph (#78): a Nerd Font code point reads as noise or nothing. A use
    // with a glyph alone names itself with `Accessible.name`.
    Accessible.name: root.text

    Accessible.role: Accessible.Button

    // A screen reader's press is the click (#75); disabled, it does nothing, as the pointer.
    Accessible.onPressAction: if (root.enabled)
                                  root.clicked()

    activeFocusOnTab: root.enabled
    border.color: root._borderColor
    border.width: (root.primary || root.hot || root.bordered || root.selected) ? 1 : 0
    // The ground's own fill; resting in a block, nothing: the block's is under it already.
    color: root._filled ? Theme.accent : root.ground === root.block ? "transparent" : root.ground.fill
    height: Theme.controlHeight
    implicitWidth: label.implicitWidth + glyphLabel.implicitWidth + 2 * root.sidePadding + (root.glyph.length
                                                                                       > 0 && root.text.length
                                                                                       > 0 ? Theme.space(
                                                                                                 6) : 0)
    opacity: root.enabled ? 1.0 : 0.4
    radius: Theme.cornerRadius

    Behavior on color {
        ColorAnimation {
            duration: Theme.fillChangeDuration
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: Theme.space(6)

        Glyph {
            id: glyphLabel

            anchors.verticalCenter: parent.verticalCenter
            color: root._textColor
            pixelSize: Theme.font.icon
            text: root.glyph
            visible: root.glyph.length > 0
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            color: root._textColor
            font.family: Theme.fontFamily
            font.pixelSize: root.fontSize
            text: root.text
            visible: root.text.length > 0
        }
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

    // Keypad `enter` is its own key (`Qt.Key_Enter`); without it the key went on to the
    // Sheet, which ran its primary action instead of this button (KIT-7).
    Keys.onEnterPressed: if (root.enabled)
                             root.clicked()
    Keys.onReturnPressed: if (root.enabled)
                              root.clicked()
    Keys.onSpacePressed: if (root.enabled)
                             root.clicked()
}
