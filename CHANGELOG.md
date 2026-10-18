# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

### Added

- Set location is a move like a folder's (board 43): checked before anything is sent (room on
  each drive, writing, files already where a torrent goes, automatic management), sent smallest
  first, and followed in qBittorrent's log until the last one lands, not for 30 s. A move that
  lands in a few seconds says so in the footer; a longer one gets the band, its summary and
  "move back…", which puts each torrent back where it was. One big torrent shows how much of it
  has arrived, when omaseed can see the destination's disk. A move started while another runs
  waits its turn and is checked again then.
- "Set location" is "Move…" now (`m` as before; `:` still finds "set location"). Its sheet lists
  places under the field (↓, or as you type): where you moved things lately, the library's
  folders and the categories' folders. Torrents from several folders go into one folder, or keep
  their places below a new one. Its checks say something only when there is something to say.
  A move's summary stays while the next move, queued behind it or started after it, runs
  under it; when only one of a selection moves, the footer names that one.

- The bar widget speaks to screen readers: the bar button ("omaseed", with its state in words),
  the popup's buttons, sections, rows, menu and settings, and the figures it shows. It works on
  today's Omarchy and on the shell kit with accessibility (omacom/omarchy#13212), once the shell
  runs on Quickshell 0.3.2 or later, which is the first to publish the shell to a screen reader.
- The bar widget's popup has a ⋯ (`ctrl+.`) with omaseed's own menu, the one its tray icon
  shows: the servers (with two or more), alternative limits, stop all, start all, add, settings
  and quit. With two servers or more the popup's server line ends in ▾ (`ctrl+s`) and switches
  omaseed from the bar; the popup stays open with the new server's figures.
- The tray's menu lists the servers, with two or more, each with what omaseed saw on it last,
  and switches between them without opening the window.
