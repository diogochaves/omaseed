# Architecture

omaseed is one process and one binary. It has no torrent engine: everything it shows and does
goes through qBittorrent's public Web UI API v2 (2.15), against one or more servers, local or
remote. C++20 and Qt 6 Quick, built with CMake.

## Layers

Three static libraries and the executable. Each layer only depends on the ones above it.

**`omaseed-core`** (`src/core/`): Qt Core and Network only, no QML, every class unit-tested.

- `QbtClient`, one per server: Bearer API key (qBittorrent 5.2 and later) with cookie login as
  the fallback, a request queue with retry and back-off, a typed method per endpoint.
  **Errors are values, never exceptions** (`ApiError`, `QbtReply`): no call throws, and every
  failure reaches the UI as data it can show.
- `SyncEngine` polls `sync/maindata` with `rid` and applies full and partial updates to
  `TorrentModel`, `CategoryModel`, `TagModel`, `TrackerModel` and `GlobalState`. It polls every
  1.5 s while the window shows, 5 s while hidden and 15 s after an error. It raises the events
  that notifications are built from: a torrent completed, a server lost or back.
- `TorrentFilterModel` sorts and filters the table (status, category, tag, tracker, text);
  `FilterCounts` derives the rail's counts.
- The details tabs have their own models (`TorrentDetails`, `TorrentFilesModel`,
  `TorrentTrackersModel`, `TorrentPeersModel`, `TorrentWebSeedsModel`, `PieceStates`).
- Parsing: `Bencode`, `TorrentFile` and `MagnetLink` read names, sizes and info-hashes for the
  add sheet and duplicate detection. `MetadataLookup` asks qBittorrent for a magnet's file list
  before the add, and `PrivateTrackers` says which magnets it can't get one for
  ([DECISIONS.md](DECISIONS.md) C5). `TorrentProblem` and `ProblemLog` explain an errored
  torrent from qBittorrent's own log, and the failures only that log knows: a move, rename,
  web seed or .torrent copy that didn't happen ([DECISIONS.md](DECISIONS.md) C2).
  `ServerLog` reads the whole log for the log face while it shows, and `LogFilterModel` narrows
  it by level, words and torrent (C3).
- `ErrorWords` is the one table of plain words for what went wrong: an `ApiError` and what it
  was about become a headline naming the server or torrent, a line on what happens next, and
  the technical text for "details" ([DECISIONS.md](DECISIONS.md) C12).
