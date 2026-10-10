import QtQuick
import Omaseed.Kit

// A mouse wheel that moves a list a set number of rows a notch, the notches adding up.
//
// Qt's Flickable glides a wheel notch three lines of 24 px, but restarts the glide on every
// notch instead of adding to it: in the table, one notch moved 72 px, ten quick notches
// 150 px and twenty 165 px, so a long list felt stuck however fast the wheel turned. Here a
// notch is `step` (three of the list's own rows), each notch adds to where the last one was
// going, and the move is a short glide.
//
// Lay it over the list (`anchors.fill`, a `z` above it). It takes no buttons and no hover, so
// the rows under it still hear clicks and the pointer (docs/UX-NOTES M11). What it leaves
// alone goes on to the list: a touchpad's pixel deltas (Flickable follows those 1:1 and
// stops them with its own kinetic glide), sideways wheels, and a wheel with shift or ctrl
// held.
MouseArea {
    id: root

    /// The list the wheel moves.
    property Flickable flickable: null
    /// The list's row height. A notch moves `rowsPerNotch` of them (three, Qt's own
    /// `wheelScrollLines` default; Wayland has no desktop setting for it).
    property real rowHeight: Theme.rowHeight
    property int rowsPerNotch: 3
    /// One notch, in pixels.
    readonly property real step: root.rowHeight * root.rowsPerNotch
    /// How long a notch glides, in ms; 0 moves at once.
    property int duration: 120

    /// Where the list is going: the end of the glide while one runs.
    readonly property real target: glide.running ? glide.to : root.flickable !== null
                                                              ? root.flickable.contentY : 0

    /// Move the list `pixels` (negative: up) from where it is going, clamped to its ends.
    function scrollBy(pixels: real): void {
        const f = root.flickable;
        if (f === null)
            return;
        const top = f.originY - f.topMargin;
        const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height);
        const to = Math.max(top, Math.min(bottom, root.target + pixels));
        glide.stop();
        f.cancelFlick();
        if (root.duration <= 0 || Math.abs(to - f.contentY) < 0.5) {
            f.contentY = to;
            return;
        }
        glide.from = f.contentY;
        glide.to = to;
        glide.start();
    }

    acceptedButtons: Qt.NoButton

    onWheel: wheel => {
        if (root.flickable === null || wheel.angleDelta.y === 0 || wheel.pixelDelta.y !== 0
                || (wheel.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0) {
            wheel.accepted = false;
            return;
        }
        root.scrollBy(-wheel.angleDelta.y / 120 * root.step);
    }

    NumberAnimation {
        id: glide

        duration: root.duration
        easing.type: Easing.OutCubic
        property: "contentY"
        target: root.flickable
    }
}
