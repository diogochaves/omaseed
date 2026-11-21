# 294-locate: re-pointing a torrent at a folder that already holds its files

DECISIONS C50 GT2 (`do locate`, #294): `EngineClient::pointAt` sets a torrent's folder over
files already there, moving nothing, and a recheck follows. Is each engine's call safe for
that: with the files at NEW and gone from OLD, does it re-point without copying, deleting or
failing, and where does the torrent stand before the recheck? With the files at both OLD and
NEW, does it leave both alone?

    scripts/dev/test-server.sh start                  # qBittorrent :8081
    scripts/dev/test-server.sh start --transmission   # :8085
    scripts/dev/test-server.sh start --deluge         # :8086 (DELUGE_TEST_PASS in dev.env)
    scripts/dev/research/294-locate/locate.py [qbt|transmission|deluge ...]

Each case adds a two-file torrent made on the spot, complete at OLD, and removes it (without
its data) at the end; every folder lives under `~/.local/share/omaseed-test/research-294/`
and is deleted. Exit 0 when every `verdict` is safe. `tests/core/tst_pointat_live.cpp` runs
the same flow through omaseed's engines (`ctest -R tst_pointat_live`).

The cases: **gone** (files moved by hand to NEW, the torrent started so the engine notices,
then stopped), **rechecked** (moved by hand, then a recheck: what a missing-files torrent
looks like), **error** (Deluge only: a recheck with OLD unreadable, Deluge's Error), **both**
(NEW a copy of OLD with one byte of `a.bin` changed, so an overwrite would show). "Untouched"
is each file's inode, mtime and sha256 the same before and after.

## What it printed (2026-11-21: qBittorrent 5.3.0, API 2.16.2; Transmission 4.1.3, RPC 6.0.1; Deluge 2.2.0)

    qbt gone: started with the files gone: stalledUP, progress 1      (libtorrent hadn't noticed)
    qbt gone: setLocation → before a recheck: stoppedUP, progress 1, save_path new; after: progress 1
    qbt rechecked: progress 0, stoppedDL → setLocation → stoppedDL, 0 → recheck → stoppedUP, 1
    qbt both: setLocation → stoppedUP, save_path new; old and new untouched; recheck → 0.944 (the changed piece)
    tr gone: started: error 3 "No data found! … use "Set Location"" → set_location move:false: {} in 0.0 s,
             error cleared, percent_done still 1.0 → verify → 1.0, status 6 (it seeds: the start was remembered)
    tr rechecked: error 3 "Paused torrent as no data was found!", 0.0 → move:false → error 0, 0.0 → verify → 1.0, stopped
    tr both: move:false → download_dir new, old and new untouched; verify → 0.944
    de gone: resumed: Seeding, no error (nothing read yet) → move_storage → Paused, progress 0.0 → recheck → 100, Paused
    de rechecked: Paused, 0.0 → move_storage → Seeding, 100 (libtorrent checked by itself) → recheck → 100, Seeding
    de error: Error "Permission denied" (or Checking, stuck) → move_storage taken, save_path new, nothing touched
              → recheck: Checking at 0 % for good (40 s and more)
    de both: move_storage → Paused, 94.4 (libtorrent checked by itself), old and new untouched
    verdict: safe on every case, all three engines

1. **All three re-point safely.** No file at NEW changed (same inode, so not even rewritten in
   place), nothing came back at OLD, and with both holding files neither side lost or changed
   one: libtorrent's move (qBittorrent's setLocation, Deluge's move_storage) skips a source
   that is gone and keeps a file already at the destination; Transmission's `move: false` only
   sets the folder. So every engine's `pointAt` is on; none stays refused (plan Q8 unused).
2. **The state before the recheck is not to be read.** qBittorrent keeps the old progress;
   Transmission keeps it too but clears its "No data found" error; Deluge's is anything
   (0 %, or 100 % and Seeding when libtorrent checked by itself because files were already at
   the destination). Only the recheck that follows says whether the files are whole: `do
   locate` always rechecks.
3. **qBittorrent's setLocation turns automatic management off** (as any move does); the cases
   here added torrents with it off already.
4. **Transmission's `move: false` answers at once** (0.0 s): no K22 wait, nothing busy.
   A torrent that had been started before it lost its files seeds after the verify.
5. **Deluge in a file Error re-points, but its recheck hangs.** A torrent whose check hit a
   file error (here OLD unreadable) takes move_storage (save_path changes, nothing touched),
   then sits in Checking at 0 % for good, with or without a re-point; resume and a second
   recheck don't free it (a pause shows Paused, the next recheck hangs again). A Deluge
   torrent whose move failed (K24's forced Error) is not this: there a recheck clears it.
   `do locate` (S8) should treat a Deluge torrent in a file Error with care: see the plan's
   assumed list.
6. **A recheck after a re-point over different bytes** leaves the torrent short (0.944 here):
   the changed piece is downloaded again over the file at NEW once it runs. That is the
   recheck's doing, not the re-point's; `do locate` compares sizes only (plan Q9).