- `ActionOutcome` words what a quick action on torrents came to ("3 stopped", "NAME didn't
  move", "2 moved to PATH, 1 didn't"), and `ActionOutcomes` holds the one strip on show and
  each row's mark, for the strip in the selection bar's place (C18 D1–D3).
- Servers and settings: `Server`, `ServerStore` (`servers.json`), `SecretStore` (the keyring
  through qtkeychain, a 0600 file under `secrets/` when there is none), `StateFile`,
  `ServerPreferences` (qBittorrent's own settings), `CertificatePin` (trust on first use).
- Features built on top: `SeedConsole`, `SeedLedger` and `WeeklyRecap` (the seed console),
  `OmaseedBackup`, `AutoBackup` and `TarArchive` (backups), `QbtSettingsExport` and
  `QbtSettingsImport` (a server's settings as a file, and back), `QbtSettingsFetch` (another
  server's settings, read aside without moving the app), `BackupFormat` (which of those files
  a file is, by its contents), `TorrentsBackup`,
  `TorrentsExport` and `TorrentsImport` (its torrents as a file, and back), `LibraryMatch`,
  `FreeSpace` and `SharedData` (what the add and delete sheets know about files already on
  disk), `RenamePlan` and `RenameDisk` (what a rename would land on).
- Skipped files and the disk ([DECISIONS.md](DECISIONS.md) C17): `SkippedOnDisk` (what a
  torrent's skipped files hold on this machine's disk, and the plan to free it),
  `FreeSkippedJob` and `AddAgainJob` (`SkippedJobs`: the free, and a hybrid or a v2-only
  torrent added again, C29), and
  `SelectionHold` (a big widening of a selection waits 3 s before it is sent).
- Moving a folder or a selection ([DECISIONS.md](DECISIONS.md) C15, C16: every move is a job):
  `FolderTree` (the library's save paths as the rail's folders), `MovePlan` (the preflight:
  targets by rule (a folder's structure, one folder, each its own for move back), room per
  drive, writability, shared data, files already at a target), `LooseFiles` (the files no torrent owns, failing closed),
  `ServerFolder` (what qBittorrent lists of a folder, for a server whose disks this machine
  can't see: C16 L13), and
  `MoveJob`, which sends the plan in waves and follows it for hours, saved under
  `~/.config/omaseed/moves/`. `MoveJob` hears and asks in neutral terms; `QbtMoveFeed` is its
  qBittorrent side (setLocation, the polls, the log's move failures through `ProblemLog`, and
  the bytes at a target for live progress). `MoveHistory` keeps the moves that landed (the
  last 20 or 30 days, per server, in `state.json`), and says what each one's move back would do
  per torrent (C18). In the app, `MoveController` puts them together for the window (its
  sheet's half in `MoveControllerSheet.cpp`); `App::setLocation` starts a job.

**`omaseed-integration`** (`src/integration/`): the desktop, through Qt DBus and Gui.

- `ThemeReader` and `Theme`: read the current Omarchy theme's `colors.toml` and `shell.toml`
  (the bundled Tokyo Night when Omarchy is absent), watch them, and repaint live. A theme hook
  can also ask for a reload over D-Bus. QML never sees `Theme` by that name: `Omaseed.Kit`
  registers it as `ThemeBase`, and its `Theme` singleton is `Kit/Theme.qml`, which derives from
  it and adds the one thing C++ can't give a binding, a `space(px)` that reads `spaceScale`
  ([DECISIONS.md](DECISIONS.md) C28, #156).
- `SingleInstance`: `org.freedesktop.Application` on `io.github.diogochaves.omaseed`; a second
  launch hands its magnets and `.torrent` files to the first one and raises it, and
  `omaseed --quit` asks it to quit. `QuitSignals` turns SIGTERM, SIGINT and SIGHUP into the
  same quit as the tray's, so what the app writes on quit is written.
- `Tray` (StatusNotifierItem), `Notifier` (`org.freedesktop.Notifications` with actions), `Xdg`
  (file manager, MIME handlers, clipboard), `Autostart`, `HyprlandEvents`.
- The companion bar widget's side: `StatusWriter` writes the status file it reads, `StatusBus`
  takes its commands, `WidgetHeartbeat` tells the tray when the widget is there, and
  `BarWidget` / `BarWidgetInstaller` put it in the shell. `BarMenu` is the one menu the tray
  and the widget's ⋯ both show, built once and answered by App
  ([DECISIONS.md](DECISIONS.md) C13). The file's format is in
  [`plugin/STATUS.md`](../plugin/STATUS.md).

**`omaseed-app`** (`src/ui/app/`): `App`, the one object QML talks to. It owns the servers, the
sync engine and the models, and turns key presses and clicks into API calls.

**`omaseed`** (`src/ui/`): `main.cpp` and the QML.

- `qml/Kit/`: the component kit (buttons, fields, sheets, the table's pieces). Kit components
  take only `Theme` tokens and each has a `tests/qml/tst_<Name>.qml`.
- `qml/Views/`: the screens, built from the kit; views take models. The window has three
  faces: the library, the seed console (`Console`) and qBittorrent's log (`LogView`).
- `Keymap.qml` maps keys to actions per focus area and feeds the command palette and the `?`
  sheet.

**`plugin/`** is the bar widget itself, a Quickshell plugin (`chaves.omaseed`). It never talks
to qBittorrent: it reads the status file and sends commands over D-Bus.

Network calls run on the main thread through Qt's async API; only bencode parsing and hashing
of large `.torrent` files go to `QtConcurrent`.

Everything omaseed keeps lives in `~/.config/omaseed/`: `servers.json`, `state.json` (window,
columns, filters) and `secrets/` when there is no keyring. No database.

## Theme rules

Every QML file follows these, so the app looks like the Omarchy shell and repaints when the
theme changes.

1. Colours only through `Theme.<role>`: `background`, `foreground`, `dimForeground`,
   `brightForeground`, `accent`, `muted`, `red`, `yellow`, `orange`, `green`, `cyan`, `blue`,
   `magenta`, and the torrent states (`stateDownloading`, `stateSeeding`, `stateStopped`,
   `stateError`, `stateChecking`, `stateQueued`, `stateStalled`). No hex colours in QML.
2. Fills through `Theme.fill(alpha)`: 0.04 idle, 0.08 hover or cursor, 0.18 selected, 0.22
   pressed. Borders 0.40 idle, 0.25 hover. Hairlines 0.12.
3. Sizes from the shell's `base-size`: `Theme.font.caption|small|body|subtitle|title|heading|display`
   (× 0.833 / 0.917 / 1 / 1.083 / 1.167 / 1.333 / 2), spacing through `Theme.space(px)`. A
   binding of `Theme.space(…)` alone follows a base-size change like one of `Theme.font.*`.
4. The window is `Theme.background` at `Theme.windowAlpha` with a 1 px edge, `accent` when
   active and `muted` when not. The ground is opaque (`windowAlpha` 1) unless Settings ›
   Appearance › Window is translucent, which takes the launcher's alpha or the transparency
   set there (DECISIONS C24 H13). Popups, sheets and palettes are opaque with an `accent` edge.
5. Corner radius comes from Hyprland, 0 by default. Never hardcode rounding.
6. The font is fontconfig's `monospace`; icons are Nerd Font glyphs chosen once in
   `Kit/Glyphs.qml`.
7. Every colour is a binding, never a copy: a theme change repaints without a restart.

## Design

[`05-ui-spec-v2.html`](05-ui-spec-v2.html) is the approved design; the numbered boards after it
(`08-…` to `28-…`) each design one feature or change, and code comments cite them as
`docs/NN §…`. Open them in a browser. [`DECISIONS.md`](DECISIONS.md) keeps the rulings the code
cites by number.
