pragma Singleton
import QtQuick
import Omaseed.Kit

// A value written "icon:<name>" is not a character: `Glyph.qml` draws it as the SVG `Icon` of
// that name (docs/08 §2, option D). Those are the symbols the UI font does not carry, which
// fontconfig would otherwise hand to another font (for ⚙ and ⌨, the colour emoji font).
// Everything else stays a typographic symbol, drawn as text.
//
// The Nerd Font / Unicode glyph choices for every icon the spec v2 uses as a stand-in (spec
// §1–7, plan §7: "pick the Nerd Font equivalents once in Kit/Glyphs.qml"). The spec's own demo
// script (docs/05-ui-spec-v2.html) already renders these as plain Unicode symbols in
// "JetBrains Mono"/"JetBrainsMono Nerd Font", so the same code points are reused here rather
// than reaching into the Nerd Font private-use area — they render identically and keep the
// kit's glyphs visually identical to the approved spec. Every property is a single glyph,
// exposed by name so a Kit component never carries a literal symbol.
//
// Each name goes through the skin's glyph table first (#171, the design note's `glyphs`): the
// value here is Omarchy's, which a skin may replace. A skin's "ms:<ligature>" draws in its
// icon font (`Glyph.qml`), and only while that font is installed; without it the name is
// Omarchy's again, so a skin never draws tofu or a ligature's name.
QtObject {
    /// `name`'s glyph in the skin drawn, else `omarchy`.
    function g(name: string, omarchy: string): string {
        const glyph = Theme.token("glyphs", name, "");
        if (glyph === "" || (glyph.startsWith("ms:") && Theme.iconsFamily === ""))
            return omarchy;
        return glyph;
    }
    /// The same for a name its callers write into words or a bare `Text` ("▾ 3 below"), which
    /// only a character can be: a skin's "ms:" or "icon:" for it is Omarchy's until those
    /// callers draw it in a `Glyph`.
    function t(name: string, omarchy: string): string {
        const glyph = Theme.token("glyphs", name, "");
        return glyph === "" || glyph.startsWith("ms:") || glyph.startsWith("icon:") ? omarchy
                                                                                      : glyph;
    }

    // ---- identity ------------------------------------------------------------------
    readonly property string mark: g("mark", "icon:mark") // hero mark, spec §1 Fig. 1 `.mark`

    // ---- speeds --------------------------------------------------------------------
    readonly property string arrowDown: t("arrowDown", "↓") // download stat, spec §1 `.stat.d .v i`
    readonly property string arrowUp: t("arrowUp", "↑") // upload stat, spec §1 `.stat.u .v i`

    // ---- hero actions, spec §1 `.acts` ----------------------------------------------
    readonly property string add: g("add", "+")
    // spec §1 `.btn.icon` alt-speed
    readonly property string altSpeed: g("altSpeed", "icon:altSpeed")
    // docs/31 D4: the same gauge with its needle up to the right, for a burst set.
    readonly property string burst: g("burst", "icon:burst")
    readonly property string settings: g("settings", "icon:gear")
    readonly property string help: g("help", "?")
    // docs/30: the command palette's query row and ⋯ row
    readonly property string commands: g("commands", ":")
    readonly property string search: g("search", "/") // spec §1 `.field.search .dim`
    readonly property string jump: g("jump", "icon:jump") // ⌕, spec §4 Fig. 12 `.palette .q .g`
    readonly property string folder: g("folder", "icon:folder") // spec Fig. 13 category glyph

    // ---- tray states, spec §6 Fig. 17 -------------------------------------------------
    readonly property string trayIdle: g("trayIdle", "◆")
    readonly property string trayDownloading: g("trayDownloading", "↓")
    readonly property string trayAltSpeed: g("trayAltSpeed", "⏷")
    readonly property string trayError: g("trayError", "!")
    readonly property string trayDisconnected: g("trayDisconnected", "◌")

    // ---- rail status glyphs, spec §9 delta script `RAIL` -----------------------------
    readonly property string statusAll: g("statusAll", "◆")
    readonly property string statusDownloading: g("statusDownloading", "↓")
    readonly property string statusSeeding: g("statusSeeding", "↑")
    readonly property string statusCompleted: g("statusCompleted", "✓")
    readonly property string statusStopped: g("statusStopped", "■")
    readonly property string statusActive: g("statusActive", "●")
    readonly property string statusStalled: t("statusStalled", "◌")
    readonly property string statusChecking: g("statusChecking", "icon:checking")
    readonly property string statusErrored: t("statusErrored", "!")
    readonly property string statusQueued: g("statusQueued", "≡")
    // ---- the rail's views and five questions, board 46 `rail()` (DECISIONS C24 H2) -------
    readonly property string viewLibrary: g("viewLibrary", "≡")
    readonly property string viewSeed: g("viewSeed", "↑")
    readonly property string viewLog: g("viewLog", "~")
    /// #145 (board 48 `.q2`): the search view, in the rail and its field.
    readonly property string viewSearch: g("viewSearch", "⌕")
    /// A search result that is a link to a .torrent (board 48's ⋯), and one that is a magnet (◇).
    readonly property string searchLink: t("searchLink", "⋯")
    readonly property string searchMagnet: g("searchMagnet", "◇")
    // DECISIONS C24 H3–H4: the library's displays (board 46's display tool).
    readonly property string displayRows: g("displayRows", "≡")
    readonly property string displayTable: g("displayTable", "▤")
    readonly property string displayShelves: g("displayShelves", "▦")
    readonly property string questionNeedsYou: g("questionNeedsYou", "!")
    readonly property string questionArriving: g("questionArriving", "↓")
    readonly property string questionLanded: g("questionLanded", "✓")
    readonly property string questionGivingBack: g("questionGivingBack", "↑")
    readonly property string questionResting: g("questionResting", "■")
    /// The folded rail's one row for its categories, folders, tags and trackers.
    readonly property string railGroups: g("railGroups", "▤")
    readonly property string category: g("category", "icon:folder")
    readonly property string tag: g("tag", "#")
    readonly property string tracker: g("tracker", "◦")

    // ---- generic chrome ---------------------------------------------------------------
    readonly property string chevronRight: t("chevronRight", "›")
    // docs/17 M10.4: the folded right dock
    readonly property string chevronLeft: t("chevronLeft", "‹")
    readonly property string chevronDown: t("chevronDown", "▾")
    // table header sort-ascending, spec §2 `.tbl > div.h.sort`
    readonly property string chevronUp: t("chevronUp", "▴")
    readonly property string check: t("check", "✓")
    // unchecked row of a multi-select palette, spec §4
    readonly property string middot: t("middot", "·")
    readonly property string close: t("close", "✕")
    readonly property string rename: t("rename", "✎") // the add sheet's file tree (manual box M18)
    readonly property string dot: g("dot", "●")
    // docs/29 (#44): the add sheet's Files row while a magnet's list is looked up
    // F1 fetching, F7 waiting for the server, F8 a link
    readonly property string lookup: g("lookup", "⋯")
    readonly property string lookupSlow: g("lookupSlow", "◷") // F3 still looking
    readonly property string lookupNever: g("lookupNever", "!") // F4 may never come
    // F5 a private tracker's magnet
    readonly property string privateTracker: g("privateTracker", "◆")

    // ---- settings rows, batch-2 J8: the rows that had only a typographic dot -----------
    readonly property string scrollHint: g("scrollHint", "icon:scroll") // Appearance › scroll hint
    readonly property string host: g("host", "icon:globe") // Appearance › tracker hosts

    // ---- settings nav, spec §5 Fig. 16 `.settings .nav .it .g` ------------------------
    // also the server sheet's head, Fig. 9
    readonly property string server: g("server", "icon:server")
    readonly property string notifications: g("notifications", "◔")
    readonly property string keyboard: g("keyboard", "icon:keyboard")
    readonly property string appearance: g("appearance", "icon:appearance")
    readonly property string backup: g("backup", "icon:backup") // #31 Settings › Backup
    readonly property string about: t("about", "i")
    // docs/34 D6 a: copy settings from another server
    readonly property string copyFrom: g("copyFrom", "⇣")
    readonly property string home: g("home", "⌂") // Fig. 16's path-mapping row, Fig. 7's head
    readonly property string plus: g("plus", "+")

    // ---- the selection bar, docs/17 M9.3 (limits and more are the ⋯ menu's below) ----
    readonly property string start: g("start", "icon:start")
    readonly property string stop: g("stop", "icon:stop")
    readonly property string recheck: g("recheck", "icon:checking")
    readonly property string reannounce: g("reannounce", "icon:reannounce")
    readonly property string location: g("location", "icon:location")
    readonly property string openFolder: g("openFolder", "icon:openFolder")
    readonly property string trash: g("trash", "icon:delete")

    // ---- the app menu's window block, board 46 `menuHtml("app")` (DECISIONS C24 H9) -----
    readonly property string hideWindow: g("hideWindow", "–")
    readonly property string quit: g("quit", "×")

    // ---- hero ⋯ menu, docs/08 §3 -------------------------------------------------------
    readonly property string more: g("more", "icon:more")
    readonly property string columns: g("columns", "icon:columns")
    readonly property string sidebar: g("sidebar", "icon:sidebar")
    readonly property string limits: g("limits", "icon:limits")
    readonly property string selectAll: g("selectAll", "icon:selectAll")

    // ---- create a torrent, board 49 (DECISIONS C27) -------------------------------------
    // + add ▾'s row, the palette's, the band's mark
    readonly property string create: g("create", "◇")
    readonly property string roundTrip: t("roundTrip", "↺") // the private sheet's round-trip note
    readonly property string dragHandle: t("dragHandle", "⠿") // the saved .torrent's chip, to drag
    // a fact that holds (private flag on, v1, source)
    readonly property string factOk: t("factOk", "✓")
    // a fact said for the record (no source tag)
    readonly property string factInfo: g("factInfo", "i")
    readonly property string factWarn: t("factWarn", "!") // a fact to look at (no source tag yet)
}
