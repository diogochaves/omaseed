import QtQuick
import Omaseed.Kit

// The responsive main-area container (plan §1: "Details docked on the right, dropping below
// the table under 1100 px"). Two `Item`-typed slots, `main` and `side`, are reparented and
// sized into place rather than laid out as QML children directly, since the caller hands in
// already-built views (`TorrentTable`, `Details`) rather than declaring them inline here.
// At or above the breakpoint (`Theme.space(1100)`, scaled by base size like every other
// spacing token) `side` sits to the right at `sideWidth` with a hairline between; below it,
// `side` drops under `main` at `sideHeight` with a hairline above. `sideVisible: false`
// collapses `side` away entirely and gives `main` the full area. `docked` is exposed so a
// caller can react to the breakpoint too (e.g. to change the details' own internal layout).
//
// docs/10 §5 (B1.7): `placement` can pin the side right or bottom instead of following the
// breakpoint, and the divider can be dragged. It is still a 1 px hairline, but it answers
// across `Theme.space(6)`, turns accent and shows a grip while hovered or dragged, and a
// double-click puts the size back. `resizeBy()` is the keyboard's way (`<` `>`). Sizes are
// kept apart per side and clamped to what leaves the main area usable; `resized` fires when
// the user changed one, so the caller can remember it.
Item {
    id: root

    property Item main: null
    property Item side: null
    property bool sideVisible: true
    /// "auto" follows the breakpoint; "right" and "bottom" pin the side there.
    property string placement: "auto"
    property int defaultSideWidth: Theme.space(330)
    property int defaultSideHeight: Theme.space(260)
    property int sideWidth: root.defaultSideWidth
    property int sideHeight: root.defaultSideHeight

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
    readonly property bool dragging: grip.pressed

    /// The user dragged the divider, pressed `<` `>`, or reset it.
    signal resized

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

    /// The divider's double-click: back to the default size on the current side.
    function resetSize(): void {
        if (root.docked)
            root.sideWidth = root.defaultSideWidth;
        else
            root.sideHeight = root.defaultSideHeight;
        root.resized();
    }

    /// The width the breakpoint is measured against. The spec's rule is about the *window*
    /// ("dropping below the table under 1100 px"), and the split itself sits beside the rail,
    /// so MainView hands in the window's width; on its own the split measures itself.
    property int referenceWidth: root.width

    readonly property int breakpoint: Theme.space(1100)
    readonly property bool docked: root.placement === "right" ? true : root.placement === "bottom"
                                                                     ? false : root.referenceWidth
                                                                               >= root.breakpoint
    readonly property bool showSide: root.sideVisible && root.side !== null

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

    Item {
        id: mainSlot

        height: root.docked || !root.showSide ? root.height : root.height - root.shownSideHeight - 1
        width: root.docked && root.showSide ? root.width - root.shownSideWidth - 1 : root.width
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
        visible: root.showSide
        width: root.docked ? root.shownSideWidth : root.width
        x: root.docked ? mainSlot.width + 1 : 0
        y: root.docked ? 0 : mainSlot.height + 1
    }

    // The divider's handle: 6 u across the hairline, above both panes.
    MouseArea {
        id: grip

        cursorShape: root.docked ? Qt.SplitHCursor : Qt.SplitVCursor
        height: root.docked ? root.height : Theme.space(6)
        hoverEnabled: true
        objectName: "grip"
        visible: root.showSide
        width: root.docked ? Theme.space(6) : root.width
        x: root.docked ? divider.x - Theme.space(3) : 0
        y: root.docked ? 0 : divider.y - Theme.space(3)
        z: 2

        onDoubleClicked: root.resetSize()
        onPositionChanged: mouse => {
            if (!grip.pressed)
                return;
            const at = grip.mapToItem(root, mouse.x, mouse.y);
            if (root.docked)
                root.sideWidth = Math.max(root.minimumSideWidth, Math.min(
                                              root.maximumSideWidth, root.width - at.x - 1));
            else
                root.sideHeight = Math.max(root.minimumSideHeight, Math.min(
                                               root.maximumSideHeight, root.height - at.y - 1));
        }
        onReleased: root.resized()

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
}
