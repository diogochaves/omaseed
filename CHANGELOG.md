# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

### Changed

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

### Fixed

- The tag palette could not save tags with the mouse: ticks were applied only on `enter`.
  Clicking `create "x"` now creates the tag and ticks it.
- A click elsewhere while renaming a file left the details pane ignoring every key.
- Settings' own number fields (low-disk threshold, base size, transparency) lost a typed value
  when the caret left them by a click.
- A switch in Settings kept its focus ring after a click.

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
