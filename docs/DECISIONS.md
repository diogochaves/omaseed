# Decisions

Rulings the code cites by number, kept here so the comments lead somewhere. `Q` numbers were
design questions; `N` and `S` came from a visual pass over the app, `M` from a manual pass over
the running app, and `C` records a board's whole ruling that the code leans on. Each is settled and built. Bigger designs live in the numbered boards (see
[ARCHITECTURE.md](ARCHITECTURE.md)).

## Q1 — The window's alpha

The window's ground defaults to the shell's `[launcher] background-alpha` from `shell.toml`
(0.95 on a stock Omarchy), so omaseed sits on the wallpaper the way the launcher does.
Settings → Appearance overrides it. Sheets, palettes and popups stay opaque.

## Q16 — Formatting, states and notices

- Sizes and speeds print three significant digits everywhere (`641 MB`).
- `stoppedUP` / `pausedUP` show as green "completed"; `stoppedDL` as grey "stopped".
- The low-disk notice's threshold is a setting (Notifications, 2 GiB by default). It watches
  `free_space_on_disk`, which qBittorrent reports for its default save path only; other disks
  and per-category paths are not covered, and the setting's caption says so.
- Tracker health is a badge and a filter, never a desktop notice. That is what qBittorrent,
  qui, Transmission, Deluge and Flood all do.
- While the sync engine polls, the HTTP client does not retry reads on its own: the engine's
  visible "retry in 10 s" is the only retry, so a lost server shows after one failure.

## Q17 — The Status column is 116 px

The design's 104 px cannot hold `downloading` beside its glyph in JetBrains Mono. The column
is 116 px, taken from the flexible Name column, and the word is never cut.

## Q19 — Captions stay readable on every theme

In about half of Omarchy's themes `dark_foreground` is under 3:1 against `background`
(everforest 1.72, white 1.82, miasma 2.13, Tokyo Night 2.76), which made captions and column
headers faint. `Theme.dimForeground` therefore has a contrast floor: it is mixed towards
`foreground` until it reads at 3:1 against the window background. A theme already at 3:1
keeps its own colour.

## Q23 — A server tested before it is saved keeps its result

Settings → Servers shows each server's version and torrent count from its last test. A test
run in the server sheet before `save` has no server to belong to yet, so on save the sheet's
fresh result is carried onto the new row.

## Q27 — Self-signed certificates: trust on first use

On a server's first TLS error, the server sheet (and first run) shows the certificate's
fingerprint and offers "trust this certificate". The fingerprint is saved with the server in
`servers.json` (it is not a secret) and only that exact certificate is accepted afterwards.
Turning on the Web UI's HTTPS in qBittorrent's settings warns about this first.

## C1 — The command palette (board 30, ruled 2026-10-15)

