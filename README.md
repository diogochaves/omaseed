# omaseed

Torrents for Omarchy, powered by qBittorrent.

A native Linux desktop client (C++20, Qt 6 Quick) for one or many qBittorrent servers. It
owns `magnet:` links and `.torrent` files, lives in the tray, notifies on completion, a lost
server and a full disk, opens finished content in the file manager, and presents a dense
keyboard-driven table in the exact visual language of the Omarchy shell — repainting live
the instant the Omarchy theme changes. It does not contain a torrent engine; qBittorrent
(local or remote) is the engine. It runs on any Linux, with a bundled default theme when
Omarchy is absent.

Private during development; public with v1.

<p>
  <img src="docs/evidence/phase3/exit/fig01-main-tokyo-night-12.png" alt="omaseed's main window in Tokyo Night" width="49%">
  <img src="docs/evidence/phase3/exit/fig01-main-lupine-12.png" alt="omaseed's main window in Lupine (light)" width="49%">
</p>

- Follows the selected Omarchy theme live, all bundled themes including the light ones
- Lives in the tray; closing the window hides it, it keeps polling and notifying
- Default handler for `magnet:` links and `.torrent` files, registered on request
- Desktop notifications with actions (open folder, show, settings), not just a toast
- One server or many, with per-server credentials and a default
- Keyboard-first: every action in the table, rail and details pane has a key; `?` shows
  the full map

## Install

### Arch

From a tagged release (once one exists):

```
git clone https://github.com/diogochaves/omaseed.git
cd omaseed/packaging/arch
makepkg -si    # builds the tagged source tarball (packaging/arch/PKGBUILD), needs a pushed tag
```

From the working tree, before any tag exists or while iterating on packaging:

```
mkdir -p build/makepkg-scratch && cp packaging/arch/PKGBUILD.local build/makepkg-scratch/PKGBUILD
cd build/makepkg-scratch && makepkg -si
```

`-si` installs with pacman and needs root — that part is yours to run, not an agent's (see
`CLAUDE.md`).

Later, once the package is accepted into the `[omarchy]` repo:

```
pacman -S omaseed
```

**Runtime requirements:** qBittorrent 5.2 or newer with its WebUI turned on (`Options →
WebUI` in the qBittorrent app, or `--webui-port` for `qbittorrent-nox`), and either an API
key or a WebUI username/password. Older servers without API keys work over the cookie
login fallback (username/password). The qBittorrent engine itself — local or remote — is
never installed by omaseed except through the first-run offer below.

**What the package installs:** the `omaseed` binary, its `.desktop` file and D-Bus service
file, hicolor icons, the MIT licence, and three Omarchy integration files staged (not
activated) under `/usr/share/omaseed/`: a Hyprland window-rule module, a theme-change hook,
and an `omarchy-menu.jsonc` row. Two commands turn them on, since a package must never edit
your own config files silently:

```
ln -sf /usr/share/omaseed/hooks/theme-set.d/omaseed ~/.config/omarchy/hooks/theme-set.d/omaseed
```

and add this line next to your other personal `require("hypr...")` lines in
`~/.config/hypr/hyprland.lua` (nothing edits that file for you):

```
require("hypr.omaseed")
```

That module opts the window out of Omarchy's default compositor-level opacity — omaseed
paints its own translucent background, and the two would otherwise multiply. The
`omarchy-menu.jsonc` row (a "Torrents" shortcut on the root menu) is copy-paste from
`/usr/share/omaseed/omarchy-menu.jsonc` into
`~/.config/omarchy/extensions/omarchy-menu.jsonc`; the app is already reachable from the
built-in Apps submenu without it.

## First run

On a fresh launch, with no server configured, omaseed shows a full-window setup page
instead of the table. Three steps, `esc` to quit at any point:

