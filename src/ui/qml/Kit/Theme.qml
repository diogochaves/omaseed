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
    /// The mark's block before the host gives it the title bar's height.
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

    // ---- the frame's form: the title bar (board 46 `titleBar()`, `.top`, `.tool`) ----------
    /// Its height, which the rail's mark block takes too so their rules meet (board 46: 54 px
    /// at 13 px type).
    readonly property int titleBarHeight: space(46)
    /// Before the title and after the last tool (board 46: 18 and 14 px), between the parts
    /// (12 px), and between the title and its sentence.
    readonly property int titleBarInsetStart: space(16)
    readonly property int titleBarInsetEnd: space(13)
    readonly property int titleBarGap: space(11)
    readonly property int titleBarLineGap: space(1)
    /// The title (board 46: 15 px, 600, bright) and its sentence (11.5 px, dim).
    readonly property int titleBarTitleSize: font.title
    readonly property int titleBarTitleWeight: Font.DemiBold
    readonly property int titleBarSentenceSize: font.small
    /// A tool's height (board 46 `.tool`, 28 px: the kit's control, as the filter field's),
    /// and the filter tool's width (176 px).
    readonly property int titleBarToolHeight: controlHeight
    readonly property int titleBarFindWidth: space(164)
    /// The widths its steps start at, widest first: from step 1 the tools keep their glyph
    /// alone (their words in the tooltip), from step 2 the filter folds into its `/` tool and
    /// opens over the title.
    readonly property var titleBarSteps: [space(760), space(560)]

    // ---- the frame's form: the status line (board 46 `statusLine()`, `.status`) ----------
    /// Its height: the line it replaced (the footer), one small line with room around it.
    readonly property int statusLineHeight: Math.round(font.baseSize * 2.2)
    /// Its type: the small size (board 46's 11.8 px at 13 px type); the server's name weighs
    /// a little more.
    readonly property int statusLineFontSize: font.small
    readonly property int statusLineNameWeight: Font.Medium
    /// Before its first part and after its last (board 46: 18 and 14 px at 13 px type).
    readonly property int statusLineInsetStart: space(16)
    readonly property int statusLineInsetEnd: space(13)
    /// Between its parts (board 46's 12 px), and inside the server lane (dot, name, ▾: 7 px).
    readonly property int statusLineGap: space(11)
    readonly property int statusLineLaneGap: space(6)
    /// The server's dot.
    readonly property int statusLineDotSize: space(7)
    /// How far the server lane's hover fill reaches past its words.
    readonly property int statusLineHoverInset: space(3)
    /// The widths its steps start at, widest first (step 0 at the first, step 4 below the
    /// last): from step 1 the lane is the name alone and the alt-speed limits are short, from
    /// 2 the facts' words shorten, from 3 the speeds are short and the count loses its word,
    /// at 4 the downloading count and the free space go.
    readonly property var statusLineSteps: [space(880), space(740), space(600), space(460)]
}
