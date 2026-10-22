# omaseed — instructions for agents

Native Linux desktop client for qBittorrent, Omarchy-first. C++20 + Qt 6 Quick, CMake, MIT.
Read in this order before changing anything: this file → `README.md` → `docs/ARCHITECTURE.md`
(the layers and the theme rules) → `docs/DECISIONS.md` (rulings the code cites) →
`docs/05-ui-spec-v2.html` and the numbered boards after it (the design; open them in a browser).
A maintainer's private `CLAUDE.local.md` beside this file, when there is one, adds to it.

## Non-negotiables

- **The approved design is v2** (`docs/05-ui-spec-v2.html`). **Never edit it**, nor a board
  that has been ruled. A design change is a new numbered board (`docs/NN-….html`) and needs
  the maintainer's approval before it is built.
- **MIT only. No code copied from qui, qBittorrent, Taurent or any GPL project.** Read them
  for behaviour, implement against the public qBittorrent WebUI API v2 (2.15).
- **Know which qBittorrent you are talking to.**
  - A real library on this machine (often `http://127.0.0.1:8080`) is off limits to agents.
    The only permitted contact is the first-run detector's TCP port probe, which never
    authenticates.
  - `http://127.0.0.1:8081` is the playground: `scripts/dev/test-server.sh start`. Localhost
    is auth-bypassed, so no credential is needed. Add, delete, `reset` at will.
  - `http://127.0.0.1:8082` is the strict twin: `scripts/dev/test-server.sh start --strict`.
    Real auth: cookie login with `QBT_STRICT_USER`/`QBT_STRICT_PASS`, Bearer with
    `QBT_STRICT_API_KEY` from `dev.env`. Never `reset --strict`: the key lives in that profile.
    The first `start --strict` makes a random login (and a key, if there is none) and saves
    it to `dev.env`; `rotate --strict` changes the login and keeps the key.
- Credentials live in `~/.config/omaseed/dev.env` (0600, outside the repo). Load with
  `set -a; . ~/.config/omaseed/dev.env; set +a`. Never print them, never commit them: no
  credential is written in the repo. Scripts and tests read `QBT_STRICT_*` from the
  environment, and live strict tests skip when those are unset.
- No `sudo`. If something needed is missing, stop and report it.
- Follow the theme rules in `docs/ARCHITECTURE.md` exactly: no hex colours in QML, only
  `Theme` roles; sizes from `base-size`; fills 4/8/18/22 %; hairlines at 12 %.

## Layout

```
CMakeLists.txt            top-level, Qt 6.8+, C++20, warnings as errors in CI
src/core/                 pure Qt, no QML: api client, sync model, servers, secrets, parsing
src/integration/          dbus, tray, notifications, theme reader, hooks, xdg helpers
src/ui/                   main.cpp, App (app/), QML kit (qml/Kit/), views (qml/Views/)
plugin/                   the companion bar widget (a Quickshell plugin), and its status file
tests/                    QtTest unit tests (core, integration, app) + qmltestrunner tests
data/                     .desktop, icons, hyprland rule snippet, theme-set hook, menu row
packaging/                arch/ PKGBUILD, omarchy/ package.json for omarchy-pkgs
scripts/dev/              test-server.sh and other developer scripts
docs/                     architecture, decisions, the design (spec v2 and the boards), releasing
.github/workflows/ci.yml  CI: Arch container, -DOMASEED_WERROR=ON, ctest -LE live, lint, shellcheck
```

## Commands

```
export PATH="/usr/lib/qt6/bin:$PATH"            # qmllint, qmlformat, qmlls, qmltestrunner
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure        # unit + qml tests
QT_QPA_PLATFORM=offscreen ./build/omaseed --help  # headless smoke
./build/omaseed                                   # real run under Hyprland
qmllint src/ui/qml/**/*.qml && qmlformat -i <file>
scripts/dev/shellcheck.sh                          # every tracked shell script (also ctest -R shellcheck)
scripts/dev/test-server.sh start|stop|status|reset|seed [--strict]
scripts/dev/test-server.sh rotate --strict        # new random strict login in dev.env; key kept
scripts/dev/swarm.sh start|seed|status|break CASE|fix|reset|stop   # offline swarm: real downloads on :8081
scripts/dev/swarm.sh leech start [--rate KIB]|status|stop  # a leecher on :8084 pulls from :8081, nonstop: live uploads
```

