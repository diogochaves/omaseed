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
    /// The same ground as `Theme.on`'s, which the bar paints and takes its words from
    /// (DECISIONS C30 F1): a skin that moves the bar's fill moves its text with it.
    readonly property ThemeInk actionBarGround: on.hover
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

    // ---- the library's form: the rows display (board 46 `rows()`, `.row`, `.gh`, C24 H3) --
    /// A row: two lines, the name's and the sentence's (board 46: 50 px at 13 px type).
    readonly property int libraryRowHeight: space(46)
    /// Before the state glyph and after the fact (board 46: 14 and 18 px).
    readonly property int libraryRowInsetStart: space(13)
    readonly property int libraryRowInsetEnd: space(16)
    /// The state glyph's column (20 px), and the gap between the columns (10 px).
    readonly property int libraryRowGlyphWidth: space(18)
    readonly property int libraryRowGap: space(9)
    /// Between the name's line and the sentence's.
    readonly property int libraryRowLineGap: space(1)
    /// The name (13.5 px, 600, bright), the meta beside it and the sentence (11.5 px, dim).
    readonly property int libraryRowNameSize: font.body
    readonly property int libraryRowNameWeight: Font.DemiBold
    readonly property int libraryRowMetaSize: font.small
    readonly property int libraryRowSentenceSize: font.small
    /// The fact (14 px, 500, bright) over its caption (10 px, upper case, tracked .12em), in a
    /// column at least 92 px wide.
    readonly property int libraryRowFactSize: font.body
    readonly property int libraryRowFactWeight: Font.Medium
    readonly property int libraryRowCaptionSize: font.caption
    readonly property real libraryRowCaptionTracking: font.caption * 0.12
    readonly property int libraryRowFactWidth: space(85)
    /// The progress line before the sentence (board 46 `.strip`: 168 × 4 px on an 8 % track).
    readonly property int libraryRowLineWidth: space(155)
    readonly property int libraryRowLineThickness: space(4)
    readonly property real libraryRowLineTrack: fillHover
    /// The fix (`.fix`): its room either side and its border's accent alpha.
    readonly property int libraryRowFixPadding: space(6)
    readonly property real libraryRowFixBorderAlpha: 0.45
    /// A selected row's edge (`.row.picked`, an inset 2 px accent rule).
    readonly property int libraryRowSelectedEdge: space(2)
    /// A group's head (`.gh`): its height, its inset, its words (10.5 px, upper case, tracked
    /// .12em; the count in the foreground; the line 11.5 px at the right).
    readonly property int libraryGroupHeadHeight: space(30)
    readonly property int libraryGroupHeadInset: space(16)
    readonly property int libraryGroupHeadPaddingBottom: space(4)
    readonly property int libraryGroupHeadSize: font.caption
    readonly property real libraryGroupHeadTracking: font.caption * 0.12
    readonly property int libraryGroupLineSize: font.small
    /// The widths its steps start at, widest first: from step 1 a group's line goes and the
    /// progress line shortens, from step 2 the meta beside the name goes too.
    readonly property var libraryRowsSteps: [space(620), space(440)]
    readonly property int libraryRowLineWidthNarrow: space(90)
    /// The inline rename's field over a row's name (the table's `.ren`): the room inside it
    /// and how far it rises past the name's line above and below.
    readonly property int libraryRenamePadding: space(6)
    readonly property int libraryRenameRise: space(3)
    /// From a row's start to where `enter`'s menu opens under it (the table's 24 u).
    readonly property int libraryMenuInset: space(24)

    /// The display tool's menu: its width.
    readonly property int displayMenuWidth: space(240)

    // ---- the library's form: the overview's shelves (board 46 `overview()`, `.shelf`) -----
    /// The shelves' inset (6 / 18 px) and the room between two shelves (14 px).
    readonly property int shelfInset: space(16)
    readonly property int shelfInsetTop: space(6)
    readonly property int shelfGap: space(13)
    /// A shelf's head (`.shelf h5`): the name (13 px, 600, bright), its count and size (11 px,
    /// dim, upper case, tracked), and the room under it.
    readonly property int shelfNameSize: font.body
    readonly property int shelfNameWeight: Font.DemiBold
    readonly property int shelfLineSize: font.caption
    readonly property real shelfLineTracking: font.caption * 0.12
    readonly property int shelfHeadGap: space(6)
    /// A cover (`.cover`, 92 px wide, its art 118 px tall) and the room between covers.
    readonly property int coverWidth: space(85)
    readonly property int coverArtHeight: space(109)
    readonly property int coverGap: space(9)
    /// The piece grid inside the art (`.grid`: 6 px in, 10 cells across, 6 px tall, 1 px
    /// apart), at 55 % of the state colour over an 8 % cell.
    readonly property int coverGridInset: space(6)
    readonly property int coverGridColumns: 10
    readonly property int coverGridRows: 6
    readonly property int coverCellHeight: space(6)
    readonly property int coverCellGap: 1
    readonly property real coverCellAlpha: 0.55
    readonly property real coverCellTrack: fillHover
    /// The two letters (`.ab2`: 30 px, 700, bright, 8 px in from the art's bottom left).
    readonly property int coverMonogramSize: space(28)
    readonly property int coverMonogramWeight: Font.Bold
    readonly property int coverMonogramInset: space(7)
    /// The name and the fact under the art (11 and 10.5 px), and the room between the parts.
    readonly property int coverNameSize: font.caption
    readonly property int coverFactSize: font.caption
    readonly property int coverLineGap: space(4)
    /// A selected cover's border, and the cursor's.
    readonly property int coverSelectedBorder: 2
}