- With two servers or more, the other ones are always one step away: the rail keeps them in a
  block pinned to its bottom, with what omaseed saw last on each (its torrents, or why it
  didn't answer), even while the library is empty or the server is down; `ctrl+s`, a click on
  the hero's server name and `:` "switch server…" open a server palette, and `:` has a "switch
  to NAME" row for each. The block folds to one line with `z`. With one server none of this
  shows.
- The add sheet says which server a torrent goes to ("Add torrent to private") and can change
  it: the server chip or `ctrl+s` switches omaseed there and the sheet stays open with what you
  typed, its save path, free space and library check read from the new server. While the
  server is down the sheet still opens, and "add when back" sends the torrent as soon as it
  answers.

- Copy settings from another server (Settings › qBittorrent › Backup): omaseed reads the
  other qBittorrent just now, without leaving the one it is on, and shows what would change
  as an import, so you pick what to take. Nothing changes on the other server. When it can't
  be reached, its settings as omaseed last kept them stand in, and the sheet says so.
- The file manager names omaseed's backups "omaseed backup" and "omaseed torrents", with
  omaseed's icon, instead of "Tar archive". They are still plain tars that archive tools open.
- Export and import a qBittorrent's torrents (Settings › qBittorrent › Backup): every
  `.torrent` with its save path, category, tags, limits, started or stopped, queue position,
  renamed files and file priorities, in one plain tar file that stays private (announce URLs
  can carry a tracker's passkey); magnets that never got their metadata go in as links. Large
  libraries are fine: a few thousand torrents export in seconds, with a progress line and a
  stop button. Importing shows what is new, what is already there (skipped) and the links;
  "from → to" rows, started from the path maps omaseed knows, move the save paths to where the
  files are now. Categories and tags come first, every torrent is added stopped and checked
  against the files on disk, and the complete ones start; the rest stay stopped instead of
  downloading from zero. A rename that would replace a file on disk is not made. An import that
  fails or is stopped keeps what it added, and importing the same file again finishes it.
- Export and import a qBittorrent's settings (Settings › qBittorrent › Backup): its
  preferences, categories, tags, RSS feeds and rules in one readable JSON file, with no
  password or API key in it. Importing shows only what differs, in the settings' own words, and
  changes nothing until you apply: folders, ports, the network interface and the Web UI's
  address and login stay as they are unless you tick them, settings the other qBittorrent
  version lacks are counted and left out, and categories, tags, feeds and rules are only
  added. A category's or RSS rule's folder under the old default save path moves under the new
  one. "export current first" saves what the server has before you apply. The weekly automatic
  copies of omaseed now carry each server's qBittorrent settings too, and an import can read
  them from a copy.
- Rows that are uploading can pulse: a thin green tick at the left of each one, breathing
  faster the more it sends (Settings › Appearance › Pulse sending rows, off by default). It
  moves only while omaseed's window is the one in use and holds still when the desktop asks
  for less motion, which also stills the seed console's halo.
- A weekly recap: one notice on Sunday evening with what the week gave, the ratio, your
  streak, the rare torrents you kept alive, what paid back and the best day; it opens the seed
  console on the week (Settings › Notifications › Weekly recap, off by default).
- The table's columns resize: drag the right edge of a header cell (the pointer turns into a
  resize arrow), or double-click it to fit the widest cell on screen. Widths are kept per
  server with the rest of the table's layout and grow with the base size; the columns
  palette's "default order" puts them back. Name still takes whatever room is left, and its
  edge sets the least it gets.

- A magnet in the add sheet shows its files before anything downloads. omaseed asks
  qBittorrent for the magnet's file list as soon as it's in the sheet; usually it's there in a
  second, as the same tree a `.torrent` gets, so you untick what you don't want and add it with
  those choices in one go (a skipped file is never created). Renames, the free-space line and
  "files already on disk" work as for a `.torrent`. While the list is on its way, the Files row
  says so, then that it's still looking, then that it may never come and that qBittorrent goes
  on looking after you close the sheet. `add` never waits: before the list it adds every file,
  and "add stopped" stops the torrent as soon as its list arrives, so you choose in the Files
  tab before a byte is downloaded. A magnet from a tracker your library knows as private isn't
  asked about (qBittorrent can't get a private torrent's list from a magnet): the sheet says to
  use the site's `.torrent`. A link to a `.torrent` gets the same. Needs qBittorrent 5.2.
- A command palette: `:` (or `ctrl+shift+p`, which works from a field too) lists every command
  that makes sense now, with its key and what it acts on; type a few letters and enter runs it
  as its key would. Commands that can't run say why, the last five run come first, and a name
  that is no command goes to the jump palette. The footer, the ⋯ menu and the `?` sheet point
  to it.
- A move or a file rename that qBittorrent accepted and then couldn't do no longer goes
  unnoticed. qBittorrent says "OK" and writes the reason only to its log, so omaseed reads it:
  General opens with a yellow block saying what didn't happen, why, and where the files still
  are, with set location (already holding the destination), open folder or rename in Files,
  and `x` to dismiss it. It goes by itself once the move or rename happens, and a dismissal is
  remembered. When omaseed asked for the move, rename or delete itself, the footer says so at
  once ("x.bin didn't move: Permission denied").
- The HTTP sources tab says under each URL what qBittorrent's log says about it ("404 File not
  found · today 09:40", "host not found · 10 times"), or that nothing was logged.
- Settings › qBittorrent › Downloads says under "Copy .torrent files to" when the last copy
  into that folder failed, and why.
- qBittorrent's log, as the window's third face beside library and seed: `~` opens it from
  anywhere and goes back. Its lines read like a terminal, newest at the bottom, followed while
  you're there; scroll up and it pauses and counts what's new, `G` catches up. `1`–`4` turn the
  normal, info, warning and critical lines on and off (remembered per server), `/` keeps the
  lines with every word you type, `w` jumps to the next warning, `y` copies the marked lines
  or the cursor's. `enter` shows a line's torrent in the library and `t` keeps only that
  torrent's lines; a torrent's error or failure block has "its lines in the log". The face says
  where qBittorrent's memory of its log begins and where its log file is. It is read from the
  server only while it is on screen.
- Burst: alternative speed limits that are faster than your normal ones are called **burst**,
  in green, with the gauge's needle up; slower ones are **turtle**, in today's yellow, and a
  set that is faster one way and slower the other keeps "alt-speed". Nothing to set: omaseed
  reads it from the two sets, and `A` works as before. With qBittorrent's schedule on, the
  hero's chip says until when ("burst ↓∞ ↑∞ · until 07:00"), its tooltip says whether the
  schedule or a hand toggle did it, and after `A` the footer says how long a hand toggle really
  lasts (until the schedule next flips the other way, often later than you'd think). The tray,
  the `?` sheet, the command palette, the ⋯ menu and the limits sheet use the same word, and
  the bar widget draws burst in green with its end.
- Settings › qBittorrent › Speed says which way your alternative set goes, has a "nights"
  preset (`n`, 23:00 to 07:00 every day), and says under Days what the schedule will really
  do, warning when a time past midnight makes qBittorrent read the days the other way round.
  omaseed reads the sets and the schedule again whenever the alternative limits switch, so a
  change made in qBittorrent's Web UI shows the next time they flip.
- `omaseed --theme-roles` prints the current theme's colours and alphas as omaseed resolves
  them, as JSON (the theme, its mode and `colors.toml`, every colour role, the dim foreground
  lifted to 3:1 among them, every alpha and the window's), and exits without a window. It is
  for tools that check a theme's contrast the way omaseed will draw it.

### Changed

- Captions, links, state words and coloured glyphs read at 4.5:1 against the window in every
  theme (WCAG AA for text; captions were held at 3:1 and other colours not at all). A theme
  colour that falls short is moved towards the theme's text colour just far enough; one that
  reads already is kept exactly: captions move a little in most themes, the other colours
  mostly in the light themes and miasma. The command palette's "via" before a path reads in
  the caption colour; faint text is left for what can't be used.
- Settings › Appearance has one row for omaseed in the bar, "In the bar: widget · tray icon ·
  both · none", in place of the separate Tray icon and Bar widget rows; your current settings
  read as one of the four. The widget's own settings stay a row of their own (customize).
- The tray's menu puts "add torrent…" with settings, after start all, and quit after a line of
  its own, as in the widget's menu.
- When the server stops answering, a band across the window says so in plain words, naming it
  ("private didn't answer", "seedbox refused the sign-in") with what omaseed does next, "retry
  now" or "edit login", "switch server", and the technical text under "details". Yellow while
  omaseed retries by itself, red when it needs you. A refused sign-in is no longer called
  "unreachable" and no longer retried by itself, and the footer says "not synced yet" before a
  first sync. The desktop notice says the same words.
- A failed action says what didn't happen in one sentence that stays in the footer until the
  next key ("Field Recordings wasn't moved: qBittorrent can't create or write /proc/omaseed"),
  with the request and qBittorrent's own text under "details"; a key the read-only state stops
  says why.
- Settings › Servers says "in use" and "switch to it" instead of "default" and "make default",
  and keeps what was last seen on the servers omaseed isn't on.
- Settings says whose settings each part is. omaseed's Backup starts by saying what it holds
  (its settings, the servers without their passwords, the seed history, each server's
  qBittorrent settings) and that torrents are in qBittorrent › Backup, one `t` away.
  qBittorrent › Backup names its server in every row ("Export public's settings", "Import
  torrents into public"), and the import sheet says which server and day the file came from.
- The qBittorrent group's server name is now a select: a click (or `enter` on it in the nav)
  lists your servers with their state, and picking one moves omaseed there. `s` still goes
  straight to the next server, from every qBittorrent pane, Backup included, and the footer
  says so; after a switch it says the library moved too.
- Import settings opens on qBittorrent exports and omaseed backups together, and a file is
  read by what is inside it, so a renamed one still imports.

### Fixed

- Set location's refusals name the folder, not the sign-in: "qBittorrent can't create
  /mnt/x" (check that the drive is mounted) or "qBittorrent can't write to /mnt/x". A 403 that
  qBittorrent sends for a folder it can't write no longer makes omaseed sign in again or mark
  the server as refusing it.
- A screen reader can now use the controls it finds: pressing a button, a tab, a menu or
  palette row, a rail row, a link or a key hint does what a click does, and a switch's toggle
  flips it and saves the setting, as a click would. Before, the press did nothing, and a
  toggle moved the switch without changing the setting behind it. Disabled controls still
  ignore it.
- A screen reader can now find the rows and the other places you click: the library's
  torrents, the details tabs' trackers, peers, HTTP sources and files, the add sheet's lists,
  the console's and the log's rows, the rail's group headings, the empty states' ways in and
  the settings' rows are named by what they are (a torrent's name, a peer's address), say
  whether they are selected, ticked or folded, and pressing one does what a click does.
  Before, these 72 places were not in its tree at all.
- A screen reader now hears what a control does, not its key or its glyph: "add torrent",
  "settings", "more", "help", "filter", "delete", "clear selection", with the shortcut said
  after it ("shortcut a"), not "add · a". Icon-only buttons are named instead of reading out a
  Nerd Font glyph, the table's ↓ and ↑ are "Download speed" and "Upload speed", every switch
  in Settings is named by its row, and the alt-speed chip is named by its set instead of
  re-reading its limits each time they change. First run's `enter set up` and `esc quit`
  are no longer found twice beside the buttons that do the same.
- A screen reader now reads what a sighted user reads first, not only the controls: first
  run's "Torrents for Omarchy, powered by qBittorrent." and "no qBittorrent on this machine
  yet", each step's question and a test's answer; "No torrents yet" and the other empty
  states; the server line, the footer's "16 torrents, 7 downloading, 1.80 TB free" and its
  notes; the details pane's torrent name, figures and headings; a row's status and progress;
  the seed console's figures and sections; the log's state; and the sheets' titles, captions,
  results and warnings. Titles are headings, and a failure or warning that comes on its own
  is an alert. A figure that changes every second is said by what it is ("download", "eta"),
  with the value after it, so it is not read out again each time it ticks.
- A server added in Settings › Servers is tested before it is saved, as in the first-run
  setup: `save` waits, dimmed with "test first", until a test has run; a failed test offers
  "save anyway" for a server that is down on purpose. Editing a server asks for a new test only
  when its address or login changed; a rename saves at once.
- Importing exported torrents no longer stops at once with "unknown (415)". qBittorrent's own
  export breaks the `.torrent` of a v2 or hybrid torrent whose files share content (it repeats
  their piece layers), and then refuses to add it, with the rest of its batch. omaseed now puts
  those piece layers right when it exports and again when it imports (so files exported before
  this fix import too), and a torrent the server still can't read is named ("qBittorrent
  couldn't read the .torrent of …") while the others go in.
- Turning automatic management on for a torrent whose files another torrent also seeds asks
  first, as a category change does: "move both" gives the other one the same category and
  management so the two move together. Before, the other torrent lost its files without a word.
- Renaming a file or folder no longer replaces a file of that name the torrent doesn't own.
  qBittorrent only checks the torrent's own files and lets the rename overwrite anything else,
  so where omaseed can see the server's disk (a local server, or a mapped path) it refuses such
  a name first: the rename sheet marks the row, `r` says the name already exists on disk.
  The add sheet checks its file renames the same way against where the torrent will be saved,
  and refuses the add if something took the name since.

## v0.2.0 — 2026-10-13

### Added

- An errored torrent says why. qBittorrent's API never gives the reason, so omaseed reads it
  from qBittorrent's own log and ties each line to its torrent. General then opens with what
  went wrong in plain words ("Can't write to its save folder", "The disk is full"),
  qBittorrent's own words, the folder, and the actions that fix it: set location, try again,
  recheck. The table's status reads the short cause ("no access", "disk full") instead of
  "error". A torrent whose files are gone says where qBittorrent looked.
- A notice when a torrent stops with an error, with the reason; a click shows it. It has its
  own switch in Settings › Notifications.

### Changed

- Each release ships a prebuilt package, so omaseed installs with one line and no build:
  `curl -fLO https://github.com/diogochaves/omaseed/releases/latest/download/omaseed-x86_64.pkg.tar.zst && sudo pacman -U omaseed-x86_64.pkg.tar.zst`.
- `p` on an errored torrent starts it, which makes qBittorrent try again, instead of stopping
  it.
- Everything works with the mouse as well as the keys. Every list follows one set of click
  rules: a click picks a row, ctrl+click adds or removes one, shift+click selects a range
  (ctrl+shift+click adds the range), a click on empty space clears the selection, a
  right-click outside the selection acts on that row alone, and clicking away from a rename
  keeps it.
- The Files tab takes ctrl+click and shift+click, and a right-click on a file outside the
  selection acts on that file (priority and rename used to act on the old selection).
- The Trackers, Peers and HTTP sources tabs have right-click menus (add, remove or ban,
  reannounce, copy).
- Text fields have a right-click menu (cut, copy, paste, select all) and take a click anywhere
  in their box.
- Menus taller than the window are cut to it and scroll, so the actions menu's last rows
  ("open folder", "delete") can be clicked on a short window.
- Key hints that name a safe action are clickable: "esc" in a sheet's head or a filter,
  "enter apply" in the tag palette, "esc clears the filter" and "0 shows all statuses" when
  nothing matches, the "?" in Settings.
- The help sheet's command rows, the jump palette's `tab` actions (right-click), a ratio
  ladder dot in the seed console, About's repository link and first run's "✓ server" step all
  answer a click; the wheel scrolls Settings, first run and every details list, and steps
  through folded Settings tabs.
- Tooltips name the keys of the add, settings and help buttons, and show the full text of
  cut-off names in the table, rail, details, seed console and settings.
- A double-click on a torrent opens its details; a ✕ on the selection bar clears the
  selection; a right-click on a column header opens the columns palette; the mouse's back
  button steps back like `esc`, and a middle-click adds or removes a row from the selection.
- While the pointer is over the torrent table, its rows hold their order (values still
  update), so a click lands on the torrent you aimed at; leaving it sorts again.
- Dragging files over the window shows "drop to add".
- Trackers, Peers and HTTP sources select several rows: remove several trackers or HTTP
  sources, ban several peers, or copy them all at once.
- In the Files tab a folder folds on its arrow or a double-click; a plain click selects it.
  A third click on the sorted column goes back to the torrent's own order.
- A server can be made the default by mouse: a button in its sheet, or its row's right-click
  menu in Settings.
- A number field steps with the mouse wheel while it has the caret.

### Fixed

- The tag palette could not save tags with the mouse: ticks were applied only on `enter`.
  Clicking `create "x"` now creates the tag and ticks it.
- A click elsewhere while renaming a file left the details pane ignoring every key.
- Settings' own number fields (low-disk threshold, base size, transparency) lost a typed value
  when the caret left them by a click.
- A switch in Settings kept its focus ring after a click.
- Dropping one .torrent from a file manager listed it twice in the add sheet.
- A palette or menu that opened under a resting pointer handed it the cursor.
- The add sheet's "N ticked" line counted torrents already in the library; it now says how
  many the add takes.
- File and folder pickers open as dialogs of the omaseed window, so a portal chooser other
  than GTK's (Strata's, for one) floats over omaseed instead of tiling beside it.
- The add sheet's `add` button takes the files ticked in "In Downloads" (ctrl+click or
  `space`): it reads `add N` and adds them, where it used to do nothing until a row was
  clicked.

## v0.1.0 — 2026-10-10

First release. A native desktop client for one or many qBittorrent servers (5.2 or newer,
with the Web UI on), built for Omarchy and usable on any Linux. omaseed has no torrent
engine of its own: qBittorrent, local or remote, does the transferring.

### The library

- A dense, keyboard-first torrent table: 29 columns to show, hide, reorder and drag, sorting
  that follows live values, inline rename, multi-select with a selection bar, and `?` for
  every key.
- A rail of filters by status, category, tag, tracker and server; a filter field with
  wildcards; jump, category, tag, sort and column palettes.
- A details pane docked right or below, or folded to a one-line strip: General (with a
  pieces bar), Files, Trackers, Peers and HTTP sources.
- Files: pick and skip files by ticking them, set priorities, hide skipped files, rename one
  file or many at once with find and replace, open or reveal them.
- Every torrent action qBittorrent offers: start, stop, force start, recheck, reannounce,
  category, tags, automatic management, set location, share and speed limits, queue order,
  copy a magnet, hash or name, and delete with or without its files.
- Deleting or moving data that another torrent also uses asks first and says which ones.

### Adding torrents

- omaseed opens `magnet:` links and `.torrent` files when you let it, takes them from the
  clipboard, drag and drop, the file picker or the newest `.torrent` files in Downloads,
  and can add several at once with one set of options.
- The add sheet shows the file tree to tick, rename and prioritise, the free space where it
  will save, and files already on disk that it can seed from (cross-seeding).
- A torrent already in the library is spotted before it is added; its new trackers can be
  merged into the one you have (public torrents only).

### The seed console

- A second face of the main window for what you give back: upload today and over time,
  ratios on a ladder, a year of seeding days, the torrents you keep alive and the trackers
  you seed to. omaseed keeps that history itself, since qBittorrent does not.

### Omarchy

- Follows the Omarchy theme live, every bundled theme and the light ones, with its fonts,
  base size, transparency and Hyprland's window rounding.
- A bar widget, `Omaseed`, ships with the app: speeds and activity in the bar, and a popup
  to add torrents, switch alternative speeds and see your share and what finished. First run
  offers it; Settings › Appearance turns it on and off; `omaseed --install-bar-widget` adds
  it from a terminal.
- Lives in the tray (hidden while the bar widget runs), starts at login if you ask, and
  sends notifications with actions for finished downloads, a lost or restored server and a
  disk running low.

### Servers and settings

- First run finds a local qBittorrent (or offers to install one) and signs in with a cookie
  login, an API key or none.
- Several servers, switched from the rail, the hero or the tray; passwords and keys go to
  the system keyring.
- HTTPS with a self-signed certificate: omaseed shows its fingerprint and trusts it once
  you say so.
- qBittorrent's own settings, edited inside omaseed's settings.
- omaseed backs itself up (servers, settings, its seed history), restores from a backup,
  and keeps weekly copies on its own.

### Known limits

- Keys cannot be rebound yet.
