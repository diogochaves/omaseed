# Testing omaseed

How omaseed is checked, in the order it is checked, and the rules every session and contributor
follows. The commands themselves are listed in `CLAUDE.md`; `README.md` says how to build.

## The layers

1. **Unit and QML tests** (`ctest --test-dir build -LE live`). QtTest for every `core` class and
   most of `integration` and `App`; `tst_<Name>.qml` (qmltestrunner) for each Kit component.
   They run offscreen and need nothing else. CI runs them on every push, with warnings as errors.
2. **Live tests** (`ctest -L live`). Against two throwaway qBittorrents from
   `scripts/dev/test-server.sh`: the playground on `:8081` (no login from localhost) and the
   strict twin on `:8082` (real logins and an API key, kept in `~/.config/omaseed/dev.env`,
   outside the repo). Without `dev.env` loaded the strict tests skip and say so.
3. **Walks** (`scripts/dev/*-walk.sh`). The app driven through its own UI in an
   [omabox](https://github.com/diogochaves/omabox), an invisible desktop, checking what
   qBittorrent says and what is on disk after each step, with a shot per step and an `index.md`
   in the output folder. `--interactive` shows the box as a window and waits for enter at each
   step. Most use the offline swarm (`scripts/dev/swarm.sh`): real downloads that never leave the
   machine.

   | Walk | What it covers |
   |---|---|
   | `swarm-walk.sh` | every torrent state the swarm can produce, in the table and details, then in the rows display; it ends on space |
   | `strict-walk.sh` | first run against `:8082`: cookie login and an API key |
   | `notify-walk.sh` | downloads finishing: one notice each, a click shows the torrent |
   | `magnet-walk.sh` | a magnet or `.torrent` opened with `xdg-open`: closed, hidden, running; with `gio open` (D-Bus activation) while quit; a magnet added before its file list is in (#193) |
   | `create-walk.sh` | create a torrent from a folder (#146): "+ add ▾", public (the strip, seeding from where it is, the magnet copied, the `.torrent` read back with `scripts/dev/torrent-file.py`), the band while it hashes, private (saved in Downloads, the passkey only as dots, in no text of the window or line of the log), the site's copy back both ways, an unreadable and a missing folder, a cancel |
   | `actions-walk.sh` | renames, categories, tags, automatic management, the Move… sheet, delete, skipped files that hold space (free the space, add it again) |
   | `settings-walk.sh` | the qBittorrent settings pane, against a qBittorrent in the box |
   | `servers-walk.sh` | two qBittorrents: first run, add, switch, an action on each, remove |
   | `backup-walk.sh` | a qBittorrent's torrents exported and imported through the app |
   | `error-walk.sh` | a torrent going into error, and the app saying why |
   | `menu-walk.sh` | the one menu shown by the tray and the bar widget |
   | `stuck-walk.sh` | a finished torrent whose move into a full disk failed (a loop image), and a recheck once room is made |
   | `move-walk.sh` | moving folders and selections: queue, places, partial packs, move back, recheck |
   | `search-walk.sh` | search through invented plugins (`scripts/dev/search-plugins.py`, whose rows are the offline swarm's): install from Settings, a failed install, a plugin off and on; a search filling in, a failed and a slow plugin and their answers; a folded magnet and a link (through its plugin) added and arriving; hide dead, `/`, uninstall |

   A box is a new install, so omaseed starts on the rows display there (DECISIONS C24 H4). Each
   walk pins the display it reads with `OMASEED_DISPLAY` (`table`, `rows` or `space`, which also reads the shelves' old `overview`: every
   scope starts on it); all but the swarm walk's rows pass read the table.

   `OMASEED_ACTIONS_WALK_FROM=disk` runs only the actions walk's board 44 steps. Their ntfs3 side
   (a tick that allocates in full) needs a loop image udisks mounts on this machine, which a box
   can't write to: `ctest -R tst_skipped_ntfs3_live` runs it against the playground, and
   `ctest -R tst_app_tickroom_live` a tick that doesn't fit on a FAT image (it waits, C28).

   `move-walk.sh` takes about 12 minutes; `OMASEED_MOVE_WALK_FROM` set to `selection`, `sheet`,
   `history`, `big`, `music` or `remote` starts it later. Its `big` step reads a sheet's line
   with `tesseract` where the accessibility tree cuts it at 80 characters (without it, only the
   line's start is checked).

4. **Visual check** (`omabox check run … -- scripts/dev/visual-check.sh`). The screens the walks
   reach (library, details, menus, palette, help, the add sheet, the rows display and space,
   every Settings page, the bar widget, and last a torrent that needs you, the pieces, the
   trackers, picked rows, a refused action's strip and two servers) compared with a baseline:
   "same", or which region changed and which elements moved.
   The baseline is per machine and GPU, in `.local/visual` (not committed): make it once with
   `--approve` (three runs, about 8 minutes); a check takes about 3 minutes. Each skin has its
   own pass and baseline: `OMASEED_SKIN=console … --keep fonts --base .local/visual-console`
   (DECISIONS C31).
5. **A person's pass** (`docs/manual-tests/*.md`). What tests can't judge: whether it reads well,
   looks right and feels right. Each plan names its setup, its steps and what each should show.
   `scripts/dev/manual-box.sh` makes a throwaway desktop with omaseed built from the checkout and
   two qBittorrents of its own; a plan's own script fills it (`scripts/dev/move-library.sh` for
   [moves](manual-tests/moves.md)).

## The order

Automated first, then a person. Before asking anyone for a manual pass of a feature, its tests,
its walk and the visual check pass on the same commit. A person's pass is for wording, look and
feel, and for what a walk can't reach; anything it finds that a walk could check becomes a walk
step, so it is checked every time after.

## The rules

- **Never a real library.** Tests, walks and scripts talk to the playground, the strict twin, a
  box's own qBittorrent, or the swarm. A script that takes a Web API URL refuses `:8080`.
- **Take turns on the playground.** Several agents on one machine share `:8081` and the swarm:
  wrap every live ctest and every walk in `flock -o LOCK …`, LOCK being one file they all name
  (`/tmp/claude-$UID/omaseed-playground.lock`). `-o`, so nothing the command starts keeps the
  lock. Start, stop and restart the playground with `test-server.sh` outside the lock. The
  servers the scripts start themselves (`scripts/dev/detach.sh`: qBittorrent, the swarm's
  tracker and leecher) take none of their caller's descriptors, so a walk that restarts the
  playground (`error-walk.sh`) doesn't hand the lock to it either (#141).
- **Anything with a window runs in a box.** Apps, test binaries outside `ctest`, the tray, the
  bar, notifications, the keyring: an omabox, never the desktop you are using.
- **Test scripts are committed and re-runnable.** A script that builds test state (a library,
  destinations, servers) or drives a test lives in `scripts/dev/`, is shellcheck-clean
  (`scripts/dev/shellcheck.sh`), is listed in `CLAUDE.md`, and can run again on the same box. A
  manual plan lives in `docs/manual-tests/`. Nothing a test needs lives only in someone's
  temporary folder.
- **Research is kept.** A script that answered a question about qBittorrent, libtorrent or a
  filesystem goes in `scripts/dev/research/<issue>-<topic>/` with a README: the question, the
  commands, what they printed. See [the index](../scripts/dev/research/README.md).
- **Public repo.** No real paths, server names or torrent names from anyone's library in tests,
  plans, shots or issues; boxes use `/home/sbx` and `/tmp` paths.

## Adding a walk

Start from the closest existing walk: its helpers (`api`, `keys`, `press`, `expect_text`,
`expect_count`, `shot`, `step`) are copied, not shared. Seed state through the API or files in
the box, drive the app through its own keys and its accessibility tree (`omabox tree`), and
check the server and the disk after each step, not only the screen. Wait on conditions, never on
fixed sleeps alone: a busy machine is slower. Add an `OMASEED_<NAME>_WALK_FROM` start point when
the walk is long. List it in `CLAUDE.md` and in the table above.

## Adding a manual plan

`docs/manual-tests/<feature>.md`: what the walk already covers (so the person doesn't repeat
it), the setup as commands (a script in `scripts/dev/` when it needs data), and numbered steps,
each "do this, then you should see that". Use only box paths. When a step turns out to be
checkable, move it into the walk and keep the plan for what remains.
