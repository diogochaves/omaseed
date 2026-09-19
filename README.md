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
