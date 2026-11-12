#!/usr/bin/env python3
"""Write docs/38-contrast-floor.html: each installed theme's text colours, before and after a
change to Theme's contrast floor, as omaseed itself resolves them. Or check every text role on
every ground it is painted on (docs/DECISIONS.md C30 F7).

    scripts/dev/contrast-board.py --before OLD_BIN --after NEW_BIN [--out FILE]
                                  [--before-label TEXT] [--after-label TEXT]
                                  [--themes-root DIR]...
    scripts/dev/contrast-board.py --check BIN [--themes-root DIR]... [--skins LIST]
                                  [--json OUT] [--quiet] [--strict-marks]
                                  [--marks-report FILE]
    scripts/dev/contrast-board.py --self-test

--check reads each theme's `grounds` (Theme.on: the window, every fill, every tint) from
`BIN --theme-roles --skin SKIN`, once per skin in --skins (default omarchy,console: #171, so a
skin's ground that fails in one theme fails the check), and measures each text role on each ground, plus the key badge's
background on accent and the fills nested in a block that keep their plain ground's colours
(#188, NESTED_KEPT); it prints the failing pairs and exits 1 when there is one. The nested
grounds (blockSelected, hoverPressed, …) are grounds like the rest, so every role is measured
on them too.

--check then measures every mark (MARKS: a line's fill, a dot, a box, an edge: no words) on
every ground it sits on in each skin, at WCAG 2.2 1.4.11's 3:1, its opacity composited over
the ground or over its track (#200). Marks keep the window's colours where those read at 3:1;
the ones painted in them on a darker ground are lifted there by `Theme.markOn` (C30, marks:
MARK_LIFT here), and the faint ones, at an opacity, are raised by `Theme.markAt` (MARK_AT).
--check reports a mark under 3:1 and exits 0 on it; --strict-marks (ctest's check) fails on it
too. --marks-report FILE writes them grouped by mark, worst first. --self-test checks
the mark pass on made-up roles. A build from
before C30 prints no grounds: the check then paints its window roles on each fill, as that
build did, which is board 50's "today".

For each theme (Omarchy's in /usr/share/omarchy/themes, then the user's in
~/.config/omarchy/themes, which win on a name clash) it runs `BIN --theme-roles` (#76) once
per build, offscreen, against a throwaway XDG_STATE_HOME whose omarchy/current/theme links to
the theme and an empty XDG_CONFIG_HOME, so the desktop's own theme and shell overlay stay out.
It never opens a window. The board shows every role that moved as before → after swatches on
the theme's window background, with both contrast ratios (WCAG 2), and how many stayed.

omaseed is run directly, never through a `python3` or mise shim: a shim reads its own state
from the XDG_STATE_HOME this script swaps out. Run the script itself with the real
interpreter if `python3` is a shim (`/usr/bin/python3 scripts/dev/contrast-board.py …`).
docs/DECISIONS.md Q19 has the rule; the board is its evidence, regenerated, never hand-edited.
"""

import argparse
import glob
import html
import json
import os
import statistics
import subprocess
import sys
import tempfile

FLOOR = 4.5  # Theme::kTextContrastFloor, docs/DECISIONS.md Q19
NON_TEXT = 3.0  # WCAG 1.4.11, a glyph or part that is not text

# The roles Q19 puts the floor under, in the order the board shows them.
TEXT_ROLES = ["dimForeground", "accent", "red", "yellow", "orange", "green", "cyan", "blue",
              "magenta"]
# Each state role and the palette role it reads (Theme.hpp).
STATE_ROLES = {"stateDownloading": "blue", "stateSeeding": "green",
               "stateStopped": "dimForeground", "stateError": "red", "stateChecking": "yellow",
               "stateQueued": "magenta", "stateStalled": "orange"}
# What each role paints, for the swatch's sample text.
SAMPLES = {"dimForeground": "caption", "accent": "link", "red": "✕ error", "yellow": "▲ check",
           "orange": "◌ stall", "green": "↑ seed", "cyan": "cyan", "blue": "↓ down",
           "magenta": "≡ queue"}

DEFAULT_ROOTS = ("/usr/share/omarchy/themes", "~/.config/omarchy/themes")


# ---- colour ------------------------------------------------------------------------------

def parse(hexstr):
    s = hexstr.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def hexof(c):
    return "#%02x%02x%02x" % tuple(int(round(v)) for v in c)


