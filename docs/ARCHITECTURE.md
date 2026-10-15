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
  add sheet and duplicate detection. `TorrentProblem` and `ProblemLog` explain an errored
  torrent from qBittorrent's own log, and the failures only that log knows: a move, rename,
  web seed or .torrent copy that didn't happen ([DECISIONS.md](DECISIONS.md) C2).
- Servers and settings: `Server`, `ServerStore` (`servers.json`), `SecretStore` (the keyring
  through qtkeychain, a 0600 file under `secrets/` when there is none), `StateFile`,
  `ServerPreferences` (qBittorrent's own settings), `CertificatePin` (trust on first use).
- Features built on top: `SeedConsole` and `SeedLedger` (the seed console), `OmaseedBackup`,
  `AutoBackup` and `TarArchive` (backups), `LibraryMatch`, `FreeSpace` and `SharedData` (what
  the add and delete sheets know about files already on disk), `RenamePlan` and `RenameDisk`
  (what a rename would land on).

**`omaseed-integration`** (`src/integration/`): the desktop, through Qt DBus and Gui.

- `ThemeReader` and `Theme`: read the current Omarchy theme's `colors.toml` and `shell.toml`
  (the bundled Tokyo Night when Omarchy is absent), watch them, and repaint live. A theme hook
  can also ask for a reload over D-Bus.
- `SingleInstance`: `org.freedesktop.Application` on `io.github.diogochaves.omaseed`; a second
  launch hands its magnets and `.torrent` files to the first one and raises it.
- `Tray` (StatusNotifierItem), `Notifier` (`org.freedesktop.Notifications` with actions), `Xdg`
  (file manager, MIME handlers, clipboard), `Autostart`, `HyprlandEvents`.
- The companion bar widget's side: `StatusWriter` writes the status file it reads, `StatusBus`
  takes its commands, `WidgetHeartbeat` tells the tray when the widget is there, and
  `BarWidget` / `BarWidgetInstaller` put it in the shell. The file's format is in
  [`plugin/STATUS.md`](../plugin/STATUS.md).

**`omaseed-app`** (`src/ui/app/`): `App`, the one object QML talks to. It owns the servers, the
sync engine and the models, and turns key presses and clicks into API calls.

**`omaseed`** (`src/ui/`): `main.cpp` and the QML.

- `qml/Kit/`: the component kit (buttons, fields, sheets, the table's pieces). Kit components
  take only `Theme` tokens and each has a `tests/qml/tst_<Name>.qml`.
- `qml/Views/`: the screens, built from the kit; views take models.
- `Keymap.qml` maps keys to actions per focus area and feeds the key hints.

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
   (× 0.833 / 0.917 / 1 / 1.083 / 1.167 / 1.333 / 2), spacing through `Theme.space(px)`.
4. The window is `Theme.background` at `Theme.windowAlpha` with a 1 px edge, `accent` when
   active and `muted` when not. Popups, sheets and palettes are opaque with an `accent` edge.
5. Corner radius comes from Hyprland, 0 by default. Never hardcode rounding.
6. The font is fontconfig's `monospace`; icons are Nerd Font glyphs chosen once in
   `Kit/Glyphs.qml`.
7. Every colour is a binding, never a copy: a theme change repaints without a restart.

## Design

[`05-ui-spec-v2.html`](05-ui-spec-v2.html) is the approved design; the numbered boards after it
(`08-…` to `28-…`) each design one feature or change, and code comments cite them as
`docs/NN §…`. Open them in a browser. [`DECISIONS.md`](DECISIONS.md) keeps the rulings the code
cites by number.
