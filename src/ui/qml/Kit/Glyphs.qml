pragma Singleton
import QtQuick

// The Nerd Font / Unicode glyph choices for every icon the spec v2 uses as a stand-in (spec
// §1–7, plan §7: "pick the Nerd Font equivalents once in Kit/Glyphs.qml"). The spec's own demo
// script (docs/05-ui-spec-v2.html) already renders these as plain Unicode symbols in
// "JetBrains Mono"/"JetBrainsMono Nerd Font", so the same code points are reused here rather
// than reaching into the Nerd Font private-use area — they render identically and keep the
// kit's glyphs visually identical to the approved spec. Every property is a single glyph,
// exposed by name so a Kit component never carries a literal symbol.
QtObject {
    // ---- identity ------------------------------------------------------------------
    readonly property string mark: "◆" // hero mark, spec §1 Fig. 1 `.mark`

    // ---- speeds --------------------------------------------------------------------
    readonly property string arrowDown: "↓" // download stat, spec §1 `.stat.d .v i`
    readonly property string arrowUp: "↑" // upload stat, spec §1 `.stat.u .v i`

    // ---- hero actions, spec §1 `.acts` ----------------------------------------------
    readonly property string add: "+"
    readonly property string altSpeed: "⏷" // spec §1 `.btn.icon` alt-speed
    readonly property string settings: "⚙"
    readonly property string help: "?"
    readonly property string search: "/" // spec §1 `.field.search .dim`
    readonly property string folder: "▤" // spec Fig. 13 category glyph

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
    readonly property string statusChecking: "⟳"
    readonly property string statusErrored: "!"
    readonly property string statusQueued: "≡"
    readonly property string category: "▤"
    readonly property string tag: "#"
    readonly property string tracker: "◦"

    // ---- generic chrome ---------------------------------------------------------------
    readonly property string chevronRight: "›"
    readonly property string chevronDown: "▾"
    readonly property string chevronUp:
        "▴" // table header sort-ascending, spec §2 `.tbl > div.h.sort`
    readonly property string check: "✓"
    readonly property string middot: "·" // unchecked row of a multi-select palette, spec §4
    readonly property string close: "✕"
    readonly property string dot: "●"
}