def luminance(c):
    def lin(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def over(top, under, alpha):
    """`top` at `alpha` over an opaque `under`, as Qt paints it."""
    return tuple(t * alpha + u * (1 - alpha) for t, u in zip(top, under))


def lift(colour, toward, against, minimum):
    """Theme::liftToContrast, for the board's what-if (lifting against a row fill) and the
    marks' lift (`lift_mark`)."""
    if ratio(colour, against) >= minimum:
        return colour
    if ratio(toward, against) < minimum:
        return toward
    for step in range(1, 256):
        t = step / 256
        cand = tuple(round(a + (b - a) * t) for a, b in zip(colour, toward))
        if ratio(cand, against) >= minimum:
            return cand
    return toward


# ---- the roles, as omaseed resolves them ---------------------------------------------------

def themes(roots):
    found = {}
    for root in (os.path.expanduser(r) for r in roots):
        for d in sorted(glob.glob(os.path.join(root, "*"))):
            if os.path.isfile(os.path.join(d, "colors.toml")):
                found[os.path.basename(d)] = d
    return dict(sorted(found.items()))


def roles_for(binary, theme_dir, name, skin=None):
    with tempfile.TemporaryDirectory(prefix="omaseed-contrast-board-") as tmp:
        cur = os.path.join(tmp, "state", "omarchy", "current")
        os.makedirs(cur)
        os.symlink(os.path.abspath(theme_dir), os.path.join(cur, "theme"))
        with open(os.path.join(cur, "theme.name"), "w", encoding="utf-8") as f:
            f.write(name + "\n")
        cfg = os.path.join(tmp, "config")
        os.makedirs(cfg)
        env = dict(os.environ, XDG_STATE_HOME=os.path.join(tmp, "state"), XDG_CONFIG_HOME=cfg,
                   QT_QPA_PLATFORM="offscreen")
        args = [os.path.abspath(binary), "--theme-roles"] + (["--skin", skin] if skin else [])
        out = subprocess.run(args, env=env,
                             capture_output=True, text=True, timeout=60, check=False)
        if out.returncode != 0:
            sys.exit(f"contrast-board: {binary} --theme-roles exited {out.returncode} on {name}: "
                     f"{out.stderr.strip()[:300]}")
        return json.loads(out.stdout[out.stdout.find("{"):])


# ---- what is painted on a filled colour (docs/38 §4) ---------------------------------------
# (what, where it is, foreground, ground, needs). A ground is a role, or (role, alpha) over the
# window background; the foreground likewise.

FILL_PAIRS = [
    ("selected text in a rename field", "Views/AddFilesTree.qml: selectedTextColor on selectionColor",
     "background", "accent", FLOOR),
    ("✓ in a ticked box", "Views/QbtImportSheet.qml, RestoreSheet.qml",
     "brightForeground", ("accent", "fillPressed"), FLOOR),
    ("a switched-on toggle's knob", "Kit/Toggle.qml",
     "brightForeground", ("accent", "fillPressed"), NON_TEXT),
    ("a selected danger button's label", "Kit/Button.qml (danger and selected)",
     "red", ("red", "fillSelected"), FLOOR),
    ("a yellow warning line's words", "Views/DeleteConfirm.qml, SharedDataLine.qml, BrowsedPathLine.qml",
     "yellow", ("yellow", 0.08), FLOOR),
    ("…its plain words", "the same bands", "foreground", ("yellow", 0.08), FLOOR),
    ("…its dim words", "Views/BrowsedPathLine.qml", "dimForeground", ("yellow", 0.08), FLOOR),
    ("the console's attention title", "Views/Console.qml", "stateError", ("stateError", 0.07), FLOOR),
    ("…its \"show them\"", "Views/Console.qml", "dimForeground", ("stateError", 0.07), FLOOR),
]


def paint(spec, colors, alphas):
    bg = parse(colors["background"])
    if isinstance(spec, str):
        return parse(colors[spec])
    role, alpha = spec
    if isinstance(alpha, str):
        alpha = alphas[alpha]
    return over(parse(colors[role]), bg, alpha)


def fill_pairs(data, side):
    rows = []
    for what, where, fg, ground, need in FILL_PAIRS:
        worst = None
        fails = []
        each = {}
        for name, both in data.items():
            roles = both[side]
            g = paint(ground, roles["colors"], roles["alphas"])
            f = paint(fg, roles["colors"], roles["alphas"])
            r = ratio(f, g)
            each[name] = r
            if worst is None or r < worst[0]:
                worst = (r, name, f, g)
            if r < need:
                fails.append((name, r))
        rows.append((what, where, fg, ground, need, worst, fails, each))
    return rows


# ---- the board ---------------------------------------------------------------------------

CSS = """
:root {
  --accent:#5492f8; --bg:#11111b; --bg-dark:#0d0d14;
  --fg:#b9c5ef; --fg-dark:#8b94b3; --fg-bright:#cbd4f3;
  --green:#7dd676; --red:#ee5681; --yellow:#f6d58c;
  --fill-normal: color-mix(in srgb, var(--fg) 4%, transparent);
  --hairline: color-mix(in srgb, var(--fg) 12%, transparent);
  --mono: "JetBrains Mono", "JetBrainsMono Nerd Font", ui-monospace, monospace;
  color-scheme: dark;
}
* { box-sizing:border-box; }
body { margin:0; background:var(--bg-dark); color:var(--fg); font:400 13px/1.6 var(--mono); -webkit-font-smoothing:antialiased; }
main { max-width:1320px; margin:0 auto; padding-block:32px 72px; padding-left:max(16px, env(safe-area-inset-left, 0px)); padding-right:max(16px, env(safe-area-inset-right, 0px)); display:grid; gap:32px; }
h1,h2,h3 { margin:0; font-weight:600; color:var(--fg-bright); text-wrap:balance; }
h1 { font-size:26px; } h2 { font-size:17px; } h3 { font-size:13px; }
p, li { max-width:84ch; }
p { margin:0; }
ul { margin:0; padding-left:20px; display:grid; gap:4px; }
code { color:var(--fg-bright); background:var(--fill-normal); padding:0 4px; overflow-wrap:anywhere; }
b { color:var(--fg-bright); font-weight:600; }
a { color:var(--accent); }
.eyebrow { font-size:10px; letter-spacing:.12em; text-transform:uppercase; color:var(--fg-dark); }
.dim { color:var(--fg-dark); }
.lead { font-size:14px; color:var(--fg); max-width:86ch; }
section { display:grid; gap:14px; border-top:1px solid var(--hairline); padding-top:22px; min-width:0; }
.sec-head { display:grid; gap:4px; }
.scroll { overflow-x:auto; }
table.t { border-collapse:collapse; width:100%; font-size:12px; }
.t th, .t td { text-align:left; padding:6px 10px; border-bottom:1px solid var(--hairline); vertical-align:top; }
.t th { font-weight:500; color:var(--fg-dark); font-size:10px; letter-spacing:.1em; text-transform:uppercase; white-space:nowrap; }
.t td:first-child { color:var(--fg-bright); }
.t td.num { white-space:nowrap; font-variant-numeric:tabular-nums; }
.t .y { color:var(--green); } .t .n { color:var(--red); }
.note { border-left:2px solid var(--accent); padding:6px 12px; background:var(--fill-normal); font-size:12px; max-width:96ch; }
.note.warn { border-color:var(--yellow); }
.themes-grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(380px, 1fr)); gap:18px; }
.card { border:1px solid var(--hairline); display:grid; grid-template-rows:auto 1fr; min-width:0; }
.card header { display:flex; gap:10px; align-items:baseline; flex-wrap:wrap; padding:8px 12px; border-bottom:1px solid var(--hairline); }
.card header b { font-size:13px; }
.card header .dim { font-size:11px; }
.stage { padding:10px 12px; display:grid; gap:6px; align-content:start; font-size:12.8px; line-height:1.4; }
.row { display:grid; grid-template-columns:104px minmax(0,1fr) 14px minmax(0,1fr); gap:8px; align-items:center; }
.row .role { font-size:11px; opacity:.85; overflow-wrap:anywhere; }
.sw { display:flex; gap:8px; align-items:center; min-width:0; }
.sw .chip { width:12px; height:12px; flex:none; }
.sw .txt { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; min-width:0; }
.sw .r { font-size:11px; white-space:nowrap; font-variant-numeric:tabular-nums; }
.arrow { opacity:.6; text-align:center; }
.kept { font-size:11px; opacity:.85; }
.untouched { display:flex; flex-wrap:wrap; gap:6px; }
.untouched span { border:1px solid var(--hairline); padding:1px 8px; font-size:12px; }
@media (max-width: 520px) {
  .themes-grid { grid-template-columns:minmax(0,1fr); }
  .row { grid-template-columns:minmax(0,1fr); }
  .arrow { display:none; }
}
"""


def esc(s):
    return html.escape(str(s), quote=True)


def alpha_label(alpha):
    """A tint's alpha: a percentage, or the shell's name for it (fillSelected differs per theme)."""
    return f"<code>{esc(alpha)}</code>" if isinstance(alpha, str) else f"{round(alpha * 100)} %"


def fmt(r):
    return f"{r:.2f}"


def swatch(colour, bg, sample):
    r = ratio(parse(colour), parse(bg))
    mark = "" if r >= FLOOR else " ✕"
    return (f'<div class="sw"><span class="chip" style="background:{colour}"></span>'
            f'<span class="txt" style="color:{colour}">{esc(sample)}</span>'
            f'<span class="r" style="color:{colour}">{fmt(r)}{mark}</span></div>')


def card(name, both):
    before, after = both["before"]["colors"], both["after"]["colors"]
    bg, fg = after["background"], after["foreground"]
    changed = [r for r in TEXT_ROLES if before[r] != after[r]]
    kept = [r for r in TEXT_ROLES if before[r] == after[r]]
    rows = []
    for r in changed:
        rows.append(f'<div class="row"><span class="role" style="color:{fg}">{esc(r)}</span>'
                    f'{swatch(before[r], bg, SAMPLES[r])}<span class="arrow" style="color:{fg}">→</span>'
                    f'{swatch(after[r], bg, SAMPLES[r])}</div>')
    mode = both["after"].get("mode", "")
    return (f'<article class="card" id="t-{esc(name)}"><header><b>{esc(name)}</b>'
            f'<span class="dim">{esc(mode)} · window {esc(bg)} · foreground {esc(fg)}</span></header>'
            f'<div class="stage" style="background:{bg};color:{fg}">' + "".join(rows) +
            f'<div class="kept">{len(kept)} of {len(TEXT_ROLES)} roles unchanged'
            + (f": {esc(', '.join(kept))}" if kept and len(kept) < len(TEXT_ROLES) else "")
            + "</div></div></article>")


def board(data, before_label, after_label):
    n = len(data)
    total = n * len(TEXT_ROLES)
    changed_total = sum(1 for b in data.values() for r in TEXT_ROLES
                        if b["before"]["colors"][r] != b["after"]["colors"][r])

    # Per role: themes changed, worst before, worst after.
    role_rows = []
    for r in TEXT_ROLES:
        moved = [t for t, b in data.items() if b["before"]["colors"][r] != b["after"]["colors"][r]]
        def worst(side, role=r):
            return min(((ratio(parse(b[side]["colors"][role]), parse(b[side]["colors"]["background"])), t)
                        for t, b in data.items()))
        wb, wa = worst("before"), worst("after")
        role_rows.append(
            f"<tr><td>{esc(r)}</td><td class='num'>{len(moved)} / {n}</td>"
            f"<td class='num {'y' if wb[0] >= FLOOR else 'n'}'>{fmt(wb[0])} <span class='dim'>{esc(wb[1])}</span></td>"
            f"<td class='num {'y' if wa[0] >= FLOOR else 'n'}'>{fmt(wa[0])} <span class='dim'>{esc(wa[1])}</span></td>"
            f"<td class='dim'>{esc(', '.join(moved)) or '—'}</td></tr>")

    # State roles follow the palette role they read.
    follows = all(b[side]["colors"][s] == b[side]["colors"][base]
                  for b in data.values() for side in ("before", "after")
                  for s, base in STATE_ROLES.items())

    # Why the window and not the row fills: how far captions sit from body text.
    def sep(side):
        return statistics.median(ratio(parse(b[side]["colors"]["dimForeground"]),
                                       parse(b[side]["colors"]["foreground"])) for b in data.values())

    def sep_if_lifted_on_selected_row():
        seps = []
        for b in data.values():
            c, a = b["after"]["colors"], b["after"]["alphas"]
            bg, fg = parse(c["background"]), parse(c["foreground"])
            row = over(fg, bg, a["fillSelected"])
            dim = lift(parse(c["dimForeground"]), fg, row, FLOOR)
            seps.append(ratio(dim, fg))
        return statistics.median(seps)

    what_if = sep_if_lifted_on_selected_row()
    fills_after = fill_pairs(data, "after")
    fills_before = {row[0]: row for row in fill_pairs(data, "before")}
    fill_rows = []
    for what, where, fg, ground, need, worst, fails, _ in fills_after:
        g = esc(ground) if isinstance(ground, str) else f"{esc(ground[0])} at {alpha_label(ground[1])}"
        wb = fills_before[what][5]
        fill_rows.append(
            f"<tr><td>{esc(what)}</td><td class='dim'>{esc(where)}</td>"
            f"<td>{esc(fg)} on {g}</td><td class='num'>{need:g}:1</td>"
            f"<td class='num'>{fmt(wb[0])} <span class='dim'>{esc(wb[1])}</span></td>"
            f"<td class='num {'y' if not fails else 'n'}'>{fmt(worst[0])} <span class='dim'>{esc(worst[1])}</span></td>"
            f"<td class='num'>{len(fails)} / {n}</td></tr>")

    tinted = [row for row in fills_after if row[6] and not isinstance(row[3], str)]
    tint_note = ""
    if tinted:
        better = all(row[7][t] >= fills_before[row[0]][7][t] - 1e-9 for row in tinted for t in data)
        tint_note = (
            '<p class="note warn">The tinted grounds (a colour at a low alpha over the window, '
            'behind text) read '
            + ("at least as well as before in every theme" if better else
               "better than before in most themes, <b>worse in some</b>")
            + ", but some text on them stays under 4.5:1: "
            + ", ".join(f"<code>{esc(row[2])}</code> on <code>{esc(row[3][0])}</code> at "
                        f"{alpha_label(row[3][1])} in {len(row[6])} of {len(data)} themes"
                        for row in tinted)
            + ". The floor holds on the bare window, so any tint behind the text takes a little "
            "of it back: the same problem as the row fills (§3), not ruled here.</p>")

    moved_themes = [t for t, b in data.items()
                    if any(b["before"]["colors"][r] != b["after"]["colors"][r] for r in TEXT_ROLES)]
    still = [t for t in data if t not in moved_themes]
    cards = "".join(card(t, data[t]) for t in moved_themes)

    return f"""<title>Contrast floor</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;600;700&display=swap">
<style>
/* ------------------------------------------------------------------
   docs/38: the contrast floor on text colours (Q19 amended, #89).
   Generated by scripts/dev/contrast-board.py from `omaseed --theme-roles`
   (before: {esc(before_label)}; after: {esc(after_label)}). Regenerate it;
   do not edit it by hand. Board chrome: board 35's.
   ------------------------------------------------------------------ */
{CSS}</style>
<main>
<header class="sec-head">
  <span class="eyebrow">omaseed · board 38 · Q19 amended · #89</span>
  <h1>Text colours read at 4.5:1 on the window, in every theme</h1>
  <p class="lead">Every colour omaseed paints text and glyphs with is lifted towards the theme's
  foreground until it reads at <b>4.5:1</b> (WCAG 2.2 AA, normal-size text) against the window
  background: <code>dimForeground</code> (it was 3:1) and now <code>accent</code>, <code>red</code>,
  <code>yellow</code>, <code>orange</code>, <code>green</code>, <code>cyan</code>, <code>blue</code>
  and <code>magenta</code>; the <code>state*</code> roles read those. A colour that already reads
  is kept byte for byte. Across the {n} installed themes, <b>{changed_total} of {total}</b> role
  colours move, in {len(moved_themes)} themes; {len(still)} keep every colour.</p>
  <p class="dim">Generated by <code>scripts/dev/contrast-board.py</code> from each build's
  <code>omaseed --theme-roles</code>, offscreen, one throwaway theme state per theme. Before:
  {esc(before_label)}. After: {esc(after_label)}.</p>
</header>

<section>
  <div class="sec-head"><span class="eyebrow">§1</span><h2>The rule</h2></div>
  <ul>
    <li>One floor, <code>Theme::kTextContrastFloor</code> = 4.5, cited from docs/DECISIONS.md Q19.</li>
    <li>Same mechanism as before (<code>Theme::liftToContrast</code>): the colour is mixed towards
      <code>foreground</code> in 1/256 steps and the first step that reads is kept, so the hue
      moves as little as it can.</li>
    <li>Measured against the opaque window background, as Q19 always was: the ground moves with
      the wallpaper behind the window's alpha.</li>
    <li>Not against the row fills (§3). Faint text (40 %) stays for disabled things, which WCAG
      exempts; the "via" before a path in the command palette is <code>dimForeground</code> now.</li>
    <li>State roles follow the role they read: {'checked, every theme, both builds' if follows else '<b>NOT</b> in every theme (a bug)'}.</li>
  </ul>
</section>

<section>
  <div class="sec-head"><span class="eyebrow">§2</span><h2>Per role</h2>
  <p class="dim">The worst contrast on the window background across the themes, before and after.</p></div>
  <div class="scroll"><table class="t">
    <thead><tr><th>role</th><th>themes changed</th><th>worst before</th><th>worst after</th><th>which</th></tr></thead>
    <tbody>{''.join(role_rows)}</tbody>
  </table></div>
</section>

<section>
  <div class="sec-head"><span class="eyebrow">§3</span><h2>Why the window and not the row fills</h2></div>
  <p>Captions are told from body text by their distance from <code>foreground</code>. With the
  floor on the window, the median contrast between <code>dimForeground</code> and
  <code>foreground</code> is <b>{fmt(sep('after'))}:1</b> (it was {fmt(sep('before'))}:1). Lifting
  against the selected row's fill as well (the foreground at the shell's
  <code>fillSelected</code>, 18 % on a stock Omarchy) would leave it at
  <b>{fmt(what_if)}:1</b>: captions would read as body text. Coloured text on selected and
  pressed rows gets its own fix (#90).</p>
</section>

<section>
  <div class="sec-head"><span class="eyebrow">§4</span><h2>What is painted on a filled colour</h2>
  <p class="dim">Every place the QML draws text or a glyph on a fill in one of these roles
  (grep of <code>src/ui/qml</code>, 2026-10-16), worst across the themes. The primary button is
  an outline, not a fill (spec §9 item 10); the accent bars, dots and underlines carry no text.</p></div>
  <div class="scroll"><table class="t">
    <thead><tr><th>what</th><th>where</th><th>pair</th><th>needs</th><th>worst before</th><th>worst after</th><th>below, after</th></tr></thead>
    <tbody>{''.join(fill_rows)}</tbody>
  </table></div>
  {tint_note}
</section>

<section>
  <div class="sec-head"><span class="eyebrow">§5</span><h2>Each theme that moves</h2>
  <p class="dim">Each changed role as a sample of what it paints, on that theme's own window
  background, before → after, with its contrast; ✕ marks one under 4.5:1.</p></div>
  <div class="themes-grid">{cards}</div>
  <p class="dim">Unchanged in every role:</p>
  <div class="untouched">{''.join(f'<span>{esc(t)}</span>' for t in still) or '<span>none</span>'}</div>
</section>
</main>
"""


# ---- every role on every ground (docs/DECISIONS.md C30 F7) -----------------------------------

INK_ROLES = ["foreground", "dimForeground", "brightForeground", "accent", "red", "yellow",
             "orange", "green", "cyan", "blue", "magenta"]
# A build from before C30 has no grounds: each one as that build painted it, (tint, alpha).
OLD_GROUNDS = {"window": ("foreground", 0.0), "idle": ("foreground", "fillIdle"),
               "hover": ("foreground", "fillHover"), "cursor": ("foreground", "fillHover"),
               "selected": ("foreground", "fillSelected"),
               "selectedCursor": ("foreground", "fillSelectedCursor"),
               "pressed": ("foreground", "fillPressed"), "warning": ("yellow", 0.08),
               "attention": ("red", 0.07), "danger": ("red", "fillSelected"),
               "ticked": ("accent", "fillPressed"), "accentIdle": ("accent", "fillIdle"),
               "blockIdle": ("foreground", "fillIdle", "fillIdle"),
               "blockHover": ("foreground", "fillHover", "fillIdle"),
               "highlight": ("accent", 0.35, "fillIdle")}


# #188: a fill nested in a block that keeps its plain ground's colours, measured on what is
# painted there, its fill over the block's ground: (what, role, fill's ground, block, needs).
NESTED_KEPT = [
    ("a Toggle's knob, on, on a hovered row", "brightForeground", "ticked", "hover", NON_TEXT),
]


def nested_kept(roles):
    """[(what, role, ratio, needs)] for NESTED_KEPT, from a build that prints its grounds."""
    out = []
    for what, role, fill, block, need in NESTED_KEPT:
        top, under = roles["grounds"][fill], roles["grounds"][block]
        g = tuple(round(v) for v in over(parse(top["fill"][:7]), parse(under["ground"]),
                                         top["alpha"]))
        out.append((what, role, ratio(parse(top["colors"][role]), g), need))
    return out


def grounds_of(roles):
    """{ground: (opaque ground, {role: colour})}, as the build resolved them, or as a build
    from before C30 painted them (its window roles on every fill)."""
    c, a = roles["colors"], roles["alphas"]
    if "grounds" in roles:
        return {name: (parse(g["ground"]), {r: parse(g["colors"][r]) for r in INK_ROLES})
                for name, g in roles["grounds"].items()}
    out = {}
    for name, (tint, alpha, *block) in OLD_GROUNDS.items():
        al = a[alpha] if isinstance(alpha, str) else alpha
        under = parse(c["background"])
        if block:  # a fill inside a block: over the foreground's fill at the block's alpha
            under = tuple(round(v) for v in over(parse(c["foreground"]), under, a[block[0]]))
        g = tuple(round(v) for v in over(parse(c[tint]), under, al))
        cols = {r: parse(c[r]) for r in INK_ROLES}
        out[name] = (g, cols)
    return out


def check(data):
    """Every pair, measured: {"pairs", "fails": [(theme, ground, role, ratio)], "sep"}."""
    fails, pairs, seps = [], 0, []
    for theme, roles in data.items():
        grounds = grounds_of(roles)
        for ground, (g, cols) in grounds.items():
            for role in INK_ROLES:
                pairs += 1
                q = ratio(cols[role], g)
                if q < FLOOR:
                    fails.append((theme, ground, role, q))
        if "grounds" in roles:
            for what, role, q, need in nested_kept(roles):
                pairs += 1
                if q < need:
                    fails.append((theme, what, role, q))
        # Kit/KeyBadge: background on a solid accent.
        c = roles["colors"]
        pairs += 1
        q = ratio(parse(c["background"]), parse(c["accent"]))
        if q < FLOOR:
            fails.append((theme, "accent (solid)", "background", q))
        # The hierarchy C30 F2 keeps: a caption under a selected row's main text.
        g, cols = grounds["selected"]
        seps.append(ratio(cols["dimForeground"], cols["brightForeground"]))
    return {"themes": len(data), "pairs": pairs, "fails": fails,
            "sep": statistics.median(seps) if seps else 0, "sep_min": min(seps) if seps else 0}


# ---- every mark on every ground it sits on (WCAG 2.2 1.4.11, #200) --------------------------
# A mark is a colour that carries meaning without words: a line's fill, a dot, a box, an edge.
# 1.4.11 asks 3:1 between it and what is next to it. C30's floor lifts words only, so a mark
# keeps the window's colours (TorrentTable's comment on the progress cell) and may read under
# 3:1 on a darker ground. Each entry:
#   (what, where, colours, alpha, grounds, against)
# colours: window roles (Theme.<role>, as `colors` prints them), or ("ink", [roles]) for the
#   ground's own lifted colours (row.ink.<role>, row.ground.<role>);
# alpha: the mark's opacity, a number or an `alphas` name; the mark is composited over what it
#   is measured against, as the text pass composites a fill over its ground;
# grounds: {skin: [ground]}, the grounds the mark is painted on in that skin;
# against: None for the ground itself, or an `alphas` name: the mark lies on a track of the
#   foreground at that alpha over the ground (the track's unfilled part is what it meets).
# Rows only paint the grounds `Theme.rowGround(…, false)` gives: never `pressed`, and under a
# skin with `select.pickedGround` (Console) a selected row is `picked`.
ROW_GROUNDS = {"omarchy": ["window", "hover", "cursor", "selected", "selectedCursor"],
               "console": ["window", "hover", "cursor", "picked"]}
# Tiles and chips (Views/SpaceTile.qml, SpaceChip.qml) take `Theme.ground()`, not rowGround.
PICK_GROUNDS = {"omarchy": ["selected", "selectedCursor"],
                "console": ["selected", "selectedCursor"]}
# A focus ring is drawn on whatever holds the focus.
FOCUS_GROUNDS = {"omarchy": ["window", "idle", "hover", "cursor", "selected", "selectedCursor",
                             "pressed", "blockIdle", "hoverIdle"],
                 "console": ["window", "idle", "hover", "cursor", "selected", "selectedCursor",
                             "picked", "pressed", "blockIdle", "hoverIdle"]}
STATES = list(STATE_ROLES)

def lift_mark(colour, under, fg, bright):
    """Theme::liftMark (`Theme.markOn`, C30 marks, #200): `colour` kept where it reads at 3:1
    on `under`, else the least lift towards the foreground, then towards the better of the
    foreground and the bright one."""
    col = lift(colour, fg, under, NON_TEXT)
    if ratio(col, under) < NON_TEXT:
        best = bright if ratio(bright, under) > ratio(fg, under) else fg
        col = lift(col, best, under, NON_TEXT)
    return col


def lift_mark_at(colour, alpha, under, meet, fg, bright):
    """Theme::liftMarkAt (`Theme.markAt`, C30 marks, #200): (colour, alpha) for a faint mark,
    `colour` at `alpha` over `under` measured against `meet`: kept where it reads at 3:1, else
    at the least higher opacity, in 1/256 steps, that does, else opaque and `lift_mark`ed."""
    def reads(a):
        return ratio(tuple(round(v) for v in over(colour, under, a)), meet) >= NON_TEXT
    if reads(alpha):
        return colour, alpha
    for step in range(1, 257):
        if alpha >= 1:
            break
        a = alpha + (1 - alpha) * step / 256
        if reads(a):
            return colour, a
    return lift_mark(colour, meet, fg, bright), 1


# A mark's colours: a list of the window's roles as they are; ("ink", roles), its ground's
# text ink; or (MARK_LIFT, roles, track), the window's roles lifted by `Theme.markOn` against
# the ground, or against `track` (an alpha key) over the ground when the code lifts it there
# (Kit/Meter lifts a line against its track, so the bare line is lifted against the table's);
# or (MARK_AT, roles) and (MARK_AT_INK, roles), a faint mark at the entry's alpha, the window's
# roles or the ground's ink, raised by `Theme.markAt` where it reads under 3:1 on what it is
# measured against. A MARK_AT entry's grounds may be (over, against) pairs: an edge composited
# over one ground and measured against the other it meets (a toggle's outline, block › track).
MARK_LIFT = "mark"
MARK_AT = "at"
MARK_AT_INK = "at-ink"
# A toggle's outline lies on its block (the window, a settings row's 4 % or its hover fill)
# and meets the idle track inside it: lifted against the track, and measured against both.
TOGGLE_EDGES = [("window", "idle"), ("idle", "blockIdle"), ("hover", "hoverIdle")]

MARKS = [
    ("the progress line", "Views/TorrentTable.qml ProgressLine, Views/LibraryRow.qml Meter: Kit/Meter markColor",
     (MARK_LIFT, STATES + ["dimForeground"], "hairlineAlpha"), 1, ROW_GROUNDS, None),
    ("the progress line on the table's track", "Kit/ProgressLine.qml: Meter markColor on trackColor fill(hairlineAlpha)",
     (MARK_LIFT, STATES, "hairlineAlpha"), 1, ROW_GROUNDS, "hairlineAlpha"),
    ("the progress line on the rows' track", "Views/LibraryRow.qml: Meter markColor on trackColor fill(libraryRowLineTrack = fillHover)",
     (MARK_LIFT, STATES + ["dimForeground"], "fillHover"), 1, ROW_GROUNDS, "fillHover"),
    ("the files tab's progress line, on its track", "Views/DetailsFilesTab.qml: row.ink, hairline track",
     ("ink", ["stateSeeding", "accent"]), 1, ROW_GROUNDS, "hairlineAlpha"),
    ("the pulse, still or at its peak", "Views/TorrentTable.qml pulseTick: markOn (breathes 0.25 → 1)",
     (MARK_LIFT, ["stateSeeding"], None), 1, ROW_GROUNDS, None),
    ("the ratio track to 1.0", "Views/TorrentTable.qml ratioTracks: markAt(foreground, 0.45) on fill(fillHover)",
     (MARK_AT, ["foreground"]), 0.45, ROW_GROUNDS, "fillHover"),
    ("the ratio track past 1.0", "Views/TorrentTable.qml ratioTracks: markOn(stateSeeding) on fill(fillHover)",
     (MARK_LIFT, ["stateSeeding"], "fillHover"), 1, ROW_GROUNDS, "fillHover"),
    ("the tracker's given bar", "Views/ConsoleTrackers.qml: markAt(row.ground.stateSeeding, 0.6)",
     (MARK_AT_INK, ["stateSeeding"]), 0.6, {"omarchy": ["window", "hover"], "console": ["window", "hover"]},
     None),
    ("\"only you\"'s box", "Views/TorrentTable.qml T3: border markAt(yellow, 0.4)",
     (MARK_AT, ["yellow"]), 0.4, ROW_GROUNDS, None),
    ("a selected row's edge", "Views/LibraryRow.qml SelectionMark rule (not under pickedGround): markColor",
     (MARK_LIFT, ["accent"], None), 1, {"omarchy": ["selected", "selectedCursor"]}, None),
    ("a selected tile's or chip's mark", "Views/SpaceTile.qml, SpaceChip.qml SelectionMark (litColor): markColor",
     (MARK_LIFT, ["accent"], None), 1, PICK_GROUNDS, None),
    ("the cursor's mark", "Views/LibraryRow.qml, SearchRow.qml SelectionMark (Console's brackets): markColor",
     (MARK_LIFT, ["accent"], None), 1, {"console": ["cursor", "picked"]}, None),
    ("the focus ring", "Kit/FocusRing.qml: border markOn(accent) on its control's block",
     (MARK_LIFT, ["accent"], None), 1, FOCUS_GROUNDS, None),
    ("a toggle's outline", "Kit/Toggle.qml: border markAt(foreground, borderIdle), block › track",
     (MARK_AT, ["foreground"]), "borderIdle", {"omarchy": TOGGLE_EDGES, "console": TOGGLE_EDGES},
     None),
    ("a toggle's outline, hot", "Kit/Toggle.qml: border markAt(foreground, borderHover), block › track",
     (MARK_AT, ["foreground"]), "borderHover", {"omarchy": TOGGLE_EDGES, "console": TOGGLE_EDGES},
     None),
    ("the status line's dot, connecting", "Views/StatusLine.qml dotColor: markAt(foreground, faintAlpha) on the lane",
     (MARK_AT, ["foreground"]), "faintAlpha", {"omarchy": ["window", "hover"],
                                               "console": ["window", "hover"]}, None),
    ("a web seed's lamp", "Views/DetailsWebSeedsTab.qml: markOn(muted) on the row",
     (MARK_LIFT, ["muted"], None), 1, ROW_GROUNDS, None),
]


def mark_pairs(roles, skin, marks=None):
    """[(what, role, ground, ratio)] for every mark in `marks` (MARKS) on every ground it sits
    on in `skin`, from a build that prints its grounds."""
    c, a, grounds = roles["colors"], roles["alphas"], roles["grounds"]
    fg = parse(c["foreground"])
    bright = parse(c["brightForeground"]) if "brightForeground" in c else fg
    out = []
    for what, _where, colours, alpha, on, against in marks if marks is not None else MARKS:
        al = a[alpha] if isinstance(alpha, str) else alpha
        kind = colours[0] if isinstance(colours, tuple) else None
        for entry in on.get(skin, []):
            gname, mname = entry if isinstance(entry, tuple) else (entry, entry)
            g = parse(grounds[gname]["ground"])
            meet = parse(grounds[mname]["ground"])
            under = g
            label = gname if mname == gname else f"{gname} › {mname}"
            if against:
                under = tuple(round(v) for v in over(fg, g, a[against]))
                meet = tuple(round(v) for v in over(fg, meet, a[against]))
                label = f"{label} + track"
            for role in (colours[1] if kind else colours):
                if kind in (MARK_AT, MARK_AT_INK):
                    src = (parse(grounds[mname]["colors"][STATE_ROLES.get(role, role)])
                           if kind == MARK_AT_INK else parse(c[role]))
                    col, al2 = lift_mark_at(src, al, under, meet, fg, bright)
                    m = tuple(round(v) for v in over(col, under, al2))
                    out.append((what, role, label, ratio(m, meet)))
                    if mname != gname:  # an edge: the ground it lies on, outside, too
                        out.append((what, role, f"{gname} (outside)", ratio(m, under)))
                    continue
                if kind == "ink":
                    col = parse(grounds[gname]["colors"][STATE_ROLES.get(role, role)])
                elif kind == MARK_LIFT:
                    track = colours[2]
                    lifted_on = tuple(round(v) for v in over(fg, g, a[track])) if track else g
                    col = lift_mark(parse(c[role]), lifted_on, fg, bright)
                else:
                    col = parse(c[role])
                m = tuple(round(v) for v in over(col, under, al))
                out.append((what, role, label, ratio(m, under)))
    return out


def check_marks(data):
    """Every mark pair: {"pairs", "fails": [(theme, what, role, ground, ratio)]}."""
    pairs, fails = 0, []
    for key, roles in data.items():
        if "grounds" not in roles:
            continue
        for what, role, ground, q in mark_pairs(roles, key.split(" · ")[0]):
            pairs += 1
            if q < NON_TEXT:
                fails.append((key, what, role, ground, q))
    return {"pairs": pairs, "fails": fails}


def marks_report(res, skins, themes_n):
    """The failing marks as Markdown, grouped by mark, worst first (#200 step 2's input)."""
    by_mark = {}
    for theme, what, role, ground, q in res["fails"]:
        by_mark.setdefault(what, []).append((q, theme, role, ground))
    where = {m[0]: m[1] for m in MARKS}
    order = sorted(by_mark, key=lambda w: min(r[0] for r in by_mark[w]))
    lines = [f"# Marks under {NON_TEXT:g}:1 (WCAG 2.2 1.4.11)", "",
             f"Generated by `scripts/dev/contrast-board.py --check BIN --marks-report FILE`: "
             f"{len(skins)} skins ({', '.join(skins)}) × {themes_n} themes, {res['pairs']} mark "
             f"pairs, {len(res['fails'])} under {NON_TEXT:g}:1, in {len(by_mark)} of "
             f"{len(MARKS)} marks. A mark is composited over what it is measured against (its "
             "ground, or its track over the ground: \"+ track\"); colours are the window's unless "
             "the mark takes its ground's ink.", ""]
    lines.append("| mark | failing | worst |")
    lines.append("|---|---:|---|")
    for w in order:
        rows = sorted(by_mark[w])
        q, theme, role, ground = rows[0]
        lines.append(f"| {w} | {len(rows)} | {q:.2f} {role} on {ground} ({theme}) |")
    lines.append("")
    for w in order:
        rows = sorted(by_mark[w])
        lines += [f"## {w} ({len(rows)})", "", f"`{where.get(w, '')}`", "",
                  "| ratio | skin · theme | colour | ground |", "|---:|---|---|---|"]
        lines += [f"| {q:.2f} | {theme} | {role} | {ground} |" for q, theme, role, ground in rows]
        lines.append("")
    return "\n".join(lines)


def self_test():
    """The mark pass on two made-up marks, one that reads and one built to fail (ctest)."""
    roles = {"colors": {"foreground": "#000000", "accent": "#000000", "muted": "#cccccc"},
             "alphas": {"hairlineAlpha": 0.12},
             "grounds": {"window": {"ground": "#ffffff", "colors": {"accent": "#000000"}},
                         "selected": {"ground": "#d0d0d0", "colors": {"accent": "#000000"}}}}
    marks = [("reads", "", ["accent"], 1, {"omarchy": ["window", "selected"]}, None),
             ("faint", "", ["muted"], 1, {"omarchy": ["window"]}, None),
             ("faint ink on a track", "", ("ink", ["accent"]), 0.2, {"omarchy": ["selected"]},
              "hairlineAlpha"),
             ("not in this skin", "", ["muted"], 1, {"console": ["window"]}, None),
             ("lifted", "", (MARK_LIFT, ["muted"], "hairlineAlpha"), 1, {"omarchy": ["window"]},
              "hairlineAlpha"),
             ("raised", "", (MARK_AT, ["accent"]), 0.1, {"omarchy": ["window"]}, None),
             ("edge", "", (MARK_AT, ["accent"]), 0.1, {"omarchy": [("window", "selected")]}, None)]
    got = {(w, g): q for w, _r, g, q in mark_pairs(roles, "omarchy", marks)}
    ok = (len(got) == 8 and got[("raised", "window")] >= NON_TEXT
          and got[("edge", "window › selected")] >= NON_TEXT
          and got[("edge", "window (outside)")] >= NON_TEXT and got[("lifted", "window + track")] >= NON_TEXT and got[("reads", "window")] >= NON_TEXT
          and got[("reads", "selected")] >= NON_TEXT and got[("faint", "window")] < NON_TEXT
          and got[("faint ink on a track", "selected + track")] < NON_TEXT)
    print("contrast-board: marks self-test " + ("ok" if ok else f"FAILED: {got}"))
    return 0 if ok else 1


def check_main(a, found):
    skins = [s for s in a.skins.split(",") if s]
    data = {f"{skin} · {name}": roles_for(a.check, d, name, skin)
            for skin in skins for name, d in found.items()}
    for key, roles in data.items():
        if roles.get("skin", "omarchy") != key.split(" · ")[0]:
            sys.exit(f"contrast-board: {key} drew the {roles.get('skin')} skin "
                     f"({roles.get('skinReason', 'no reason given')})")
    res = check(data)
    by_ground = {}
    for theme, ground, role, q in res["fails"]:
        by_ground.setdefault(ground, []).append((theme, role, q))
    if not a.quiet:
        for ground, rows in sorted(by_ground.items()):
            worst = min(rows, key=lambda r: r[2])
            print(f"{ground}: {len(rows)} failing, worst {worst[1]} {worst[2]:.2f} ({worst[0]})")
    caps = {t: r["alphas"].get("fillCap", 1.0) for t, r in data.items()}
    # A cap holds a theme's fills when one of them sits at it (C30 F6).
    fills = ("fillIdle", "fillHover", "fillSelected", "fillSelectedCursor", "fillPressed")
    capped = {t: v for t, v in caps.items()
              if any(abs(data[t]["alphas"].get(f, 0) - v) < 1e-9 for f in fills)}
    print(f"contrast-board: {len(skins)} skins × {len(found)} themes, {res['pairs']} pairs, "
          f"{len(res['fails'])} under {FLOOR:g}:1; caption under a selected row's text, median "
          f"{res['sep']:.2f}:1 (closest {res['sep_min']:.2f}); fills held: "
          + (", ".join(f"{t} {v:g}" for t, v in sorted(capped.items())) or "none"))
    marks = check_marks(data)
    by_mark = {}
    for theme, what, role, ground, q in marks["fails"]:
        by_mark.setdefault(what, []).append((q, theme, role, ground))
    if not a.quiet:
        for what, rows in sorted(by_mark.items(), key=lambda kv: min(kv[1])[0]):
            q, theme, role, ground = min(rows)
            print(f"mark {what}: {len(rows)} failing, worst {role} on {ground} {q:.2f} ({theme})")
    print(f"contrast-board: {marks['pairs']} mark pairs, {len(marks['fails'])} under "
          f"{NON_TEXT:g}:1 in {len(by_mark)} of {len(MARKS)} marks"
          + ("" if a.strict_marks else " (reported; --strict-marks fails on them)"))
    if a.marks_report:
        with open(a.marks_report, "w", encoding="utf-8") as f:
            f.write(marks_report(marks, skins, len(found)) + "\n")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({**res, "caps": caps, "marks": marks}, f, indent=1)
    return 1 if res["fails"] or (a.strict_marks and marks["fails"]) else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", metavar="BIN", help="check every role on every ground")
    ap.add_argument("--json", help="--check: write the measurement here")
    ap.add_argument("--quiet", action="store_true", help="--check: the summary line only")
    ap.add_argument("--before", help="omaseed built before the change")
    ap.add_argument("--after", help="omaseed built with it")
    ap.add_argument("--before-label", default="before")
    ap.add_argument("--after-label", default="after")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                  "..", "..", "docs", "38-contrast-floor.html"))
    ap.add_argument("--themes-root", action="append", default=[])
    ap.add_argument("--skins", default="omarchy,console",
                    help="--check: the skins to check, comma-separated")
    ap.add_argument("--strict-marks", action="store_true",
                    help="--check: fail on a mark under 3:1 too (#200), not only report it")
    ap.add_argument("--marks-report", metavar="FILE",
                    help="--check: write the failing marks here, as Markdown")
    ap.add_argument("--self-test", action="store_true",
                    help="test the mark pass on made-up roles; needs no build")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    found = themes(a.themes_root or DEFAULT_ROOTS)
    if not found:
        sys.exit("contrast-board: no themes found")
    if a.check:
        return check_main(a, found)
    if not (a.before and a.after):
        ap.error("--before and --after are needed for the board (or --check BIN)")
    data = {name: {"before": roles_for(a.before, d, name), "after": roles_for(a.after, d, name)}
            for name, d in found.items()}
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(board(data, a.before_label, a.after_label))
    moved = sum(1 for b in data.values() for r in TEXT_ROLES
                if b["before"]["colors"][r] != b["after"]["colors"][r])
    print(f"contrast-board: {len(data)} themes, {moved} role colours moved → {os.path.normpath(a.out)}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
