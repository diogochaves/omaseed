import QtQuick
import Omaseed.Kit

// The responsive main-area container (plan §1: "Details docked on the right, dropping below
// the table under 1100 px"). Two `Item`-typed slots, `main` and `side`, are reparented and
// sized into place rather than laid out as QML children directly, since the caller hands in
// already-built views (`TorrentTable`, `Details`) rather than declaring them inline here.
// At or above the breakpoint (`Theme.space(1100)`, scaled by base size like every other
// spacing token) `side` sits to the right at `sideWidth` with a hairline between; below it,
// `side` drops under `main` at `sideHeight` with a hairline above. `sideVisible: false`
// takes `side` away entirely and gives `main` the full area. `docked` is exposed so a
// caller can react to the breakpoint too (e.g. to change the details' own internal layout).
//
// docs/10 §5 (B1.7): `placement` can pin the side right or bottom instead of following the
// breakpoint, and the divider can be dragged. It is still a 1 px hairline, but it answers
// across `Theme.space(6)`, turns accent and shows a grip while hovered or dragged.
// `resizeBy()` is the keyboard's way (`<` `>`). Sizes are kept apart per side and clamped to
// what leaves the main area usable; `resized` fires when the user changed one, so the caller
// can remember it.
//
// docs/17 M10 (Diogo's rule, the board's answers): the side folds.
//   double-click on the divider   folds an open side and unfolds a folded one; the size is
//                                 never touched, so unfolding brings back the default or the
//                                 size that was dragged, whichever it was
//   drag                          any size; within `snapDistance` of the default it snaps
//                                 there, and the tip beside the divider reads "default" in the
//                                 accent (the size, in plain text, the rest of the drag).
//                                 Dragging a folded side unfolds it to where it is let go.
//   `toggleFold()`                the same fold for a key (`i`) or a click on what is folded
// Folded at the bottom, `foldedBar` (the caller's one-line strip) takes the side's place, at
// its implicit height; on the right, the split draws a `Theme.space(12)` column with the grip
// and ‹, and a click on it unfolds. `foldToggled` fires whenever the user folded or unfolded.
//
// Do NOT run `qmlformat` on this file: with the project's 100-column wrap it re-indents the
// typed functions further on every pass (the defect MainView.qml notes). It is
// hand-formatted to qmlformat's own style instead.
Item {
    id: root

    property Item main: null
    property Item side: null
    /// What a folded side leaves at the bottom (docs/17 M10.1's strip). Null draws the right
    /// dock's folded column there too, turned on its side.
    property Item foldedBar: null
    property bool sideVisible: true
    /// The side is folded (docs/17 M10): `foldedBar` or the folded column instead of `side`.
    property bool folded: false
    /// "auto" docks on the right only when the window is wide in both senses (at least the
    /// breakpoint, and 16:10 or wider); "right" and "bottom" pin the side there.
    property string placement: "auto"
    property int defaultSideWidth: Theme.space(330)
    property int defaultSideHeight: Theme.space(260)
    property int sideWidth: root.defaultSideWidth
    property int sideHeight: root.defaultSideHeight
    /// How close a drag has to come to the default size to land on it (docs/17 M10.2).
    property int snapDistance: Theme.space(16)

    readonly property int minimumSideWidth: Theme.space(220)
    readonly property int maximumSideWidth: Math.max(root.minimumSideWidth, Math.min(Theme.space(
                                                                                         560),
                                                                                     root.width
                                                                                     - Theme.space(
                                                                                         320)))
    readonly property int minimumSideHeight: Theme.space(140)
    readonly property int maximumSideHeight: Math.max(root.minimumSideHeight, Math.round(
                                                          root.height * 0.6))
    /// The sizes actually drawn: the chosen ones, clamped to the room there is.
    readonly property int shownSideWidth: Math.max(root.minimumSideWidth, Math.min(
                                                       root.maximumSideWidth, root.sideWidth))
    readonly property int shownSideHeight: Math.max(root.minimumSideHeight, Math.min(
                                                        root.maximumSideHeight, root.sideHeight))
    /// A drag of the divider is under way (past the drag distance, so not a click).
    readonly property bool dragging: grip.pressed && grip.moved
    /// The drag under way sits on the default size (docs/17 M10.2).
    readonly property bool snapped: root.dragging && grip.snapped
    /// How deep the folded side is along its axis: the strip's height, or the column's width.
    readonly property int foldedExtent: !root.docked && root.foldedBar !== null
                                        ? root.foldedBar.implicitHeight : Theme.space(12)

    /// The user dragged the divider, pressed `<` `>`, or unfolded the side by dragging it.
    signal resized
    /// The user folded or unfolded the side (a double-click, `toggleFold()`, a drag).
    signal foldToggled

    /// Grow (positive) or shrink the side along the axis it is docked on.
    function resizeBy(delta: int): void {
        if (root.docked)
            root.sideWidth = Math.max(root.minimumSideWidth, Math.min(root.maximumSideWidth,
                                                                      root.shownSideWidth + delta));
        else
            root.sideHeight = Math.max(root.minimumSideHeight, Math.min(
                                           root.maximumSideHeight, root.shownSideHeight + delta));
        root.resized();
    }

    /// Fold an open side, unfold a folded one (docs/17 M10). The size stays what it was.
    function toggleFold(): void {
        root.folded = !root.folded;
        root.foldToggled();
    }

    /// The size a drag to `at` (in the split's coordinates) asks for: clamped to the room,
    /// then pulled onto the default when it comes within `snapDistance` of it.
    function _dragSize(at: point): var {
        const wanted = root.docked ? root.width - at.x - 1 : root.height - at.y - 1;
        const minimum = root.docked ? root.minimumSideWidth : root.minimumSideHeight;
        const maximum = root.docked ? root.maximumSideWidth : root.maximumSideHeight;
        const fallback = root.docked ? root.defaultSideWidth : root.defaultSideHeight;
        const size = Math.max(minimum, Math.min(maximum, wanted));
        if (Math.abs(size - fallback) <= root.snapDistance)
            return {
                "size": fallback,
                "snapped": true
            };
        return {
            "size": size,
            "snapped": false
        };
    }

    /// The width the breakpoint is measured against. The spec's rule is about the *window*
    /// ("dropping below the table under 1100 px"), and the split itself sits beside the rail,
    /// so MainView hands in the window's width; on its own the split measures itself.
    property int referenceWidth: root.width
    /// The height the shape is measured against, the window's beside `referenceWidth`.
    property int referenceHeight: root.height

    readonly property int breakpoint: Theme.space(1100)
    /// "auto" docks on the right only from 16:10: a narrower window gives the table the width
    /// and the details the bottom (Diogo, 2026-10-09: half of an ultrawide is 1720 x 1440).
    readonly property real dockRatio: 1.6
    readonly property bool docked: root.placement === "right" ? true : root.placement === "bottom"
                                                                     ? false : root.referenceWidth
                                                                               >= root.breakpoint
                                                                               && root.referenceWidth
                                                                               >= root.dockRatio
                                                                               * root.referenceHeight
    readonly property bool showSide: root.sideVisible && root.side !== null
    readonly property bool _open: root.showSide && !root.folded
    readonly property bool _shut: root.showSide && root.folded
    /// What the side takes along its axis, hairline included.
    readonly property int _extent: root._open ? (root.docked ? root.shownSideWidth
                                                             : root.shownSideHeight) + 1
                                              : root._shut ? root.foldedExtent + 1 : 0

    onMainChanged: {
        if (root.main) {
            root.main.parent = mainSlot;
            root.main.anchors.fill = mainSlot;
        }
    }
    onSideChanged: {
        if (root.side) {
            root.side.parent = sideSlot;
            root.side.anchors.fill = sideSlot;
        }
    }
    onFoldedBarChanged: {
        if (root.foldedBar) {
            root.foldedBar.parent = foldedSlot;
            root.foldedBar.anchors.fill = foldedSlot;
        }
    }

    Item {
        id: mainSlot

        height: root.docked ? root.height : root.height - root._extent
        width: root.docked ? root.width - root._extent : root.width
        x: 0
        y: 0
    }

    Rectangle {
        id: divider

        color: grip.containsMouse || grip.pressed ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
        height: root.docked ? root.height : 1
        visible: root.showSide
        width: root.docked ? 1 : root.width
        x: root.docked ? mainSlot.width : 0
        y: root.docked ? 0 : mainSlot.height
    }

    Item {
        id: sideSlot

        height: root.docked ? root.height : root.shownSideHeight
        visible: root._open
        width: root.docked ? root.shownSideWidth : root.width
        x: root.docked ? mainSlot.width + 1 : 0
        y: root.docked ? 0 : mainSlot.height + 1
    }

    // Folded at the bottom: the caller's strip.
    Item {
        id: foldedSlot

        height: root.foldedExtent
        visible: root._shut && !root.docked && root.foldedBar !== null
        width: root.width
        x: 0
        y: mainSlot.height + 1
    }

    // Folded on the right (or at the bottom with no strip): a narrow column with the grip
    // and a chevron that points to where the side comes from. A click unfolds.
    Rectangle {
        id: foldedColumn

        color: foldedMouse.containsMouse ? Theme.fill(Theme.fillHover) : "transparent"
        height: root.docked ? root.height : root.foldedExtent
        objectName: "foldedColumn"
        visible: root._shut && (root.docked || root.foldedBar === null)
        width: root.docked ? root.foldedExtent : root.width
        x: root.docked ? mainSlot.width + 1 : 0
        y: root.docked ? 0 : mainSlot.height + 1

        Rectangle {
            anchors.centerIn: parent
            color: Theme.accent
            height: root.docked ? Theme.space(28) : Theme.space(3)
            radius: Theme.space(1)
            width: root.docked ? Theme.space(3) : Theme.space(28)
        }

        // Placed by x/y, not anchors: anchors switched by `docked` kept the old ones and drew
        // the chevron in the middle of the table after a placement change.
        Label {
            color: Theme.accent
            objectName: "foldedChevron"
            size: "small"
            text: root.docked ? Glyphs.chevronLeft : Glyphs.chevronUp
            x: root.docked ? Math.round((foldedColumn.width - width) / 2) : Theme.space(14)
            y: root.docked ? Theme.space(10) : Math.round((foldedColumn.height - height) / 2)
        }

        MouseArea {
            id: foldedMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: root.toggleFold()
        }
    }

    // The divider's handle: 6 u across the hairline, above both panes.
    MouseArea {
        id: grip

        /// Where the press landed (split coordinates), and whether the pointer has since gone
        /// further than a click would: until then a press is a click, maybe a double-click.
        property point pressedAt: Qt.point(0, 0)
        property bool moved: false
        property bool snapped: false

        cursorShape: root.docked ? Qt.SplitHCursor : Qt.SplitVCursor
        height: root.docked ? root.height : Theme.space(6)
        hoverEnabled: true
        objectName: "grip"
        visible: root.showSide
        width: root.docked ? Theme.space(6) : root.width
        x: root.docked ? divider.x - Theme.space(3) : 0
        y: root.docked ? 0 : divider.y - Theme.space(3)
        z: 2

        onDoubleClicked: root.toggleFold()
        onPositionChanged: mouse => {
            if (!grip.pressed)
                return;
            const at = grip.mapToItem(root, mouse.x, mouse.y);
            if (!grip.moved) {
                const along = root.docked ? at.x - grip.pressedAt.x : at.y - grip.pressedAt.y;
                if (Math.abs(along) < Application.styleHints.startDragDistance)
                    return;
                grip.moved = true;
            }
            const next = root._dragSize(at);
            grip.snapped = next.snapped;
            if (root.docked)
                root.sideWidth = next.size;
            else
                root.sideHeight = next.size;
            if (root.folded)
                root.toggleFold();
        }
        onPressed: mouse => {
            grip.pressedAt = grip.mapToItem(root, mouse.x, mouse.y);
            grip.moved = false;
            grip.snapped = false;
        }
        onReleased: {
            if (grip.moved)
                root.resized();
            grip.moved = false;
            grip.snapped = false;
        }

        // A grip on the hairline while it can be dragged.
        Rectangle {
            anchors.centerIn: parent
            color: Theme.accent
            height: root.docked ? Theme.space(28) : Theme.space(3)
            radius: Theme.space(1)
            visible: grip.containsMouse || grip.pressed
            width: root.docked ? Theme.space(3) : Theme.space(28)
        }
    }

    // docs/17 M10.2: the size while dragging, and "default" in the accent on the snap.
    Rectangle {
        id: sizeTip

        border.color: root.snapped ? Theme.accent : Theme.fill(Theme.hairlineAlpha)
        border.width: 1
        color: Theme.background
        height: tipText.implicitHeight + Theme.space(6)
        objectName: "sizeTip"
        radius: Theme.cornerRadius
        visible: root.dragging
        width: tipText.implicitWidth + Theme.space(16)
        x: root.docked ? Math.max(0, divider.x - width - Theme.space(10)) : root.width - width
                                                                          - Theme.space(14)
        y: root.docked ? Theme.space(14) : Math.max(0, divider.y - height - Theme.space(10))
        z: 3

        Label {
            id: tipText

            anchors.centerIn: parent
            color: root.snapped ? Theme.accent : Theme.foreground
            size: "small"
            text: root.snapped ? qsTr("default") : qsTr("%1 px").arg(root.docked
                                                                      ? root.shownSideWidth
                                                                      : root.shownSideHeight)
        }
    }
}
