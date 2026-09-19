# omaseed

Torrents for Omarchy, powered by qBittorrent.

A native Linux desktop client (C++20, Qt 6 Quick) for one or many qBittorrent servers.
It owns `magnet:` links and `.torrent` files, lives in the tray, notifies on completion,
and follows the selected Omarchy theme live. Runs on any Linux with a bundled default theme.

Private during development; public with v1.

## Building

Requires Qt 6.8 or newer, CMake and Ninja. The Qt tools (`qmllint`, `qmlformat`, `qmlls`,
`qmltestrunner`) live outside the default `PATH` on Arch:

```
export PATH="/usr/lib/qt6/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure        # unit + qml tests
QT_QPA_PLATFORM=offscreen ./build/omaseed --help  # headless smoke
./build/omaseed                                   # real run under Hyprland
```

`-DOMASEED_WERROR=ON` turns compiler warnings into errors (CI uses it).
`-DBUILD_TESTING=OFF` skips the tests.

Linting and formatting:

```
scripts/dev/lint.sh build     # qmllint + clang-format --dry-run --Werror
cmake --build build --target lint
clang-format -i <file>        # C++
qmlformat -i <file>           # QML
```

Development servers (never the real library on :8080):

```
scripts/dev/test-server.sh start|stop|status|reset|seed [--strict]
```

## Omarchy integration

Three data files, none of them auto-loaded by Omarchy on their own; a dev script installs
the first two.

- **`data/hyprland/omaseed.lua`** — a Hyprland window-rule module for the window class
  `io.github.diogochaves.omaseed`. It drops Omarchy's default compositor-level window
  opacity (omaseed paints its own translucent "ground" per docs/05-ui-spec-v2.html and
  would otherwise get that translucency applied twice), and floats/centres/sizes the
  window while its title is the literal string `omaseed · first run` (the first-run
  wizard's contract — see T4.6). This Omarchy install configures Hyprland natively in Lua
  (no `hyprland.conf`, no `windowrule =` lines), so the file uses Omarchy's own
  `o.window(match, rules)` helper. It is not auto-required; after installing it you add one
  line yourself to `~/.config/hypr/hyprland.lua`, next to the other personal-override
  `require(...)` lines:
  ```
  require("hypr.omaseed")
  ```
- **`data/hooks/theme-set.d/omaseed`** — an executable hook. Omarchy runs every file in
  `~/.config/omarchy/hooks/theme-set.d/` after `omarchy-theme-set` finishes; this one calls
  `Reload()` on omaseed's D-Bus theme object (`io.github.diogochaves.omaseed`
  `/io/github/diogochaves/omaseed/Theme` `io.github.diogochaves.omaseed.Theme`) so the
  palette flips the instant the rest of the desktop's theme does, instead of waiting for
  `ThemeReader`'s file-watcher debounce. If omaseed is not running the D-Bus call fails
  silently and the hook still exits 0 — a theme switch is never held up by it.
- **`data/omarchy-menu.jsonc`** — one row (`torrents`, `` glyph, action `uwsm-app --
  omaseed`) to merge into `~/.config/omarchy/extensions/omarchy-menu.jsonc` (JSONC,
  hot-reloads on save). omaseed's `.desktop` file already makes it reachable from the
  built-in "Apps" submenu; this is a direct top-level shortcut.

Install (or remove) the hook and the Hyprland module for local development:

```
scripts/dev/install-omarchy-extras.sh              # installs the hook + hypr module, prints the require() line
scripts/dev/install-omarchy-extras.sh --menu        # also appends the menu row, only if absent
scripts/dev/install-omarchy-extras.sh --uninstall   # removes everything this script installed
```

Packaged copies are staged for T5.1's PKGBUILD under
`${CMAKE_INSTALL_DATADIR}/omaseed/{hyprland,hooks/theme-set.d}/` and
`${CMAKE_INSTALL_DATADIR}/omaseed/omarchy-menu.jsonc` (see `data/CMakeLists.txt`).

## Documents

- [CLAUDE.md](CLAUDE.md) — standing instructions for agents working in this repo (read first)
- [docs/06-implementation-plan.md](docs/06-implementation-plan.md) — the work breakdown: phases, tasks, acceptance criteria
- [docs/05-ui-spec-v2.html](docs/05-ui-spec-v2.html) — **approved UI spec** (open in a browser; theme and base-size toggles)
- [docs/04-ui-spec.html](docs/04-ui-spec.html) — first UI draft, kept for comparison
- [docs/03-charter.md](docs/03-charter.md) — what we are building, decisions, milestones (start here)
- [docs/02-exploration.md](docs/02-exploration.md) — research: landscape, bases evaluated, Omarchy-native mechanisms, sources
- [docs/01-plan-draft.md](docs/01-plan-draft.md) — the earlier omarqui-based draft, kept for the API notes and history

## License

MIT
