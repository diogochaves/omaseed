# Changelog

All notable changes to omaseed are logged here, newest first. Loosely follows
[Keep a Changelog](https://keepachangelog.com/); omaseed does not yet promise semantic
versioning stability (pre-1.0). `scripts/release.sh` requires a `## v<version>` section here
before it will tag a release.

## Unreleased

## v0.9.0 — 2026-11-11

### Added

- **A Transmission's library moves to a qBittorrent.** Settings › Transmission has a new last
  section, Backup, with **Export NAME's torrents**: every torrent with its folder, labels,
  limits and renames, in one private file. Import that file into a qBittorrent (Settings ›
  qBittorrent › Backup, as before) and the first label becomes the category and the rest
  become tags; the import sheet lists each name as it will be, and what qBittorrent can't
  keep, before anything is added. Transmission keeps its .torrent files on its own disk: when
  omaseed can't see them (a container, another machine), the row says so and takes their
  folder as this machine sees it, remembered for that server, or exports each torrent as its
  magnet link. Importing into a Transmission comes next. (#88)

## v0.8.4 — 2026-11-11

### Fixed

- **Seed history no longer counts a server's whole all-time total as one minute's upload.**
  When omaseed reconnected within seconds of a qBittorrent restart, qBittorrent answered with
  no all-time totals yet, and the next answer's whole total went into the history as if it had
  all been uploaded in that minute. Such an answer is now left out, and an amount no connection
  could carry in the time is never counted. A history that already holds such an hour is
  repaired the next time omaseed opens it: the hour takes the level of the hours around it,
  its day gives up the difference, and both are drawn as a gap. (#256)

## v0.8.3 — 2026-11-11

### Changed

- **A retry that fails again stays quiet.** When omaseed starts a torrent in error (`p`, retry,
  the actions menu, the selection bar, the bar widget or the tray) and it fails again with the
  same error before it has worked, no second "stopped with an error" notice goes: the row and
  its story already say it. A different error, or one after the torrent worked again, still
  notifies. (#255)

## v0.8.2 — 2026-11-08

### Fixed

- **Free the space frees files qBittorrent was holding.** Right after a misclick on a stopped
  torrent, qBittorrent keeps the files it made open, so freeing starts and stops the torrent
  once to make it let go. The stop went out right behind the start, and when it reached
  qBittorrent before the start had taken effect it stopped nothing: the files stayed, "open in
  qBittorrent". Now the stop waits until the torrent reads as started. (#210)

## v0.8.1 — 2026-11-07

### Fixed

- **A rename shows in the row at once.** The strip and the torrent's story said the new name
  as soon as the server took it, but the row and the details' header kept the old one until
  the next refresh, up to a second and a half later (five with the window hidden). (#249)
- **No more "QString::arg: Argument missing" in the journal.** A torrent whose story had a
  move made with one other torrent logged that warning on every refresh while its details
  were open. What it showed was right. (#250)

## v0.8.0 — 2026-11-06

### Added

- **Transmission.** omaseed drives Transmission 4.1 and later as well as qBittorrent: the
  library, details, adding with labels and a folder, renaming, moving, labels, limits, turtle
  and its clock, errors in Transmission's own words, the seed console, the tray and the
  widget. What Transmission can't do over its RPC is left out instead of offered and failing:
  categories, a magnet's files before the add, super seeding, first and last pieces first,
  automatic management, adding or banning peers, editing web seeds, the log, search, creating
  a torrent and the qBittorrent backups. Its key says so once in the status line, and the `?`
  sheet's last line names them. A daemon older than 4.1 is refused in words that say why.
  (#85)
- **Deluge.** omaseed drives Deluge 2.1 and later (tested on 2.2) through deluge-web, its web
  UI's JSON API: the library, details, adding with a label, a folder and a magnet's files
  read before the add, renaming (the name Deluge shows; the folder keeps its own), moving,
  labels, ratio limits that stop or remove, the queue, errors in Deluge's own words, the seed
  console, the tray and the widget. Labels come from Deluge's Label plugin and take the
  categories' place, lower case as Deluge keeps them; with the plugin off, C says where to
  turn it on. When deluge-web drives no daemon, or knows several, the server sheet asks which
  daemon to use and keeps it with the server; a deluge-web with no daemon reads as a server
  that is down, "no daemon". A move's band shows Deluge's own percent. A move Deluge fails
  pauses the torrent, and only a recheck clears that error, so recheck comes first; a move
  Deluge takes and drops without a word (most often it couldn't make the folder) reads as
  failed, "Deluge didn't move it and doesn't say why", in the strip, the band and the
  torrent's story, and isn't sent again. What Deluge can't do is left out instead of offered
  and failing: tags, turtle (its Scheduler plugin is a weekly grid), time limits, automatic
  management, banning peers, web seeds, the log, search, creating a torrent and the
  qBittorrent backups. Its key says so once in the status line, and the `?` sheet's last line
  names them. Deluge keeps no all-time totals, so the seed console counts them from when
  omaseed began watching, and says so. (#87)
- **Servers know their engine.** Testing an address recognises qBittorrent, Transmission or
  Deluge, and the sign-in follows the engine (a Deluge's one password); first run looks for
  Transmission and Deluge too, one row per engine found. The status line, the rail's tooltip,
  ctrl+s and Settings › Servers name it: "seedbox · Transmission 4.1.3". servers.json moves to
  version 2; an existing one opens unchanged, every server in it a qBittorrent. (#84)
- **Settings › Transmission.** A Transmission's own settings, in qBittorrent's sections: speed
  and turtle, discovery and privacy, connection with its port test, seeding, queue, downloads.
  With a qBittorrent and a Transmission both saved, each keeps its group in Settings; the
  other's sections say whose they are and switch there. (#86)
- **Settings › Deluge.** A Deluge's own settings, from its core.conf, in the same sections:
  speed, discovery and privacy, connection with its port test, seeding, queue, downloads; then
  Plugins, which says which of Deluge's plugins are on (Label for the labels, Scheduler, the
  others). Turning a plugin on or off stays in Deluge's own preferences. (#87)
- **A walk for Transmission.** `scripts/dev/test-server.sh start --transmission` runs a
  throwaway daemon on :8085, and `scripts/dev/transmission-walk.sh` adds, renames, moves,
  labels and deletes on it in an omabox, checked on the daemon and on disk. (#85)
- **A walk for Deluge.** `scripts/dev/test-server.sh start --deluge` runs a throwaway deluged
  behind deluge-web on :8086, and `scripts/dev/deluge-walk.sh` picks its daemon, adds, reads a
  magnet's files, renames, labels, sets a ratio limit, moves (with Deluge's percent, one it
  drops without a word, one it fails) and deletes on it in an omabox, checked on the daemon
  and on disk. (#87)
- **The story tells renames.** The details pane's what happened now has a line for each
  rename, in the accent like a move: "Renamed, was OLD" for the torrent's name, "Renamed a
  file" or "a folder" (OLD → NEW), and "Renamed 14 files" for one run of the rename sheet. A
  name changed in another client reads "renamed outside omaseed", timed at the poll that saw
  it; on Transmission, where a rename moves the folder on disk, the line says so. A typo fixed
  within ten minutes stays one line, a rename back to where it began leaves none, and a file
  rename qBittorrent logs as failed takes its line back. omaseed keeps the last ten per
  torrent, per server. Board 56, DECISIONS C38. (#236)

### Changed

- **The welcome** reads "Torrents for Omarchy, powered by qBittorrent, Transmission or
  Deluge"; the install step still offers qbittorrent-nox. (#86, #87)
- **omaseed's backup** carries a qBittorrent's settings only; a Transmission's or a Deluge's
  settings and torrents stay on it, and Settings › Backup says so. (#86, #87)
- **The settings command names the server's engine.** ctrl+shift+, reads "Transmission
  settings" or "Deluge settings" in the palette and the ? sheet on those engines, where
  "qBittorrent settings" was hidden; "qBittorrent settings" on a qBittorrent as before. (#237)

## v0.7.1 — 2026-11-06

### Fixed

- **"qBittorrent" keeps its small q in the details.** The details pane started its sentence
  with a capital, so "qBittorrent stopped it with an error" and "qBittorrent is moving its
  files" read "QBittorrent …". (#232)
- **A move between btrfs subvolumes is a copy.** A subvolume shares its parent's mount point,
  so the Move sheet took a move from one into another for the same drive and promised a rename
  ("nothing is copied"), with no room needed and no time left, while the engine copied every
  file. The sheet now tells drives apart by their device, so such a move reads room on the
  destination and shows its progress like any other copy. (#240)
- **A magnet typed by hand is looked up with its trackers.** The add sheet asked qBittorrent
  for a magnet's files as soon as its hash was typed, before its `&tr=` part; qBittorrent keeps
  the trackers of the first ask, so the files of a magnet whose peers only its trackers know
  never came. A magnet typed a key at a time is now asked about once the typing stops for a
  second; a pasted one still at once. (#241)
- **A move where nothing moved says so.** A one-torrent move that qBittorrent failed after its
  band had shown ended on a green "✓ NAME moved to DEST" over "0 of 1 · 1 failed". A summary
  where nothing moved and something failed now reads red "✕ NAME didn't move". (#242)
- **A move goes out after the server's sheet is saved while it is lost.** Saving the sheet of
  the server omaseed is on restarts its connection; done while the server was lost, the move
  job never heard that it came back, so a move kept "waiting for" the server and was never
  sent, though the list updated again. A move now goes out once the server answers. (#243)
- **The story's times from an earlier day are whole.** The details' story gave its times a
  fixed column, so any time that wasn't today's was cut short: "22 Oct 17…" for 22 Oct 17:26.
  The column now fits the longest time the story writes ("yesterday 09:15", "22 Oct 17:26"),
  measured in its font; a story of today's times keeps its width. (#246)

## v0.7.0 — 2026-11-05

### Added

- **A speed picked outside the window says so.** The bus gains `PickAltSpeed` (off, turtle or
  burst) and the status file `alt.pick`; a pick from the bus, the tray or the widget's ⋯ sends
  a notice once it has taken effect, with what was picked and its limits (and a pointer to
  Settings › qBittorrent › Speed while turtle is still at qBittorrent's defaults). (#214)
- **Torrent added.** An add from the add window (a browser's or file manager's hand-off, the
  tray's or the widget's add) sends a notice once qBittorrent has it: its name, size and
  category, one per torrent when you added several, a merge of trackers into one you already
  have too. A click shows the torrent. Adds made in the main window stay quiet. Its own switch
  is in Settings › Notifications, on by default. (#219)

### Changed

- **Adds from outside open where you are.** A `.torrent` or magnet handed over by a browser or
  file manager, and the add from the tray, the widget's ⋯ or the bus, open the add sheet in a
  small window of its own, floating on the workspace you are on, with focus. The main window
  stays where it was, or hidden. When you were in the main window a moment before, the add
  opens there, as before; first run still holds adds on its page. A plain launch (the
  launcher) still brings the main window up. (#217, #218)
- **Pause Torrents and Resume Torrents, in place of stop all and start all.** Start all
  started every torrent on the server, the ones you had kept stopped on purpose too. Pause
  Torrents stops what is running and remembers exactly those, per server, across a restart of
  omaseed; while a pause is held the row reads Resume Torrents, which starts those again (a
  force-started one comes back forced) and leaves the rest as they were. One row in the tray,
  the widget's ⋯, the window's menu and a server's menu in the rail; it is off while a pause or
  a resume is on its way. The status file gains `paused.count` (`plugin/STATUS.md`); the bus's
  `Invoke` takes `pause` and `resume`, and still `stop-all` and `start-all`, so scripts written
  against them keep working. (#213)

### Fixed

- **"Already in your library" answers the keyboard again.** When it showed alone, after a
  duplicate was handed over or dropped, esc didn't close it and its buttons had no keyboard,
  in the main window as well. (#229)

## v0.6.0 — 2026-11-05

### Added

- **Search has a rail of its own.** On the search view the rail holds what a search is made of:
  your plugins, each ticked or not and with how it answered (a count, still asking, slow,
  failed), a click to leave one out or bring it back; the type to search in; and this
  session's recent searches, a click to ask again. Recent searches stay in memory only and are
  never saved. While the rail is open the plugins and type menus leave the search bar; fold the
  rail (`ctrl+b`) and they come back. (#205)
- **A way to your plugins from search.** "install a plugin…" at the foot of the rail's plugins
  (or "manage plugins…" in the plugins menu with the rail folded) opens Settings › qBittorrent ›
  Search plugins with the install field ready; esc goes back to the search. A plugin turned off
  in Settings shows dimmed, and a click opens its settings. (#205)
- **Clear a search.** The search field ends in "× esc" while it has words or results: a click,
  or esc, clears both and stops any plugin still asking. esc backs out one step at a time: an
  open pane, then the rows, then the search, then the view. (#205)

### Fixed

- v0.5.0 listed a Settings freeze as a known limit; it was already fixed in that release, and a
  check of every other scrolling pane found none like it. (#186)

## v0.5.1 — 2026-11-04

### Fixed

- **An interrupted import starts what it should.** When qBittorrent checks the imported
  torrents one at a time, a complete torrent waiting for its turn could be taken for one with
  nothing on disk and left stopped. The import now waits while any of its torrents is still
  being checked. (#208)
- **A move tidies up after itself.** The folders a torrent's own move leaves empty are removed,
  on the first move and on the move back, up to the folder you moved; a folder that still
  holds anything, a hidden file included, stays, as do every destination and qBittorrent's
  default save path. Only on disks omaseed can see. (#203)
- **The floating action bar is opaque** over the list, in both skins, and so is the outcome
  strip above it. (#202)
- **Search's idle sentence** names the server before its plugins ("asks public's 3 plugins
  that are on"), instead of "that is on on public". (#206)
- The title bar no longer fills the log with "anchors on an item managed by a layout". (#204)

## v0.5.0 — 2026-11-04

### Added

- **The window answers five questions.** The rail now lists what needs you, what is arriving,
  what landed today, what is giving back and what is resting, keys `1`–`5` (`0` is the whole
  library), under the views library, seed, log and search, with the servers and settings at
  its foot. Drag its edge to size it; it folds to a column of icons by itself when the window
  gets narrow, or by hand with `ctrl+b` or the tab on its edge. Right-click a server for its
  menu: switch to it, its alternative speeds, stop or start all, edit, test, open its web UI,
  copy its address. A question asks across the whole library, whatever folder you were in.
  Every state is still in the filter (`is:stalled`) and the `:` palette. (#167, #201)
- **Rows, table or space.** The library can show rows grouped under the five questions, in
  words before numbers ("waiting · no seed", "ready in 3 min"), the table as one sorted list,
  or space: where the space goes, a size map by folder, then torrent. The display tool in the
  title bar or `:` "show as…" picks one, and each folder, category, tag and question remembers
  its own; the tool's menu also sorts, and its word says how ("rows · added"). New installs
  start on rows; an existing install keeps the table. (#168, #201)
- **Details tell the story.** The pane opens on one fact and a sentence, then story · files ·
  swarm · trackers · facts (`1`–`5` while the pane has the keys). The story starts with what
  went wrong or the last move, then what happened to the torrent, newest first, and its lines
  in the log. The size says what skipped files still hold on disk. (#169, #201)
- **Search through your qBittorrent's search plugins.** `ctrl+f` searches with the plugins you
  install on your server (Settings › qBittorrent › Search plugins); omaseed names, ships and
  scrapes no site. Results arrive as each plugin answers, one row per torrent with the other
  copies' trackers folded in, unknown seed counts shown as "?", and a plugin that fails or is
  slow named with the reason. Rows you already have read "in your library" or "likely on
  disk"; adding goes through the add sheet. (#145)
- **Create a torrent from a folder.** "+ add ▾" › create (`ctrl+n`), also from `:` and a
  folder's menu. For anyone, it seeds at once from where the files are and copies the magnet.
  For a private tracker, it fills in the announce from your library (the passkey only ever
  shown as dots), sets the private flag, v1 and the source tag, and saves the `.torrent` for
  the upload without adding it; when you add the site's copy back, omaseed recognises the
  files and seeds it in place without downloading. (#146)
- **Turtle and burst.** qBittorrent keeps one set of alternative speeds; omaseed now gives you
  two presets for it. Turtle is that set, as qBittorrent's schedule and other clients see it;
  burst is written over it for 30 minutes, an hour or until you stop it, and turtle is written
  back after, also when omaseed quits mid-burst. The status line's chip, or the menus'
  alternative speeds row, opens the choice; `A` flips the last one picked; both presets are in
  Settings › qBittorrent › Speed. While one is on, the chip is tinted in its colour and each
  speed reads against its cap ("↓ 290 / 300 KB/s"). (#201)
- **The log has its own rail.** The four levels with their counts, ticked on and off (`1`–`4`),
  the torrents its lines are about, and since when; `/` filters its lines in the title bar, and
  "export log…" saves them as shown. (#201)
- **A second skin, Console.** Settings › Appearance › Skin: Omarchy, Console, or follow the
  desktop (Console while the console desktop's skin is on). The whole window repaints at
  once, no restart. (#171)

### Changed

- **A floating action bar.** Select torrents and a bar floats over the list with the torrent
  menu's own actions, icons and words: stop or start, recheck, move…, category › and tags ›
  (lists that set them for the whole selection), limits…, more › for the rest, delete…, esc;
  narrow, the words go before the icons. The status line stays on the server meanwhile. The
  outcome strip floats above the bar, and its "show" appears only when it brings something
  back into view, and says why. (#167, #170, #201)
- **A title bar instead of the hero.** The title and a sentence name what you are looking at
  ("2 need you · 7 arriving, the next in 3 min"); a picked folder gets "move this folder…".
  The speeds sit beside it, each with its last minute drawn under it (Settings › Appearance ›
  Speeds puts them in the status line instead), and the filter takes the free room when you
  use it, `cat:` and the like on chips. Notices, moves and creations sit in bands under it,
  beside a rail you can still use. (#167, #201)
- **Keys when you ask for them.** Hold alt for badges on every control, the details tabs
  included, read them in tooltips, menus, the palette and `?`. Sheets no longer print key
  lines at their foot; `?` has an "in a sheet" block. Moving the keys between the rail, the
  list and the details shows which one has them. (#167, #170, #201)
- **The omaseed menu** (`ctrl+.` or the mark) grows out of the mark, the version first: the
  tray's rows plus the window's own (seed console, log, commands, keys and help, hide).
  (#167, #170, #201)
- **One look for every sheet and menu.** Sheets share one head and foot, the main button
  filled; the torrent menu has an icon on every row and groups its verbs (run, place,
  transfer, reach, delete), with transfer, queue and copy as submenus, keys on the right and
  the torrent's state in its head. (#170, #201)
- **Settings works as a form.** Tab goes to the next field; each server row ends in a ›, and
  "edit" from its menu (or `e`) opens its form. Picking an API key or a username when signing
  in puts the caret in its field. (#201)
- **Move back brings back the loose files** a folder move carried, under their own tick; a file
  changed at the new place since stays there, and the summary names it. (#201)
- **The surface tint is back**, on the rail only: Settings › Appearance › Surface tint, off,
  4 % or 8 %. (#201)
- **Opaque by default,** like the other apps; Settings › Appearance › Window keeps
  "translucent". (#167)
- **Text reads on every fill.** Selected, hovered, pressed and tinted rows lift their dim and
  coloured words just enough to read at 4.5:1 in every theme, without flattening captions into
  the main text; also inside blocks, on the action bar's buttons and on hovered settings rows.
  (#90, #188)
- **When only adding it again frees the space,** on a hybrid or v2-only torrent, the band, `F`
  and the free sheet say so up front and offer "Add it again…" instead of a free that frees
  nothing. (#169)

### Fixed

- **Adding a magnet before its file list arrives no longer crashes qBittorrent 5.3.0.** The add
  sheet holds the add until the list is in, or a minute has passed, and says so. (#193)
- **A base-size change applies at once.** Spacing, column widths, the details pane and the
  rail's "N below" hint follow the shell's base size (or Appearance's) without a restart.
  (#156, #184)
- **The rail's group headings** no longer show the hover fill when nothing hovers them, and their
  fill no longer cuts against the line above. (#187, #201)
- **Switching servers no longer flashes red**: the dot reads "connecting" until the server
  answers. A server just added shows its torrent count at once. (#201)
- **Tooltips** are opaque, as wide as their words, and no longer stay on screen after `ctrl+b`.
  (#201)
- **A narrow window** keeps room for the names: no words drawn over others, the category, time
  and state giving way first. (#201)
- **A sheet could freeze the window** when one of its lines was just short of wrapping (the
  folder Move sheet, on some folders). (#201)
- **A move's long details** in its band show a scroll bar. (#201)

### Known limits

- The floating action bar is see-through over the list; it becomes opaque in v0.5.1. (#202)
- Settings' pane can, in rare window sizes, flip its scroll bar on and off and freeze the
  window. (#186)
- Once a search plugin is installed, the search view has no way to the plugins: add or remove
  them in Settings › qBittorrent › Search plugins. A later release links them from search.

## v0.4.0 — 2026-10-23

### Added

- **Skipped files that still take space.** A file you skip after it was written stays on disk:
  qBittorrent never deletes it. The Files tab now has an "On disk" column, the footer says what
  skipped files hold, and past 1 GiB or 5 % of the torrent a yellow band says so with "Why?".
  **Free the space** (the band's button, `shift+f`, or "free the space…" in the `:` palette)
  stops the torrent, deletes those files, starts it and rechecks, after a sheet that says
  exactly what goes. On a hybrid or v2-only torrent a file that holds downloaded pieces stays
  (a recheck there would leave the torrent short of 100 % for good); the sheet says why and
  offers to add the torrent again instead, which keeps your other files and choices.
- **A big tick waits 3 s, with Undo.** Ticking files that are skipped makes qBittorrent create
  them at full size at once, even on a stopped torrent. A tick that adds more than 1 GiB, or
  wants the whole torrent, now waits 3 s in a band with Undo (`u`) and Send now (`⏎`); a
  misclick undone in time never reaches qBittorrent.
- **Drives that can't hold part of a file.** On ntfs3, exFAT and FAT, a ticked file takes its
  whole size the moment it is wanted. The add sheet and the Files tab say so, check the room
  before a tick is sent (one that doesn't fit always waits), and name files of 4 GiB or more on
  FAT32, with "Skip these".
- **The add sheet names a torrent's extras.** NFO files, samples and adverts get a dim "extra"
  on their row and a line under the tree; they stay ticked, so the torrent seeds whole.
- **Search by words.** The library filter and the Files filter match words in any order and
  ignore the dots, dashes and underscores between them: "big buck bunny" finds
  Big.Buck.Bunny.

### Changed

- **What an action came to shows where you look.** A quick move, a stop, a category, a
  delete… now says how it went in a strip in the selection bar's place, and on the row's own
  status for a moment ("✓ moved · archive"). Done is green for 6 s; a failure is red and stays
  until × or esc, with "details"; several at once are counts ("2 moved, 1 didn't"). The footer
  keeps its counts and keys.
- **Picking several rows with the mouse shows how.** A □ at a row's start under the pointer
  adds that row (shift+click adds the range), ■ marks the selected ones, the `?` sheet has a
  "select with the mouse" block, and the selection bar says "ctrl+click add · shift+click
  range" the first three times.
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
- **A stuck move offers a recheck.** When a finished torrent couldn't move because the
  destination was full, its red block now offers `c recheck`: once there is room, the recheck
  moves it, where moving it to the same folder again did nothing.
- **"replace it" for an empty file in a move's way.** When a move's destination already holds
  an empty file that no torrent owns, the Move sheet offers to replace it instead of leaving
  that torrent out.
- **Ready for other engines.** A `servers.json` written by a future omaseed that talks to
  Transmission or Deluge is read and kept as it is; a server of another engine is listed and
  never contacted.
- **Screen readers** hear every field by its name, which rail row and which choices are
  selected, a notice's line before its actions, and nothing of a sheet's background.

### Fixed

- **A magnet clicked while omaseed is closed opens the add sheet.** From a browser or the file
  manager the link was lost on the way in (the app started, with no sheet); it now opens the
  sheet, or says at once that the torrent is already in your list.
- **The library table's Status cells line up.** About one start in eight drew a row's glyph
  and word 4 px to the left.

- **"Skip hash check" works with qBittorrent 5.3.** 5.3 reads the option under a new name and
  silently ignored the old one, so a torrent added with it for files already on disk came in
  at 0 % and had to be checked or downloaded again. omaseed now sends the name the server's
  version reads (both, when it can't tell the version yet).
- **Email encryption can be set on qBittorrent 5.3.** 5.3 replaced the email's "Use SSL"
  switch with a choice of SMTPS, STARTTLS or none; the email rows in the qBittorrent settings
  now offer that choice on a 5.3 server, and keep the switch on 5.2. As before, a setting the
  server doesn't have (such as 5.3's dropped folders for copies of .torrent files) is left out.
- **`omaseed --help`, `--version` and `--theme-roles` work with no display.** Over SSH, in a
  container or in a package check they print and exit, instead of aborting before a word.

## v0.3.3 — 2026-10-22

### Fixed

- **A big torrent's move no longer reads 100 % from its first second.** qBittorrent makes a
  torrent's file at its new place at full length before it copies into it, so the band
  measured the whole size there at once and its bar sat at 100 % for the whole move. It now
  says how long the torrent has been moving, and that qBittorrent doesn't report a single
  move's progress, until it lands.
- **"Added during the move" counts only torrents added during the move.** After moving one
  torrent, the summary could say that dozens were "added during the move" and still at the
  old place: every other torrent saved in the same folder. It now counts only those added
  after the move started.
- **A file already at the destination says what it is.** When a torrent is left out because a
  file with its name is already where it would go, the Move sheet now says what that file is
  and whose: "an empty file no torrent owns" (often what an earlier failed copy left), or "a
  file of 4.2 GB that NAME owns". On a server whose disks omaseed can't see, it says only
  whose it is.
- **A big move's details stay inside its band.** "details" on the summary of a move of many
  torrents listed every one of them down to the bottom of the window, with the details pane
  and the footer drawn over the list. Long details now take at most a third of the window and
  scroll inside it (with the arrow keys, j/k, page up/down, home and end once Tab reaches
  them); the library moves down under them. This holds for every notice with long details.
- **A moved torrent's later trouble is said in plain words.** The move's record said
  "later: missingFiles"; it now says "later: missing files · set location… or recheck it", as
  the table does, and starts where the band's own words end instead of repeating them.

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