The walks and boxes below run the app in an [omabox](https://github.com/diogochaves/omabox), an
invisible Omarchy desktop, so nothing opens on the real one:

```
scripts/dev/swarm-walk.sh [OUT] | --interactive   # every torrent state, shown in the app in an omabox
scripts/dev/strict-walk.sh [OUT] | --interactive  # first run + cookie / API key login against :8082, in an omabox
scripts/dev/notify-walk.sh [OUT] | --interactive  # downloads finish: one notice each, a click shows the torrent
scripts/dev/magnet-walk.sh [OUT] | --interactive  # xdg-open a magnet / .torrent: the add sheet, closed, in the tray, duplicate
scripts/dev/actions-walk.sh [OUT] | --interactive # renames, categories, tags, auto-TMM, set location, bar, delete: checked on server and disk
scripts/dev/settings-walk.sh [OUT] | --interactive  # qBittorrent settings pane on a box-owned qBittorrent: sections, switches, Web UI port and API key confirms
scripts/dev/servers-walk.sh [OUT] | --interactive # two qBittorrents: first run, add, switch three ways, an action each side, remove
scripts/dev/backup-walk.sh [OUT] | --interactive  # a qBittorrent's torrents exported and imported through the app, checked on both servers
scripts/dev/error-walk.sh [OUT] | --interactive   # a torrent goes into error: the row's cause, the error block, one notice
scripts/dev/menu-walk.sh [OUT] | --interactive    # the one menu: tray and bar widget ⋯, server switches, quit
scripts/dev/move-walk.sh [OUT] | --interactive    # folder and selection moves, queue, places, partial pack, move back, recheck (~11 min; OMASEED_MOVE_WALK_FROM=selection|sheet|history|music|remote)
omabox check run [--approve] --base .local/visual --plugin plugin -- scripts/dev/visual-check.sh  # the walks' screens against a baseline: same, or what changed
scripts/dev/manual-box.sh [--headless] | down          # throwaway desktop for manual tests: omaseed installed, two qBittorrents (public :8090, private :8091)
scripts/dev/move-library.sh [-b BOX] [--server S]  # the move test library in a box's qBittorrent: 109 torrents (4 partial packs), /tmp/fast, a clash, a read-only dest
scripts/dev/app-box.sh up [-b NAME] [--build DIR] [--handlers] [--qbt]  # agents: build/omaseed in a box on :8081 (--qbt: the box's own qBittorrent), swarm .torrents in ~/Downloads
scripts/dev/app-box.sh restart | shot | down [-b NAME]  # relaunch after a rebuild (the log has Qt's output), a window shot, away
scripts/dev/app-box.sh api [-b NAME] [--server public|private] [-X M] PATH [CURL ARGS]  # the box's qBittorrent (playground, or a manual box's own, signed in)
```

`docs/TESTING.md` is the guide: the layers, the order (automated first, then a person's pass from
`docs/manual-tests/`), and the rules (test scripts committed and re-runnable, research repros in
`scripts/dev/research/`, `scripts/dev/fastresume.py` to read what qBittorrent saved).

Anything that can open a window, reach the session D-Bus or the keyring runs in an omabox or
through `ctest`, never straight on the desktop. Test binaries force Qt's offscreen platform
even when run directly; `OMASEED_TEST_PLATFORM` overrides that, and only inside an omabox.

Visual checks: run the app in a box, take a window shot (`app-box.sh shot`), and look at the
image. Close what you open. A theme switch (`omarchy-theme-set <name>`) inside a box is fine;
on a real desktop, note the theme first (`cat ~/.local/state/omarchy/current/theme.name`) and
put it back afterwards.

## Conventions

- Small commits, imperative subject, body says why. Every commit builds and passes tests.
- C++: `.hpp/.cpp`, `camelCase` methods, `m_` members, Qt naming, no raw `new` outside Qt
  parent ownership, `QStringLiteral`, no exceptions across Qt boundaries.
- QML: one component per file, `Kit/` components take only `Theme` tokens, views take models.
  `required property` for model data. No `Component.onCompleted` hacks for layout.
- Shell is shellcheck-clean (`scripts/dev/shellcheck.sh`, `.shellcheckrc`). Point each
  non-constant `source` at its file with `# shellcheck source=…`; any other exception is a
  per-line `# shellcheck disable=SCxxxx # reason`, never a behaviour change.
- Tests before UI: every `core` class ships with a QtTest. Kit components ship a
  `tst_<Name>.qml`.
- A comment that rests on a ruling cites it (`docs/DECISIONS.md Q19`, `docs/12 §3`). A new
  ruling the code depends on gets an entry in `docs/DECISIONS.md`.
