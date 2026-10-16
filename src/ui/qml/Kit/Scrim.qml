import QtQuick
import Omaseed.Kit

// The 70 % ground behind every overlay (spec v2 §3 `.scrim`, plan §3 rule 4: "Scrim 70 %
// `background`"). It covers whatever it is anchored over, eats every mouse event so the view
// underneath cannot be clicked, scrolled or hovered through it, and reports a click on itself
// as `dismissed()` — the "click outside closes" of Fig. 5–14.
//
// It does not close anything by itself: the overlay that owns it decides what a dismissal
// means (Sheet and ConfirmDialog close and emit `rejected`, Menu just closes).
//
// With `passRightClick`, a right-click is `rightClicked(x, y)` (this item's coordinates)
// instead of a dismissal, so the owner can close and pass the click on to what lies under
// it, as a desktop menu does when another row is right-clicked (#51 D17).
Rectangle {
    id: root

    /// How much of `Theme.background` the scrim paints. The spec's `--scrim` is 70 %.
    readonly property real scrimAlpha: 0.7
    /// A right-click is `rightClicked` rather than `dismissed`.
    property bool passRightClick: false

    signal dismissed
    signal rightClicked(real x, real y)

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, root.scrimAlpha)

    // a11y-ignore A2: a dismiss scrim, not a control; esc closes what it lies under too (#77)
    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
        cursorShape: Qt.ArrowCursor
        hoverEnabled: true
        preventStealing: true

        onClicked: mouse => {
            if (root.passRightClick && mouse.button === Qt.RightButton)
                root.rightClicked(mouse.x, mouse.y);
            else
                root.dismissed();
        }
        onWheel: wheel => wheel.accepted = true
    }
}
