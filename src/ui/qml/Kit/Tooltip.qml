pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// A hover label: the text a cell had to elide, shown in full near the pointer.
//
// It is not a `Kit/Menu` with one row — a tooltip never takes the keyboard, never dims what
// is behind it and is never dismissed by `esc`, because nothing ever focused it. It only
// appears after the pointer has rested (`delay`), so dragging across a table does not trail a
// box behind the cursor.
//
// The caller owns the position: `showAt(x, y, text)` in *this item's* coordinates, `hide()`
// when the pointer leaves. Placing it as the last child of the view whose cells it serves
// puts it above them; the panel clamps itself inside this item's bounds, so a row at the
// window's right edge gets the box pulled left rather than clipped.
//
// The spec (v2 §2) asks for the full URL in Fig. 3 and the full speed in Fig. 4; T3.4
// deferred the component and it was never built, which is why those two read short as well.
Item {
    id: root

    /// The text the panel draws. Setting it while shown re-flows the panel in place.
    property string text: ""
    /// How long the pointer must rest before the panel appears, in milliseconds.
    property int delay: 450
    /// Widest the panel may get before it wraps.
    property int maxWidth: Theme.space(420)

    /// Whether the panel is up. It is explicit state rather than a read of `panel.visible`,
    /// because `visible` reports the *effective* value: a tooltip inside an item that is not
    /// itself visible would answer false however it was set.
    readonly property bool shown: root._open
    /// The panel, for callers that need its geometry (the tests do).
    readonly property alias panelItem: panel

    property bool _open: false

    /// Ask for the panel at `x`, `y` (this item's coordinates, the pointer's position). The
    /// panel is drawn below and right of it when there is room.
    function showAt(x: int, y: int, text: string): void {
        if (text.length === 0) {
            root.hide();
            return;
        }
        root.text = text;
        internal.wantX = x;
        internal.wantY = y;
        if (root._open) // already up: follow the pointer without blinking
            return;
        timer.restart();
    }

    function hide(): void {
        timer.stop();
        root._open = false;
    }

    anchors.fill: parent
    // The panel must never eat a press meant for the row underneath it.
    enabled: false
    z: 100

    onTextChanged: {
        if (root.text.length === 0)
            root.hide();
    }

    QtObject {
        id: internal

        property int wantX: 0
        property int wantY: 0
    }

    Timer {
        id: timer

        interval: root.delay
        repeat: false

        onTriggered: root._open = true
    }

    Rectangle {
        id: panel

        readonly property int gap: Theme.space(12)
        readonly property int padding: Theme.space(6)

        border.color: Theme.fill(Theme.hairlineAlpha)
        border.width: 1
        color: Theme.background
        implicitHeight: label.implicitHeight + 2 * panel.padding
        implicitWidth: label.implicitWidth + 2 * panel.padding
        radius: Theme.cornerRadius
        visible: root._open
        // Below and right of the pointer, pulled back inside the view at either edge; above
        // it when there is no room below.
        x: Math.max(0, Math.min(internal.wantX + panel.gap, root.width - panel.width))
        y: internal.wantY + panel.gap + panel.height <= root.height ? internal.wantY + panel.gap : Math.max(
                                                                          0,
                                                                          internal.wantY - panel.gap - panel.height)

        Text {
            id: label

            anchors.centerIn: parent
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.small
            text: root.text
            width: Math.min(implicitWidth, root.maxWidth)
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
}
