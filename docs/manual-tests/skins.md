# Manual test: the skins

The person's pass over the console skin (#171, DECISIONS C31) before v0.5.0. Only what the
automated layer can't judge: whether Console reads like board 46 on a real window, and whether
the switch and Follow the desktop feel right. The rest was checked by tests and walks on the same
commit (listed at the end). About 45 minutes. Keep [board 46](../46-the-whole-app-on-v03.html)'s
Console pass open beside the box (and [board 49](../49-create-a-torrent.html) for create, [board
51](../51-the-v050-pass.html) for what the first pass changed, C32).

## Setup

```sh
scripts/dev/manual-box.sh          # a throwaway desktop, a window on workspace 9, omaseed from this checkout
```

In the box: click into the window, **SUPER+ALT+ESC** (SUPER keys go to the box; again to give them
back), **SUPER+SPACE**, `omaseed`. First run finds "public" on `127.0.0.1:8090`; both logins are in
the box's `~/qbittorrent-login.txt`. In Settings › Servers (**a**) add "private"
(`127.0.0.1:8091`, its login) and switch to it: it is downloading four small torrents and seeding
Sketchbook Scans (v1), so every display has something in it.

Every command below runs in a terminal **in the box** (**SUPER+RETURN** there), so it changes the
box's files, never yours. Before step 3, check whether the box's theme carries a desktop skin of
its own (a box starts on a copy of your theme), and set it aside if it does:

```sh
ls ~/.local/state/omarchy/current/theme/console-skin.json && \
  mv ~/.local/state/omarchy/current/theme/console-skin.json ~/theme-console-skin.json.aside
```

## Steps

Each: what to do, then what you should see. Note OK, or what you saw instead.

1. **The switch.** **,** (Settings) › Appearance. Skin is the first row: Omarchy, Console,
   follow the desktop, the line under it board 46's words for the one chosen. Pick Console. The
   whole window repaints at once, the open settings sheet included: no restart, no flash, the
   sheet stays where it was. Pick Omarchy: back as it was. **enter** on the row steps through
   the three. Quit omaseed (**ctrl+.** › quit) and start it again: the choice is kept.
2. **The missing fonts line.** With Console chosen, the line under the row says nothing about
   fonts if all four are there. Hide one (the box's `/usr` is read-only, so through fontconfig):

   ```sh
   mkdir -p ~/.config/fontconfig/conf.d
   cat > ~/.config/fontconfig/conf.d/90-hide-chakra.conf <<'EOF'
   <?xml version="1.0"?>
   <!DOCTYPE fontconfig SYSTEM "fonts.dtd">
   <fontconfig>
     <selectfont><rejectfont>
       <pattern><patelt name="family"><string>Chakra Petch</string></patelt></pattern>
     </rejectfont></selectfont>
   </fontconfig>
   EOF
   ```

   Quit and start omaseed. Appearance's Skin row now has a yellow line: "Not installed: Chakra
   Petch. Console uses stand-ins for them." Labels draw in the monospace font, nothing in tofu.
   Pick Omarchy: the line goes. Judge: is the line clear, and is Console still Console without
   its label font? Then `rm ~/.config/fontconfig/conf.d/90-hide-chakra.conf` and restart
   omaseed.
3. **Follow the desktop.** Pick follow the desktop. With no desktop skin the line says "Now:
   Omarchy." and the window is Omarchy. Turn the desktop's console skin on, as omarchy-console
   writes it:

   ```sh
   mkdir -p ~/.config/omarchy
   echo '{"skin": "console", "version": 1}' > ~/.config/omarchy/console-skin.json
   ```

   Within a second, without touching omaseed: Console, and the line says "Now: Console.". Then
   turn the skin off in the panel's file, and on again with the system's fonts:

   ```sh
   echo '{"fx": {"skin": "off"}}' > ~/.config/omarchy/console.json.next && \
     mv ~/.config/omarchy/console.json.next ~/.config/omarchy/console.json     # Omarchy
   echo '{"fx": {"skin": "on", "fonts": "system"}}' > ~/.config/omarchy/console.json.next && \
     mv ~/.config/omarchy/console.json.next ~/.config/omarchy/console.json     # Console, system fonts
   ```

   With `fonts: system`: Console's cuts, brackets and kana words stay, but every family is the
   system's (the monospace font, Omarchy's glyphs, not Material Symbols), and no missing-fonts
   line. Pick Console by hand: its own fonts come back (`fx.fonts` only counts while following).
   Back to follow the desktop, then `rm ~/.config/omarchy/console-skin.json
   ~/.config/omarchy/console.json`: Omarchy again. Judge: does following feel immediate, and do
   the words under the row say what is drawn?
