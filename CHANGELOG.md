# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

### Added

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

### Fixed

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
