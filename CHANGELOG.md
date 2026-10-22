# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

### Changed

- **A server that is slow is no longer "unreachable".** When qBittorrent takes the connection
  but doesn't reply in time (busy writing to its disk, say), omaseed says it "isn't answering",
  in yellow, with how long it has waited, and "answered again" when it does. A refused
  connection or no route says "unreachable", now in red, with "Is qBittorrent running?".
- **A server's failed test says what to check.** In Settings › Servers, a test that fails is a
  notice under the sheet's title, in plain words ("Nothing answered at 127.0.0.1:8095", then
  what to check), with the technical text under "details", where it used to be one line of
  Qt's words.

- **A finished torrent that never left its incomplete folder says so.** When qBittorrent
  can't move a finished torrent out of its incomplete folder (the destination full), it keeps
  seeding from there in no error state. omaseed now marks the row "move failed", in red, and
  General says where the files never arrived, why (from qBittorrent's log), where it is seeding
  from and what partial copies were left, with "show both folders" and "set location…".

## v0.3.3 — 2026-10-22

### Fixed

- **"Skip hash check" works with qBittorrent 5.3.** 5.3 reads the option under a new name and
  silently ignored the old one, so a torrent added with it for files already on disk came in
  at 0 % and had to be checked or downloaded again. omaseed now sends the name the server's
  version reads (both, when it can't tell the version yet).
- **`omaseed --help`, `--version` and `--theme-roles` work with no display.** Over SSH, in a
  container or in a package check they print and exit, instead of aborting before a word.

## v0.3.2 — 2026-10-22

### Added

- **A bare info hash works like a magnet.** Paste one into the add sheet or the bar widget's
  popup (ctrl+v), drop it on the window, or run `omaseed <hash>`: a v1 hash (40 hex digits, or
  32 upper-case base32) or a v2 one (64 hex digits), standing alone, is added as the magnet it
  stands for, and the sheet says "from an info hash". It is never taken from the clipboard when
  the sheet opens, since a commit id copied for something else looks the same.
- **A warning when the server has DHT off.** A bare hash names no tracker, so it finds peers
  only through DHT; on a server with DHT off the add sheet says it may never find one.

## v0.3.1 — 2026-10-22

### Fixed

- **A failed move no longer offers to break a hybrid torrent.** After a move where some
  torrents fail, "recheck" leaves out every hybrid torrent (one made for both BitTorrent v1 and
  v2). Its files may be split between the old place and the new, and a recheck would put it in
  an error that only adding it again clears. The sheet lists it as not rechecked, says why, and
  offers "move again" and "move back" to bring its files together; the others are rechecked as
  before. When every failed torrent is hybrid, the sheet says there is nothing to recheck.
- **Share limits work with qBittorrent 5.3.** 5.3 refuses a limits change without a new field,
  so every ratio or seeding-time limit set from omaseed failed there; omaseed now sends it.

## v0.3.0 — 2026-10-22

Moving torrents and whole folders, with a way back. Backups of a qBittorrent's torrents and
settings. Several servers within reach. qBittorrent's log in the window, a command palette, and
a screen reader that can use all of it.

### Added

- **Move a folder.** The rail lists the library's folders. Right-click one (or press the menu
  key on it), or pick it and use the line over the table, to move everything in it: its
  torrents, then the files no torrent owns. Before anything is sent, omaseed checks for room on
  each drive, permission to write, files already at the destination and automatic management.
  It sends the smallest torrents first and follows the move until the last one lands, even on
  a server whose disks it can't see. A band under the top shows how far the move has got, with
  pause and resume. Quitting while a move runs asks first, and a paused move picks up at the
  next start. A summary ends it, with "recheck" and "move back…".
- **Move…** (`m`, which was "Set location"; `:` still finds "set location") moves a selection
  the same way. Its field lists places with "places ▾", ↓ or as you type: where you moved
  things lately, the library's folders and the categories' folders. Torrents from several
  folders go into one folder, or keep their places under a new one. A move that takes a few
  seconds says so in the footer. A longer one gets the band. A move started while another
  runs waits its turn.
- **Move back, later.** The last 20 moves, or 30 days of them, can be undone from a
  torrent's details, the log or the palette ("undo last move"). A torrent goes back only if it
  is still where that move put it. The button says what will happen ("move 46 back · 3 stay").
- **Several servers within reach.** With two or more, the rail keeps the others at its bottom,
  with what omaseed last saw on each. `ctrl+s`, a click on the server's name, the palette, the
  tray's menu and the bar widget all switch between them. The add sheet says which server a
  torrent goes to and can change it. While that server is down, "add when back" sends the
  torrent as soon as it answers.
- **Back up a qBittorrent** (Settings › qBittorrent › Backup). Export its torrents to one
  private file, each with its save path, category, tags, limits, state, renamed files and file
  priorities. On import, omaseed moves save paths to where the files are now, checks every
  torrent against the disk and starts only the complete ones. Export its settings, categories,
  tags and RSS rules to a readable file with no password in it. An import shows only what
  differs and changes nothing until you apply. You can also copy settings straight from
  another server. omaseed's weekly copies now carry each server's qBittorrent settings, and
  the file manager shows both kinds of backup with omaseed's icon.
- **qBittorrent's log**, as a third face beside the library and the seed console (`~`). It is
  followed live. `1`–`4` pick the levels, `/` filters by words, `w` jumps to the next warning
  and `y` copies. `enter` goes to a line's torrent, and `t` shows that torrent's lines alone.
- **Failures qBittorrent only logs are shown.** When qBittorrent accepts a move or rename and
  then can't do it, General opens with a yellow block: what didn't happen, why, where the files
  are and how to fix it. The HTTP sources tab shows what the log says about each URL. Settings ›
  Downloads says when the last copy of a `.torrent` file failed.
- **A command palette**: `:` or `ctrl+shift+p` runs any command by name and shows its key. A
  command that can't run says why. The last five you ran come first.
- **A magnet's files before it downloads** (qBittorrent 5.2). The add sheet asks for the
  magnet's file list, so you can untick and rename files as for a `.torrent`. For a magnet from
  a private tracker your library knows, the sheet tells you to use the site's `.torrent`
  instead.
- **Burst and turtle.** Alternative limits that are faster than your normal ones are called
  burst, in green. Slower ones are called turtle. With qBittorrent's schedule on, the chip says
  until when, and after `A` the footer says how long the toggle really lasts. Settings › Speed
  has a "nights" preset and says what the schedule will really do.
- **Resizable columns**: drag a header's edge, or double-click it to fit. Widths are kept per
  server.
- **Screen readers can use omaseed**: the window and the bar widget. Every row, control and
  piece of text can be found. Controls are named by what they do, not by a glyph or a key, and
  pressing one does what a click does. The bar widget reaches a screen reader once the shell
  runs on Quickshell 0.3.2 or later.
- The bar widget's popup has omaseed's menu behind ⋯ (`ctrl+.`), the one the tray shows. Its
  server line switches servers.
- Optional, both off by default: rows that are uploading can pulse (Settings › Appearance), and
  a weekly recap can arrive on Sunday evening (Settings › Notifications).
- `omaseed --quit` quits the running omaseed cleanly. `omaseed --theme-roles` prints the theme's
  colours as omaseed draws them, for contrast checkers.

### Changed

- An errored torrent can be stopped: `p`, the actions menu and the selection bar stop it, so a
  retry that only fails again comes to an end. `e` retries. A stopped errored torrent reads
  "stopped" in red, even when it was stopped from qBittorrent's Web UI.
- Text reads at 4.5:1 against the window in every theme (WCAG AA): captions, links, state
  words and coloured glyphs. A colour that falls short is shifted just enough, and the rest are
  kept exactly.
- When a server stops answering, a band names it in plain words ("private didn't answer"),
  says what omaseed does next and offers "retry now", "edit login" or "switch server". A
  refused sign-in is no longer called "unreachable" or retried by itself.
- A failed action says what didn't happen in one sentence in the footer, with qBittorrent's own
  text under "details".
- Settings says whose settings each part is. The qBittorrent group's server name is a select.
  Servers says "in use" instead of "default". A new server is tested before it is saved.
  Appearance has one row for "In the bar: widget · tray icon · both · none".
- After a move, "recheck" starts again only the torrents that were running before it.
- In the Files tab, a click on a half-ticked folder unticks it, and the next click brings back
  exactly what was wanted before. Files that were never wanted stay skipped.
- On Omarchy 4.5, notices show sentence-case buttons, and reduced motion also follows
  Hyprland's animations. The bar widget matches the bar's icons and labels, can sit in any of
  the bar's sections and answers `omarchy-shell` commands faster. Omarchy 4.0.x behaves as
  before.
- The bar widget's popup is built the first time it opens, not at shell start-up. Its server
  line drops the qBittorrent version before it would cut the server's name.

### Fixed

- First run reads the Web UI's port from a qBittorrent 5.3 profile instead of offering 8080.
- omaseed quits cleanly on a logout, a shutdown or `kill`, and saves what it writes on quit.
- "A file name isn't allowed on this disk" is shown only when the name really is one, and not
  for a disk that refused to open a file.
- Adding torrents one after another: each one shows in its sorted place at once. The sheet
  closes on qBittorrent's OK, and a second press no longer sends the torrent twice.
- Move and set location refusals name the folder that couldn't be written. They no longer look
  like a refused sign-in.
- Turning on automatic management, or changing the category, for a torrent that shares files
  with another one asks first instead of moving the files out from under the other torrent.
- A rename no longer overwrites a file on disk that belongs to no torrent.
- Restoring a backup puts each server's table layout back on the right server and shows it at
  once.
- Importing exported v2 or hybrid torrents whose files share content no longer fails with
  "unknown (415)": omaseed repairs what qBittorrent's own export breaks.
- The log keeps omaseed's own lines in time order. The rail's cursor is drawn only while the
  rail has the keys.

For contributors: walks for every flow in an omabox, a visual baseline check, and
`scripts/dev/move-library.sh` (see `docs/TESTING.md`).

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
