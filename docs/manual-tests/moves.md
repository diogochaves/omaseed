# Manual test: moving torrents

What `scripts/dev/move-walk.sh` can't judge: whether the moves read well, look right and feel
right. Run the walk first (it checks the same steps on the server and on disk); this pass is for
a person. Covers boards 42, 43 and 45 (DECISIONS C15, C16, C18).

## Setup (about 5 minutes)

```sh
scripts/dev/manual-box.sh          # a throwaway desktop, a window on workspace 9, omaseed from main
scripts/dev/move-library.sh        # the library below, in the box's "public" qBittorrent
```

In the box: click into the window, **SUPER+ALT+ESC** (SUPER keys go to the box; again to give them
back), **SUPER+SPACE**, `omaseed`. First run finds "public" on `127.0.0.1:8090`; its login is in
the box's `~/qbittorrent-login.txt` (also printed by `manual-box.sh`). 109 torrents; most read
"queued" (qBittorrent runs 5 at a time), which is expected.

What `move-library.sh` makes:

| Where | What |
|---|---|
| `/home/sbx/library/games` | Arcade Set 01–35; Big Collection 1–4 (partial packs: two of six files wanted) |
| `/home/sbx/library/games/handheld` | Handheld Pack 01–10 |
| `/home/sbx/library/films` | Short Film 01–60 |
| loose files | `games/manual.pdf`, `games/notes/readme.txt`, `films/poster.jpg` (no torrent owns them) |
| `/tmp/fast` | another filesystem (the box's tmpfs): a move there copies real bytes |
| `/tmp/fast/clash` | already holds a different "Arcade Set 04" |
| `/tmp/locked/dest` | read-only |

Type full paths (`~` is not expanded). To narrow the table use the filter at the top; select with
**space**, or **ctrl+click** (add/remove), **shift+click** (range), a middle click (add/remove).
**ctrl+k** is the jump palette, not the filter. A terminal in the box: **SUPER+ENTER**.

## Steps

Each: what to do, then what you should see. Note OK, or what you saw instead.

1. **A quick move (one torrent, same disk).** Cursor on Arcade Set 01, **m**. The checks fold to
   one green line. Type `/home/sbx/library/archive`, enter. No band; the footer says
   "Arcade Set 01 moved to …" with a ✓ for about 6 s.
2. **The places list.** Another torrent, **m**. "places ▾" sits in the field, before browse.
   Click it, or press **↓**: a "recent" group (archive), then the library's folders with counts.
   ↓ moves, enter (or a click) fills the field and moves nothing; esc closes only the list, a
   second esc the sheet.
3. **Several folders, keeping their places.** Filter `02`; space on Short Film 02, Arcade Set 02,
   Handheld Pack 02; **m**; `/tmp/fast/keep`; pick "keep their places below /home/sbx/library".
   One row per source folder, a room line for `/tmp`, the button "move 3 torrents". They land in
   `/tmp/fast/keep/films`, `…/games`, `…/games/handheld`.
4. **The band and the queue.** Rail → games → **shift+m** → `/tmp/fast/games` → enter. While it
   runs: Short Film 05, **m**, `/tmp/fast/after`: the button reads "move after games"; the band
   then says "next: Short Film 05 …". The loose files follow into `/tmp/fast/games`. These
   torrents are tiny: a move under 3 s shows no band (intended), so the queue is easier to see in
   move-walk, which slows its folder down.
5. **Move back, from the summary.** On the games summary, **move back…**: each torrent to its old
   place, "move N back". Short Film 05 stays in `/tmp/fast/after`. Moving back and forth while
   the summary is open repeats the cycle.
6. **A name clash.** Space on Arcade Set 04 and 05, **m**, `/tmp/fast/clash`. "1 already there …
   Skipped", button "move 1 torrent". 05 moves, 04 stays; `cat "/tmp/fast/clash/Arcade Set
   04/data.bin"` still reads `not the same data`.
7. **Can't write.** Short Film 06, **m**, `/tmp/locked/dest`. A yellow warning naming
   `/tmp/locked/dest` (not `/tmp`), not a block. Enter: "… didn't move: qBittorrent can't write
   to …"; the torrent still seeds where it was.
8. **Names.** In the **:** palette, "set location" finds "move…"; the actions menu and the
   selection bar say "move…".
9. **A partial pack, and re-ticking a folder.** Big Collection 1 → details → Files: the "Disc 1"
   folder's tick is half (parts 0 and 1 of six wanted). One click on it unticks all of Disc 1;
   the next click brings back part 0 and part 1 only, the other four stay skipped, and it stays
   at 100 % (DECISIONS C19 T1). A click never wants the whole folder: the folder row's priority
   menu (right-click → normal) does that. Then **m** → `/tmp/fast/partial`: only the two wanted
   files go (`ls "/tmp/fast/partial/Big Collection 1/Disc 1"`), still 100 %.
10. **Move back after the summary closes.** Move the games folder to `/tmp/fast/games` and close
    its summary. Arcade Set 10's details show a "last move" block with "move back…".
11. **Moves that overlap.** Filter `Arcade Set 1`; select Arcade Set 10, 11, 12; **m** →
    `/tmp/fast/elsewhere`; close that summary. In the log (**~**), each move has its line with
    move back. The games move's sheet says "3 were moved again since, by move to
    /tmp/fast/elsewhere (HH:MM)", the button "move N back · 3 stay", and offers "move that one
    back first".
12. **Undo in order.** "move that one back first" (or **:** → "undo last move", which only offers
    the newest move): the 3 return to `/tmp/fast/games`. The games move's move back is now whole
    ("move N back", nobody stays); press it: everything is back in `/home/sbx/library/games`. Loose
    files carried with the folder stay in `/tmp/fast/games` (board 43).

Done: `scripts/dev/manual-box.sh down`, or close the box's window.
