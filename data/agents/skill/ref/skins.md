# omaseed: skins

A skin is the form of omaseed's window: which theme roles and alphas paint
which grounds, the lines, cuts and rounding, the fonts and voices, the
densities, glyphs and labels. Its colours are the person's Omarchy theme:
no hex, no layout, no code. A face names a built-in skin (omarchy or
console) whose face that seam draws, never a file.

A skin's name is its folder: letters, digits, . _ and -, up to 63, never
omarchy, console or desktop in any case. clone never touches its source nor
a folder that is there (in any case). The open window follows each save of
the drawn skin.json; one that doesn't read draws Omarchy's till a good one.

## check

check needs no display, bus or server, and writes only its pictures in
~/.cache/omaseed/check/. Its JSON: drawn, why (when not drawn), dropped
[{token, value, why}] (each left at Omarchy's), missingFonts, baseSize, and
pictures [{mode, theme, picture, lifted [{role, ground, from, to}]}], light
first; unchecked names a lightness with no theme installed. --theme NAME
draws that one alone. Exit 0 drawn, even with tokens dropped; 1 drawn whole
as Omarchy's (read why); 2 no such skin, theme.

## The rules

- A skin is drawn under every theme, so its colours are roles, not values.
- Text is lifted to 4.5:1 over its ground, so it stays readable: lifted is
  not an error. A few tenths is fine; a big lift means that ground is too
  loud, or its tint too near the text's hue: lower its alpha or change role.
- Sizes are spec px, scaled by the person's base size. glow is always clean.
- A font in missingFonts isn't installed (fc-list) and gets a stand-in.

## The tokens

~/.config/omaseed/skin.schema.json has every token, its range and Omarchy's
value; a clone names it in $schema. The families:

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

Rounder is radius (corners); cut is a chamfer, a different shape.

## What the picture doesn't show

One sheet on invented torrents: rows, table, a menu, a sheet, a notice and
the four faces. No other view: after skin use, ask the person to look.
With omabox installed, check it under every theme, in a desktop of its own:

  omabox check run --approve --base DIR -- \
      /usr/share/omaseed/agents/skin-check.sh warm
Once with --approve (the baseline), then without: same, or what changed.

## The way back, in full

  omaseed skin use omarchy          or Settings › Appearance › Skin
  OMASEED_SKIN=omarchy omaseed      one run, nothing saved

With no omaseed running, skin use saves the choice for its next start
("live": false), never over a state.json that doesn't parse (exit 5); exit
3 when the running one can't answer. Follow the desktop is set in Settings.

## Sharing

A shared skin is a public folder, topic omaseed-skin: skin.json, RECIPE.md
(what it is, how it was made) and check's picture as preview.png. To add one,
read every file, copy it to skins/NAME/ (never over a name there), check it.
