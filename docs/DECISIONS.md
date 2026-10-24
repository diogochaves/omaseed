# Decisions

Rulings the code cites by number, kept here so the comments lead somewhere. `Q` numbers were
design questions; `N` and `S` came from a visual pass over the app, `M` from a manual pass over
the running app, and `C` records a board's whole ruling that the code leans on. Each is settled and built. Bigger designs live in the numbered boards (see
[ARCHITECTURE.md](ARCHITECTURE.md)).

## Q1 — The window's alpha

The window's ground defaults to the shell's `[launcher] background-alpha` from `shell.toml`
(0.95 on a stock Omarchy), so omaseed sits on the wallpaper the way the launcher does.
Settings → Appearance overrides it. Sheets, palettes and popups stay opaque.

Changed by C24 H13 (v0.5.0): the ground is opaque by default; this alpha is Appearance's
"translucent" choice.

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

## Q19 — Text stays readable on every theme

In about half of Omarchy's themes `dark_foreground` is under 3:1 against `background`
(everforest 1.72, white 1.82, miasma 2.13, Tokyo Night 2.76), which made captions and column
headers faint. `Theme.dimForeground` therefore has a contrast floor: it is mixed towards
`foreground` until it reads at 3:1 against the window background. A theme already at 3:1
keeps its own colour.

**Amended 2026-10-16** (#89, board [38](38-contrast-floor.html); Diogo took the contrast call
as recommended). The floor is **4.5:1**, WCAG 2.2 AA for normal-size text
(`Theme::kTextContrastFloor`), and it covers every colour omaseed paints text and glyphs with:

- `dimForeground` (3:1 → 4.5:1) and the palette roles `accent`, `red`, `yellow`, `orange`,
  `green`, `cyan`, `blue` and `magenta`. The `state*` roles read those, so they follow.
  `muted`, `selection`, `brown` and the grounds are never text and stay as the theme has them.
- Same mechanism: mixed towards `foreground` in 1/256 steps until it reads; a colour that
  already reads keeps its bytes. Measured against the opaque window background, as before.
- Across the 27 installed themes, 64 of 243 role colours move, only in themes that fall
  short: `dimForeground` in 23 (most sat at exactly 3.0:1), `accent` 4, `red` 7, `yellow` 6,
  `orange` 8, `green` 4, `cyan` 4, `blue` 3, `magenta` 5. The worst before were yellow 2.05:1
  (rose-pine), red 2.30:1 (miasma) and magenta 2.34:1 (catppuccin-latte).
- Not against the row fills. With the floor on the window, captions stay apart from body
  text (`dimForeground` against `foreground`, median 2.43:1); lifting against the selected
  row's fill as well would leave them at a median 1.66:1, nearly body text. Selected rows get
  their own fix (#90).
- Faint text (`Theme.faintAlpha`, 40 %) is for disabled things only, which WCAG exempts. The
  "via" before a path in the command palette, which is not disabled, is `dimForeground`.
- Text on a fill in one of these colours: the rename field's selected text (`background` on
  `accent`) reads at the same ratio as accent on the window, so at least 4.5:1, and the ✓ on
  a ticked box at 4.84:1 or more. Text on a tint of its own colour (a selected danger button,
  red on red at `fillSelected`; the warning and attention bands at 7–8 %) reads at least as
  well as before in every theme but is still under 4.5:1 in some: board 38 §4 lists them, and
  they are not ruled yet.

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
  per action ("3 of 12 didn't move"), then stays as the block. *Changed by C18 D1 (v0.4.0):
  a move's failure is said in the outcome strip, not the footer; a rename's or a delete's
  files still get this note.*
- **D3** The block stays until it is fixed (the save path changes; the file is no longer among
  the torrent's files), dismissed with `x`, or qBittorrent restarts (its log is in memory).
- **D4** Dismissals are kept in `state.json` (`"failures"`), per server, as line keys (id,
  time and a hash of the text, since ids start from 0 again at each restart). A read from the
  top of the log forgets those whose line is gone.
- **D5** Web seed failures are a status under each URL on the HTTP sources tab: the error's
  own words and when, or "no errors logged" (never "working"). No block, no notice.
- **D6** An export failure is a note under "Copy .torrent files to" in Settings › qBittorrent ›
  Downloads, while either export folder is the one the copy tried.
- **D7** The table does not change. *Changed by C18 D1 (v0.4.0): a torrent whose move
  failed reads "✕ didn't move" in its status cell while its block stands.*
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

## C6 — The weekly recap (board 22 T8, ruled with the console)

Board [22](22-seed-console.html) draws T8 as one desktop notice on Sunday evening, off by
default, through the notices omaseed already sends, its button opening the console on 7d.
What the board left open:

- **When.** Sunday from 18:00, local time, checked every ten minutes and after each sync; once
  per Sunday (`state.json` `"notifications"."weeklyRecapSent"`). A Sunday evening omaseed did
  not run is skipped: Monday's notice would be about a week already over. It covers the server
  omaseed is on (the ledger is per server), and a week that gave nothing sends nothing.
- **The week** is the seven days ending that Sunday, Monday to Sunday, as the console's 7d.
- **The numbers**, each as the console already counts it, so the notice and the console it
  opens agree: given is the ledger's days; the ratio is the server's all-time given ÷ taken
  (the totals' ratio; a week's own ratio is ∞ for a library that only seeds); the streak is the
  ledger's; "rare torrents kept alive" are the keeping-alive block's rare ones now (complete,
  seeding, at or under the rare threshold of seeds); "paid back" are the torrents whose ratio
  crossed 1.0 this week, from what the ledger saw each one give; the best day is the weekday
  that gave most. A part with nothing to say (no streak of two days or more, nothing rare,
  nothing paid back) is left out.
- **Words.** Title "Your week", the board's two lines, the button "Open the console" (also the
  click). The ratio prints as the console prints it ("2.40", where the board's mock says
  "2.4"). The button was "open the console" while Omarchy's toasts drew no buttons; Omarchy 4.5
  draws a notice's actions in sentence case, so every omaseed notice button is sentence case
  now (ruled 2026-10-21, #111, 3148dc9).
- **The setting** is a fifth switch under Settings › Notifications, "Weekly recap", off.

The pure part is `core/WeeklyRecap`.

## C7 — Rows that are sending pulse (board 22 T5)

Board [22](22-seed-console.html) draws T5 as a 2 px green tick at a row's left edge,
breathing (opacity .25 → 1) faster the more it uploads; idle rows stay still; off under
reduced motion and only while the window is visible. The decision sheet lists it as "a setting,
off by default". What the board left open:

- **The setting** is the last row of Settings › Appearance, "Pulse sending rows", off.
- **Sending** is what the ↑ column shows as more than 0. The beat comes in four steps, so a
  speed that drifts every poll doesn't keep changing it: 2.6 s under 128 KiB/s (the board's
  96 KB/s row), 2.1 s, 1.6 s from 512 KiB/s, 1.1 s from 1.5 MiB/s (its 2.1 MB/s row).