1. **Choose a server.** omaseed probes for one already running on this machine (a
   listening port 8080, a `qbittorrent` Docker container, a running `qbittorrent-nox`) and
   offers it if found. You can always choose "connect to a remote server" instead. If
   nothing local was found, a third option offers to install `qbittorrent-nox` here —
   two copyable commands, or a **run with pkexec** button; omaseed itself never runs
   `sudo`.
2. **Enter the URL and credentials**, then **test** before continuing. Pick API key,
   cookie (username/password) or none as the auth mode. To get an API key, open the
   server's WebUI (the **open web UI** button does this) and look for the WebUI's API key
   setting — in qBittorrent 5.2 that lives under the WebUI options — generate a key there
   and paste it in. The **test** button reports the version, API version and torrent count
   before you commit.
3. **Register as the magnet/`.torrent` handler**, or **not now**. `application/x-bittorrent`
   and `x-scheme-handler/magnet` are only registered if you say yes here (or later, from
   Settings → General).

## Keys

Every key omaseed binds comes from one table in the source
(`src/ui/qml/Keymap.qml`), and the in-app `?` sheet is drawn from that same table, so the
two can never drift. It groups keys into four columns; a **capital** letter is the
stronger variant of its lower-case neighbour (`p` stop/start vs. `P` force-start; `d`
delete vs. `D` delete with files; `y` copy magnet vs. `Y` copy hash), and `ctrl` marks an
app-level shortcut rather than a per-row one.

**Navigate**

| Key | Does |
|---|---|
| `j` `k` | move the cursor down / up |
| `g` `G` | first / last row |
| `h` `l` | rail · table · details — move focus one pane left/right |
| `/` | filter (focus the search field) |
| `0`–`9` | in the rail: jump to a status filter |
| `i` | toggle the details pane |
| `1`–`4` | in the details pane: switch tab (general, files, trackers, peers) |
| `ctrl+k` | jump to a torrent by name, hash or tracker |

**Select**

| Key | Does |
|---|---|
| `space` | toggle the cursor row's selection |
| `v` | extend the selection to the cursor (range) |
| `ctrl+a` | select all visible rows |
| `esc` | clear the selection, then the filter, then close whatever is open |

**Act**

| Key | Does |
|---|---|
| `a` | add a torrent |
| `enter` | open the actions menu for the cursor/selection |
| `p` `P` | stop/start · force-start |
| `d` `D` | delete · delete with files |
| `c` `R` | recheck · reannounce |
| `C` `t` `m` `L` `r` | category · tags · location · limits · rename |
| `[` `]` | move up / down the queue |
| `o` | open folder |
| `y` `Y` | copy magnet link · copy info hash |

**App**

| Key | Does |
|---|---|
| `s` | sort (repeat to reverse) |
| `A` | toggle alternative speed limits |
| `,` | open settings |
| `ctrl+,` | choose visible columns |
| `q` | hide to tray |
| `ctrl+q` | quit |
| `super+t` | suggested Hyprland bind — not bound by omaseed itself, see below |
| `?` | this key map |

**Inside the details pane** (not shown in the `?` sheet, but live): `h`/`l` fold/unfold a
files-tree row, `+`/`-` raise/lower a file's priority, `x` skip a file, `o` open a file,
`y` copy a row's value, `a`/`d` add/remove a tracker row, `R` reannounce, `b` ban a peer.
The rail reuses `j`/`k`/`g`/`G`/`l`/`enter`/`space` with the same navigate/select meanings
as the table.

`super+t` is a suggestion, not something omaseed registers itself — add it in your own
Hyprland keybinds to raise or summon the window over D-Bus activation
(`org.freedesktop.Application.Activate` on `io.github.diogochaves.omaseed`), which a second
`omaseed` launch (or `uwsm-app -- omaseed`) already triggers for you.

## Theme

omaseed reads the same `colors.toml`/`shell.toml` files the rest of the Omarchy shell
does and repaints live the instant `omarchy-theme-set` runs — no restart, and it works on
every bundled theme, light ones (Lupine) included. Base text size follows `shell.toml`'s
`[font] base-size`. Without Omarchy present, it falls back to a bundled Tokyo Night theme
at base size 12.

