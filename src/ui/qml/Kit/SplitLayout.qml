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
Item {
    id: root

    property Item main: null
    property Item side: null
    property bool sideVisible: true
    property int sideWidth: Theme.space(330)
    property int sideHeight: Theme.space(260)

    readonly property int breakpoint: Theme.space(1100)
    readonly property bool docked: root.width >= root.breakpoint
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

        height: root.docked || !root.showSide ? root.height : root.height - root.sideHeight - 1
        width: root.docked && root.showSide ? root.width - root.sideWidth - 1 : root.width
        x: 0
        y: 0
    }

    Rectangle {
        id: divider

        color: Theme.fill(Theme.hairlineAlpha)
        height: root.docked ? root.height : 1
        visible: root.showSide
        width: root.docked ? 1 : root.width
        x: root.docked ? mainSlot.width : 0
        y: root.docked ? 0 : mainSlot.height
    }

    Item {
        id: sideSlot

        height: root.docked ? root.height : root.sideHeight
        visible: root.showSide
        width: root.docked ? root.sideWidth : root.width
        x: root.docked ? mainSlot.width + 1 : 0
        y: root.docked ? 0 : mainSlot.height + 1
    }
}