- **Cost.** Every frame of an animation repaints the whole window (#49: the tide's halo at the
  display's rate cost about a core in a software-rendered box). So the rows share one clock
  that steps every 80 ms, about 12 times a second, instead of one animation each at 60 Hz;
  it runs only while the table is on show, the window is the active one (#49's rule, stricter
  than the board's "visible"), something uploads and the setting is on. A row reads the clock
  only while it is inside the view, so the rows the list keeps built past its edges never
  cause a repaint. When the clock stops, a sending row's tick stays, whole and still.
  Measured on 3,500 torrents with 400 uploading, a Release build in a box with a GPU, % of one
  core over 60 s: 1.6 with the setting off (1.3 before T5), 3.1 with every row on screen
  sending, 2.3 sorted by name, 1.8 with the window not in use; the halo at 60 Hz costs 16.1.
- **Reduced motion** is the settings portal's `org.freedesktop.appearance` `reduced-motion`
  (`MotionPreference`). It also stills the tide's halo, which board 22's own page stills under
  `prefers-reduced-motion`.

## C8 — A qBittorrent's settings, exported and imported (board 24, ruled 2026-10-03)

Board [24](24-backup-restore.html) §4–§6, D1 a, D2 a, D3 a, D5 a (#32). What the board left
open, or what the API decided:

- **Secrets** (D5 a) are kept out by name and by suffix (`core/QbtSettingsExport`):
  `web_ui_password`, `web_ui_api_key`, `proxy_password`, `mail_notification_password`,
  `dyndns_password`, qBittorrent 4.1's `ssl_key`/`ssl_cert`, an RSS rule's
  `torrentParams.ssl_private_key`, and any key ending in `password`, `_api_key`, `_apikey`,
  `_secret`, `_private_key` or `_token`. A file read back drops them again, and the import
  never sends one.
- **Machine-specific** (§6), unticked on import (`core/QbtSettingsImport::machineGroup`): the
  board's keys, plus `random_port`, `ssl_listen_port`, `announce_ip`, `announce_port`,
  `embedded_tracker_port`, `embedded_tracker_port_forwarding` and `current_interface_name`
  (network, for the board's own reason: a port forwarded to one machine), and `use_https`,
  `web_ui_upnp` and `alternative_webui_enabled` (Web UI: each can cut the connection the
  import goes through).
- **Folders of new categories and RSS rules.** §6 calls them machine-specific; the settings
  sheet has no "from → to" rows (those are the torrents sheet's, #33). So one more row under
  "this machine only", "category and RSS rule folders", unticked: then a folder under the
  file's default save path (or incomplete folder) moves to the same place under the target's
  (the file's own, when its save path row is ticked), and any other absolute folder is left
  out, so qBittorrent uses its default (`<save path>/<category>`). Ticked, they are kept as
  exported.
- **Matching.** Categories, tags and rules by name, feeds by URL, never by an id either
  machine gave them. What is already there is kept as it is, including a category's folder.
- **Categories' share limits** are exported, but qBittorrent 5.2.3's
  `torrents/createCategory` and `editCategory` take none (checked on the playground), so they
  are left out and the sheet counts them.
- **Order**: categories, tags, RSS folders, feeds, rules, then the preferences, last (the
  board's §8 puts RSS after them): a ticked Web UI change could move the connection the rest
  goes through. A step that fails is reported and the rest still runs.
- **The preferences** go through `ServerPreferences.apply`, as a change made in the panes, so
  the panes show them at once and App follows a Web UI port or login change.
- **Which server.** The pane acts on the server the app is on. Import needs it connected (the
  preview is read against it), so both rows dim while it is not; the board's "import is always
  on" assumed a choice of target that the app's one connection can't offer.
- **Automatic copies** (D3 a) carry `qbittorrent/<server id>.json` for each server: the
  connected server's is read just before the copy; another server keeps what the newest copy
  had. An import can read one from an omaseed backup; it finds this server's by address, then
  by id, then as the only one, since ids differ between machines (#34).
- **Keys**: `0` picks the qBittorrent group's tenth section, Backup.

## C9 — A qBittorrent's torrents, exported and imported (board 24, ruled 2026-10-03)

Board [24](24-backup-restore.html) §2, §4, §5, §7, D2 a, D3 a, D6 a, D7 a, D8 a (#33). What the
board left open, or what the API decided:

- **The file** (`core/TorrentsBackup`) is a plain tar, `.omaseed-torrents`: `manifest.json`,
  then per torrent `torrents/<id>.json` and `torrents/<id>.torrent` side by side, `<id>` being
  qBittorrent's id. It is written once through (`TarWriter`), to a temporary name that takes
  the file's place only when every torrent is in, so a failure or "stop" leaves no half file.
  It is **private (0600)**: a private tracker's announce URL carries the account's passkey, in
  the .torrent files and the records alike. The automatic copies don't carry torrents (D3 a).
- **A record** keeps the board's list. Renamed files are kept against the .torrent's own names
  under the content layout that leaves fewest renames (a torrent moved out of its folder is
  `NoSubfolder`, not a rename per file). `torrents/export` already writes a torrent's current
  trackers into its .torrent; the import still adds any tracker the record has and the .torrent
  lacks. A torrent without metadata (`has_metadata` false, or the export's 409) goes in as its
  magnet link. A torrent deleted while the export runs is left out and counted.
- **Large libraries.** The export reads 20 torrents at a time (.torrent, files, trackers) and
  writes them as soon as their batch is in; the import reads only the headers and records and
  fetches a .torrent when it is sent. Measured on 3,000 torrents (a Debug core, qBittorrent
  5.2.3 on this machine, torrents without data): export 1.0 s, import 10.6 s (adding 6.8 s,
  the check the rest), 54 MB peak for the whole process.
- **Already there** (D8 a) is any of the record's id, v1 or v2 hash on the server.
- **Save paths** (D7 a): the longest `from` a path starts with, at a folder boundary, is
  replaced by its `to`; a path no row covers is kept, and the "!" line counts those. A
  category created first takes its folder through the same rows. The rows start from the path
  maps omaseed has: when the file's server is one of omaseed's (by address) and not the
  target, each of its remote folders goes to where the target sees that same local folder (as
  it is when the target has no map); and the target's own map read backwards, for a file whose
  paths are this machine's. Only rows that change some torrent's path. With none, one row from
  the file's default save path, its "to" left to fill.
- **The add.** Each torrent is added stopped, with its content layout, folders (the incomplete
  folder said either way, so the server's own default can't send a check to a folder the files
  were never in), category, tags, speed limits, sequential and first/last piece; torrents
  whose options agree share one request, up to 20. qBittorrent answers an add before the
  torrent is in its session, so the next steps wait until it is listed. Then renames, file
  priorities, missing trackers and share limits (`torrents/setShareLimits`, as the add takes no
  action). **Automatic management** is turned on only where the category's folder here is the
  folder the files are in and there is no incomplete folder; otherwise it would move them, so
  the torrent stays managed by hand and the result counts it. **The queue**: a multipart add
  keeps no order, so the queued ones go to the bottom once more, one by one, in their exported
  order (409, queueing off, is fine).
- **The check** (D6 a): every added torrent is rechecked; a torrent counts as checked once it
  is no longer checking and was seen checking, or after three polls (a check that ended before
  the first). It starts when it is complete (nothing left of its wanted files) and was started
  or forced at export (forced again). "Complete ones start" is taken as written: a torrent that
  was still downloading stays stopped, whatever it has. Links are not checked: they start as
  exported, unless "leave them stopped", which adds everything stopped and checks nothing.
- **Renames on the target's disk** (#64, #67, upstream#85): where this machine sees the
  server's folders (`FreeSpaceProbe::localPathFor`, as the rename sheets), a rename whose old
  and new names are both on disk would move the one over the other, so it is not sent, and
  that torrent is never started; the result names it, as a warning. A new name alone on disk
  is the expected case (qBittorrent only notes where the file is); an old name alone is a plain
  rename. Where the disk can't be seen the renames are sent (#64's rule).
- **Resumable** (§7): a journal per server, `$XDG_DATA_HOME/omaseed/imports/<server id>.json`
  (0600), keyed by the file's date, server and count, holds each torrent between its add and
  its last step, saved after every batch and every poll and removed when the run ends. A run
  that fails or is stopped keeps it; the same file read again shows those torrents as "added
  last time, not finished" and finishes them instead of skipping them.
- **Which server**, as C8: the one the app is on, connected; one export or import at a time,
  its row saying how far it got, its button "stop". Not in the demo.
- **A .torrent the server can't read** (#71). qBittorrent 5.2.3's `torrents/export` repeats a v2
  or hybrid torrent's piece layer once per file sharing its "pieces root" (files of the same
  content), and libtorrent then refuses that .torrent: `torrents/add` answers 415 for the whole
  request (upstream#97). `TorrentFile::repairPieceLayers` cuts such a layer back to one copy (only
  whole copies of it; the info bytes, so the hashes, untouched), on export and again on import,
  for files exported before. A 415 that is left is about one torrent, not the server: the batch
  goes again one torrent at a time (a 409 then means the batch had added it), the one refused is
  named ("qBittorrent couldn't read the .torrent of …") and the import goes on. Board 24 §7's stop
  is for a server gone or a disk full, so this keeps to it (confirmed 2026-10-22, C20).

## C10 — A server is tested before it is saved (#68, ruled 2026-10-15)

Settings › Servers' sheet works like first run (docs/14, M2): a server is not saved until a
test has said something about it.

- **A new server:** `save` is dimmed, out of the `tab` cycle, with "test first" at the right
  of the sheet's footer, and `test` is the primary action (`enter` in a field tests). A passing
  test makes `save` the primary. A failing one turns the third button into `save anyway`
  (not the primary: `enter` tests again), so a server that is down right now can still be
  added on purpose.
- **A test answers for the address and login it ran with** (URL, auth mode, username,
  secret). Changing any of them clears the answer and `save` waits again; the name and the
  path map are not part of it. An answer that comes back after the fields changed is dropped.
- **Editing an existing server** — *confirmed 2026-10-22 (C20):* it needs a new test only when its
  URL, auth mode or credentials (username, a newly typed key or password) changed. A rename, or a
  new path map, saves straight away; putting the old address back saves straight away too.
- **Q23 still holds:** the test that let the server be saved, passed or failed, is carried
  onto the new row; one for other fields is not.
- `Kit/Sheet` keeps its buttons when only their labels or states change, so the keyboard
  stays on `test` while `save` changes under it.

## C11 — Whose settings, which backup (board 34, ruled 2026-10-15)

Board [34](34-settings-scope.html), D1–D9, each the recommended option (#70). What the board
left open, or what building it decided:

- **The scope lines** (D1 a, D2 a) are `Views/ScopeLine`: the Kit banner's look (an accent rule
  and the `i` glyph) on the idle fill, wrapped, the link in accent with its key dim before it.
  `Kit/Banner` is one line high and could not hold them. qBittorrent's line names the other
  server when there is one ("private has its own: s switch server"), says "the other servers"
  when there are more, and has no link with one server.
- **`t`** (D1 a) works in omaseed's Backup only, from the nav or the pane, and lands on
  qBittorrent › Backup with the keyboard on Export torrents. The help sheet has no Settings
  section, so the footer is where `t` is listed — *confirmed 2026-10-22 (C20).*
- **omaseed's Backup** names whose qBittorrent settings a backup holds ("with public's and
  private's qBittorrent settings") and reads the connected server's settings when it opens, so
  a backup made by hand carries them as they are (before, only after an export or a weekly
  copy). The automatic copies' row no longer repeats that, since the scope line says it.
- **The rows and results** (D2 a) name the server; the export rows' captions end with the file
  they propose ("Saved as qbittorrent-public-2026-10-16.json"). The settings import sheet's
  columns are "public now" and "from private" (the file's server; "from the file" when it has
  none), its button "export public first"; the torrents sheet's "from" line names its file's
  server the same way. A file read from an omaseed backup says "From private's settings in
  omaseed-….omaseed, read …".
- **The select** (D3 a) is `Views/ServerSelect`, taking the server list as its model and
  knowing nothing of App, so board 33's palette, if ruled, can share the model. Its list is the
  Kit menu, which gained a footer, a per-row note (not a key) and coloured glyphs for it. The
  dots are the Servers pane's: the server omaseed is on is green when it answers, red when not;
  another is green with its torrents counted when its last test answered, dim and "offline"
  when it failed, dim with no note when nothing is known. Opening the list probes the other
  servers quietly (App::probeOtherServers: the list updates, no server sheet hears it). An
  offline server can still be picked. The list opens with its cursor on the next server, where
  `s` would go.
- **The keys** (D4 a): the select is a nav stop between About and Speed; on it the pane on show
  stays as it was, and `enter`, `space` or `l` open the list. `s` works from every qBittorrent
  pane and from the select; every one of their footers, Backup's too, says "s next server".
  Below 700 px the nav folds into tabs and there is no select; `s` still switches —
  *confirmed 2026-10-22 (C20).*
- **After a switch** (D5 a) from the select or `s`, the footer's right end says "omaseed is on
  private now · the library too" for 8 s; Settings › Servers' `d` doesn't add it.
- **Copy from another server** (D6 a) is a row of its own between Import settings and the
  torrents, with a select of the other servers (`space` on the row opens it) and `copy…`. The
  other server is read by `QbtSettingsFetch`: a client of its own with the stored login, no
  retries, 8 s per request, a probe, the export's five reads, a sign-out after a password
  login. What it read is kept as that server's newest settings for omaseed's next backup.
  **The fallback** is the newest of what this run read of that server and what the newest
  automatic copy holding it has, by the settings' own date, so a server read earlier in the
  session needn't wait for a weekly copy — *confirmed 2026-10-22 (C20)* (the board says "the newest
  omaseed copy"). The sheet's line says why it couldn't read ("no answer in time",
  "connection refused", "wrong username or password") and the date of what it shows. With no
  other server the row is dimmed and says to add one. A demo's "other server" is the demo
  server's own settings with one queue limit moved.
- **The MIME types** (D7 a) are `data/io.github.diogochaves.omaseed.xml`, installed to
  `share/mime/packages` in the `mime` component. A `cmake --install` outside `DESTDIR` runs
  `update-mime-database` on its prefix; a package (the PKGBUILD's `DESTDIR` install) leaves it
  to shared-mime-info's pacman hook, so the PKGBUILD needed no change. The settings export
  stays `application/json`, with no magic rule (the board's reason: key order).
- **The pickers** (D8 a): Import settings opens on "qBittorrent settings or omaseed backup
  (*.json *.omaseed)", then each alone, then All files. `BackupFormat` reads a tar's manifest or
  a JSON file's "format": a torrents export chosen there is sent to its own row, anything else
  is "neither a qBittorrent settings export nor an omaseed backup". Restore's picker keeps its
  name filters; the MIME type adds nothing a name filter doesn't there.
- **D9 a:** the .desktop entry's MimeType is unchanged, and `tst_mimetypes` checks it.

## C12 — Servers in any state, and the error notice (boards 33 and 35, ruled 2026-10-16)

Board [33](33-servers-any-state.html) (#69, #72), D1–D9 and D11–D23 as recommended, built; D10,
D24 and D25 (the tray and the widget's shared menu, docs/20's changes, "In the bar") are C13.
Board 33's notice look (D21, D22) was ruled on its own board,
[35](35-error-notice.html), N1–N8 as recommended, and is built with it. With two servers or
more a way to the others is always on screen; with one, nothing offers to switch (D19).

- **Last seen (D5 a)** is `ServerListModel`'s: per server the torrent count and when, or why the
  last try failed ("sign-in refused", "didn't answer") and when, in state.json's `servers`
  section (`<id>.lastSeen = {torrents, at, error, detail}`). It is written as the app syncs, at
  most once a minute while the count does not move, on a failure, and on a switch away. No
  request goes to a server the app is not on. The list's `serverState` and `countLabel` roles
  give the current server's live state (live, syncing, down, refused, certificate) and the
  others' remembered one. Board 34's `ServerSelect` (C11) still probes the other servers when
  its list opens; the palette and the rail don't (D5 a's "no requests").
- **The rail (D1 a, D7 a, D12 a, D16 a–D18 a):** the servers are a block pinned under the
  scroll, the idle fill under a hairline, "servers · N" with a fold chevron, each row a dot and
  its count ("down", "!" after a failed try). Folded (`z`, the heading, `enter` on the line) it
  is "● NAME ▴ · N servers", app-wide in state.json's `general.serversFolded`, open the first
  time. The short-window rule leaves the filters at least 5 rows (and their heading); below
  that the block shows folded without saving it, and a hand unfold opens it over the filters,
  opaque with an accent edge, until `esc` or a pick. 5 rows at most, then "… N more · ctrl+s".
  With two servers the rail shows in every state but the log face; while there is no list
  (empty, first sync, lost or refused before a list) it is the block alone, `serversOnly`.
- **The colours follow docs/35 N3, not board 33's mocks:** a server omaseed retries on its own
  (didn't answer, can't be found, too slow) is yellow everywhere: the notice, the hero's dot,
  the rail's "down", the add sheet's chip. Red is for a server that needs you (sign-in refused,
  403, certificate, an unreadable answer) and for a failed action.
- **The switch (D2 a–D4 a, D14 a)** is one `App.switchServer`, which makes the server the
  default as `setDefaultServer` did. `ctrl+s` (app-level) opens `Views/ServerPalette`; the
  current server first ("on it now", live figures), the cursor on the next, `,` to Settings ›
  Servers on the cursor's row. The hero's server line takes a ▾, a hover fill and a click (the
  lone dot at step 4); its tooltip adds "switch server · ctrl+s". `:` lists "switch server…" and
  one "switch to NAME" per other server (`switchTo:<id>`), and both run while the server is lost
  (`Keymap.isOffline`). The read-only guard lets `ctrl+s` through.
  *Changed by C24 H15 (v0.5.0): the server line, its ▾, hover and click are the status line's
  server lane (`MainView.serverLaneText()`), the same words; the footer's "last sync HH:MM" is
  "list from HH:MM", as the notice says it, and a live server's line ends in "synced now".*
- **One server (D19 a):** no block, no ▾, no `:` rows, no hint; `ctrl+s` answers in the footer
  "one server · add another in Settings › Servers"; the `?` sheet lists it dim as "Switch
  server, with two servers or more".
- **Words (D6 a, D9 a, D11 a, D23 a):** `core/ErrorWords` is the one table (a QtTest row per
  line of board 33's): the window's notice, the add sheet, the footer's failed actions and the
  desktop notice all read it. The desktop notice's summary is the window's headline and its body
  the window's line, with the retry figure the engine counts down (Q16): "private didn't answer"
  / "Nothing is listening at 127.0.0.1:8091. omaseed tries again in 10 s." The hero says "NAME ·
  sign-in refused"; a refused sign-in is not retried until `R`, a new login or a switch; the
  footer says "not synced yet" where it said "last sync" and nothing. A key the guard stops
  says "private is unreachable: p waits until it answers". Settings › Servers says "in use" and
  "switch to it" (the server sheet's button too), its footer names `d` (with two servers) and
  `a`, and its other rows keep what was seen last ("4 torrents last time", "sign-in refused
  today 23:28"). A 403 to a cookie login reads as a ban ("NAME refused omaseed"); to a key or
  to no login, as a refused sign-in.
- **The notice (docs/35 N1–N8)** is `Kit/Notice`: under the hero, the window's width, the body
  starting under its line; in a sheet (`Kit/Sheet.headBand`) under the head, edge to edge;
  from the footer, docked above it, opaque, over the table. It grows in over 120 ms (at once
  with reduced motion) once the server is declared lost. The line keeps the retry countdown in
  a slot of fixed width; under about 600 px the actions go under the line. The lost server's
  line adds "The list below is from HH:MM." before the retry, where board 33 put it after —
  *confirmed 2026-10-22 (C20).* Actions: "retry now", "edit login" (the server sheet, editing, the
  keyboard on the password) or "review certificate", then "switch server" with two servers.
- **The footer's failure (docs/35 N8):** a refused action is one sentence with a red ✕
  ("Field Recordings wasn't moved: qBittorrent can't create or write /proc/omaseed") that stays
  until the next key, with "details" opening the band above the footer. The next key (`esc`
  included) closes it and clears the sentence — *confirmed 2026-10-22 (C20)* (the board names esc, a
  click outside and "details" again).
  *Changed by C18 D2 (v0.4.0): an action on torrents from the table says it in the outcome
  strip, its "details" opening the same band above the strip. Other actions (the details
  tabs, settings, an add) keep this sentence.*
- **The add sheet (#72, D13 a–D15 a, D20 a):** with two servers its title is "Add torrent to" and
  a chip (the server's dot, its name, ▾) that opens the palette, as `ctrl+s` does; the foot never
  says "server: NAME" (one server: "Add torrent", nothing). A switch from the sheet keeps it open
  with what was typed, category and tags kept by name; once the new server's first sync is in, the
  default save path (unless one was typed), free space, the library check and the files match are
  read again and one yellow line says what moved, a category or tag the new server lacks named
  "new there and will be created" — *confirmed 2026-10-22 (C20)* (the board marks the chip "new").
  While the server is down the band is yellow ("NAME isn't answering"), free space says it is
  unknown, and `add when back` holds the add (as stopped or not) and sends it, built from the
  fields as they are then, when the server answers or the app is on another one that does. A
  refused add is the red band in ErrorWords' words.
- **The server sheet's failed test (board 33's N5, #126):** docs/35 N7's band under the sheet's
  head, edge to edge, red, or yellow for a timeout (C17 D11), in `ErrorWords::forTest`'s words
  ("Nothing answered at 127.0.0.1:8095" / "Check the address and port, and that qBittorrent's
  Web UI is on (Tools › Options › Web UI)."), with no actions of its own ("test" and "save
  anyway" stay the sheet's buttons, C10) and the technical text under "details" with copy. The
  line under the fields now says only "testing…" and a pass. First run keeps its own line.

## C13 — The tray and the widget share one menu, and one setting (board 33 §11, ruled 2026-10-16)

Board [33](33-servers-any-state.html) D10, D24 and D25 as recommended (#73). Choosing the widget
loses nothing the tray had: the tray and the widget popup's ⋯ show one menu, built once in the
app, and Settings offers the two as one choice.

**This entry supersedes**, as board 33 §11 lists, the lines of the widget's ruled boards it
contradicts; the boards themselves stay as ruled: [docs/20](20-companion-widget-contract.md)
§1 (Round 4 "No start/stop buttons", Q7), §3 (the head's three buttons, the keys), §5 (read
and act) and §8, and docs/07 §1 ("No quit"). [`plugin/STATUS.md`](../plugin/STATUS.md) is the
status file's contract as built.

- **One menu (D24 a):** `integration/BarMenu` holds the rows and answers `invoke(id)`. App
  feeds it (`AppBarMenu.cpp`): the servers from `ServerListModel` (C12's last-seen figures), the
  alternative limits' check and word (docs/31: burst, turtle) and whether the server answers.
  Rows: with two servers or more a "servers" caption, one radio row per server (the current
  one checked, its figure at the right: "4 torrents", "sign-in refused", "first sync") and a
  separator (D19: none with one); then alternative limits, stop all, start all; a separator;
  add torrent…, settings…; a separator; quit, the last row. Alternative limits, stop all and
  start all are off while the server does not answer (docs/20 §3: disabled, not hidden). A
  server row switches through `App::switchServer`; add brings the window up on the add sheet,
  settings on Settings, quit is a real quit (queued, so a D-Bus caller still gets its answer).
- **The order is one for both**, the order the issue gives and W1 draws (servers, limits, stop
  all, start all, add, settings, quit). Board 33's T0 kept the tray's "add torrent…" second,
  under "show omaseed"; it now sits with settings, after start all — *confirmed 2026-10-22 (C20).*
- **The tray (D10 a):** "show omaseed", the two figure rows (Fig. 18), then the shared rows. A
  server row is a radio (the shell draws ✓); DBusMenu has no column on the right, so its figure
  follows the name: "private · 4 torrents" — *confirmed 2026-10-22 (C20)* (T1 puts the count at
  the right). The row's check follows the app, not the click: a refused switch leaves it where the
  app is. A middle click is the menu's alt-speed.
- **The widget (§11 changes 3 and 4, W1, W2):** a fourth head button, ⋯ (`ctrl+.`), draws the
  status file's `menu` under the head, in the card style (Title Case labels from the app,
  UPPERCASE caption), with the popup's own key at the right of alt-speed (A) and add (m). With
  two servers or more the server line ends in ▾ (▴ while open) and a click on it, or `ctrl+s`,
  opens just the server rows. In the menu `j` `k` move, `↵`/`space` pick, `esc` closes it; the
  sections' cursor is hidden meanwhile. A server pick switches and the popup stays to refill;
  alternative limits, stop all and start all close the menu; add, settings and quit close the
  popup. A server row's right-hand words are "N torrents" in ⋯ as in ▾, where W1 printed the
  bare count — *confirmed 2026-10-22 (C20).* With four buttons the server line elides sooner
  ("PRIVATE · QBITTORRENT 5.…"); it was not shortened.
- **Round 4 and docs/07 §1 (§11 changes 1, 2):** still no start or stop buttons and no
  per-torrent start or stop (`p`/`P` unbound); stop all and start all are menu rows. Quit is
  the menu's last row, after a separator, in both.
- **The status file (§11 change 5):** within v2 (board 31 D12's rule; `v` stays 2), `servers`
  `[{id, name, current, state, count, seen, reason}]` (state ok · down · refused · first-sync ·
  unknown; a certificate refusal is "refused") and `menu` `[{id, label, kind, checked, enabled,
  detail, urgent}]`. `detail` and `urgent` are added to §11's row fields so a row says its figure
  without the widget joining it to `servers` — *confirmed 2026-10-22 (C20).* `label` is the card
  style; the tray keeps its lower-case words. Captions and separators have ids of their own
  (`servers`, `separator:…`) that do nothing. A widget that does not know them, or an app that
  does not write them, shows the popup as before (no ⋯, no ▾).
- **The bus (§11 change 6):** `SwitchServer(s id) → b` and `Invoke(s id) → b` on
  `io.github.diogochaves.omaseed.Status`. Ids: add, alt-speed, stop-all, start-all, settings,
  quit, `server:<id>`. Unknown or disabled: false.
- **"In the bar" (D25 a, §11 change 7):** Settings › Appearance has one row, "In the bar:
  widget · tray icon · both · none", in place of the Tray icon and Bar widget rows, and a
  "Widget" row with customize. The heartbeat and `trayIconMode` are unchanged; the row is read
  from the widget being in the bar and the mode, and writing it sets both: widget = in the bar
  + auto; tray icon = out + auto; both = in + always; none = out + never. Nothing stored is
  migrated: the old settings read as one choice — in the bar with "never" reads as widget (auto
  hides the icon while the widget runs anyway), out of the bar with "always" as tray icon —
  and the first pick writes the mode it stands for. Without the shell or the packaged widget,
  widget and both are off; while the widget goes in or out the row shows the choice asked for.
- **The tray heartbeat note (§11 change 7):** unchanged; `plugin/STATUS.md` says the two are
  one choice in Settings.
- **The manifest version** stays: the contract changes within v2 and the widget ships with the
  app.
- **The window, a third place (C24 H9, v0.5.0, as built):** the rail's mark and `ctrl+.` open
  the omaseed menu (`Views/AppMenu`) under the mark. Its rows are these, in this order, from
  the same `BarMenu` (`App.appMenuRows`, in the tray's words; a pick goes through
  `App.invokeAppMenu`, the tray's and the widget's invoke), with a head (omaseed, its version,
  the status line's server words) and, before quit's rule, a "this window only" block of the
  window's own rows: seed console `` ` ``, log `~`, commands `:`, keys and help `?`, hide window
  `q`. Each row teaches its key (alternative limits `A`, add `a`, settings `,`, quit
  `ctrl+q`); quit reads "quit omaseed" and asks first while a move runs, as `ctrl+q` does, and
  alternative limits leaves `A`'s note. The old ⋯ menu's other rows (speed limits…, alt-speed
  limits…, columns…, select all, fold rail) are `:` rows and keys.

## C15 — Move a folder (board 42, ruled 2026-10-18)

Board [42](42-move-a-folder.html) M1–M20, all as recommended (#95). A folder of the library,
not a selection, moves to another drive as one job that can run for hours: every torrent keeps
its place below the folder, a preflight checks before anything is sent, the job runs in waves
it can pause, survives a hidden window and a restart, and ends with one summary. (C14 is the
engines ruling, kept on the `engines` branch until that work lands; the board's M numbers are
its own, not the manual pass's M8–M18 below.)

**This entry changes C2 D2 for moves made by a job:** the log is read on every poll for the
job's whole life, not 30 s. Set location keeps D2.

- **Picking (M1, M2, M3):** a folders group in the rail, after categories: each save path is a
  row, a save path under another sits under it, a drive's top with no torrents of its own is a
  dim foldable heading (one that is itself a save path is a row, "4 here"). Picking one filters
  the table to it and everything below, as a category does. The row shows the name and the
  torrent count (the job's % while it moves); bytes on disk (qBittorrent's `completed`), save
  paths and loose files go in the scope line and the row's tooltip. The scope line offers
  "move this folder…"; so do `shift+m`, the row's menu and `:` "move folder…" (`m` stays set
  location). The Save path column cuts from the left.
  *Changed by C24 H5 (v0.5.0): the scope line is the title bar's: the folder's name is the
  title, and its sentence takes the facts in board 46's order, "49 torrents · 1.32 MB on disk ·
  2 loose files · PATH" ("loose files" for "files no torrent owns", the save paths when there
  are several); while that folder moves, "moving to PATH · 17 of 49 landed". "move this
  folder…" is a title-bar tool. A move's "show" (M10) is the title too ("The move of NAME",
  "From the move sheet"), the line's link a tool beside it: "all torrents", or "back to the
  sheet" with esc.*
- **The field (M4):** the folder's new path, prefilled with its current one; browse picks a
  parent and the sheet adds the folder's name. Each torrent's new save path is the new path
  plus its save path below the folder.
- **Room (M5):** bytes on disk plus the loose files when carried, against free space read on
  this machine (or qBittorrent's figure for its default drive) less the low-disk threshold
  (Q16). Short turns the move off with the figures; can't tell is a yellow line and the move is
  allowed; the same drive needs no room. Read again before every send.
- **Writable (M6):** qBittorrent 5.2.3 answers 200 for a folder it can't write
  (diogochaves/upstream#120). A local check against the owner of the folder's files when
  omaseed sees the disks, and always the smallest torrent alone first: if it fails, the job
  stops with its reason before anything else moves.
- **Automatic management (M7):** a yellow line with the count and the category's folder;
  nothing changed (setLocation turns it off; turning it back on would move the torrent back).
- **A file already at a torrent's target (M8):** qBittorrent keeps it and reports the move done
  (upstream#123). Those torrents are left out and listed in red; the rest go.
- **Loose files, which (M9):** under no torrent's content path and none of a no-subfolder
  torrent's files; never loose: `.parts`, `*.!qB`, `Thumbs.db`, `desktop.ini`, `.DS_Store`
  (the list is shown); changed in the last 15 min stays; unreadable is unknown and stays; the
  preflight's list is the dry run; with no local view nothing is shown or carried, and the sheet
  says so.
- **The running job (M10):** a band under the hero in board 35's box (C12), accent kind, with a
  2 px progress line; pause / show / details. "show" lists the job's torrents in the order sent,
  with a Move column (moved, failed, moving now, queued, waiting, left out).
- **Pause and end (M11):** pause stops sending; the ones already with qBittorrent (3 at most)
  finish. End the move = pause, then the summary; nothing moves back; "move the rest…" reopens
  the sheet and skips what is already there.
- **Order (M12):** smallest first, keeping 3 in qBittorrent's queue (one moving, two behind).
- **Hidden, quit, restart (M13):** hidden to the tray the job goes on, the tray tooltip carrying
  its line (M20). Quit asks (keep running / pause and quit). After a crash, a logout or a reboot
  the job resumes by itself at the next start. It is saved in
  `~/.config/omaseed/moves/<server id>.json`.
- **Stuck (M14):** nothing landed for 4 × the current torrent's expected time and at least
  10 min (a file growing at the destination counts as progress): stop sending, a yellow band
  that says a qBittorrent restart usually frees it, one notice; after the restart the job sends
  again what hadn't landed.
- **Failures (M15):** a failure marks the torrent and gets C2 D1's block; the job goes on. No
  space, the first torrent failing, or stuck pause it. Errors and missing files in the next 24 h
  are tied to the job's record.
- **The end (M16):** a green summary band until closed, its record in details; one desktop
  notice (a job that pauses itself gives one too); recheck the failed ones smallest first and
  start those that come out complete; retry them to the job's target; set location….
- **Carrying loose files (M17):** automatically after the last torrent lands, when the
  preflight's tick is on (on by default when omaseed sees the disks): copy to a temporary name
  hashing as it reads, read back and compare, rename into place, remove the source; the same
  drive is a rename; the same file at the target counts as carried, a different one leaves
  both; folders left empty at the source are removed. Nothing is ever deleted otherwise.
  (#98) The sheet's dry run is what is carried, kept beside the job so a restart carries the
  same list; a file changed since (size or time) or in the last 15 min stays. A full
  destination drops the partial copy and stops the carry; pause does the same and waits.
- **More than one move (M18):** one job per server at a time; a second is queued after it.
- **Words (M19):** core/ErrorWords, board 42 §8: 409 "qBittorrent can't create FOLDER", 403
  and a 200 followed by "Permission denied" in the log "qBittorrent can't write to FOLDER",
  never a sign-in word. (#98) On 5.2.3 every unwritable destination tried (an existing 0555 or
  0000 folder, a root-owned one) logged "Permission denied", from file_rename, file_open or
  mkdir, naming the source file; "No such file or directory" reads "qBittorrent can't reach
  FOLDER … Check that both drives are mounted."
- **A torrent still downloading into the incomplete folder (#98, checked on the playground):**
  setLocation changes its save path at once and moves nothing; its data stays in the
  incomplete folder and qBittorrent moves it to the new place when the download finishes (the
  move's log lines come then). The job counts it moved at once: none of its data is in the
  folder being moved.
- **Outside the window (M20):** the tray tooltip carries the job's line; the bar widget is
  unchanged (a docs/20 change, later).

## C16 — A better set location (board 43, ruled 2026-10-18)

Board [43](43-better-set-location.html) L1–L12, all as recommended, and L13 (#101). Every move
is a job: a folder (C15) and a selection are two ways to start one, on the same checks, waves,
saving and summary. (C14 is the engines ruling, kept on the `engines` branch until that work
lands.)

**This entry changes C2 D2 for every move:** the log is read for the move's whole life (M15).
D2's 30 s watch stays for renames and deletes.

- **One move for both (L1):** set location runs on board 42's job. The folder move's sheet, in
  a selection mode, replaces LocationSheet: every move gets the checks, the waves (smallest
  first, the first alone, 3 queued), saving and resuming, and the log for its whole life.
- **The name (L2):** "Move…" in the menus, the palette and the sheet ("Move 4 torrents",
  "Move NAME…"); `m` kept; `:` still finds "set location".
- **Which checks show (L3):** board 42's lines (M5–M8) and the shared-data guard (#28/#35),
  each only when it says something; room only across drives; when all pass, one green line.
- **Places (L4):** a list under the field on ↓ or typing: recent destinations (the last 8 per
  server, in state.json), the library's folders with counts, the categories' folders; then
  browse.
- **Several save paths (L5):** when the selection's save paths differ, a choice: "into one
  folder" (the default, today's behaviour) or "keep their places below ROOT" (board 42's rule;
  ROOT is their deepest common folder).
- **The band (L6):** it appears once a move has run 3 s; a move that lands sooner shows none.
- **A quick move (L7):** the footer's note line (D2's) says it landed, "2 moved to …", for
  6 s; no band, no notice. *Changed by C18 D1 (v0.4.0): the outcome strip says it, and the
  row's status cell.*
- **The notice (L8):** for a selection, one desktop notice only when the band showed and the
  move ran past a minute or the window was hidden at the end. A folder move keeps M16 (always
  one).
- **Failures (L9):** the log is read for every move's whole life (M15); a failure gets C2 D1's
  block and the summary's recheck smallest first / retry. *Changed by C18 D1 (v0.4.0): a
  quick move's failure is the red strip, no longer the footer's note.*
- **Live bytes (L10):** for a torrent moving, MoveJob's growth probe reads the target's file
  sizes every 2 s through the path map and feeds the band's bytes, %, time left and the row;
  without a local view the band says it can't tell.
- **Move back (L11):** on the summary of every job, folder or selection: the sheet with each
  torrent to its own old place, checked as any move; it says automatic management stays off.
- **A move while another runs (L12):** checked now, queued behind it ("move after games"); the
  band names what is next; details lists the queue, each with cancel. M18 for every move.
- **A server whose disks omaseed can't see (L13):** a NAS or a remote server without a path map.
  The sheet says once which checks ran on the server (through qBittorrent's API) and which
  can't run here, so a remote move never reads as checked as a local one. What runs on the
  server: the destination's existing names, through `app/getDirectoryContent` (M8's guard,
  #102), and the probe torrent sent alone (M6). What can't run here: room (unless qBittorrent's
  figure for its default drive applies), the local writability check, the loose files' age,
  size and carry (their names are listed as staying behind), live bytes (L10).

## C17 — Skipped files and the disk (board 44, ruled 2026-10-21)

Board [44](44-skipped-files-and-the-disk.html) D1–D13, all as recommended; D10 ruled "drop"
earlier the same day. A file a torrent starts writing is created at its full length at the tick,
even on a stopped torrent; on `ntfs3`, `exfat` and `vfat` that allocates it in full and skipping
it later keeps it (#116). qBittorrent never deletes a skipped file and its API can't, so omaseed
shows that space and frees it where it can see the disk.

**This entry changes N1:** a big widening of a torrent's selection waits before it is sent (D5).

- **On disk (D1):** a column in the Files tab with each skipped file's allocated size, and a
  footer count; only when some skipped file holds space, only when omaseed can see the folder
  (board 25's FreeSpaceProbe check). #118.
- **The band (D2):** yellow, when skipped files hold more than 1 GiB or 5 % of the torrent. #118.
- **Free the space (D3):** stop, delete the skipped files with no downloaded piece (inside the
  content path, not shared, not open), start, no recheck. Files with pieces: on v1-only
  torrents included with a recheck after the start; on a hybrid left, with "add it
  again" and its cost named, because a recheck there hits libtorrent 2.1.2's
  `torrent_inconsistent_hashes` (#115). Board 28 names that error on a hybrid. #118. A v2-only
  torrent is treated like a hybrid (C29, #182).
  `shift+f` has its keymap row (the `?` sheet's "F free the space") and the Files tab's `:`
  palette offers "free the space…" (#160); both, like the key, whenever a skipped file holds
  space on a disk omaseed sees, the band or not.
- **Disks omaseed can't see (D4):** an "at least" hint from skipped files with progress above 0 %,
  pointing to path maps, no action. A NAS share mounted here and mapped counts as seen. #118.
- **Hold back a big selection (D5–D7):** a widening that adds more than 1 GiB, or the whole
  torrent, waits 3 s (restarting with each change), running or stopped, with Undo and Send now;
  rows change at once, skips go at once, leaving sends, a net-zero change sends nothing. #119.
- **Name the driver (D8):** for `ntfs3`, `exfat` and `vfat` only, Save to, the Move sheet and a
  Files tab tick name the driver and count the full size of every newly ticked file at the
  tick. The newer `ntfs` driver, Linux filesystems and unmeasured types get no line. #120.
  A tick that doesn't fit there always waits in D5's band, whatever its size, so it can be
  undone before anything is written (C28, #159).
- **FAT32 (D9):** files of 4 GiB or more on `vfat`: a red line naming them, with "Skip these";
  the add stays possible. #120.
- **No incomplete-folder hint (D10):** omaseed never suggests one: it doubles the space a
  torrent uses while downloading and adds a move step (#117).
- **Stuck move (D13):** a finished torrent whose `content_path` is still under its
  `download_path` gets a red problem with the log's reason, Show both folders and Set
  location…. #121. Built: `ProblemMatch::isStuckMove` (progress 1, not moving, a download path
  other than the save path, the content under it and not under the save path) and
  `describeStuckMove`; `ProblemLog` publishes it as the torrent's `problem` (an error, when
  there is one, wins), reads the log once when one turns up, and drops that move's yellow
  failure. The row's status cell reads "move failed" with ✕ in the error colour
  (`TorrentModel::StuckMoveRole`). The partial copies are named (`PartialCopies`, "N files,
  SIZE") where `FreeSpaceProbe::localPathFor` sees the destination, "No partial copies are
  left" when there are none, otherwise "may be left". *Not as the board drew it:* "Set
  location…" opens board 43's sheet as for any move, not aimed at the destination it never
  reached: qBittorrent ignores a move to the save path it already has, and a recheck is what
  moves it once room is made (`scripts/dev/research/121-stuck-move`). The block offers that
  recheck, `c recheck`, between the two, and says "Once there is room, a recheck moves it
  there." (C28, #158).
- **Words (D11):** a timeout reads "home isn't answering" (yellow, waiting); refused or no route
  "home is unreachable" (red). #122. Built: `ErrorWords` gives a timeout the kind `waiting`
  (W1's line, the seconds since the server last replied, `GlobalState::silentSeconds`, and
  "waiting · N s" where the countdown would be) and every other connection failure
  `unreachable`, red, still retried. This replaces C12's yellow for "didn't answer" and "can't
  be found": a name that doesn't resolve is red too (*assumed*: nothing is there, as for a
  refusal). The hero, the rail (a current server that times out reads "waiting"), the server
  palette, the add sheet, the read-only guard and the desktop notices follow ("home answered
  again" after a timeout); the bar widget's status and menu keep their contract ("down"), with
  "not answering" or "unreachable" as the menu's reason.
- **Busy disk (D12):** later, its own issue: say "busy writing to its disk" only when the local
  disk's activity shows it. #123.

## C18 — After a move (board 45, ruled 2026-10-21)

Board [45](45-after-a-move.html) D1–D9, all as recommended, with D7 made precise in the ruling
(below). From Diogo's manual pass of the moves: their outcome showed far from the torrent, the
places list had no way in for the mouse, selecting several rows with the mouse was invisible, and
closing a move's summary lost "move back". #130.

**This entry changes C16 L7 and L9 and C2 D1's footer sentence (D1), C12 D22 and board 35 N8
(D2), and adds to C16 L11 and C15 M16 (D7–D8):** a finished move's record outlives its summary.

- **Where a quick action's outcome shows (D1):** a strip in the selection bar's place (bottom
  middle of the table), plus the row's own mark in its status cell while the row is on screen.
  The details' error block stays. v0.4.0.
- **Which actions (D2):** every action on torrents from the table (move, start/stop, delete,
  rename, category, tags…). v0.4.0.
- **How long (D3):** done for 6 s, its line running down; a failure until × / esc or the next
  action; several at once as counts, yellow when some failed. v0.4.0.
- **A long move (D4):** its band and summary stay where they are, under the top bar.
- **The places list's opener (D5):** "places ▾" in the Move sheet's field, before browse; a
  combobox for screen readers. v0.3.0.
- **Mouse selection (D6):** a □ at a row's start on hover (click adds, shift+click ranges), ■ on
  selected rows, a mouse block in the `?` sheet, a faint hint on the selection bar the first 3
  times. v0.4.0.
- **Move back after the summary closes (D7):** in the torrent's details (its last move), the log
  (one line per move) and the palette. Ruled with these rules for moves that overlap:
  - Each torrent goes back only if it is still where that move put it. A later move of other
    torrents never blocks it; only an overlap matters.
  - The sheet names what is in the way: "3 were moved again since, by *move to PATH (14:02)*",
    with "move that one back first". Undoing the later move puts them where this one left them,
    and this one is whole again: undo in reverse order, only where moves overlap.
  - Never a silent partial: the button says what will happen ("move 46 back · 3 stay") and
    nothing is sent until it is pressed.
  - The palette's "undo last move" offers only the newest move. Older moves come from details
    or the log, through the sheet above.
  - A torrent moved outside omaseed (qBittorrent's Web UI, another tool) counts the same: not
    where the move put it, so it stays and is listed. v0.3.0.
- **How long (D8):** the last 20 moves or 30 days, whichever ends first. v0.3.0.
- **When (D9):** D5, D7 and D8 with v0.3.0; D1–D4 and D6 in v0.4.0.

**Built for v0.4.0 (D1–D4 and D6, #130).** `core/ActionOutcome` words the strip and holds the
rows' marks; `App.outcomes` is it, and every table action reports there once its answer is in
(one strip for an action of several requests, such as limits). What the board left open,
taken as its mocks show:

- **The words.** "NAME stopped", "3 stopped", "NAME didn't move", "2 moved to PATH, 1 didn't";
  the line is ErrorWords' headline for a refusal, the log's reason for a move; "details" (when
  there is technical text) opens docs/35's band above the strip. A done strip offers "show"
  (the cursor on the one torrent, or the several selected; a move that took them out of the
  rail's folder switches the folder to their new place); a failed move offers "move…" again.
- **The row's mark** is "✓ moved · FOLDER" (the folder's last part), "✓ stopped" and so on in
  green for 6 s, "✕ didn't …" in red while the strip lasts; a move the log says failed keeps
  "✕ didn't move" while its error block stands (board 45 O2; this amends C2 D7).
- **Its time.** The pointer resting on a done strip holds its 6 s, as the seed toast's; esc
  closes the strip before it clears a selection. The seed toast waits while the strip shows.
- **Which actions:** start, stop, retry, force start, recheck, reannounce, category, tags,
  limits, rename, queue, delete, the four options (sequential, first and last, super seeding,
  automatic management) and quick moves. The details tabs' own actions (trackers, peers, HTTP
  sources, files) keep the footer, as does a failure the log tells only after the answer (a
  delete's files, C2 D2).
- **D4:** the band and summary of a move that ran past 3 s were already under the top bar
  (C15 M10); nothing moved.
- **D6, the row's start.** The board's mocks drew a first column the table didn't have: the
  rows now start with a 16 u space before the first column, empty but for □ on the row under
  the pointer and ■ on selected rows. A click on □ adds or removes that row; shift+click on it
  adds the range from the anchor and keeps the rest, since the □ never takes a row away from
  others; two quick clicks on it are two ticks. The cursor's ▸ of the mocks is not drawn (the
  cursor has its band). The `?` sheet's block, "select with the mouse", follows the select
  keys. The hint, "ctrl+click add · shift+click range", is dim at caption size rather than
  faint (Q19 keeps faint for what is disabled); it shows when a plain click opens the bar,
  the first 3 times (state.json's `hints.selectionMouse`), goes with the bar, and is the
  first thing the bar drops when narrow.

## C19 — From the second manual pass of the moves (ruled 2026-10-21)

Three calls from Diogo's pass of `docs/manual-tests/moves.md`. All v0.3.0.

**This entry changes docs/27 D3's head tick (T1), spec v2's rail `.row.cur` (T2) and reads
C15 M1's "the row's menu" as the rail's folder row (T3).**

- **A half tick (T1):** a click on a half-ticked folder, or the half-ticked head, unticks it;
  the next click brings back what was wanted before (#129). A click never widens a half
  selection to every file: the folder row's priority menu does that. #134.
- **The rail's cursor (T2):** its fill shows only while the rail has the keys. A click still
  moves the cursor, so `j`/`k` start from the row clicked. #135.
- **Move a folder (T3):** from the folder: the rail's folder rows get a menu with "move this
  folder…"; the scope line, `shift+m` and `:` keep it. A torrent's menu no longer offers it.
  #136.

## C20 — What v0.3.0 needed from Diogo (ruled 2026-10-22)

- **The assumed calls:** every "assumed, confirm" in C9–C13 (marked confirmed where it stands),
  in #100 (board 42's build) and #106 (board 43's), and the extras the board 45 builds named,
  are confirmed as built, with one change: a move job's recheck starts only the torrents that
  were running when the job sent them (#138).
- **An errored torrent (#107; amends docs/28):** `p`, the actions menu and the selection bar
  all stop it, so the keyboard and the bar agree. "retry" stays first on the error block and is
  its own row in the actions menu. Once omaseed has stopped it, the row reads "error · stopped"
  (qBittorrent reports only `error`). Ruled after the build: the row reads "stopped" in the
  error colour with the ! glyph, since "error · stopped" doesn't fit the column; General keeps
  "error · stopped · CAUSE". Whether it is stopped comes from qBittorrent (its `stopped`
  filter lists an errored torrent only while it is stopped), so it survives a restart and
  counts a stop sent from elsewhere (#142).
- **The widget's server line (#127):** at the popup's width, the engine's version gives way
  first, then the word "qBittorrent"; the server's name, the state dot and ▾ always stay.
- **Adding in a row (#124):** the add sheet closes as soon as qBittorrent accepts the add; the
  row appears in its sorted place on the next sync, never at the end first; a refusal shows in
  the footer as other actions' do.

## C21 — No recheck for a hybrid after a failed move (ruled 2026-10-22)

A move that fails partway can leave a torrent's files split between the old place and the new.
On a hybrid (v1+v2) torrent that it downloaded, libtorrent 2.1.2 answers a recheck with
`torrent_inconsistent_hashes` when a file with downloaded pieces is missing, and only removing
and adding the torrent again clears it (#115). C17 D3 keeps hybrids out of a recheck when freeing
space; this does the same for moves.

**This entry changes C15 M16's recheck:** the summary's "recheck the N…" sheet leaves every
hybrid out. Each is listed as "not rechecked" with one line saying why, and offered "move
again" (to the move's target) and "move back", which bring its files together; the others are
rechecked as before. A torrent is hybrid when qBittorrent gives it both a v1 and a v2 hash.
v0.3.1. #144.

## C22 — Search matches words, not one string (#125, ruled 2026-10-22)

Release names put dots, underscores or dashes between their words; people type spaces. The
library's search, and the details pane's Files filter, fold both sides the same way before
they meet: case aside, every run of `.`, `_`, `-` and spaces is one space.

- **Every word, any order.** A torrent matches when each word of the query appears in its name,
  category or tags (`bunny 1080p` finds `Big.Buck.Bunny.2008.1080p`). A word is a substring, so
  `1080` still finds `1080p`.
- **Ranked while typed.** The matches list the whole query as one phrase in the name first, then
  its words in the query's order, then any order; rows of one rank keep the table's sort among
  them. Clearing the field brings the table's own sort back. A row found only through its
  category or a tag ranks last.
- **A hash prefix stays exact:** the query as typed, no fold, ranked with the phrases.
- **Files filter:** a plain word folds the same way and its parts must all appear in the path;
  `|` alternatives and `*` words stay as N1 has them, a `*` word matched on the path as it is.
  The Files tab keeps its own order: no ranking there.

The add sheet's library match (docs/25) compares files, not typed words, so it has no fold.
`core/SearchWords` is the one implementation.

## C23 — The move fixes before the post (v0.3.3, ruled 2026-10-22)

From Diogo's own moves on qBittorrent 5.3, each issue's recommended option. They ship in a
patch, v0.3.3, cut from v0.3.2 on `release/0.3`, so the version people install for the post
about moves has none of these bugs; main keeps them for v0.4.0.

- **One torrent's progress (#162 a):** qBittorrent makes a torrent's file at its new place at
  full length before it copies (`scripts/dev/research/162-preallocated-move`), so a first
  reading of the destination already at the torrent's size measures nothing: the band says
  board 43 B2's "moving for … · qBittorrent doesn't report a single move's progress" until it
  lands. *Amends C16 L10.*
- **Added during the move (#164 a):** the summary counts only torrents qBittorrent added after
  the job started (`added_on`), for every kind of move. A torrent's or a selection's root is a
  save path other torrents share.
- **What is in the way (#161 a):** the Move sheet's "left out" line says what is at the
  destination and whose it is: "(an empty file no torrent owns)", "(a file of SIZE that NAME
  owns)"; on a server omaseed can't see, only whose. *Adds to C15 M8.* Offering to replace an
  empty file no torrent owns (#161 b) waits for its own ruling. *C28 rules it for v0.4.0
  (#176):* the line's "replace it" (only for an empty regular file no torrent owns, seen
  here) deletes it as the move starts, checked again then, and the move's line in the log
  ends "after deleting the empty file no torrent owned at PATH".

Two more from the same day's big folder move, added to the patch:

- **Long details (#173):** a notice's unfolded details (docs/35 N4) stop at a third of the
  window, never under a few lines, and scroll inside their block, clipped; the band stays a
  band and what is below it moves down. The keys scroll the block while it has them (Tab
  reaches it when there is something to scroll); esc folds it. Every notice, not only moves.
- **The record's words (#174):** the move record's "later" line uses the status cell's word
  and what fixes it ("later: missing files · set location… or recheck it"), never
  qBittorrent's state name; the record starts at "started …", since the band above already
  says its headline and line.

## C24 — The whole app on v0.3 (board 46, ruled 2026-10-22)

Board [46](46-the-whole-app-on-v03.html) H1–H18, all as recommended. Board 41's thesis (the
window answers the five questions people bring to a torrent client, in words before numbers,
inside one fixed frame) and its five laws, law 2 as tightened in 46 §2 for v0.3's bands, drawn
over what v0.3 shipped. Triage the same day (#152) ruled one style, several displays for the
list (rows, table, overview), two skins (Omarchy and the console) and building board by board.
Boards 39 (D1–D8) and 40 (F1–F10) close as stashed ideas: 46 §9 lists what survives. v0.5.0.

**This entry supersedes** v2's status group and hero buttons, S1's keys 0–8 (H2), C15 M1's scope
line (H5), docs/12's tab names and docs/28's and C18 D7's "in General" (H6), docs/17's floating
selection bar and C18 D1's place (H8), v2's footer hints and C11 D4's footer line (H10), Q1 and
ARCHITECTURE rule 4's default (H13), docs/35 N1, C12's "the window's width", C15 M10's and C18
D4's "under the top bar" (H14), and C12 D2–D4's hero ▾ (H15). Their looks and words stay where
the call says so.

- **The rail (H2):** the five questions (needs you, arriving, landed today, giving back,
  resting) under the views (library, seed, log, search); keys 1–5. Every state stays in the
  filter (`is:stalled`) and the palette.
- **The library (H3–H4):** grouped under the questions in the rows display; the table stays one
  sorted list. Rows is the default for new installs; an existing install keeps the table until
  it picks another. The display is remembered per scope, folders included.
- **A folder picked (H5):** the title becomes the folder, its sentence takes the scope line's
  facts, "move this folder…" is a title-bar button; shift+m, the row's menu and `:` keep it.
- **Details (H6):** 41's story: one fact, a sentence, then story · files · swarm · trackers ·
  facts. The error block and the last move open the story; C17's on-disk column and band sit in
  files. N1 unchanged.
- **The add sheet (H7):** today's sheet in the new skins. 39's additions (type bar, guessed
  category, ready time, program check) wait for the add sheet's own board.
- **Acting on a selection (H8):** the status line becomes the action bar; C18's outcome strip
  floats bottom middle of the rows, just above the bar while a selection is on. C18 D2–D3
  unchanged; D6's first-times hint moves to the bar.
- **Menus (H9):** right-click and enter: verbs grouped, keys right, delete last. The omaseed mark
  (ctrl+.) opens C13's rows in the same order, plus a block for the window (seed console, log,
  commands, help, hide). C13 gains the window as a third place.
- **Keys (H10):** taught on demand (menus, tooltips, the palette, `?`), badges while alt is held.
- **Skins (H11–H12):** Settings › Appearance › skin: Omarchy (the default), Console, Follow the
  desktop (the console when omarchy-console's skin is on). Clean glow only. How the console skin
  is built is decided at its build step.
- **The ground (H13, #113):** opaque by default in both skins; super+backspace works as for any
  app; Appearance keeps "translucent" (the launcher's alpha).
- **Bands (H14):** a server's notice, a job, a creation: under the title, across the content
  only, the rail beside them usable, stacked when there are two.
- **Servers (H15):** the rail's foot (C12's block) and the status line, whose server name opens
  the palette (ctrl+s); one server is named, with no ▾.
- **Search (H16, #145):** a rail view after log (ctrl+f), results in the rows display, add… into
  today's add sheet; plugins in Settings › qBittorrent; also `:` and the add sheet's empty
  state. Board 48.
- **Create a torrent (H17, #146):** "+ add ▾" gets "create a torrent from a folder…" (ctrl+n),
  also `:` and a folder row's menu; while it hashes, the move's band. Board 49.
- **Build order (H18):** frame (rail, title, status line, bands, ground) → library (questions,
  displays) → details → sheets and menus (move, add, the app menu, the action bar) → the console
  skin. Boards 47, 48 and 49 are drawn in this frame meanwhile.

## C25 — Engines in the window, v2 (board 47, ruled 2026-10-22)

Board [47](47-engines-in-the-window-v2.html) K1–K34, all as recommended. It replaces board 37
(#83) inside board 46's frame (C24), with what each engine can do per feature from the engines
matrix (#153). It builds on C14 (board 36), which lives on the `engines` branch until that work
lands; this entry reaches `engines` with the next merge of main. K1–K13 and K29–K32 replace 37's
W1–W16. Built in E3–E6 (#84–#87).

**This entry changes** Q26 (K2), docs/14's welcome and step 1 (K3), C10's "save anyway" for
refused versions (K8), C12's server line (K11), N1's priorities (K17), C4 on Deluge (K20), C16
L10 for Deluge (K21), C12's "didn't answer" on Transmission during a move if verified (K22),
C16 L13 in part and C21's reach (K23), docs/28's and C2 D1's log line, C20's retry first and
C17 D13 on Transmission and Deluge (K24), C5 D11 for Transmission (K26), and M16's note (K30).

- **Finding a server (K1–K3):** first run probes ports 8080, 9091 and 8112 and the engines'
  processes and config files, no HTTP until a row is picked; one row per engine found
  (qBittorrent, Transmission, Deluge), Q26 within one engine; the welcome names the engines this
  build drives, step 1 asks "Which server should omaseed talk to?", install stays
  qbittorrent-nox.
- **Adding a server (K4–K10):** the engine is recognised on test (chips: recognise ·
  qBittorrent · Transmission · Deluge); the address picks which engine is tried first (9091 or
  `/transmission` → Transmission, 8112 or `/json` → Deluge, else qBittorrent); one URL field takes
  Transmission's web UI or RPC address, a proxy's path included; sign-in chips follow the engine
  (Deluge one password); a refused version says why, in red, with no "save anyway", an
  unrecognised answer keeps it; an engine not driven yet is named and refused, keeping what was
  typed; Deluge gets a daemon step when deluge-web knows several or none, stored with the server.
- **Naming the engine (K11–K12):** "NAME · ENGINE VERSION" in the status line's server lane, C12's
  state words in its place while the server is down; also the rail tooltip, ctrl+s, Settings ›
  Servers, the server sheet and the app menu's head; the rail block, tray and widget keep names.
- **Groups (K13–K16):** each section keeps its place, glyphs and keys under the engine's word
  (Transmission's labels where tags are, Deluge's where categories are); Transmission: "new
  label…" only while applying, commas refused; Deluge: lower case shown before apply, Label
  plugin off removes the section with one line in Settings; Transmission's bandwidth groups left
  out.
- **Priorities and limits (K17–K20):** each engine's own priority words; the limits sheet shows
  only what the engine keeps (Transmission ratio and idle; Deluge ratio, then stop or remove);
  qBittorrent 5.3's share-limits mode hidden, sent as Default; no turtle on Deluge.
- **Moves (K21–K23):** Deluge's own %; Transmission counts landed torrents, uses the growth probe
  where it sees the disks, otherwise says it doesn't report how far; if verified, "busy moving,
  answers when done" on Transmission; the Move sheet runs each engine's checks (room on the
  server for all three, Transmission can't list the destination, said once); C21's hybrid line
  only on libtorrent engines.
- **Errors (K24):** docs/28's block in the engine's own words, copyable, no log link;
  Transmission's errored torrent reads stopped and offers "start it here"; Deluge's failed move
  offers recheck first; #121's case is this block on both.
- **Adding torrents (K25–K28):** a v2-only hash refused on Transmission and Deluge; a magnet's
  files before the add as C5 on Deluge, a dim line on Transmission; add options per engine,
  absent when not possible; renaming on Transmission says it renames the folder on disk.
- **Details and what goes (K29–K30):** tabs keep their places; a part filled in part says the
  rest in one dim line; unreported facts are left out, impossible actions absent; what an engine
  lacks is gone from rail, menus, palette and `?`, and its key says so once in the status line.
- **Settings (K31–K33):** a group per engine (TRANSMISSION, DELUGE) with C11's server select and a
  Basics page (port and test, global limits, turtle where there is one, default save path, DHT;
  Deluge adds Plugins), present while a server of that engine is saved; backups and settings
  export stay qBittorrent's, its Backup scope line naming servers that have none (#88 later).
- **Seed console on Deluge (K34):** all time counts from when omaseed began watching, said once.

## C26 — Search through qBittorrent's search plugins (board 48, ruled 2026-10-22)

Board [48](48-search.html) SR1–SR14, all as recommended, in board 46's frame (C24 H16: a rail view
after log, ctrl+f). The rule from #145 holds throughout: omaseed never names, ships or scrapes a
site; search goes through qBittorrent's own search API and the plugins the user installs. On
Transmission and Deluge the view isn't offered (C25 K30). #145, v0.5.0.

**This entry adds** a Search plugins section to C11's qBittorrent sections (SR10) and a second
no-match row to C1 D8's palette (SR14).

- **Asking (SR1–SR2):** one job per plugin that is on, at most four at once (one of qBittorrent's
  five left for others), the rest queued; done when every plugin has answered or 30 s have
  passed. "1 of 3 plugins didn't answer ›" opens each one: failed (its reason from qBittorrent's
  log, search it again, turn it off) or slow (still heard until qBittorrent's 3 min, stop
  asking). Nothing found is "0 results", not a failure.
- **Rows (SR3–SR5):** one per info hash; the copy with the most seeds names it and the others'
  trackers fold into its magnet, said on line 2. A copy on a tracker the library knows as private
  is never folded, nor given another's trackers (C5 D7, board 25). Seeds a plugin doesn't report
  (−1) read "?", sorted below every known count and above 0; hide dead hides only a reported 0.
  Rows sort live as plugins answer; the cursor's row never moves, with a line when rows land
  above it.
- **The bar (SR6):** the field, plugins ▾, type ▾, hide dead (off), sort ▾ (most seeds, newest,
  size, name); plugins and type ask again; a plugin without the chosen type is named as skipped;
  at 720 px the filters fold into "filters ▾".
- **Seed first (SR7–SR8):** "in your library" by hash (add… becomes trackers…), "likely on disk"
  by name and size (the add sheet checks the files), "added" after the hand-off; seeds as words
  and one colour (five or more green, one to four "few seeds" yellow, "?" and 0 dim) until the
  seeding coach (#25) has its words.
- **The hand-off (SR9):** add… (enter) opens today's add sheet with the folded magnet, or the
  .torrent fetched through its plugin (`downloader`); its line names the plugins and trackers; esc
  returns to the results.
- **Plugins (SR10–SR11):** Settings › qBittorrent › Search plugins: install from a URL, or from a
  file only when the server is this computer; omaseed watches the list for 30 s and says
  installed or didn't install (qBittorrent answers 200 either way); a switch per plugin,
  uninstall, check for updates (outcome from its log). No catalogue, no suggested plugin, no link
  to any list; a result's page opens only on `o`; a site's host never in a row.
- **Servers and jobs (SR12–SR13):** the current server only, said in the title's sentence; each
  server's last results kept for the session; omaseed deletes the jobs it started when replaced
  and on quit, never others'. With no plugins, all off, or no Python on the server, the view stays
  in the rail with one sentence each (naming no plugin) and a link to the pane.
- **Keys (SR14):** ctrl+f from anywhere; enter asks; ↓ into rows; enter add…, space the result,
  `c` copy magnet, `o` open its page, `/` filter the rows; esc back out step by step; `:` "search…"
  and "search plugins for '…'".

## C27 — Create a torrent from a folder (board 49, ruled 2026-10-22)

Board [49](49-create-a-torrent.html) CR1–CR19, all as recommended, in board 46's frame (C24 H17,
H14). qBittorrent 5.x hashes on the server (`torrentcreator/addTask`, `status`, `torrentFile`,
`deleteTask`); left to its defaults it makes a hybrid and seeds it at once, and if that start
fails the `.torrent` is lost (409), so omaseed sends every field itself. #146, v0.5.0, in H18's
sheets step (#170).

**This entry adds** a dropped folder to the window's drop handling (CR1), the made-here record as
a candidate for board 25's files match (CR8, superseding §3's "library torrents only"), one
exception to board 25's "no merging for private torrents" that you ask for (CR9), a creation to
C15 M10's band and C16 L6–L8 (CR13), and §7's rows to C12's ErrorWords (CR17). It supersedes
#146's "short default list" of trackers (CR4).

- **Starting (CR1–CR3):** + add ▾ "create a torrent from a folder…" (ctrl+n), `:` "create
  torrent…" (offering the folder picked in the rail), a folder row's menu, a folder dropped on the
  window. One sheet; "for": anyone / a private tracker, remembered per server. The folder through
  the Move sheet's field, places and browse (the server's own folder list when omaseed can't see
  its disks); a single file allowed.
- **Public (CR4):** no tracker list shipped; the library's most used public trackers (top 2–3) and
  last time's, editable; none is fine (DHT and peer exchange).
- **Private (CR5–CR7):** the library's private trackers by host; the announce URL from the newest
  torrent from that tracker (`torrents/trackers`). **The passkey is never stored by omaseed,
  never shown (the host, then dots, no reveal), never logged:** creator `status` replies, which
  echo it, are not logged at all, and one masking function cleans any text that could hold an
  announce URL. A pasted URL for another tracker is used for that creation only. Two passkeys for
  one host (a reset, or two accounts): the newest torrent's wins, the line names which torrent and
  when. Private flag on (locked), v1, the source tag of the newest torrent from that tracker (or
  the last typed for it) as facts with a change link. The private torrent is **not added**: the
  `.torrent` is saved and the folder remembered in a made-here record (both hashes, the files, the
  host, no passkey) until the site's copy is added, 30 days, or you forget it.
- **The round trip (CR8–CR9):** in the add sheet, the site's copy with the same hash is added at
  the folder in seed mode; another hash is matched file by file and added there with a check.
  Something made here already in the library: another hash, offer to replace it (files kept);
  same hash, offer to put the site's announce on it; the made `.torrent` itself added by mistake,
  a warning.
- **Format and pieces (CR10–CR12):** v1 by default on both paths (C21 is one reason), hybrid and
  v2 offered with a line each; piece size automatic and shown before creating, overridable 16 KiB
  to 64 MiB; web seeds, comment and hidden files (left out) under more ▾. Always `startSeeding`
  off and no `torrentFilePath`: omaseed fetches the file, then adds a public one itself with
  `seedMode` at the folder's parent.
- **While and after (CR13–CR15):** a job in the move's band (none under 3 s), followed by task id
  while hidden, one notice when long or hidden, one creation per server at a time. Public
  summary: seeding, magnet copied. Private summary: the `.torrent` saved as "NAME [HOST].torrent"
  in this machine's Downloads (XDG), a file chip to drag into the site's upload form.
- **The rest (CR16–CR19):** the seeding coach (#25) stays parked; the sheet's own lines explain
  the private flag and the source tag. Errors checked through the server first, in §7's words,
  qBittorrent's text under details. Transmission: no create entry; Deluge: no source row and no
  %, said once (C25). Built in 46's frame, in the sheets step.

## C28 — What the v0.4.0 builds asked (ruled 2026-10-22)

Diogo's answers, all as recommended, to the questions the v0.4.0 builders recorded as assumed.

- **The row's □ (#157, C18 D6):** the 16 u space at every row's start stays as built; shift+click
  on the □ adds the range and keeps the rest; the selection bar's hint is dim, not faint (Q19).
- **A stuck move's retry (#158, C17 D13):** qBittorrent ignores a move to the save path a torrent
  already has, so the stuck-move block offers `c recheck`, which moves it once there is room;
  "set location…" opens the Move sheet with no preset.
- **A small tick that doesn't fit (#159, C17 D8):** on `ntfs3`, `exfat` and `vfat`, a tick that
  doesn't fit always waits in D5's band, whatever its size, so it can be undone before anything
  is written.
- **Extras in the add sheet (#63):** the issue's wording as written: a dim line under the file
  tree ("2 extras: an NFO and a sample (12 MB). Ticked, so the torrent seeds whole.") and a dim
  "extra" meta on each; nothing is unticked.
- **An empty file in a move's way (#161 b):** when the file at the destination is empty and no
  torrent owns it, the Move sheet offers "replace it": delete it, then move, said in the log.
  v0.4.0 (v0.3.3 ships #161 a only).
- **Fills and contrast (#90):** ruled in v0.5.0's frame step (#167), on every fill at once
  (selected, cursor, hover, pressed, idle controls, tinted bands).
- **Theme bindings (#156):** a QML Theme wrapper so spacing follows the shell's base size live;
  built first, before v0.5.0's frame.
- **The view behind a sheet (#155):** left as it is: Qt can't hide a subtree from screen readers;
  revisit when Qt can.
- **servers.json from an engines build (#178, #84):** servers.json written by an engines build
  (v2) is read and kept by v0.4.0; a non-qBittorrent server is listed but not used. A file v0.4
  starts is version 1; one it read is written back at its version, with the keys it doesn't
  know as they were (`Server::extra`); 3 and up are still moved aside. A server whose `engine` is
  anything but absent or `"qbittorrent"` is never asked anything (no sync, test, probe or copy)
  and shows in the refused state's places in ErrorWords' words: "seedbox is a Transmission
  server" / "This omaseed only talks to qBittorrent. A newer omaseed can.", with only "switch
  server" to offer.

## C29 — v2-only in the free plan (#182, ruled 2026-10-23)

Diogo's answer, as recommended. On a v2-only torrent, deleting a skipped file that holds
downloaded pieces and rechecking leaves the torrent short of 100 % for good: libtorrent 2.1.2
then counts each wanted piece at its full size, the last piece of a file included, but counts
only the file's real bytes as done, and since it reads the torrent as finished it never
downloads anything to close the gap (98.6 % in the manual box). A recheck with the file still
there reads 100 %, and so does a v1-only torrent, whose missing pieces it downloads again
(`scripts/dev/research/182-v2-free`).

**This entry changes C17 D3:** a v2-only torrent is a hybrid in the free plan.

- **What stays:** on a v2-only torrent (qBittorrent gives a v2 hash and no v1), a skipped file
  with downloaded pieces stays ("has downloaded pieces · stays"), and nothing is rechecked. A
  skipped file with no downloaded piece still goes, as on v1.
- **Its words:** the sheet says why in words of its own: "1 file stays (SIZE) because this
  torrent is v2 and pieces of it were downloaded: deleting it leaves the torrent short of
  100 % for good, a libtorrent bug." ("N files stay … pieces of them … deleting them …").
- **Add it again:** offered as on a hybrid ("Free it too by adding the torrent again…"), with
  the same confirm and cost. It is sound on v2: added again from its own .torrent, with its
  files checked from scratch and that file gone, libtorrent counts what is wanted from the
  real bytes, and the torrent reads 100 % (the research's `readd-after` and `readd-at`, and
  the actions walk's disk stage, `b44-v2`).

## C30 — Fills, show and free (board 50, ruled 2026-10-23)

Board [50](50-frame-and-details-notes.html) F1–F19, all as recommended. #90's call, which C28
left for the frame step, and the drawings of the v0.4.0 manual pass's notes on #167 (the
outcome strip's "show") and #169 (nothing to free; the size of a skipped file still on disk).

**This entry amends** Q19 (the floor holds on every ground, not only the window), C18 O3's
"show" (offered only when it brings something back) and C17's band and free sheet (the words
when only adding the torrent again frees the space).

- **The floor (F1–F7, #90):** 4.5:1 against the ground each text is painted on: the window and
  every fill (idle, hover, cursor, selected, selected + cursor, pressed, tints). Every role, dim
  text and state colours included, gets the least lift against that fill, only where it sits on
  it, so captions stay distinct (1.91:1 under the main text on a selected row, median over 28
  themes; #90's full strength would have made them 1.00:1). Hover and pressed follow the same
  rule, the lift following the fill's 120 ms change. On a tinted band the tint's colour and its
  dim words are lifted against the tint. A theme whose palette reads on nothing at a fill's
  alpha gets that fill capped (everforest 0.215). Theme resolves each role on each fill once
  per theme change and the Kit asks for the ground it paints on; `tst_themecontrast` and
  `contrast-board.py` cover every pair, so a theme that fails is a failing test.
- **"show" (F8–F11, #167):** offered only when one of the strip's torrents is out of view, out
  of the scope (folder, question, category, tag, filter) or not selected, and live while the
  strip is up. It selects them, puts the cursor on the first and scrolls to it; when one left
  the scope, the rail goes where they all are (their folder, their question), else library, and
  the filter clears. Never a filter of its own (42 J1). A dim line under the strip's words says
  why it is there ("they are out of view, under giving back").
  Built with the action bar (C24 H8, #167), as the board reads it: "in view" is the row wholly
  inside the list's visible part, the strip over it aside; one torrent acted on alone counts as
  selected while it is the cursor's row and nothing is selected. The line names the first reason
  that holds, in the order scope, view, selection: "they are resting now, out of giving back"
  (a question picked, all in one question now), "it is in keep now, out of films" (a folder),
  "they are out of the filter" (the words alone hide them), "they are out of keep" (a category,
  tag or tracker), "they are out of view" (", under giving back" while no question is picked
  and they share one), "it is not selected"; "1 of them is …" when only some are.
  The rail goes to a question only when a question was picked, to a folder only when a folder
  was; else the library. The table keeps room for the strip with its line, so the line coming
  and going never scrolls the rows it is read from.
- **Nothing to free (F12–F16, #169):** on a hybrid (or v2-only, C29) torrent whose space-holding
  skipped files all have downloaded pieces, the band keeps its title, says "They were written
  while wanted, and pieces of them were downloaded. qBittorrent keeps them; on this torrent only
  adding it again frees them.", and offers Add it again… (F) · Why? ›. shift+f goes to the
  add-again confirm, which says why in one line; the palette row reads "add it again to free
  SIZE…", the ? sheet's F "free the space, or add it again". The free sheet, when its plan finds
  nothing after all, is titled "Only adding it again frees this" with "Re-add to delete them…"
  ("…it…" for one) as its main button; when some can be freed, the main button frees them and
  "re-add to free all SIZE…" sits beside it. The Files tab's reading decides (hashes, each
  file's progress, pieceStates as it reads now); shift+f from the table reads the files first,
  and the plan at delete time checks again and falls back to the "only adding it again" sheet.
- **The size (F17–F19, #169):** the size fact and the files head say "· 2 skipped, 16.0 MB still
  on disk"; on a disk omaseed can't see, "2.43 GB · 1.03 GB of it in skipped files", from
  torrents/files' progress. Ships with the details step (#169), in the new details only.

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

Changed by C17 D5: a priority change that adds more than 1 GiB (or the whole torrent) still
changes the rows at once, but waits 3 s before it is sent, with Undo.

## S1 — The status keys are `0`–`8`

The rail has nine status rows, `0` (all) to `8` (errored), so the help sheet and the rail's
footer print `0–8`. The approved design's figure prints `0–9`; `9` does nothing.

Changed by C24 H2 (v0.5.0): the rail's five questions take `1`–`5`, `0` is the library and
`6`–`9` do nothing; the states are `is:` words in the filter.

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

## The table's column widths

From the mouse pass (#51 D6, #52). What the issue left open:

- Name stays the flexible column. Its grip sets the least it gets (never under its 160 u
  minimum); with room to spare it still takes the rest, so dragging it narrower than the room
  changes nothing, and widening it past the room makes the rows scroll sideways (docs/10 §4).
- Every other column stops at 32 u and at 1200 u. A cell too narrow for its text elides it,
  and the hover label offers the whole of it; the header's caption elides too.
- A double-click fits the column to its caption (with room for the sort chevron) and the
  cells of the rows on screen. Progress has no natural width: it fits to 40 u of line and its
  percent. A complete row's Ratio keeps its tracks and the gap before the number.
- `tables.<server>.columnWidths` holds only the columns that differ from the default, in
  base-12 units like the defaults, so a base size change scales them and a later default
  reaches the rest. A drag writes `state.json` once, on release.
- The grip is 6 u across the boundary and shows nothing until the pointer is on it; then an
  accent mark, as the split's grip has. While a column is being moved, the grips are away.
- The columns palette's "default order" row (and `ctrl+r`) puts back the widths too, and shows
  whenever the order or a width differs.