Settings → Appearance exposes:

- **Follow the Omarchy theme** — off keeps the bundled Tokyo Night regardless of what the
  desktop does.
- **Base size** — overrides `shell.toml`'s value; empty follows it.
- **Window transparency** — how much wallpaper shows through the window's ground panel,
  50–100 %; empty is 90.
- **Tray icon** — `auto` / `always` / `never`.

## Servers

Settings → Servers lists every configured server, with **add**, **test**, **edit**,
**remove** and **set as default** (`a`/`enter`/`x`/`d` respectively, or click). Adding a
server asks for its URL, a name, and the auth mode (API key, cookie login with
username/password, or none for an unauthenticated localhost server) — test before saving.
The credential is stored via libsecret (through `qtkeychain-qt6`) when a Secret Service
provider is available, or in a 0600 file under `~/.config/omaseed/secrets/` otherwise.

For a remote server whose files live under a different path locally (a NAS mount, a
seedbox with a different layout), Settings → General's **path mapping** rewrites the
server's own save path to a local prefix before asking the file manager to reveal it with
`o`/open folder — off by default.

Servers with no API key configured fall back to cookie (username/password) login
automatically; this is the only way to talk to qBittorrent versions before the 5.2 API-key
feature existed.

## Troubleshooting

- **Where's the tray icon?** The Omarchy shell keeps a fixed set of tray items visible and
  puts everything else — omaseed included — behind the tray drawer's chevron. Pin
  `io.github.diogochaves.omaseed` in the shell's tray settings if you want it always
  visible.
- **The window looks translucent — is that a bug?** No: it is by design (spec
  `docs/05-ui-spec-v2.html`), ~90 % of the theme background colour showing wallpaper
  through it. The Hyprland module (`data/hyprland/omaseed.lua`) opts the window out of
  Omarchy's own default compositor opacity so the two effects do not multiply; if you skip
  activating that module the window will look slightly more transparent than intended.
- **"Failed to register with host portal" in the logs.** Harmless — omaseed logs this at
  every launch and does not currently use the desktop portal for anything but the
  `.torrent` file picker (`ctrl+o`), which still works. Nothing to act on.
- **No desktop notifications.** They go through `org.freedesktop.Notifications`, so the
  Omarchy shell's notification service needs to be running; there is no other requirement.
- **See what's happening.** Qt logs go to `journalctl` by default; run with
  `QT_FORCE_STDERR_LOGGING=1 ./build/omaseed` (or the installed binary) to see them in the
  terminal instead.
- **Try the UI without a server.** `omaseed --demo table` (add `--headless` to just print a
  summary and exit, useful over SSH or in a script) loads a fictional library so you can
  look at the table, rail and details without qBittorrent running. `omaseed --help` lists
  every `--demo` view (`details`, `firstrun`, `gallery`, `notify`, `overlays`, `rail`,
  `states`, `sync`, `table`, `torrents`, `tray`).
- **Where things live on disk:** `~/.config/omaseed/servers.json` (the server list),
  `~/.config/omaseed/state.json` (window/column/filter state, notification toggles,
  appearance overrides), `~/.config/omaseed/secrets/` (the 0600-file credential fallback
  when no keyring is available).
- **Start first run over.** Remove `~/.config/omaseed/servers.json` (and, if you want the
  handler prompt to ask again, clear the magnet/`.torrent` entries it set in
  `~/.config/mimeapps.list`) and relaunch.

## Developing

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

```
scripts/dev/lint.sh build            # qmllint + clang-format --dry-run --Werror
scripts/dev/test-server.sh start|stop|status|reset|seed [--strict]  # dev servers, never :8080
scripts/dev/install-omarchy-extras.sh [--menu] [--uninstall]        # hook + hypr module for a source checkout
```

