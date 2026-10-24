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

    // ---- text on a ground (DECISIONS C30 F1–F7) -------------------------------------------
    /// The ground of a row or control in these states, the strongest first: pressed, selected
    /// under the cursor, selected, the cursor's, the pointer's, else the window. Its roles are
    /// the text's colours there (`Theme.on.selected.dimForeground`); `Kit/Ink` eases them with
    /// the fill.
    function ground(selected: bool, cursor: bool, hovered: bool, pressed: bool): ThemeInk {
        if (pressed)
            return on.pressed;
        if (selected)
            return cursor ? on.selectedCursor : on.selected;
        if (cursor)
            return on.cursor;
        return hovered ? on.hover : on.window;
    }
    /// The same inside a block that paints `block` (a notice's or a picker's 4 % fill, which is
    /// `on.idle`): resting, the text is on the block; under the pointer or the cursor, on the
    /// hover fill over it. The stronger fills are taken as they are over the window.
    function groundIn(block: ThemeInk, selected: bool, cursor: bool, hovered: bool,
                      pressed: bool): ThemeInk {
        const plain = ground(selected, cursor, hovered, pressed);
        if (plain === on.window)
            return block;
        return block === on.idle && (plain === on.hover || plain === on.cursor) ? on.blockHover
                                                                                 : plain;
    }
    /// `colour`, one of the window's text roles (`Theme.yellow`, a caller's `tone`), as it reads
    /// on `ground`: that ground's role of the same colour, resolved with the rest (C30 F3). A
    /// colour that is no role comes back as it is. `Kit/Ink` has the same, eased.
    function inkOf(colour: color, ground: ThemeInk): color {
        const w = on.window;
        if (Qt.colorEqual(colour, w.foreground))
            return ground.foreground;
        if (Qt.colorEqual(colour, w.brightForeground))
            return ground.brightForeground;
        if (Qt.colorEqual(colour, w.dimForeground))
            return ground.dimForeground;
        if (Qt.colorEqual(colour, w.accent))
            return ground.accent;
        if (Qt.colorEqual(colour, w.red))
            return ground.red;
        if (Qt.colorEqual(colour, w.yellow))
            return ground.yellow;
        if (Qt.colorEqual(colour, w.orange))
            return ground.orange;
        if (Qt.colorEqual(colour, w.green))
            return ground.green;
        if (Qt.colorEqual(colour, w.cyan))
            return ground.cyan;
        if (Qt.colorEqual(colour, w.blue))
            return ground.blue;
        if (Qt.colorEqual(colour, w.magenta))
            return ground.magenta;
        return colour;
    }
    /// How long a fill takes to change colour (the rows', the buttons'), and the text on it
    /// with it (C30 F4).
    readonly property int fillChangeDuration: 120

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

    // ---- the frame's form: the action bar (board 46 `statusLine()` while selecting, `.sel`) --
    /// The status line's own height and insets: the bar takes its place, and nothing above it
    /// moves when a selection starts or ends (DECISIONS C24 H8).
    readonly property int actionBarHeight: statusLineHeight
    readonly property int actionBarInsetStart: statusLineInsetStart
    readonly property int actionBarInsetEnd: statusLineInsetEnd
    /// Its ground, a fill over the window's (board 46 `.status.sel`: the 8 % fill).
    readonly property real actionBarFill: fillHover
    /// A button, square, inside the line with a little room above and below it.
    readonly property int actionBarButtonSize: statusLineHeight - space(4)
    /// Between two buttons, and a group's hairline: its room and how far it stops short of
    /// the line's edges.
    readonly property int actionBarGap: space(2)
    readonly property int actionBarRuleRoom: space(8)
    readonly property int actionBarRuleInset: space(7)

    // ---- the frame's form: the app menu (board 46 `menuHtml("app")`, DECISIONS C24 H9) ---
    /// Its width: board 46's `.menu` is 268 px at least at 13 px type; the head's version and
    /// server line ("0.5.0 · public · qBittorrent 5.3.0") want more than the kit's 280.
    readonly property int appMenuWidth: space(340)

    // ---- the frame's form: keys on demand (board 46 `.kb`, DECISIONS C24 H10) ------------
    /// A key's badge while alt is held: its height, the room either side of the key, its type
    /// (board 46: a 14 px line, 4 px each side, 10 px bold at 13 px type) and its corners
    /// (board 46: square).
    readonly property int keyBadgeHeight: space(14)
    readonly property int keyBadgePadding: space(4)
    readonly property int keyBadgeFontSize: font.caption
    readonly property int keyBadgeWeight: Font.Bold
    readonly property int keyBadgeRadius: 0
    /// How far it hangs past its control's top-right corner (board 46: 6 px right, 9 px up).
    readonly property int keyBadgeOverhangX: space(6)
    readonly property int keyBadgeOverhangY: space(9)
    /// Placed inside a control rather than over its corner (a rail row, the mark): the gap to
    /// the row's count, and the room to an edge where nothing else is (the folded rail).
    readonly property int keyBadgeRowGap: space(6)
    readonly property int keyBadgeEdge: space(3)
    /// How long alt is held alone before the badges show, in ms: the alt of alt+j never shows
    /// them, even for a frame.
    readonly property int keyBadgeDelay: 200

    // ---- the frame's form: the outcome strip (board 46 `.ostrip`, board 50 §2) -------------
    /// How far over the rows' bottom it floats (board 46's 12 px): with a selection on, the
    /// action bar is just under that edge.
    readonly property int outcomeStripLift: space(12)
    /// Between its words and the dim line under them that says why "show" is there
    /// (DECISIONS C30 F10).
    readonly property int outcomeStripReasonGap: space(1)
}
