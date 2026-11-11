# 88 — what Transmission and Deluge can do for an engine-neutral backup

Issue #88 (move a library between engines): what each engine's API gives an export (the
.torrent, the magnet, the fields of a C9 record) and takes at an import (an add from bytes,
paused, into a folder with the data; the check; renames before it; limits; the queue; a
duplicate).

```
scripts/dev/test-server.sh start --transmission
scripts/dev/research/88-engines-backup/transmission.py [BIG_MIB]
scripts/dev/test-server.sh start --deluge
scripts/dev/research/88-engines-backup/deluge.py [BIG_MIB] [--trap]
```

Both make their own private fixture torrent (`lib.py`: a top folder, three files, two tracker
tiers, a passkey in the announce URL) under `~/.cache/omaseed-research-88`, print what the
daemon answers and remove what they added. `deluge.py --trap` also shows the removal Deluge
refuses (below) and leaves that torrent for a restart of the playground to drop.

Run on 2026-11-11 against Transmission 4.1.3 (RPC 6.0.1) and Deluge 2.2.0 (libtorrent 2.0.14).

## Transmission

- **.torrent**: only `torrent_file`, a path in the daemon's config dir
  (`<config>/torrents/<hash>.torrent`); there is no `metainfo` field. The file is rewritten
  when the trackers change (`torrent_set tracker_list`), and keeps the original names after a
  rename. `magnet_link` carries `dn` and every tracker, passkey included, private or not.
- **The add** (`torrent_add`, `metainfo`) takes `paused`, `download_dir`, `labels`,
  `files_unwanted`, `priority_high|low`, `sequential_download`, `bandwidth_priority`; it
  ignores `download_limit`, `seed_ratio_limit`, `queue_position` without an error.
  `torrent_set` right after takes them all.
- **The check** runs by itself on a paused add (status 2, then 0), 0.45 s for 1 GiB here;
  the torrent stays paused. `done_date` becomes the time of that check.
- **Renames** (`torrent_rename_path`) answer success when the old name is absent and the new
  one is on disk; nothing is checked again until `torrent_verify`, then 100 %. A rename onto a
  name another file has also answers success: the torrent's file takes the name, nothing
  moves on disk. A name with a `/` is "Invalid argument".
- **Duplicate**: `torrent_duplicate` instead of `torrent_added`; its arguments are dropped.
- **A magnet without metadata**: `metadata_percent_complete` 0, `files` empty, `magnet_link`
  with its `dn` and trackers; `torrent_file` names a file that isn't there.
- `torrent_set_location` with `move: false` starts no check.

## Deluge

- **.torrent**: no method hands it over. It is `<config>/state/<id>.torrent`, the bytes as
  added: tracker edits (`core.set_torrent_trackers`) never reach it. `core.get_magnet_uri`
  carries `dn` and the current trackers, passkey included.
- **The add** (`core.add_torrent_file`) takes `add_paused`, `download_location`,
  `file_priorities`, `max_download_speed`, `max_upload_speed` (KiB/s), `stop_at_ratio`,
  `stop_ratio`, `remove_at_ratio`, `sequential_download`, `prioritize_first_last_pieces`,
  `move_completed(_path)`, `seed_mode`, `name` (the name only, not the folder). A `label` key
  is ignored. `mapped_files` can't be sent through `/json`: its keys arrive as strings and
  libtorrent refuses them.
- **The check** never runs by itself on a paused add (0 %, "Paused"); `core.force_recheck`
  does it (0.13 s for 259 MiB) and the torrent stays paused. `seed_mode: true` gives 100 %
  with no check at all.
- **Renames** (`core.rename_folder`, `core.rename_files`) with the old name absent and the new
  one on disk: accepted, and a recheck then finds 100 %. `orig_files` keeps the .torrent's
  names beside `files`.
- **Label**: `label.set_torrent` to an unknown label is "Unknown Label"; `label.add` takes
  only `[a-z0-9_-]`.
- **Duplicate**: `core.add_torrent_file` answers "Torrent already in session (<id>)";
  `core.add_torrent_files` (the batch) answers "type AddTorrentError not handled".
- **The trap**: a complete torrent with a skipped file (priority 0), then that file wanted
  again (`file_priorities`): `is_finished` goes false, `queue` stays -1, and
  `core.remove_torrent` fails ("is not in queued torrents set", then `false`) until the daemon
  restarts. Set priorities in the add, before the check.
- A torrent removed and added again within half a second came back broken once
  ("Torrent id not in torrents loading list" in deluged's log; `core.force_recheck` hung).