Read `CLAUDE.md` first — the servers rule, theme rules and conventions all live there — then
`docs/06-implementation-plan.md` for the architecture and work breakdown, and
`docs/03-charter.md` for scope and decisions.

## Packaging

`cmake --install` lays out a full FHS tree under its prefix: the `omaseed` binary,
`.desktop`, D-Bus service file, hicolor icons (the scalable SVG plus PNGs at 16–256 px,
rendered from it at build time by `rsvg-convert` or ImageMagick — see `data/CMakeLists.txt`;
if neither is installed the PNGs are skipped and only the SVG lands, with a configure-time
warning), the MIT `LICENSE` under `share/licenses/omaseed/`, and the three Omarchy extras
above under `share/omaseed/`. `packaging/arch/PKGBUILD` wraps exactly that install with
`DESTDIR`; nothing in `package()` reaches outside what CMake already installs.

Two PKGBUILDs live in `packaging/arch/`:

- **`PKGBUILD`** — the real recipe, the one that eventually goes to the `omarchy-pkgs` PR.
  Builds from the tagged GitHub release source tarball
  (`https://github.com/diogochaves/omaseed/archive/refs/tags/v$pkgver.tar.gz`); needs a
  pushed tag to resolve. `sha256sums` is filled in by `scripts/release.sh` once a release
  is cut.
- **`PKGBUILD.local`** — builds from the working tree instead (`prepare()` copies the repo
  two directories up via `rsync`, excluding `.git` and build directories), so the package can
  be built and tried before any tag exists, or while iterating on packaging itself. Otherwise
  identical to `PKGBUILD` (kept in sync by hand — makepkg has no templating).

Build the local package (no `-i`, no `sudo`; run from a scratch copy so `makepkg`'s own
`src/`/`pkg/` litter stays out of the repo):

```
mkdir -p build/makepkg-scratch && cp packaging/arch/PKGBUILD.local build/makepkg-scratch/PKGBUILD
cd build/makepkg-scratch && makepkg -f
tar tf omaseed-*.pkg.tar.zst        # inspect what would be installed
namcap PKGBUILD *.pkg.tar.zst       # lint the recipe and the built package
```

To try the built package without installing it system-wide:

```
bsdtar -xf build/makepkg-scratch/omaseed-*.pkg.tar.zst -C /tmp/pkgroot
QT_QPA_PLATFORM=offscreen /tmp/pkgroot/usr/bin/omaseed --help
```

**`packaging/omarchy/`** holds `package.json` (and a `package.json.md` explaining every
field), the `.omarchy/package.json` shape the `omarchy-pkgs` PR needs alongside
`packaging/arch/PKGBUILD` (charter §6). `scripts/release.sh` fills in the real source
tarball checksum once a tag exists; opening the PR itself is Diogo's step, guided by
`docs/RELEASING.md`.

## Documents

- [CLAUDE.md](CLAUDE.md) — standing instructions for agents working in this repo (read first)
- [docs/06-implementation-plan.md](docs/06-implementation-plan.md) — the work breakdown: phases, tasks, acceptance criteria
- [docs/05-ui-spec-v2.html](docs/05-ui-spec-v2.html) — **approved UI spec** (open in a browser; theme and base-size toggles)
- [docs/04-ui-spec.html](docs/04-ui-spec.html) — first UI draft, kept for comparison
- [docs/03-charter.md](docs/03-charter.md) — what we are building, decisions, milestones (start here)
- [docs/RELEASING.md](docs/RELEASING.md) — how to cut a release
- [CHANGELOG.md](CHANGELOG.md) — what shipped, release by release
- [docs/02-exploration.md](docs/02-exploration.md) — research: landscape, bases evaluated, Omarchy-native mechanisms, sources
- [docs/01-plan-draft.md](docs/01-plan-draft.md) — the earlier omarqui-based draft, kept for the API notes and history

## License

MIT
