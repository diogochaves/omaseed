pragma ComponentBehavior: Bound
import QtQuick
import Omaseed.Kit

// The kit's one tooltip (review Q8): a label in full, shown after a short rest, with an accent
// edge. Two ways in:
//
//   showAt(x, y, text)    near the pointer: the text a cell had to elide (tables, rail, tabs)
//   showFor(item, text)   under a control: IconButton's name, shown on hover and on keyboard
//                         focus alike
//
// It is not a `Kit/Menu` with one row: a tooltip never takes the keyboard, never dims what is
// behind it and is never dismissed by `esc`, because nothing ever focused it. It only appears
// after the pointer (or the focus) has rested for `delay`, so dragging across a table does
// not trail a box behind the cursor.
//
// The panel clamps itself inside this item's bounds, so a row at the window's right edge gets
// the box pulled left rather than clipped. Place it as the last child of the view whose cells
// it serves, or reparent it to the window's content item when the owner is small (the hero's
// server line, KIT-8; every IconButton).
//
// The spec (v2 §2) asks for the full URL in Fig. 3 and the full speed in Fig. 4.
//
// Hand-formatted: qmlformat de-indents everything after a typed function (KIT-19).
Item {
    id: root

    /// The text the panel draws. Setting it while shown re-flows the panel in place.
    property string text: ""
    /// How long the pointer must rest before the panel appears, in milliseconds.
    property int delay: 400
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
        internal.below = false;
        internal.wantX = x;
        internal.wantY = y;
        if (root._open) // already up: follow the pointer without blinking
            return;
        timer.restart();
    }

    /// Ask for the panel under `item`, centred on it; above it when there is no room below.
    /// The position is taken now: call again if the item moves while the panel is up.
    /// `above` turns the preference round: over the item, under it when there is no room
    /// above (the selection bar, docs/17, sits at the table's bottom edge).
    function showFor(item: Item, text: string, above = false): void {
        if (!item || text.length === 0) {
            root.hide();
            return;
        }
        const at = item.mapToItem(root, 0, 0);
        root.text = text;
        internal.below = true;
        internal.above = above;
        internal.itemX = at.x;
        internal.itemY = at.y;
        internal.itemWidth = item.width;
        internal.itemHeight = item.height;
        if (root._open)
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

        /// Placed under an item (`showFor`) rather than beside the pointer (`showAt`).
        property bool below: false
        /// `showFor(…, true)`: over the item first.
        property bool above: false
        property int wantX: 0
        property int wantY: 0
        property real itemX: 0
        property real itemY: 0
        property real itemWidth: 0
        property real itemHeight: 0
    }

    Timer {
        id: timer

        interval: root.delay
        repeat: false

        onTriggered: root._open = true
    }

    Rectangle {
        id: panel

        /// From the pointer (`showAt`), clear of the cursor's own shape.
        readonly property int gap: Theme.space(12)
        /// From the item's edge (`showFor`).
        readonly property int itemGap: Theme.focusExtent + Theme.space(4) // clear of a FocusRing
        readonly property int padding: Theme.space(6)
        readonly property real wantX: internal.below
                                      ? internal.itemX + (internal.itemWidth - panel.width) / 2
                                      : internal.wantX + panel.gap
        readonly property real underY: internal.below
                                       ? internal.itemY + internal.itemHeight + panel.itemGap
                                       : internal.wantY + panel.gap
        readonly property real overY: internal.below
                                      ? internal.itemY - panel.itemGap - panel.height
                                      : internal.wantY - panel.gap - panel.height

        border.color: Theme.accent
        border.width: 1
        color: Theme.background
        implicitHeight: label.implicitHeight + 2 * panel.padding
        implicitWidth: label.implicitWidth + 2 * panel.padding
        radius: Theme.cornerRadius
        visible: root._open
        // Below the pointer or the item, pulled back inside the view at either edge; above it
        // when there is no room below (or the other way round for `above`).
        x: Math.round(Math.max(0, Math.min(panel.wantX, root.width - panel.width)))
        y: Math.round(internal.below && internal.above
                      ? (panel.overY >= 0 ? panel.overY : panel.underY)
                      : panel.underY + panel.height <= root.height ? panel.underY
                                                                   : Math.max(0, panel.overY))

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
