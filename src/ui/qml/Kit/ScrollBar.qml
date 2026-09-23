import QtQuick
import Omaseed.Kit

// A thin scrollbar bound to a Flickable (docs/10 §4: the table scrolls sideways rather than
// dropping columns). A track in the idle fill and a thumb in the selected fill that turns to
// the idle border while the pointer is on it or dragging it. It is there only while the
// content overflows. Drag the thumb, or press the track to jump the thumb to the pointer.
Item {
    id: root

    property Flickable flickable: null
    /// Horizontal by default, like `Hairline`.
    property bool vertical: false

    // Read from the geometry rather than `visibleArea`, which only catches up on the next
    // viewport move.
    readonly property real viewSize: root.flickable === null ? 0 : root.vertical
                                                                   ? root.flickable.height
                                                                   : root.flickable.width
    readonly property real contentSize: root.flickable === null ? 0 : root.vertical
                                                                      ? root.flickable.contentHeight
                                                                      : root.flickable.contentWidth
    /// The visible share of the content, 0..1.
    readonly property real ratio: root.contentSize <= 0 ? 1 : Math.min(1, root.viewSize
                                                                           / root.contentSize)
    readonly property bool needed: root.flickable !== null && root.ratio < 0.999
    readonly property bool active: hover.hovered || press.pressed

    readonly property real length: root.vertical ? root.height : root.width
    readonly property real thumbLength: Math.min(root.length, Math.max(Theme.space(24),
                                                                       root.length * root.ratio))
    /// Where the thumb starts, in this item's coordinates along its axis.
    readonly property real thumbStart: {
        if (root.flickable === null)
            return 0;
        const range = root.contentSize - root.viewSize;
        const offset = root.vertical ? root.flickable.contentY - root.flickable.originY
                                     : root.flickable.contentX - root.flickable.originX;
        const travel = root.length - root.thumbLength;
        return range <= 0 ? 0 : Math.max(0, Math.min(travel, offset / range * travel));
    }

    /// Scroll so the thumb starts at `start` (this item's coordinates along its axis).
    function scrollThumbTo(start: real): void {
        if (root.flickable === null)
            return;
        const travel = root.length - root.thumbLength;
        const fraction = travel <= 0 ? 0 : Math.max(0, Math.min(1, start / travel));
        if (root.vertical) {
            const range = root.flickable.contentHeight - root.flickable.height;
            root.flickable.contentY = root.flickable.originY + fraction * Math.max(0, range);
        } else {
            const range = root.flickable.contentWidth - root.flickable.width;
            root.flickable.contentX = root.flickable.originX + fraction * Math.max(0, range);
        }
    }

    implicitHeight: root.vertical ? Theme.space(100) : Theme.space(8)
    implicitWidth: root.vertical ? Theme.space(8) : Theme.space(100)
    visible: root.needed

    HoverHandler {
        id: hover
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.fill(Theme.fillIdle)
    }

    Rectangle {
        id: thumb

        objectName: "thumb"
        color: Theme.fill(root.active ? Theme.borderIdle : Theme.fillSelected)
        height: root.vertical ? root.thumbLength : parent.height
        width: root.vertical ? parent.width : root.thumbLength
        x: root.vertical ? 0 : root.thumbStart
        y: root.vertical ? root.thumbStart : 0
    }

    MouseArea {
        id: press

        property real grab: 0

        anchors.fill: parent

        onPositionChanged: mouse => {
            if (press.pressed)
                root.scrollThumbTo((root.vertical ? mouse.y : mouse.x) - press.grab);
        }
        onPressed: mouse => {
            const at = root.vertical ? mouse.y : mouse.x;
            const onThumb = at >= root.thumbStart && at < root.thumbStart + root.thumbLength;
            // On the thumb: keep the point that was grabbed under the pointer. On the track:
            // centre the thumb on the pointer, then drag from there.
            press.grab = onThumb ? at - root.thumbStart : root.thumbLength / 2;
            if (!onThumb)
                root.scrollThumbTo(at - press.grab);
        }
    }
}
