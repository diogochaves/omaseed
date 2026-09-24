import QtQuick
import Omaseed.Kit

// The keyboard-focus cue, one for the whole kit (review Q6): a 1 px accent ring drawn
// `Theme.focusGap` outside its parent's edge. The primary button's accent outline sits on the
// edge itself, so a focused ordinary button no longer looks like the primary one, and a
// focused primary shows both: its outline, then a gap, then the ring.
//
// Drop it in as a child of the control; it fills the parent and grows out of it by
// `Theme.focusExtent`. `shown` follows the parent's `activeFocus` unless the caller binds it
// (Toggle rings its track while the whole control holds the focus). A parent that clips cuts
// the ring off, so every clipping container that holds focusable controls leaves
// `Theme.focusExtent` of room around them (Sheet's body, the settings panes).
Rectangle {
    id: root

    /// Whether the ring is drawn. Defaults to the parent's keyboard focus.
    property bool shown: root.parent ? root.parent.activeFocus : false

    anchors.fill: parent
    anchors.margins: -Theme.focusExtent
    border.color: Theme.accent
    border.width: 1
    color: "transparent"
    // Found by name in the tests, which have no other way into a control's children.
    objectName: "focusRing"
    // Concentric with the control's corner; square stays square.
    radius: Theme.cornerRadius > 0 ? Theme.cornerRadius + Theme.focusExtent : 0
    visible: root.shown
}