Board [30](30-command-palette.html) (#61), all ten decisions as recommended:

- **D1** `:` opens it, and `ctrl+shift+p` too (it also works from a field). Both are in the
  Keymap's "global" area and share one row of the `?` sheet's "app" section. Inside any
  palette `ctrl+shift+p` is not "up".
- **D2** It lists the global and torrent commands always, plus the focused area's own: the
  Files, Trackers, Peers and HTTP sources tabs and the seed console. Movements, gestures and
  the two menus themselves are left out.
- **D3** Menu-only actions (the actions menu's four option rows, the ⋯ menu's two limit sheets)
  are Keymap entries with no key and a `via` path ("enter s", "ctrl+."). Key routing, the
  footer and the `?` sheet skip entries with no key.
- **D4** A row that can't run is listed dim with the reason on its second line, after the rows
  that can, and the cursor skips it. The rule is the entry's `needs` and `Keymap.whyNot()`:
  act on the selection, or on the cursor row when nothing is selected, as the actions menu
  does; while the server is lost only settings, shortcuts, hide and quit run (Fig. 23).
- **D5** Start and stop are two rows on the whole selection, each dim when it would change
  nothing; `p` shows on the one it would run now.
- **D6** Each command has a `title` ("force start"); the footer keeps its short `label`.
- **D7** The last five commands run from the palette come first with nothing typed and break
  ties in a search. Their action ids are kept in `state.json` (`"commands"."recent"`).
- **D8** No match: a dim line says so, and one row hands the text to the jump palette.
- **D9** The `?` sheet stays, with its filter and `enter`. Both run through one
  `MainView.runAction(id)`.
- **D10** Found through `: commands` beside `? help` in the footer, a "commands…" row at the
  top of the ⋯ menu, and a line in the `?` sheet's subtitle.

The rows come from one pure `Keymap.commandRows(context)` over plain data, so the site's demo
can reuse it.

## C2 — Failures only qBittorrent's log knows (board 32, ruled 2026-10-15)

Board [32](32-log-and-failures.html) (#55), D1–D8 as recommended. qBittorrent answers "OK" to
a move or rename that then fails, and a dead web seed or a .torrent copy it can't write leaves
the torrent looking fine: the reason is in `log/main` only. D9–D15 (the log as a face of its
own) are #56.

- **D1** A move, rename or part file that failed gets #54's block at the top of General, in
  yellow (the torrent still works), under the red one when it is also errored. Two failures on
  one torrent show the newest. The block's actions: set location (the sheet holds the
  destination), open folder, rename in Files (the cursor on that file), copy the line, dismiss.
- **D2** After a move, a rename or a delete with files that omaseed asked for, the log is read
  on every poll for 30 s; a failure shows at once in the footer's note line, yellow, one note
  per action ("3 of 12 didn't move"), then stays as the block.
- **D3** The block stays until it is fixed (the save path changes; the file is no longer among
  the torrent's files), dismissed with `x`, or qBittorrent restarts (its log is in memory).
- **D4** Dismissals are kept in `state.json` (`"failures"`), per server, as line keys (id,
  time and a hash of the text, since ids start from 0 again at each restart). A read from the
  top of the log forgets those whose line is gone.
- **D5** Web seed failures are a status under each URL on the HTTP sources tab: the error's
  own words and when, or "no errors logged" (never "working"). No block, no notice.
- **D6** An export failure is a note under "Copy .torrent files to" in Settings › qBittorrent ›
  Downloads, while either export folder is the one the copy tried.
- **D7** The table does not change.
- **D8** None of them is a desktop notice.

The log is read every 15 s while the window shows (failures nobody watched, such as a category
change's automatic move, show within that). Lines are told apart as #54's are: the quoted
segments and libtorrent's untranslated text; a URL seed message and an export by their shape; a
rename by a file path under the torrent's name; the part file and content lines in English only.
A line older than a torrent's `added_on` is never about it.

## M8 — A click leaves the keyboard focus where it was

Buttons and toggles do not take the focus when clicked. Otherwise an overlay gives the focus
back to the button that opened it, and the button keeps its focus ring and tooltip with the
pointer far away. `tab` still focuses them.

## M11 — Clicked rows lose their hover fill

Rail rows that were clicked kept the hover fill after the pointer left: the scroll hint lay
over the list with its own hover handler, which took the hover events of the rows under it.
Overlays on a list take no buttons and no hover; whatever must follow the pointer listens on
the list itself, alongside its rows.

## M12 — An option shows its new state at once

Ticking an option (sequential download, first and last pieces, super seeding, automatic
management) shows the requested state immediately, from `SyncEngine`'s pending values, instead
of waiting for the next poll. If the server refuses, it goes back.

## M16 — The footer's note stays in the footer

A short note such as "details: bottom · I to change" sits in the footer's own line, after the
facts. It never draws over the details panel.

## M18 — Renaming files in the add sheet

qBittorrent's `torrents/add` takes no per-file names, so renames made in the add sheet are
applied right after the add with `torrents/renameFile` and `torrents/renameFolder`, the same
way the file priorities are.

## N1 — The Files tab matches qBittorrent's

The details pane's Files tab does what qBittorrent's Web UI allows on a torrent's content:
filter by name, sort by any column the API gives (name, size, progress, priority, remaining,
availability, pieces), progress on every row, priority changes, copy path, rename files and
folders.

## S1 — The status keys are `0`–`8`

The rail has nine status rows, `0` (all) to `8` (errored), so the help sheet and the rail's
footer print `0–8`. The approved design's figure prints `0–9`; `9` does nothing.

## The click rules every list follows

From a pass over the whole app with the mouse:

- A plain click puts the cursor and the range anchor on that row. The table selects only that
  row; the Files tab clears its selection, since there the cursor is the target when nothing is
  marked.
- Ctrl+click toggles a row. Shift+click makes the selection anchor..row; ctrl+shift+click adds
  anchor..row.
- A right-click outside the selection retargets to that row; inside it, the selection stays.
- A click on empty list space clears the selection. A click away from an inline rename commits
  it (`esc` still cancels).
- Anchors are kept by hash or path, so a live re-sort or a fold does not move them.
- Text that names a safe key ("esc", "enter apply", "?") is clickable, with a pointing hand.
