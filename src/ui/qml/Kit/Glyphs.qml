pragma Singleton
import QtQuick

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
QtObject {
    // ---- identity ------------------------------------------------------------------
    readonly property string mark: "icon:mark" // hero mark, spec §1 Fig. 1 `.mark`

    // ---- speeds --------------------------------------------------------------------
    readonly property string arrowDown: "↓" // download stat, spec §1 `.stat.d .v i`
    readonly property string arrowUp: "↑" // upload stat, spec §1 `.stat.u .v i`

    // ---- hero actions, spec §1 `.acts` ----------------------------------------------
    readonly property string add: "+"
    readonly property string altSpeed: "icon:altSpeed" // spec §1 `.btn.icon` alt-speed
    // docs/31 D4: the same gauge with its needle up to the right, for a burst set.
    readonly property string burst: "icon:burst"
    readonly property string settings: "icon:gear"
    readonly property string help: "?"
    readonly property string commands: ":" // docs/30: the command palette's query row and ⋯ row
    readonly property string search: "/" // spec §1 `.field.search .dim`
    readonly property string jump: "icon:jump" // ⌕, spec §4 Fig. 12 `.palette .q .g`
    readonly property string folder: "icon:folder" // spec Fig. 13 category glyph

    // ---- tray states, spec §6 Fig. 17 -------------------------------------------------
    readonly property string trayIdle: "◆"
    readonly property string trayDownloading: "↓"
    readonly property string trayAltSpeed: "⏷"
    readonly property string trayError: "!"
    readonly property string trayDisconnected: "◌"

    // ---- rail status glyphs, spec §9 delta script `RAIL` -----------------------------
    readonly property string statusAll: "◆"
    readonly property string statusDownloading: "↓"
    readonly property string statusSeeding: "↑"
    readonly property string statusCompleted: "✓"
    readonly property string statusStopped: "■"
    readonly property string statusActive: "●"
    readonly property string statusStalled: "◌"
    readonly property string statusChecking: "icon:checking"
    readonly property string statusErrored: "!"
    readonly property string statusQueued: "≡"
    readonly property string category: "icon:folder"
    readonly property string tag: "#"
    readonly property string tracker: "◦"

    // ---- generic chrome ---------------------------------------------------------------
    readonly property string chevronRight: "›"
    readonly property string chevronLeft: "‹" // docs/17 M10.4: the folded right dock
    readonly property string chevronDown: "▾"
    readonly property string chevronUp:
        "▴" // table header sort-ascending, spec §2 `.tbl > div.h.sort`
    readonly property string check: "✓"
    readonly property string middot: "·" // unchecked row of a multi-select palette, spec §4
    readonly property string close: "✕"
    readonly property string rename: "✎" // the add sheet's file tree (manual box M18)
    readonly property string dot: "●"
    // docs/29 (#44): the add sheet's Files row while a magnet's list is looked up
    readonly property string lookup: "⋯" // F1 fetching, F7 waiting for the server, F8 a link
    readonly property string lookupSlow: "◷" // F3 still looking
    readonly property string lookupNever: "!" // F4 may never come
    readonly property string privateTracker: "◆" // F5 a private tracker's magnet

    // ---- settings rows, batch-2 J8: the rows that had only a typographic dot -----------
    readonly property string scrollHint: "icon:scroll" // Appearance › scroll hint
    readonly property string host: "icon:globe" // Appearance › tracker hosts

    // ---- settings nav, spec §5 Fig. 16 `.settings .nav .it .g` ------------------------
    readonly property string server: "icon:server" // also the server sheet's head, Fig. 9
    readonly property string notifications: "◔"
    readonly property string keyboard: "icon:keyboard"
    readonly property string appearance: "icon:appearance"
    readonly property string backup: "icon:backup" // #31 Settings › Backup
    readonly property string about: "i"
    readonly property string home: "⌂" // Fig. 16's path-mapping row, Fig. 7's head
    readonly property string plus: "+"

    // ---- the selection bar, docs/17 M9.3 (limits and more are the ⋯ menu's below) ----
    readonly property string start: "icon:start"
    readonly property string stop: "icon:stop"
    readonly property string recheck: "icon:checking"
    readonly property string reannounce: "icon:reannounce"
    readonly property string location: "icon:location"
    readonly property string openFolder: "icon:openFolder"
    readonly property string trash: "icon:delete"

    // ---- hero ⋯ menu, docs/08 §3 -------------------------------------------------------
    readonly property string more: "icon:more"
    readonly property string columns: "icon:columns"
    readonly property string sidebar: "icon:sidebar"
    readonly property string limits: "icon:limits"
    readonly property string selectAll: "icon:selectAll"
}
