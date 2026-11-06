# 87 — Deluge's metadata prefetch before an add, and its diff through deluge-web

**Questions (#87, board 47 §3, DECISIONS C25 K26; the engine's `fetchMetadata` and its poll).**
153's K26 check showed that `core.prefetch_magnet_metadata` works through deluge-web and holds
nobody else's answers. Before the engine relies on it:

- Does it give a magnet's **files** before the add (a multi-file torrent, paths and sizes), in
  what shape, and how long does it take?
- What does it answer on failure: a magnet nobody has, a v2-only magnet, a link?
- Does asking again carry on where a call that timed out stopped, and is an answer already had
  quick the second time? (That decides how a lookup that is asked again and again maps onto
  one blocking call.)
- Is `core.get_torrents_status(…, diff=True)` safe through deluge-web, or must the poll use the
  whole list (`web.update_ui`) and omaseed's own differ?

**How.** Against the engines' test servers, `scripts/dev/test-server.sh start --deluge` (:8086)
and `start --transmission` (:8085, the seeder). The script runs its own tracker
(`scripts/dev/swarm-tracker.py` on 127.0.0.1:26989; DHT is off everywhere), makes three files of
random data, and removes everything it added.

```sh
cd scripts/dev/research/87-deluge-prefetch
python3 prefetch_files.py            # --timeout S for the long calls (default 30)
```

**What it printed** (Deluge 2.2.0, libtorrent 2.0.14.0; Transmission 4.1.3; 2026-11-06):

```
Deluge 2.2.0 (libtorrent 2.0.14.0) through deluge-web; seeder: Transmission on :8085
1. short call (timeout 2 s): 2.0 s → ['bffc9705971aa7f27899e1bd08b6491769fc873d', '']
1. asked again (timeout 30 s): 4.9 s → ['bffc9705971aa7f27899e1bd08b6491769fc873d', 'ZDU6ZmlsZXNsZDY6…']
   1010 bytes: top-level keys ['files', 'name', 'piece length', 'pieces']; name 'prefetch-pack'; piece length 65536; private 0; files [('extras/notes.bin', 4096), ('part-1.bin', 1048576), ('part-2.bin', 1572864)]
1. once more (timeout 30 s): 5.0 s → ['bffc9705971aa7f27899e1bd08b6491769fc873d', 'ZDU6ZmlsZXNsZDY6…']
   1010 bytes: top-level keys ['files', 'name', 'piece length', 'pieces']; name 'prefetch-pack'; piece length 65536; private 0; files [('extras/notes.bin', 4096), ('part-1.bin', 1048576), ('part-2.bin', 1572864)]
   the daemon's torrents now hold it: False
2. a magnet nobody has (timeout 5 s): 5.0 s → ['0123456789abcdef0123456789abcdef01234567', '']
2. a v2-only magnet: 0.0 s → error core.prefetch_magnet_metadata: {'message': "Failure: [… KeyError: 'info_hash' …]", 'code': 4}
2. a link that isn't a magnet: 0.0 s → error core.prefetch_magnet_metadata: {'message': "Failure: [… KeyError: 'info_hash' …]", 'code': 4}
3. diff=True: first call 3 torrents, the same session again 3; after a pause, session B sees {'f091…': {'state': 'Paused'}, '4739…': {}, 'b558…': {}}, then session A sees {'f091…': {}, '4739…': {}, 'b558…': {}}
```

(The two errors' Python tracebacks and the hashes in 3 are cut here.)

**What it means.**

- **Files before the add: yes.** The answer is `[info hash, base64 of the bencoded info
  dictionary]`: the info dictionary itself (`name`, `files` with `path` lists and `length`,
  `piece length`, `pieces`, `private` when set), not a whole .torrent. Wrapped as
  `d4:info…e` it reads as any .torrent's info; trackers are the magnet's own `tr=`. 5 s with
  one seeder found through a tracker. Nothing is added to the daemon's torrents. So
  `filesBeforeAdd` (EngineFeatures::fetchMetadata) is **on** for Deluge.
- **Every call starts over.** A call that timed out leaves nothing behind: the next one took as
  long as a first one (4.9 s after a 2 s call gave up), and an answer already had is not kept
  (5.0 s again). So a short call asked again and again would never finish on a swarm slower than
  its timeout. The engine makes **one long call (30 s, Deluge's own default) at a time per
  magnet**, answers each of omaseed's quick polls "still looking" while it runs, keeps the
  metadata it brings, and starts a new call when one comes back empty (C5's "still looking" and
  "may never come" waits come from the lookup's clock, as on qBittorrent).
- **Failure:** a magnet nobody has answers `[hash, ""]` after its timeout. A magnet without a v1
  `btih` hash, and anything that isn't a magnet (a link to a .torrent), are refused at once
  with error code 4 (a Python `KeyError: 'info_hash'` traceback): Deluge prefetches magnets
  only, v1 only (K25). The engine refuses a link before sending (not supported: the add sheet
  shows today's sheet for it) and reads the error as a refusal of the source.
- **diff=True is not safe through deluge-web**, as the engine matrix §1.2 read from the source:
  deluge-web holds one daemon session for all its web sessions, so the daemon's diff cache is
  shared. Session B's call took the pause, and session A's next diff never saw it. The poll uses
  `web.update_ui` (the whole list with the filter tree and the stats, one call) through
  SnapshotDiffer.
