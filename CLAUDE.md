# omaseed — instructions for agents

Native Linux desktop client for qBittorrent, Omarchy-first. C++20 + Qt 6 Quick, CMake, MIT.
Read in this order before doing anything: this file → `docs/CHECKPOINT.md` (where the last
session stopped and what is next) → `docs/06-implementation-plan.md`
(the work breakdown you execute) → `docs/05-ui-spec-v2.html` (the approved look, open it in
a browser) → `docs/03-charter.md` (scope and decisions). `docs/01` and `02` are history.

## Non-negotiables

- **The approved design is v2** (`docs/05-ui-spec-v2.html`). `docs/04-ui-spec.html` (v1) is
  kept for comparison. **Never edit either spec file.** Design changes go in a new file
  `docs/07-…` and need Diogo's approval.
- **MIT only. No code copied from qui, qBittorrent, Taurent or any GPL project.** Read them
  for behaviour, implement against the public qBittorrent WebUI API v2 (2.15).
- **Two servers exist on this machine. Know which you are talking to.**
  - `http://127.0.0.1:8080` is Diogo's real library ("home"). **Agents never talk to it.**
    No credential for it exists in the dev environment on purpose. The only permitted contact
    is the first-run detector's TCP port probe (T4.6), which never authenticates.
  - `http://127.0.0.1:8081` is the playground: `scripts/dev/test-server.sh start`. Localhost
    is auth-bypassed, so no credential is needed. Add, delete, `reset` at will.
  - `http://127.0.0.1:8082` is the strict twin: `scripts/dev/test-server.sh start --strict`.
    Real auth: cookie login with `QBT_STRICT_USER`/`QBT_STRICT_PASS`, Bearer with
    `QBT_STRICT_API_KEY` from `dev.env`. Never `reset --strict`: the key lives in that profile.
- Credentials live in `~/.config/omaseed/dev.env` (0600, outside the repo). Load with
  `set -a; . ~/.config/omaseed/dev.env; set +a`. Never print the key, never commit it.
- No `sudo`. Everything needed is installed; if something is missing, stop and report it.
- Follow the Omarchy theme rules in the plan's §3 exactly: no hex colours in QML, only
  `Theme` roles; sizes from `base-size`; fills 4/8/18/22 %; hairlines at 12 %.

## Layout (created by phase 1; keep it)

```
CMakeLists.txt            top-level, Qt 6.8+, C++20, warnings as errors in CI
src/core/                 pure Qt, no QML: api client, sync model, servers, secrets, parsing
src/integration/          dbus, tray, notifications, theme reader, hooks, xdg helpers
src/ui/                   main.cpp, QML kit (qml/Kit/), views (qml/Views/), resources
tests/                    QtTest unit tests (core) + qmltestrunner tests (kit)
data/                     .desktop, icons, hyprland rule snippet, theme-set hook, menu row
packaging/arch/           PKGBUILD
scripts/dev/              test-server.sh and other developer scripts
docs/                     charter, specs, plan
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
scripts/dev/swarm.sh start|seed|status|break CASE|fix|reset|stop   # offline swarm: real downloads on :8081
scripts/dev/swarm-walk.sh [OUT] | --interactive   # every torrent state, shown in the app in an omabox
scripts/dev/strict-walk.sh [OUT] | --interactive  # first run + cookie / API key login against :8082, in an omabox
scripts/dev/notify-walk.sh [OUT] | --interactive  # downloads finish: one notice each, a click shows the torrent
scripts/dev/magnet-walk.sh [OUT] | --interactive  # xdg-open a magnet / .torrent: the add sheet, closed, in the tray, duplicate
```

Visual checks under Hyprland: run the app, find it with `hyprctl -j clients`, screenshot with
`grim -g "X,Y WxH" out.png`, and look at the image. Close what you open.
Theme switch test: `omarchy-theme-set <name>` changes the live theme; put it back afterwards
(`cat ~/.local/state/omarchy/current/theme.name` first). Never leave the desktop changed.

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
- The user (Diogo) is often away. Do not stop to ask about things the plan already decides.
  If genuinely blocked, write the question into `docs/QUESTIONS.md` and continue with the
  next independent task.
