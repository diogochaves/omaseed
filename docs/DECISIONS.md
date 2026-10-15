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
own) are #56, ruled as C3.

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

## C3 — qBittorrent's log as a face (board 32, ruled 2026-10-15)

Board [32](32-log-and-failures.html) (#56), D9–D15 as recommended:

- **D9** The log is the window's third face beside library and seed, opened with `~` from
  anywhere; `~` again, or `esc` with nothing left to clear, goes back to the face it came from.
  It takes the body's whole width: the rail and the details are about torrents. `log/main` is
  read (all four levels, `last_known_id`) after each poll only while the face is on screen and
  the window shows, and its lines are dropped when it closes (`ServerLog`).
- **D10** Four level chips with their counts, `1`–`4`, kept per server in `state.json`
  (`"log"`); a `/` filter where every word must be in the line, case ignored; the bar says
  "N of M lines", M being the lines at the levels that are on.
- **D11** `y` copies the marked lines, or the cursor's, one per line as
  `yyyy-MM-dd HH:mm:ss level message`. No clear: the API has none.
- **D12** Oldest at the top, newest at the bottom. New lines pull the view down while it is at
  the bottom; a scroll or a key that leaves it pauses, and new lines are counted in a pill
  instead. `G` goes to the newest line and follows again; `f` turns following off and on. The
  footer says "● following" or "○ paused". The face opens on its newest line, following.
- **D13** "its lines in the log" on #54's red block and #55's yellow one opens the face narrowed
  to that torrent (a chip, `esc` clears it); `t` does the same for the cursor line's torrent and
  `enter` shows it in the library with General open. A line is about a torrent when a quoted
  part is its name, or starts with libtorrent's hash prefix (or its name, on libtorrent 2.0) and
  a space. In a narrow pane the block's actions that don't fit go to a second line, so the
  keyless link is never dropped.
- **D14** No blocked-peers log (`log/peers`) for now.
- **D15** Over the first line the face says where qBittorrent's memory of its log begins (its
  start, or the oldest of the 20 000 lines it keeps) and where its log file is
  (`file_log_path`), or that it writes none. The file is never read.

The face's keys are the Keymap's "log" area, so the `?` sheet and the command palette list them.
qBittorrent numbers its log from 0 again after a restart: a full update, or ids that go
backwards, start the face over, as `ProblemLog` does.

## C4 — Burst: the alternative limits named from the two sets (board 31, ruled 2026-10-15)

Board [31](31-burst.html) (#62), D1–D14 as recommended:

- **D1** The state is named from the two sets, never from a setting: for each direction the
  alternative limit is compared with the normal one, 0 read as no limit (above any number).
  At least one direction faster and none slower is **burst**; at least one slower and none
  faster is **turtle**; anything else, and the preferences not read yet, keeps today's
  **alt-speed** (**D2**: faster one way and slower the other is no claim either way). The word
  never changes what `A` does or what qBittorrent stores. Someone who uses the set as the slow
  one sees "turtle", in the same yellow, with the same limits, in the same place.
- **D3** Burst is the theme's green; turtle and alt-speed stay yellow. **D4** The same gauge
  glyph, its needle up to the right for burst (`Glyphs.burst`), down to the left otherwise.
- **D5** The hero's chip prints the word, the limits and "· until 07:00" (a little fainter),
  folding to word and end at step 2 and to the glyph at step 3; mixed or the same is today's
  chip. Off, the glyph alone, its needle already pointing the way the set goes.
- **D6** "until" is when the schedule ends the current state, with the day when it is more
  than a day away ("Mon 07:00"); tooltips add "tomorrow". With the schedule off there is none.
  A hand toggle with the schedule on lasts until the schedule next flips **the other way**:
  qBittorrent acts only when its own reading changes, so a change that agrees with the hand is
  silent and the one after it ends it (burst by hand at 15:00 against 23:00–07:00 lasts until
  07:00 tomorrow). A state that differs from the schedule within 30 s of a boundary is still
  the schedule's (qBittorrent looks every 30 s).
- **D7** The chip's tooltip says whether the schedule or a hand set it; after `A` (or the chip,
  or the palette), once the server has flipped and the schedule is on, the footer's note slot
  says how long the hand toggle lasts, until the next key.
