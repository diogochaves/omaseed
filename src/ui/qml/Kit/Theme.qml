pragma Singleton
import QtQuick
import Omaseed.Kit

// The `Theme` every QML file reads: the C++ theme (`ThemeBase`, src/integration/Theme.hpp),
// every role and size as it is, plus a `space()` a binding follows (#156, docs/DECISIONS.md
// C28). A binding re-evaluates for the properties it reads, and a call to a C++ method reads
// none: as a C++ method, `Theme.space(8)` kept the value of its first run when the shell's
// base size changed. This one reads `spaceScale` (NOTIFY changed), so every binding made of
// `Theme.space(…)` alone follows a base-size change live, with no call site changed.
//
// The frame's form (DECISIONS C24, board 46) comes from the named tokens below, the way its
// colours come from roles: a skin made later sets these, and no part of the frame carries a
// size, weight or inset of its own.
ThemeBase {
    /// Spacing in spec pixels, scaled by `[spacing] scale` and the base size.
    function space(px: real): int {
        return spaceAt(px, spaceScale);
    }

    // ---- the frame's form: the rail (board 46 `rail()`, `.mark`, `.views`) --------------
    /// The rail open, and folded to its glyphs (board 46: 214 and 48 px at its 13 px type).
    readonly property int railWidth: space(200)
    readonly property int railFoldedWidth: space(46)
    /// The mark's block before the host gives it the hero's height.
    readonly property int railHeadHeight: space(46)
    /// The mark: its glyph, the inset before it, the gap to the name, the name's weight.
    readonly property int railMarkSize: space(20)
    readonly property int railMarkInset: space(14)
    readonly property int railMarkGap: space(9)
    readonly property int railMarkWeight: Font.DemiBold
    /// Above and below the views' rows.
    readonly property int railViewsPadding: space(8)
    /// The current view's edge (board 46 `.views .ri.on`, an inset 2 px accent rule).
    readonly property int railCurrentEdge: space(2)
    /// From the mark to the menu it opens.
    readonly property int railMenuGap: space(4)
    /// From a folded row to its name's tooltip.
    readonly property int railTipGap: space(2)
}
