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
// colours come from roles: a skin sets these (see "the skin" below), and no part of the
// frame carries a size, weight or inset of its own.
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
    /// `on.idle`; the action bar's or a hovered row's hover fill, `on.hover`): resting, the
    /// text is on the block; in any other state, on that state's fill over it (`within`).
    function groundIn(block: ThemeInk, selected: bool, cursor: bool, hovered: bool,
                      pressed: bool): ThemeInk {
        return within(block, ground(selected, cursor, hovered, pressed));
    }
    /// `plain`, a ground as it lies over the window (`ground()`'s answer, `on.idle`), as it lies
    /// inside `block`: the block itself for the window, the nested ground where the fill sits
    /// over the 4 % block (`on.blockSelected`) or over the hover fill (`on.hoverPressed`), so
    /// its text is lifted against what is under it (DECISIONS C30, #188). Over the window, or a
    /// fill with no nested ground (a tint), `plain` as it is.
    function within(block: ThemeInk, plain: ThemeInk): ThemeInk {
        if (plain === on.window)
            return block;
        if (block === on.idle) {
            if (plain === on.idle)
                return on.blockIdle;
            if (plain === on.hover || plain === on.cursor)
                return on.blockHover;
            if (plain === on.selected)
                return on.blockSelected;
            if (plain === on.selectedCursor)
                return on.blockSelectedCursor;
            if (plain === on.pressed)
                return on.blockPressed;
        } else if (block === on.hover) {
            if (plain === on.idle)
                return on.hoverIdle;
            if (plain === on.hover || plain === on.cursor)
                return on.hoverHover;
            if (plain === on.selected || plain === on.selectedCursor)
                return on.hoverSelected;
            if (plain === on.pressed)
                return on.hoverPressed;
        }
        return plain;
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

    // ---- the skin (#171, the console skin's design note "The shape") -----------------------
    // Every named token below is Omarchy's value beside its citation, unless the skin drawn
    // (`skin`, src/integration/SkinReader.hpp) sets it. Omarchy's skin sets nothing, so under
    // it every token comes out as it always has. Each reads `skinTokens`, a copy of the
    // skin's notifying `tokens`, never the C++ invokables (`skin.n()`), which a binding can't
    // follow: a skin switch repaints live (rule 8).

    /// Every token the skin set, by group (`density`, `line`, `font`, `label`, `glyphs`…).
    readonly property var skinTokens: skin.tokens
    /// A form token in spec px: the skin's `density[name]`, else `px` (Omarchy's), through
    /// space(), so a skin's sizes follow `base-size` like the kit's (design note, "Later").
    function n(name: string, px: real): int {
        const density = skinTokens.density;
        return space(density !== undefined && density[name] !== undefined ? density[name] : px);
    }
    /// Any other token, `token("line", "edge", 1)`: the skin's, else `fallback` (Omarchy's).
    function token(group: string, key: string, fallback: var): var {
        const values = skinTokens[group];
        return values !== undefined && values[key] !== undefined ? values[key] : fallback;
    }
    /// A colour a skin names, as this theme has it: one of the skin's roles (`signal`,
    /// `alert`, `warn`, `accent`, through `skin.roles`) or a theme role (`lighterBackground`).
    /// The window's own colour for any other name. Read here rather than through
    /// `skinColor()`, so a binding follows the theme and the skin.
    function roleColor(role: string): color {
        const name = skin.roles[role] ?? role;
        switch (name) {
        case "foreground":
            return foreground;
        case "dimForeground":
            return dimForeground;
        case "brightForeground":
            return brightForeground;
        case "accent":
            return accent;
        case "muted":
            return muted;
        case "selection":
            return selection;
        case "red":
            return red;
        case "yellow":
            return yellow;
        case "orange":
            return orange;
        case "green":
            return green;
        case "cyan":
            return cyan;
        case "blue":
            return blue;
        case "magenta":
            return magenta;
        case "brown":
            return brown;
        case "darkBackground":
            return darkBackground;
        case "darkerBackground":
            return darkerBackground;
        case "lighterBackground":
            return lighterBackground;
        }
        return background;
    }
    /// The colour a line's role names: the skin's own `hair` or `faint`, else roleColor().
    function lineColor(role: string): color {
        if (role === "hair")
            return hairColor;
        if (role === "faint")
            return faintColor;
        return roleColor(role);
    }

    // ---- the skin's lines (design note `line`; rules 2 and 5) -------------------------------
    /// A hairline: 1 px of the foreground at the shell's hairline alpha (rule 2, 12 %). Also
    /// the width of a control's outline (a field's, a button's, a chip's) and of a focus ring.
    readonly property int hairWidth: token("line", "hair", 1)
    readonly property color hairColor: Qt.alpha(roleColor(token("line", "hairRole", "foreground")),
                                                token("line", "hairAlpha", hairlineAlpha))
    /// The faintest rule, between rows (`.tbl .r`, 5 %): what a skin's `faint` line names.
    readonly property color faintColor: Qt.alpha(roleColor(token("line", "hairRole", "foreground")),
                                                 token("line", "faintAlpha", rowRuleAlpha))
    /// A lit line, what marks the current or the selected (2 px of the accent).
    readonly property int litWidth: token("line", "lit", 2)
    readonly property color litColor: lineColor(token("line", "litRole", "accent"))
    /// The edge of a popup, a sheet, a palette or a dialog (rule 5: 1 px of the accent).
    readonly property int edgeWidth: token("line", "edge", 1)
    readonly property color edgeColor: lineColor(token("line", "edgeRole", "accent"))
    /// A menu's edge: the accent's, like every popup's (board 46 `.menu`); a skin may draw it
    /// apart (Console's is the hair, `.sk-con .menu { --pc: var(--hair) }`).
    readonly property int menuEdgeWidth: token("line", "menuEdge", 1)
    readonly property color menuEdgeColor: lineColor(token("line", "menuEdgeRole", "accent"))
    /// The ring round today's cell in the year's grid (docs/22 `renderYear`, 1.5 px).
    readonly property real dayTodayEdge: 1.5

    // ---- the skin's seams (design note "Seams": Plate, SelectionMark, Meter, Lamp) ---------
    /// The face a seam draws through: the skin's own file in its `faces/` when its `faces`
    /// names one, else Omarchy's. Faces are compiled into this module under `skins/`, so the
    /// URL resolves beside this file. Read through `skinTokens`, so a switch reloads them.
    function face(seam: string): url {
        const faces = skinTokens.faces;
        const file = faces !== undefined ? faces[seam] : undefined;
        return file !== undefined ? Qt.resolvedUrl("skins/" + skin.name + "/faces/" + file) : defaultFace(seam);
    }
    /// Omarchy's face for a seam: today's drawing, and what any face that fails falls back to.
    function defaultFace(seam: string): url {
        return Qt.resolvedUrl("skins/omarchy/faces/" + seam + ".qml");
    }
    /// A plate's cut corner, `cut.panel` or `cut.key` (board 46 `.pn`, `.pn.key`), in spec px
    /// through space(); 0 draws square corners.
    readonly property int plateCut: space(token("cut", "panel", 0))
    readonly property int plateKeyCut: space(token("cut", "key", 0))
    /// The arm of a corner bracket round the selected (board 46 `.row.sel::after`, 8 × 2 px);
    /// its thickness is `litWidth`.
    readonly property int selectionMarkArm: n("selectionMarkArm", 8)
    /// One cell of a segmented meter drawn as a line (board 46 `.strip i`), and the gap between
    /// cells (1 px, as the cover's grid).
    readonly property int meterCellWidth: n("meterCellWidth", 3)
    readonly property int meterCellGap: 1
    /// The table's progress line (spec v2 `.tbl .pg`: 2 px; board 46 `.pbar i`: 7 px cells).
    readonly property int progressLineThickness: n("progressLineThickness", 2)
    /// The details' pieces bar as a grid (board 46 `.pmap`: 34 columns of square cells, at most
    /// 7 rows); 0 columns draws it as a line, Omarchy's.
    readonly property int piecesColumns: token("meter", "piecesColumns", 0)
    readonly property int piecesRows: Math.max(1, token("meter", "piecesRows", 1))
    /// The widest a grid cell grows (the board's are about 9 px in a 360 px pane): a wider bar
    /// (the pane docked under the list) takes more columns rather than taller rows.
    readonly property int piecesCellMax: n("piecesCellMax", 10)
    /// The current tab's underline (board 46 `.tab.on`): the lit line where the skin says so,
    /// else Omarchy's 1 px in the bright foreground.
    readonly property bool tabLit: token("line", "tabLit", false)
    readonly property int tabLineWidth: tabLit ? litWidth : hairWidth
    readonly property color tabLineColor: tabLit ? litColor : brightForeground

    // ---- the skin's type (design note `font`, `label`, `numerals`; rule 7) -----------------
    // A family the skin names is used only while it is installed and the desktop doesn't ask
    // for the shell's font (`skin.systemFonts`); otherwise it falls back on its own, to
    // fontconfig's monospace (`fontFamily`), so a missing font degrades instead of drawing
    // tofu (#171 ruling 1). Omarchy names none: every family is `fontFamily`.
    function _family(role: string, fallback: string): string {
        const family = token("font", role, "");
        if (family === "" || skin.systemFonts || skin.missingFonts.indexOf(family) >= 0)
            return fallback;
        return family;
    }
    /// The words that name things: captions, heads, buttons (board 46 `--fl`).
    readonly property string labelFamily: _family("label", fontFamily)
    /// The values: names, numbers, sentences (board 46 `--fv`).
    readonly property string valueFamily: _family("value", fontFamily)
    /// The kana word beside a label (board 46 `--fk`).
    readonly property string kanaFamily: _family("kana", fontFamily)
    /// The icon font whose ligatures an `ms:` glyph draws (Kit/Glyphs.qml), "" when there is
    /// none to draw with: then every glyph is Omarchy's.
    readonly property string iconsFamily: _family("icons", "")
    /// A caption's voice (Kit/Label `tracked`): its case, its tracking in em, its weight. The
    /// spec's `.lbl` by default: upper case, 0.08 em, the plain weight.
    readonly property bool labelUpper: token("label", "case", "upper") === "upper"
    readonly property real labelTrack: token("label", "track", 0.08)
    readonly property real labelTrackTitle: token("label", "trackTitle", 0)
    readonly property int labelWeight: token("label", "weight", Font.Normal)
    /// Whether a label that names itself (Kit/Label `labelId`) carries the skin's kana word,
    /// and the role it is drawn in (board 46 `.kn`: the signal).
    readonly property bool labelKana: token("label", "kana", false)
    readonly property string kanaRole: skin.roles.signal ?? "cyan"
    /// The skin's kana word for a label id (`library`, `appearance`…), "" when it has none or
    /// draws no kana.
    function kana(id: string): string {
        if (!labelKana || id === "")
            return "";
        return token("labels", id, "");
    }
    /// Whether small counts are padded to two digits (board 46's server count "02").
    // The other voices (#171 step 5, board 46 `.sk-con`). Omarchy's answers are today's: a
    // heading, a control's words and the status line in the UI font, mixed case, untracked.
    /// A heading (the title bar's `.top h1`, a sheet's `.shh h4`, a menu's `.mh b`): the label
    /// family, this case, `labelTrackTitle`'s tracking and this weight.
    readonly property bool titleUpper: token("label", "titleCase", "none") === "upper"
    readonly property int titleWeight: token("label", "titleWeight", Font.DemiBold)
    /// A control's words (`.tool .pi`, `.btn`, `.status.sel .ab`): the label family.
    readonly property bool controlUpper: token("label", "controlCase", "none") === "upper"
    readonly property real controlTrack: token("label", "controlTrack", 0)
    readonly property int controlWeight: token("label", "controlWeight", Font.Normal)
    /// Their size, a step of the type scale (board 46 `.btn`: 10.5 px at 13 px type, where
    /// Omarchy's `.btn` is the body).
    readonly property int controlFontSize: font[token("label", "controlSize", "body")] ?? font.body
    /// The status line (`.status`): the value family, this case and tracking.
    readonly property bool statusUpper: token("label", "statusCase", "none") === "upper"
    readonly property real statusTrack: token("label", "statusTrack", 0)
    /// A strip or band names its outcome by a word ("moved", `.ostrip .g`, `.notice .stl`)
    /// where Omarchy draws its sign (✓, !).
    readonly property bool labelTags: token("label", "tags", false)
    // The Views' voices (#171 step 6). Omarchy's are today's: names untracked in the UI font,
    // the rows' names as typed, captions at each caption's own tracking and weight.
    /// A thing's name (a rail row `.ri`, a table's name cell `.tbl td.nm2`, a strip's and a
    /// note's words `.ostrip b`): the label family at this tracking (`Kit/Label` voice "name").
    readonly property real nameTrack: token("label", "nameTrack", 0)
    readonly property int nameWeight: token("label", "nameWeight", Font.Normal)
    /// The rows' names (`.row .nm`, a search result's): upper case, or as typed.
    readonly property bool rowNameUpper: token("label", "rowCase", "none") === "upper"
    /// A small caption over or beside a value (`.row .fact span`, `.tbl th`, `.kv dt`, `.tab`,
    /// `.dhero .eb`): the label family, upper case where the skin says (Omarchy's table heads
    /// and tabs are as typed); its tracking and weight where the skin sets them, else the
    /// caption's own (`fallbackTrack`, `fallbackWeight`).
    readonly property bool captionUpper: token("label", "captionCase", "none") === "upper"
    function captionTrack(fallbackTrack: real): real {
        return token("label", "captionTrack", fallbackTrack);
    }
    function captionWeight(fallbackWeight: int): int {
        return token("label", "captionWeight", fallbackWeight);
    }
    /// The table's status word in the caption voice (board 46 `.verb`: the label family at
    /// the caption size, its case, tracking and weight) rather than the body's value voice.
    readonly property bool verbCaption: token("label", "verb", false)
    // What marks the cursor and the picked (#171 step 6; board 46 `.row.sel`, `.row.picked`).
    /// The cursor's row is marked by the SelectionMark (Console's brackets) rather than by a
    /// hairline round it.
    readonly property bool cursorMarked: token("select", "cursorMark", false)
    /// A picked torrent's row is on the `picked` ground (the accent at 20 %) with no rule,
    /// rather than on the selected ground with the accent rule down its edge.
    readonly property bool pickedGround: token("select", "pickedGround", false)
    /// The blocks inside a view (the details' head, create's round-trip note) stand on their
    /// own Plate, its edge the hair (board 46 `.dhero.pn`, board 49 `.rtnote`), rather than on
    /// the view's ground with a rule.
    readonly property bool platedBlocks: token("plates", "blocks", false)
    /// A torrent row's ground (the rows', the table's): `ground()`, with a picked row on the
    /// `picked` ground where the skin says so (the cursor's mark still says which is current).
    function rowGround(selected: bool, cursor: bool, hovered: bool, pressed: bool): ThemeInk {
        if (selected && pickedGround && !pressed)
            return on.picked;
        return ground(selected, cursor, hovered, pressed);
    }
    readonly property bool padNumerals: (skinTokens.numerals ?? "plain") === "pad"
    /// `count` as the skin writes a small count: "02" when it pads, "2" when it doesn't. Only
    /// whole numbers from 0 to 9 gain a digit; anything else is written as it is.
    function numeral(count: int): string {
        return padNumerals && count >= 0 && count < 10 ? "0" + count : String(count);
    }

    // ---- the frame's form: the rail (board 46 `rail()`, `.mark`, `.views`) --------------
    /// The rail open, and folded to its glyphs (board 46: 214 and 48 px at its 13 px type).
    readonly property int railWidth: n("railWidth", 200)
    readonly property int railFoldedWidth: n("railFoldedWidth", 46)
    /// The mark's block before the host gives it the title bar's height.
    readonly property int railHeadHeight: n("railHeadHeight", 46)
    /// The mark: its glyph, the inset before it, the gap to the name, the name's weight.
    readonly property int railMarkSize: n("railMarkSize", 20)
    readonly property int railMarkInset: n("railMarkInset", 14)
    readonly property int railMarkGap: n("railMarkGap", 9)
    readonly property int railMarkWeight: Font.DemiBold
    /// Above and below the views' rows.
    readonly property int railViewsPadding: n("railViewsPadding", 8)
    /// The current view's edge (board 46 `.views .ri.on`, an inset 2 px accent rule).
    readonly property int railCurrentEdge: n("railCurrentEdge", 2)
    /// From the mark to the menu it opens.
    readonly property int railMenuGap: n("railMenuGap", 4)
    /// From a folded row to its name's tooltip.
    readonly property int railTipGap: n("railTipGap", 2)

    // ---- the frame's form: the title bar (board 46 `titleBar()`, `.top`, `.tool`) ----------
    /// Its height, which the rail's mark block takes too so their rules meet (board 46: 54 px
    /// at 13 px type).
    readonly property int titleBarHeight: n("titleBarHeight", 46)
    /// Before the title and after the last tool (board 46: 18 and 14 px), between the parts
    /// (12 px), and between the title and its sentence.
    readonly property int titleBarInsetStart: n("titleBarInsetStart", 16)
    readonly property int titleBarInsetEnd: n("titleBarInsetEnd", 13)
    readonly property int titleBarGap: n("titleBarGap", 11)
    readonly property int titleBarLineGap: n("titleBarLineGap", 1)
    /// The title (board 46: 15 px, 600, bright) and its sentence (11.5 px, dim).
    readonly property int titleBarTitleSize: font.title
    readonly property int titleBarTitleWeight: titleWeight
    readonly property int titleBarSentenceSize: font.small
    /// A tool's height (board 46 `.tool`, 28 px: the kit's control, as the filter field's),
    /// and the filter tool's width (176 px).
    readonly property int titleBarToolHeight: controlHeight
    readonly property int titleBarFindWidth: n("titleBarFindWidth", 164)
    /// The widths its steps start at, widest first: from step 1 the tools keep their glyph
    /// alone (their words in the tooltip), from step 2 the filter folds into its `/` tool and
    /// opens over the title.
    readonly property var titleBarSteps: [space(760), space(560)]
    /// The speeds in the title bar (board 51 D2 a `.tspd`): the figure (15 px, 500), its
    /// arrow (13 px) and unit (10.5 px), and the line of the last polls under each (72 × 18;
    /// held lower here so the bar keeps its height).
    readonly property int titleBarSpeedSize: font.title
    readonly property int titleBarSpeedWeight: Font.Medium
    readonly property int titleBarSpeedArrowSize: font.body
    readonly property int titleBarSpeedUnitSize: font.caption
    readonly property int titleBarSparkWidth: n("titleBarSparkWidth", 72)
    readonly property int titleBarSparkHeight: n("titleBarSparkHeight", 12)

    // ---- the frame's form: the status line (board 46 `statusLine()`, `.status`) ----------
    /// Its height: the line it replaced (the footer), one small line with room around it.
    readonly property int statusLineHeight: Math.round(font.baseSize * 2.2)
    /// Its type: the small size (board 46's 11.8 px at 13 px type); the server's name weighs
    /// a little more.
    readonly property int statusLineFontSize: font.small
    readonly property int statusLineNameWeight: Font.Medium
    /// Before its first part and after its last (board 46: 18 and 14 px at 13 px type).
    readonly property int statusLineInsetStart: n("statusLineInsetStart", 16)
    readonly property int statusLineInsetEnd: n("statusLineInsetEnd", 13)
    /// Between its parts (board 46's 12 px), and inside the server lane (dot, name, ▾: 7 px).
    readonly property int statusLineGap: n("statusLineGap", 11)
    readonly property int statusLineLaneGap: n("statusLineLaneGap", 6)
    /// The server's dot.
    readonly property int statusLineDotSize: n("statusLineDotSize", 7)
    /// How far the server lane's hover fill reaches past its words.
    readonly property int statusLineHoverInset: n("statusLineHoverInset", 3)
    /// The widths its steps start at, widest first (step 0 at the first, step 4 below the
    /// last): from step 1 the lane is the name alone and the alt-speed limits are short, from
    /// 2 the facts' words shorten, from 3 the speeds are short and the count loses its word,
    /// at 4 the downloading count and the free space go.
    readonly property var statusLineSteps: [space(880), space(740), space(600), space(460)]

    // ---- the frame's form: the action bar (board 51 D17 `.abar`, `.ab2`, DECISIONS C32) ----
    /// It floats over the list, bottom middle, `actionBarLift` above the list's foot (board 51
    /// `.fl51 { bottom: 12px }`), and the outcome strip `actionBarStripGap` above it.
    readonly property int actionBarLift: n("actionBarLift", 12)
    readonly property int actionBarStripGap: n("actionBarStripGap", 8)
    /// Round its buttons, inside its edge (`.abar { padding: 4px }`).
    readonly property int actionBarPadding: n("actionBarPadding", 4)
    /// A button's height and an icon button's width (`.ab2 { height: 30px }`, `.ab2.ico`).
    readonly property int actionBarButtonSize: n("actionBarButtonSize", 30)
    /// The whole bar: its buttons, its padding and its edge.
    readonly property int actionBarHeight: actionBarButtonSize + 2 * actionBarPadding + 2 * edgeWidth
    /// Between two of its parts (`.abar { gap: 1px }`), and a group's rule's room either side
    /// (`.sp { margin: 3px 4px }`).
    readonly property int actionBarGap: n("actionBarGap", 1)
    readonly property int actionBarRuleInset: n("actionBarRuleInset", 4)
    /// A word button: 9 px either side (`.ab2 { padding: 0 9px }`), the status line's type.
    readonly property int actionBarWordPadding: n("actionBarWordPadding", 9)
    readonly property int actionBarFontSize: Math.min(statusLineFontSize, controlFontSize)
    /// The count's room (`.cnt { padding: 0 12px 0 8px }`).
    readonly property int actionBarCountStart: n("actionBarCountStart", 8)
    readonly property int actionBarCountEnd: n("actionBarCountEnd", 12)
    /// Its ground, the strip's: the window's, or the panel's on a skin that plates its blocks
    /// (DECISIONS C30 F1: its words and buttons take their colours from it).
    readonly property ThemeInk actionBarGround: platedBlocks ? on.panel : on.window

    // ---- the sheets' form (board 46 `.sheet`, `.shh`, `.shb`, `.shf`, DECISIONS C24 H7) ------
    /// From the window's edge to a sheet that would be wider than the window.
    readonly property int sheetMargin: n("sheetMargin", 16)
    /// Its edge (`.sheet`: 1 px of the accent) and its corners (the kit's).
    readonly property int sheetEdge: edgeWidth
    readonly property int sheetRadius: cornerRadius
    /// The head, the rows and the foot share one inset at the sides (16 px); the head has 12 px
    /// over and under it, the rows 12 px, the foot 10 px (`.shh`, `.shb`, `.shf`).
    readonly property int sheetInset: n("sheetInset", 16)
    readonly property int sheetHeadPadding: n("sheetHeadPadding", 12)
    readonly property int sheetBodyPadding: n("sheetBodyPadding", 12)
    readonly property int sheetFootPadding: n("sheetFootPadding", 10)
    /// Between the head's parts (10 px), the rows (12 px), and the foot's buttons (10 px).
    readonly property int sheetHeadGap: n("sheetHeadGap", 10)
    readonly property int sheetRowGap: n("sheetRowGap", 12)
    readonly property int sheetButtonGap: n("sheetButtonGap", 8)
    /// The title (14 px, 600, bright) and the dim words beside or under it (11.5 px).
    readonly property int sheetTitleSize: font.body
    readonly property int sheetTitleWeight: titleWeight
    readonly property int sheetSubtitleSize: font.small
    /// The foot's note at its left (11.5 px, dim) and the lines it may take.
    readonly property int sheetNoteSize: font.small
    readonly property int sheetNoteLines: 2
    /// A foot button: the room either side of its words (`.btn`: 12 px), and the least width
    /// it takes, so "ok" is still a target.
    readonly property int sheetButtonPadding: n("sheetButtonPadding", 12)
    readonly property int sheetButtonMinWidth: n("sheetButtonMinWidth", 64)
    /// The primary is filled with the accent, its words in the window's ground (`.btn.pri`);
    /// false draws the kit's accent outline.
    readonly property bool sheetPrimaryFilled: true
    /// The head's bordered glyph square (spec v2 `.hd .ic`): board 46 draws none.
    readonly property bool sheetHeadGlyph: false

    // ---- the menus' form (board 46 `.menu .mh`, DECISIONS C24 H9) -------------------------
    /// A menu's head: its inset at the sides (12 px), the room over and under it (4 px), the
    /// gap between the bright title's line and the dim words under it, and the title's weight.
    readonly property int menuHeadInset: n("menuHeadInset", 14)
    readonly property int menuHeadPadding: n("menuHeadPadding", 4)
    readonly property int menuHeadLineGap: n("menuHeadLineGap", 1)
    readonly property int menuHeadTitleWeight: titleWeight

    // ---- the frame's form: the app menu (board 46 `menuHtml("app")`, DECISIONS C24 H9) ---
    /// Its width: board 51 D14 b's 292, wider than the rail it grows from (DECISIONS C32).
    readonly property int appMenuWidth: n("appMenuWidth", 292)

    // ---- the frame's form: keys on demand (board 46 `.kb`, DECISIONS C24 H10) ------------
    /// A key's badge while alt is held: its height, the room either side of the key, its type
    /// (board 46: a 14 px line, 4 px each side, 10 px bold at 13 px type) and its corners
    /// (board 46: square).
    readonly property int keyBadgeHeight: n("keyBadgeHeight", 14)
    readonly property int keyBadgePadding: n("keyBadgePadding", 4)
    readonly property int keyBadgeFontSize: font.caption
    readonly property int keyBadgeWeight: Font.Bold
    readonly property int keyBadgeRadius: 0
    /// How far it hangs past its control's top-right corner (board 46: 6 px right, 9 px up).
    readonly property int keyBadgeOverhangX: n("keyBadgeOverhangX", 6)
    readonly property int keyBadgeOverhangY: n("keyBadgeOverhangY", 9)
    /// Placed inside a control rather than over its corner (a rail row, the mark): the gap to
    /// the row's count, and the room to an edge where nothing else is (the folded rail).
    readonly property int keyBadgeRowGap: n("keyBadgeRowGap", 6)
    readonly property int keyBadgeEdge: n("keyBadgeEdge", 3)
    /// How long alt is held alone before the badges show, in ms: the alt of alt+j never shows
    /// them, even for a frame.
    readonly property int keyBadgeDelay: 200

    // ---- the frame's form: the bands under the title (board 46 `bands()`, `.notice`) -------
    /// From the title bar's rule and the content's sides to a band's plate (board 46: 10, 18
    /// and 14 px); Omarchy's band runs edge to edge under the rule.
    readonly property int noticeBandInsetTop: n("noticeBandInsetTop", 0)
    readonly property int noticeBandInsetStart: n("noticeBandInsetStart", 0)
    readonly property int noticeBandInsetEnd: n("noticeBandInsetEnd", 0)
    /// A notice's ground: its kind's colour at this alpha over the window (`.notice .pi`'s
    /// 9 %); 0 keeps the idle fill under every kind.
    readonly property real noticeTint: token("tint", "notice", 0)
    /// A plated block's ground (board 46 `.blk .pi`): its kind's colour at this alpha over the
    /// panel.
    readonly property real blockTint: token("tint", "block", 0)

    // ---- the frame's form: the outcome strip (board 46 `.ostrip`, board 50 §2) -------------
    /// How far over the rows' bottom it floats (board 46's 12 px): with a selection on, the
    /// action bar is just under that edge.
    readonly property int outcomeStripLift: n("outcomeStripLift", 12)
    /// Between its words and the dim line under them that says why "show" is there
    /// (DECISIONS C30 F10).
    readonly property int outcomeStripReasonGap: n("outcomeStripReasonGap", 1)

    // ---- the library's form: the rows display (board 46 `rows()`, `.row`, `.gh`, C24 H3) --
    /// A row: two lines, the name's and the sentence's (board 46: 50 px at 13 px type).
    readonly property int libraryRowHeight: n("libraryRowHeight", 46)
    /// Before the state glyph and after the fact (board 46: 14 and 18 px).
    readonly property int libraryRowInsetStart: n("libraryRowInsetStart", 13)
    readonly property int libraryRowInsetEnd: n("libraryRowInsetEnd", 16)
    /// The state glyph's column (20 px), and the gap between the columns (10 px).
    readonly property int libraryRowGlyphWidth: n("libraryRowGlyphWidth", 18)
    readonly property int libraryRowGap: n("libraryRowGap", 9)
    /// Between the name's line and the sentence's.
    readonly property int libraryRowLineGap: n("libraryRowLineGap", 1)
    /// The name (13.5 px, 600, bright), the meta beside it and the sentence (11.5 px, dim).
    readonly property int libraryRowNameSize: font.body
    readonly property int libraryRowNameWeight: Font.DemiBold
    readonly property int libraryRowMetaSize: font.small
    readonly property int libraryRowSentenceSize: font.small
    /// The fact (14 px, 500, bright) over its caption (10 px, upper case, tracked .12em), in a
    /// column at least 92 px wide.
    readonly property int libraryRowFactSize: font[token("label", "rowFactSize", "body")] ?? font.body
    readonly property int libraryRowFactWeight: Font.Medium
    readonly property int libraryRowCaptionSize: font.caption
    readonly property real libraryRowCaptionTracking: font.caption * captionTrack(0.12)
    readonly property int libraryRowCaptionWeight: captionWeight(Font.Normal)
    readonly property int libraryRowFactWidth: n("libraryRowFactWidth", 85)
    /// #201 F13: the room a name keeps before the fact however narrow the list; the fact
    /// shortens, then its caption drops, first.
    readonly property int libraryRowNameFloor: n("libraryRowNameFloor", 80)
    /// #201 F13: the table's name keeps as much before its category and "private".
    readonly property int tableNameFloor: n("tableNameFloor", 80)
    /// Under this, a row's sentence or fact draws nothing rather than a lone "…".
    readonly property int libraryRowWordsFloor: n("libraryRowWordsFloor", 24)
    /// The progress line before the sentence (board 46 `.strip`: 168 × 4 px on an 8 % track).
    readonly property int libraryRowLineWidth: n("libraryRowLineWidth", 155)
    readonly property int libraryRowLineThickness: n("libraryRowLineThickness", 4)
    readonly property real libraryRowLineTrack: fillHover
    /// The fix (`.fix`): its room either side and its border's accent alpha.
    readonly property int libraryRowFixPadding: n("libraryRowFixPadding", 6)
    readonly property real libraryRowFixBorderAlpha: 0.45
    /// The fix as a filled tag (Console's `.row .fix`: the accent's ground, the window's
    /// colour, the caption voice) rather than words in an accent outline.
    readonly property bool libraryRowFixTag: token("label", "fixTag", false)
    /// A selected row's edge (`.row.picked`, an inset 2 px accent rule).
    readonly property int libraryRowSelectedEdge: n("libraryRowSelectedEdge", 2)
    /// A group's head (`.gh`): its height, its inset, its words (10.5 px, upper case, tracked
    /// .12em; the count in the foreground; the line 11.5 px at the right).
    readonly property int libraryGroupHeadHeight: n("libraryGroupHeadHeight", 30)
    readonly property int libraryGroupHeadInset: n("libraryGroupHeadInset", 16)
    readonly property int libraryGroupHeadPaddingBottom: n("libraryGroupHeadPaddingBottom", 4)
    readonly property int libraryGroupHeadSize: font.caption
    readonly property real libraryGroupHeadTracking: font.caption * token("label", "track", 0.12)
    readonly property int libraryGroupHeadWeight: token("label", "weight", Font.Normal)
    readonly property int libraryGroupLineSize: font.small
    /// The bar before a group's words (Console's `.gh::before`: 3 × 11 px, 8 px in, the hair,
    /// the accent for needs you); 0 wide draws none (Omarchy's).
    readonly property int libraryGroupHeadBar: n("libraryGroupHeadBar", 0)
    readonly property int libraryGroupHeadBarHeight: n("libraryGroupHeadBarHeight", 11)
    readonly property int libraryGroupHeadBarInset: n("libraryGroupHeadBarInset", 8)
    /// The widths its steps start at, widest first: from step 1 a group's line goes and the
    /// progress line shortens, from step 2 the meta beside the name goes too.
    readonly property var libraryRowsSteps: [space(620), space(440)]
    readonly property int libraryRowLineWidthNarrow: n("libraryRowLineWidthNarrow", 90)
    /// The inline rename's field over a row's name (the table's `.ren`): the room inside it
    /// and how far it rises past the name's line above and below.
    readonly property int libraryRenamePadding: n("libraryRenamePadding", 6)
    readonly property int libraryRenameRise: n("libraryRenameRise", 3)
    /// From a row's start to where `enter`'s menu opens under it (the table's 24 u).
    readonly property int libraryMenuInset: n("libraryMenuInset", 24)

    /// The display tool's menu: its width.
    readonly property int displayMenuWidth: n("displayMenuWidth", 240)

    // ---- create's form (board 49 `publicSheet()`, `.chk`, `.fchip`, `.rtnote`, C27) -------
    /// The create sheet's width (board 49's 680, the Move sheet's folder width here).
    readonly property int createSheetWidth: n("createSheetWidth", 640)
    /// A fact's mark column (`.chk`: 16 px) and the gap after it (8 px).
    readonly property int createFactMark: n("createFactMark", 16)
    readonly property int createFactGap: n("createFactGap", 8)
    /// How far past its words a fact's link takes a click.
    readonly property int createLinkReach: n("createLinkReach", 4)
    /// The round-trip note's padding, the gap after its mark, its edge and its corners
    /// (`.rtnote`: 7 / 12 px, 10, a 12 % hairline).
    readonly property int createNotePadding: n("createNotePadding", 9)
    readonly property int createNoteGap: n("createNoteGap", 10)
    readonly property int createNoteEdge: hairWidth
    readonly property int createNoteRadius: cornerRadius
    /// Between the trackers' chips (6 px), between format and pieces side by side (`.frow2`:
    /// 18 px), and between the private facts (`.facts2`: 6 px).
    readonly property int createChipsGap: n("createChipsGap", 6)
    readonly property int createPairGap: n("createPairGap", 16)
    readonly property int createFactsGap: n("createFactsGap", 6)
    /// The saved .torrent's chip (`.fchip`: 3 / 10 px, 8 px between its parts, a 1 px green
    /// edge), and how far it sits under the band's line.
    readonly property int createChipPaddingX: n("createChipPaddingX", 10)
    readonly property int createChipPaddingY: n("createChipPaddingY", 3)
    readonly property int createChipGap: n("createChipGap", 8)
    readonly property int createChipEdge: hairWidth
    readonly property int createChipRadius: cornerRadius
    readonly property int createChipTop: n("createChipTop", 4)
    /// The "+ add ▾" menu's width (board 49's 356 px).
    readonly property int addMenuWidth: n("addMenuWidth", 330)

    // ---- search's form (board 48 `search()`, `.sbar`, `.sline`, `.pill`, `.empty`, C26) ----
    /// The bar over the results (`.sbar`: 12 / 18 / 8 px, 10 px between its parts) and the
    /// field's least width (200 px).
    readonly property int searchBarInset: n("searchBarInset", 16)
    readonly property int searchBarInsetTop: n("searchBarInsetTop", 11)
    readonly property int searchBarInsetBottom: n("searchBarInsetBottom", 7)
    readonly property int searchBarGap: n("searchBarGap", 9)
    readonly property int searchFieldMinWidth: n("searchFieldMinWidth", 185)
    readonly property int searchFieldNarrowWidth: n("searchFieldNarrowWidth", 110)
    /// The plugins' line under it (`.sline`: 11.5 px, 12 px between its parts, 6 px under).
    readonly property int searchLineSize: font.small
    readonly property int searchLineGap: n("searchLineGap", 11)
    readonly property int searchLineInsetBottom: n("searchLineInsetBottom", 6)
    /// A plugin's pill (`.pill`): its dot, the room after it, and its least width (90 px).
    readonly property int searchPillDot: n("searchPillDot", 6)
    readonly property int searchPillGap: n("searchPillGap", 6)
    readonly property int searchPillMinWidth: n("searchPillMinWidth", 84)
    /// A plugin still asked pulses its dot (`pulse48`, 1.3 s a beat), down to this opacity.
    readonly property int searchPulseDuration: 1300
    readonly property real searchPulseLow: 0.35
    /// The lines between the bar and the rows (`.newabove`, `.hid`: 11 and 11.5 px).
    readonly property int searchNoteSize: font.caption
    readonly property int searchNoteInset: n("searchNoteInset", 4)
    /// The empty states (`.empty`: at most 440 px wide, 40 px down, 8 px between the lines,
    /// the title 14 px bright, the place 11 px).
    readonly property int searchEmptyWidth: n("searchEmptyWidth", 410)
    readonly property int searchEmptyTop: n("searchEmptyTop", 37)
    readonly property int searchEmptyGap: n("searchEmptyGap", 7)
    readonly property int searchEmptyTitleSize: font.heading
    readonly property int searchEmptyWordsSize: font.body
    /// The result on the right (`.det`, `.dhero`: the fact 22 px over its caption, 16 px in).
    readonly property int searchPaneWidth: n("searchPaneWidth", 380)
    readonly property int searchPaneInset: n("searchPaneInset", 16)
    readonly property int searchPaneGap: n("searchPaneGap", 9)
    readonly property int searchPaneFactSize: font.heading
    /// A list kept while its server isn't answering (Fig. 23's 45 %): search's rows too.
    readonly property real staleOpacity: 0.45
    /// Below this window width the bar's three filters fold into "filters ▾" (SR6's 720 px).
    readonly property int searchFoldWidth: n("searchFoldWidth", 720)
    /// The plugins' answers (`.menu.pop`, 392 px) and the bar's menus.
    readonly property int searchAnswersWidth: n("searchAnswersWidth", 392)
    readonly property int searchMenuWidth: n("searchMenuWidth", 240)
    /// Settings › qBittorrent › Search plugins (`.plg`, `.inst`): a plugin's row, the room
    /// inside it, its version's column and the gap between the install field's parts.
    readonly property int searchPluginRowHeight: n("searchPluginRowHeight", 36)
    readonly property int searchPluginInset: n("searchPluginInset", 10)
    readonly property int searchPluginVersionWidth: n("searchPluginVersionWidth", 40)
    readonly property int searchPluginNameWidth: n("searchPluginNameWidth", 110)
    readonly property int searchPluginGap: n("searchPluginGap", 9)

    // ---- the library's form: the overview's shelves (board 46 `overview()`, `.shelf`) -----
    /// The shelves' inset (6 / 18 px) and the room between two shelves (14 px).
    readonly property int shelfInset: n("shelfInset", 16)
    readonly property int shelfInsetTop: n("shelfInsetTop", 6)
    readonly property int shelfGap: n("shelfGap", 13)
    /// A shelf's head (`.shelf h5`): the name (13 px, 600, bright), its count and size (11 px,
    /// dim, upper case, tracked), and the room under it.
    readonly property int shelfNameSize: font.body
    readonly property int shelfNameWeight: Font.DemiBold
    readonly property int shelfLineSize: font.caption
    readonly property real shelfLineTracking: font.caption * 0.12
    readonly property int shelfHeadGap: n("shelfHeadGap", 6)
    /// A cover (`.cover`, 92 px wide, its art 118 px tall) and the room between covers.
    readonly property int coverWidth: n("coverWidth", 85)
    readonly property int coverArtHeight: n("coverArtHeight", 109)
    readonly property int coverGap: n("coverGap", 9)
    /// The piece grid inside the art (`.grid`: 6 px in, 10 cells across, 6 px tall, 1 px
    /// apart), at 55 % of the state colour over an 8 % cell.
    readonly property int coverGridInset: n("coverGridInset", 6)
    readonly property int coverGridColumns: 10
    readonly property int coverGridRows: 6
    readonly property int coverCellHeight: n("coverCellHeight", 6)
    readonly property int coverCellGap: 1
    readonly property real coverCellAlpha: 0.55
    readonly property real coverCellTrack: fillHover
    /// The two letters (`.ab2`: 30 px, 700, bright, 8 px in from the art's bottom left).
    readonly property int coverMonogramSize: n("coverMonogramSize", 28)
    readonly property int coverMonogramWeight: Font.Bold
    readonly property int coverMonogramInset: n("coverMonogramInset", 7)
    /// The name and the fact under the art (11 and 10.5 px), and the room between the parts.
    readonly property int coverNameSize: font.caption
    readonly property int coverFactSize: font.caption
    readonly property int coverLineGap: n("coverLineGap", 4)
    /// A selected cover's border, and the cursor's (Console: the plate's 1 px edge in the
    /// accent, board 46 `.card.sel`).
    readonly property int coverSelectedBorder: token("line", "card", litWidth)
    /// A cover that needs you takes that edge in the accent, and its fact the accent (board 46
    /// `.card.needs`); Omarchy's says it by the fact's red alone.
    readonly property bool coverNeedsEdge: token("line", "needsEdge", false)

    // ---- the details' form: the story (board 46 `details()`, `.dhero`, `.tl`, `.tb h5`) -------
    /// The head's one fact (`.dhero .big`: 22 px at 13 px type, 500, in the state's colour), its
    /// caption beside it (12 px, dim) and the sentence under them (12 px, the foreground).
    readonly property int detailsFactSize: font.heading
    readonly property int detailsFactWeight: Font.Medium
    readonly property int detailsCaptionSize: font.small
    readonly property int detailsSentenceSize: font.small
    /// The sentence's lines before it elides.
    readonly property int detailsSentenceLines: 2
    /// Around the fact and the sentence (`.dhero`: 10 px over, 16 px in, 12 px under), between
    /// them (3 px), and from the fact to its caption (8 px).
    readonly property int detailsHeroInset: n("detailsHeroInset", 16)
    readonly property int detailsHeroPaddingTop: n("detailsHeroPaddingTop", 8)
    readonly property int detailsHeroPaddingBottom: n("detailsHeroPaddingBottom", 12)
    readonly property int detailsHeroGap: n("detailsHeroGap", 3)
    /// Round the head's plate where the skin draws one (`.dhero { margin: 12px 14px 4px }`).
    readonly property int detailsHeroMarginTop: n("detailsHeroMarginTop", 0)
    readonly property int detailsHeroMarginSide: n("detailsHeroMarginSide", 0)
    readonly property int detailsHeroMarginBottom: n("detailsHeroMarginBottom", 0)
    readonly property int detailsCaptionGap: n("detailsCaptionGap", 8)
    /// A tab's body (`.tb`: 12 px over, 16 px in, 12 px between its blocks).
    readonly property int detailsBodyInset: n("detailsBodyInset", 16)
    readonly property int detailsBodyPaddingTop: n("detailsBodyPaddingTop", 12)
    readonly property int detailsBodyGap: n("detailsBodyGap", 12)
    /// A block's heading (`.tb h5`: 10.5 px, upper case, tracked, dim) and the room under it.
    readonly property int detailsHeadingSize: font.caption
    readonly property real detailsHeadingTracking: font.caption * 0.12
    readonly property int detailsHeadingGap: n("detailsHeadingGap", 6)
    /// The story's timeline (`.tl`): the time's column (42 px), the marker's (10 px) with its
    /// square (7 px, 6 px down), the room between columns (8 px) and between lines (7 px); the
    /// event (12 px, the foreground) over its dim line (11 px).
    readonly property int detailsEventTimeWidth: n("detailsEventTimeWidth", 70)
    readonly property int detailsEventMarkWidth: n("detailsEventMarkWidth", 10)
    readonly property int detailsEventMarkSize: n("detailsEventMarkSize", 7)
    readonly property int detailsEventMarkTop: n("detailsEventMarkTop", 5)
    readonly property int detailsEventGap: n("detailsEventGap", 8)
    readonly property int detailsEventSpacing: n("detailsEventSpacing", 7)
    readonly property int detailsEventTitleSize: font.small
    readonly property int detailsEventDetailSize: font.caption
}