4. **Console, the frame.** Pick Console. Against board 46's Console window: the rail (group
   heads with kana, place names in the label font, counts padded "01", the servers head "servers"
   with its kana and the count apart, the settings row at its foot), the title bar (upper case,
   tracked, its kana risen a little; ↓ and ↑ with their lines, the filter open with a `cat:`
   chip), the status line (square lamp, upper case; **A** for the turtle chip on its tint, the
   choice's burst on its own), the window's edge. Hover the rail: the fold tab on its edge, its
   tooltip; drag the edge. Hold **alt**: the key badges sit where Omarchy's do. **,** ›
   Appearance › Surface tint 4 %: the rail's rows still read in Console's grounds; back to off.
5. **Console, the sheets and menus.** **a** (add), **m** on a torrent (move), **ctrl+n**
   (create), **,** (settings, a few panes), **?** (help), **:** (the palette): each sheet's head
   upper case with its kana (追加, 移動, 作成; 外観 on Appearance, 検索 on search plugins, 設定 on
   the other panes), the panel cut at its corners, the hair edge. **ctrl+.** (the app menu), the
   row's **enter** (actions, with transfer ›, queue › and copy › open in turn), a right-click on
   a server in the rail, "+ add ▾", the display menu (its sort block): the menus' heads in the
   heading voice, no kana, the cursor's row bracketed, each row's icon a Material Symbols glyph.
   The app menu grows from the lit mark but keeps its cut-plate outline all round. Judge: is the
   caption-size button text readable?
6. **Console, the library.** The rows (**:** `show as rows`): names upper case, the cursor's
   row in brackets while the list has the keys (**l** into details: the brackets go, the
   cursor's ground stays), **space** on two other rows puts them on the picked ground, with no
   brackets; the group heads' bar, the needs-you head's accent bar and red title. With those two
   selected, the action bar floating over the list: its buttons, their icons and the outcome
   strip above it (a move, say) in Console's plates. The table
   (`show as table`): the status word in the caption voice, "downloading" whole in its column,
   the same brackets on the cursor's row. Space (`show as space`): the blocks as cut plates,
   the cursor's 1 px accent edge, a selected block's brackets. Board 46's station layout is not
   built (#199).
7. **Console, a torrent that needs you.** Make one, by taking away a file it seeds:

   ```sh
   chmod 000 ~/Downloads/private/"Sketchbook Scans (v1)"/"scan 01.raw"
   ```

   In omaseed, Sketchbook Scans (v1) under the cursor, **:** `recheck`, enter. It goes into
   error: in the rows its fix is a filled accent tag after its words, and the story opens with
   the error block on a cut plate edged red, its ground faintly red over the panel. Then
   `chmod 644 ~/Downloads/private/"Sketchbook Scans (v1)"/"scan 01.raw"` and the fix.
8. **Console, the details.** On a torrent half downloaded: the hero on its plate, the pieces in
   square cells, 34 across, as many rows as the pieces fill; then the tabs (**2**–**5**): files,
   swarm, trackers (square lamps), facts, no digits (hold **alt** for their badges). The current
   tab on a 2 px accent line while the pane has the keys, a dim hairline while the list has them
   (#201 F16); every column caption whole (no "SEEDSPEERS"). The story's "what happened" newest
   first, its plain marks neutral. **~** (the log): its rail's levels with their ticks and
   counts, the torrents it names, since when.
9. **Console, search and create.** **ctrl+f**, a word: the search rows' names upper case, the
   pills' lamps square. **ctrl+n** on a folder of the box's (`~/Downloads`): create's plates and
   its round-trip note tagged in the signal colour (board 49). Start it: the band's "creating"
   tag, then "made"; the outcome strip on its cut plate edged green.
10. **The assumed points.** Each was taken as recommended while it was built (DECISIONS C31,
    #171's comments). Tick what is right; for the rest, say what you want instead.
    - [ ] Only board 46's stated grounds are Console's (panel, cursor, selected, picked, the
      warning band); idle, pressed and the other fills keep Omarchy's recipes.
    - [ ] The menu's edge is the hair, not the accent.
    - [ ] One heading track (.22 em) for the title bar, sheet heads, panes and menu heads.
    - [ ] Buttons at the caption size; a sheet's foot may wrap sooner than under Omarchy.
    - [ ] Names tracked .05 at 600, the rows' names upper case, captions .18 at 600; the views'
      and menu rows in the control voice (.16 em).
    - [ ] Kana: which titles carry it (questions, places' kinds, search, the library; add, move,
      create; 外観, 検索, else 設定), none on menu heads; the kana font is Noto Sans CJK JP.
    - [ ] Tags: a notice's word by kind (error, warning, note, done; no signal, refused; moving,
      moved; creating, made) and the strip's done, failed, "N of M", cancelled.
    - [ ] The cursor bracketed in every list, the table and the details' lists included (the
      board draws none on the table); a pick is the picked ground with no rule.
    - [ ] Plates: the hero, create's round-trip note, the strip, the confirm, the palette, the
      error and move blocks on cut plates; the tooltip and inline renames on the small cut.
    - [ ] The line meter in 3 spec px cells; the pieces in 34 square columns, in the app's own
      colours (the board draws signal and a second green).
    - [ ] The Status column 120 u in Console; the status word in the caption voice.
    - [ ] The current tab's 2 px accent line in Console; Omarchy keeps its 1 px bright line.
    - [ ] The needs-you group's title stays red (the board's Console title is dim).
    - [ ] The row fact at the heading step (17.3 px at 13, the board says 18), weight 500.
    - [ ] Follow the desktop turns on for any positive integer version, and reads none of the
      desktop file's values; `fx.fonts = system` counts only while following.
    - [ ] The app menu keeps Console's cut-plate outline all round while it grows from the lit
      mark (the open top is Omarchy's; C32 "The mark and its menu").
    - [ ] Every menu's icons, the action bar's and the server menu's included, are Material
      Symbols ligatures in Console (C32 "Menus").
    - [ ] The cursor's brackets show only while its list has the keys; the details' tab line is
      lit only while the pane has them; Settings' tabs keep the skin's line (#201 F16).

When you're done: `scripts/dev/manual-box.sh down`.

## Not in this pass

These were checked by tests and walks on the same commit.

- **The reader and the tokens:** `tst_skinreader` (parse, defaults, rejection, the version
  rule, glow, grounds); the QML suite under both skins (ctest labels `skin-omarchy`,
  `skin-console`); `skin_lint` (no branch on a skin's name, no literal `border.width`).
- **Contrast:** `tst_themecontrast` and `contrast_board_check` over both skins, every theme and
  fill set (C30's floor).
- **The switch and Follow the desktop's files:** settings-walk, in a box, against a fixture
  `console-skin.json` and `console.json`.
- **The look, screen by screen:** the visual check's Console pass (`OMASEED_SKIN=console`, its
  own baseline), the walk's screens plus a torrent that needs you, the pieces, the trackers
  tab, picked rows apart from the cursor, a refused action's strip and two servers.