- **D8** Settings › qBittorrent › Speed has a "nights" preset (`n`): 23:00 to 07:00 every day,
  all four time keys at once. **D9** A plain summary under Days says what the schedule will
  do by qBittorrent's rule, and warns in yellow when a window past midnight reads the days
  backwards (on a day that is not picked it is on all day; diogochaves/upstream#84). A line
  under the four limits names the set; the schedule's row title follows the word.
- **D10** The guidance line under the alternative upload limit ("85–90 % of your real upload
  speed") is **not shipped**: the measurement it waited for did not support it (2026-10-15, two
  qBittorrent 5.2.3 in network namespaces behind an emulated 20 Mbit/s line with a 200 ms
  buffer). Over TCP peers a burst at 90 % or 85 % left a parallel upload the same share as no
  limit (about 970 against 914 KB/s of 2.5 MB/s) and the ping at about 200 ms; even 70–80 %
  alone kept it near 130 ms. µTP peers did back off (seeding fell to about 235 KB/s), but at
  any limit, adding about 100 ms. A percentage would promise a usable line it doesn't keep.
- **D11** No desktop notice when the schedule switches.
- **D12** The bar widget follows the word (green outline and line for burst, "M: burst"). The
  app writes `alt.kind`, `alt.until` and `alt.byHand` into the status file as additions
  within schema v2: a reader that does not know them reads today's alt-speed.
- **D13** qBittorrent's preferences (the only place the other set and the schedule live) are
  read again whenever the alternative limits flip, and after the limits sheet writes them, as
  well as on connect.
- **D14** The tray's checkbox reads "burst · until 07:00" and its icon's mark is a green disc
  with ⏶; turtle keeps today's yellow ⏷.

The server's clock is read as this computer's: the API gives no time zone, so for a server in
another zone the time printed is right and only the day and the "by hand" guess can be off.
The pure parts are `core/SpeedSets` and `core/AltSchedule`; `core/AltSpeed` puts them together
for the server the app is on.

## C5 — A magnet's files before the add (board 29, ruled 2026-10-15)

Board [29](29-magnet-files.html) (#44), D1–D11 as recommended:

- **D1** As soon as one magnet (or one link to a `.torrent`) is in the add sheet and isn't a
  duplicate, qBittorrent is asked for its metadata (`torrents/fetchMetadata`), on the current
  server's client: the answer is kept in the Web UI session, so every poll and the add after it
  must share that session's cookie or API key (diogochaves/upstream#86). A magnet is keyed by its
  infohash, so typing `&dn=…` after a whole hash starts nothing new.
- **D2** Nothing shows for the first 0.5 s, so a quick answer just shows the tree. **D3**
  "Fetching" until 10 s, "still looking" until 60 s, then "may never come" in yellow. The polls
  go at 0, 0.25, 0.5 and 1 s, then every second, and every 5 s after a minute, one at a time; they
  never stop while the sheet is open.
- **D4** `add` before the list is in adds every file, as before. **D5** "add stopped" before the
  list is a started add with `stopCondition=MetadataReceived`: it stops as soon as its list
  arrives, with nothing downloaded (a plain stopped magnet never connects).
- **D6** Closing the sheet, another source over it or a batch leaves an unanswered lookup running
  on the server until it restarts or that hash is added: there is no cancel
  (diogochaves/upstream#87), and add-then-delete would flash a row everywhere. The 60 s line says
  so. Any add of the magnet ends it.
- **D7** A magnet whose `tr=` host is one the library's private torrents announce to (maindata's
  `private`, every announce URL in the tracker map) is not looked up: libtorrent never hands a
  private torrent's metadata to a magnet. The line points to the site's `.torrent`. A private
  tracker the library doesn't know goes through the slow states like a dead magnet. A magnet
  handed over before the first sync waits for the library before this check.
- **D8** A link to a `.torrent` takes the same flow (qBittorrent downloads it); its duplicate
  check runs when the answer brings the hash. A dead link never fails: it stays "no answer".
- **D9** Once in, a magnet gets everything a `.torrent` gets: the tree (the paths' `<name>/`
  root stripped), the size, the free line's need of the ticked files, the files-on-disk match
  (#27) and renames with #67's disk check. The choices go with the add itself (`urls` plus
  `filePriorities`, nothing stopped and started); renames are replayed after a stopped add, as
  for a `.torrent`. A 400 to the priorities means the session was replaced: the metadata is asked
  for once more, then the magnet is added to stop at its metadata and the priorities set then.
- **D10** No lookups in a batch: it has no Files row. "one by one" gives each its own.
- **D11** Below Web API 2.11.9, or on a 404, the sheet is today's with nothing more said. A
  source qBittorrent refuses outright (another 4xx) is the same.

The pure parts are `core/MetadataLookup` (the schedule, the phases, the 200's parse) and
`core/PrivateTrackers`; `App.addLookup` is the one lookup the sheet shows, and
`Views/AddFetchLine.qml` draws its waits.

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
