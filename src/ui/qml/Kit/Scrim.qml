import QtQuick
import Omaseed.Kit

// The 70 % ground behind every overlay (spec v2 §3 `.scrim`, plan §3 rule 4: "Scrim 70 %
// `background`"). It covers whatever it is anchored over, eats every mouse event so the view
// underneath cannot be clicked, scrolled or hovered through it, and reports a click on itself
// as `dismissed()` — the "click outside closes" of Fig. 5–14.
//
// It does not close anything by itself: the overlay that owns it decides what a dismissal
// means (Sheet and ConfirmDialog close and emit `rejected`, Menu just closes).
Rectangle {
    id: root

    /// How much of `Theme.background` the scrim paints. The spec's `--scrim` is 70 %.
    readonly property real scrimAlpha: 0.7

    signal dismissed

    color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, root.scrimAlpha)

    MouseArea {
        acceptedButtons: Qt.AllButtons
        anchors.fill: parent
        cursorShape: Qt.ArrowCursor
        hoverEnabled: true
        preventStealing: true

        onClicked: root.dismissed()
        onWheel: wheel => wheel.accepted = true
    }
}
