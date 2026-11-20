## Your skins

A skin is the form of omaseed's window: which theme roles and alphas paint
which grounds, the lines, cuts and rounding, the fonts and voices, the
densities, glyphs and labels. Its colours are the person's Omarchy theme:
no hex, no layout, no code. A face names a built-in skin (omarchy or
console) whose face that seam draws, never a file.

Look before you change anything. These two only read:
  omaseed skin list --json      every skin, where it lives, whether it reads
  omaseed check NAME --json     what NAME reads and drops, and its pictures

Check the drawn one first (skin list's choice, or pinned). check needs no
display, bus or server, and writes only its pictures in ~/.cache/omaseed/check/.

### The loop

  omaseed skin clone omarchy warm   a copy of the drawn skin (here omarchy)
                                    in skins/warm/; the first clone makes skins/
  then edit skins/warm/skin.json, and after each save:
  omaseed check warm --json         a light and a dark picture: look at both
  omaseed skin use warm             draw it in the open window, and keep it

A skin's name is its folder: letters, digits, . _ and -, up to 63, never
omarchy, console or desktop in any case. clone never touches its source nor
a folder that is there (in any case). The open window follows each save of
the drawn skin.json; one that doesn't read draws Omarchy's till a good one.

check's JSON: drawn, why (when not drawn), dropped [{token, value, why}]
(each left at Omarchy's), missingFonts, baseSize, and pictures [{mode, theme,
picture, lifted [{role, ground, from, to}]}], light first; unchecked names a
lightness with no theme installed. --theme NAME draws that one alone. Exit 0
drawn, even with tokens dropped; 1 drawn whole as Omarchy's (read why); 2 no
such skin, theme.

### The rules

- Theme roles and alphas, never a hex: a skin is drawn under every theme.
- Text is lifted to 4.5:1 over its ground, so it stays readable: lifted is
  not an error. A few tenths is fine; a big lift means that ground is too
  loud, or its tint too near the text's hue: lower its alpha or change role.
- Sizes are spec px, scaled by the person's base size. glow is always clean.
- A font in missingFonts isn't installed (fc-list) and gets a stand-in.

### The tokens

skin.schema.json beside this file has every token, its range and Omarchy's
value; a clone names it in $schema. It is long: grep it for a token, don't
read it whole. Where a wish lands:
  warmer, cooler   grounds.*.tint, roles.accent, line.*Role
  rounder, sharper radius (corners), cut (a panel's and a key's chamfer)
  denser, airier   density;   a font   font.label, font.value

  roles     accent, signal, alert, warn: each a theme role
  grounds   idle, hover, cursor, panel, picked…: {tint, alpha, over}
  line      hairline, lit, edge and menu edge: widths, roles, alphas
  cut       a panel's and a key's corner chamfer, px
  radius    corner rounding, px, or hyprland: the desktop's own rounding
  tint      a notice's and a block's own colour, as an alpha
  font      label, value, kana, icons ("" is the desktop's font)
  label     each voice's case, tracking, weight and size step
  select, plates, meter   cursor mark, picked ground, plates, pieces grid
  numerals  pad or plain;  density: named sizes (railWidth…)
  glyphs, labels   names you add: ms:ligature or icon:name; a word
  faces     Plate, SelectionMark, Meter, Lamp: omarchy or console

### What the picture doesn't show

One sheet on invented torrents: rows, table, a menu, a sheet, a notice and
the four faces. No other view: after skin use, ask the person to look.
With omabox installed, check it under every theme, in a desktop of its own:

  omabox check run --approve --base DIR -- \
      /usr/share/omaseed/agents/skin-check.sh warm
Once with --approve (the baseline), then without: same, or what changed.

### The way back

  omaseed skin use omarchy          or Settings › Appearance › Skin
  OMASEED_SKIN=omarchy omaseed      one run, nothing saved

With no omaseed running, skin use saves the choice for its next start
("live": false), never over a state.json that doesn't parse (exit 5); exit
3 when the running one can't answer. Follow the desktop is set in Settings.

### Sharing

A shared skin is a public folder, topic omaseed-skin: skin.json, RECIPE.md
(what it is, how it was made) and check's picture as preview.png. To add one,
read every file, copy it to skins/NAME/ (never over a name there), check it.

### The files

In ~/.config/omaseed/, AGENTS.md, CLAUDE.md and skin.schema.json are this
guide, rewritten on every start; skins/ is the person's. state.json,
servers.json, moves/ and secrets/ are omaseed's: never open or edit them.
Passwords live in the keyring, or in secrets/ on a machine without one:
never read that folder.
