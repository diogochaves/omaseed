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
- **Words.** Title "Your week", the board's two lines, the button "open the console" (also the
  click, for Omarchy's toasts, which draw no buttons). The ratio prints as the console prints
  it ("2.40", where the board's mock says "2.4").
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
- **A .torrent the server can't read** (#71). qBittorrent 5.2.3's `torrents/export` repeats a
  v2 or hybrid torrent's piece layer once per file sharing its "pieces root" (files of the same
  content), and libtorrent then refuses that .torrent: `torrents/add` answers 415 for the whole
  request (upstream#97). `TorrentFile::repairPieceLayers` cuts such a layer back to one copy
  (only whole copies of it; the info bytes, so the hashes, untouched), on export and again on
  import, for files exported before. A 415 that is left is about one torrent, not the server:
  the batch goes again one torrent at a time (a 409 then means the batch had added it), the
  one refused is named ("qBittorrent couldn't read the .torrent of …") and the import goes on.
  Board 24 §7's stop is for a server gone or a disk full, so this keeps to it (assumed, confirm).

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
- **Editing an existing server** — *assumed, confirm:* it needs a new test only when its URL,
  auth mode or credentials (username, a newly typed key or password) changed. A rename, or a
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
  section, so the footer is where `t` is listed — *assumed, confirm.*
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
  *assumed, confirm.*
- **After a switch** (D5 a) from the select or `s`, the footer's right end says "omaseed is on
  private now · the library too" for 8 s; Settings › Servers' `d` doesn't add it.
- **Copy from another server** (D6 a) is a row of its own between Import settings and the
  torrents, with a select of the other servers (`space` on the row opens it) and `copy…`. The
  other server is read by `QbtSettingsFetch`: a client of its own with the stored login, no
  retries, 8 s per request, a probe, the export's five reads, a sign-out after a password
  login. What it read is kept as that server's newest settings for omaseed's next backup.
  **The fallback** is the newest of what this run read of that server and what the newest
  automatic copy holding it has, by the settings' own date, so a server read earlier in the
  session needn't wait for a weekly copy — *assumed, confirm* (the board says "the newest
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
