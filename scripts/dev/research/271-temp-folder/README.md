# 271 — The temp-folder facts behind board 64 (DECISIONS C47)

Board 64's free line follows a torrent through the server's temp folder (C47 I8–I12). Before it
is built, #271 asked each engine what it really does: qBittorrent's per-add `useDownloadPath`
and `downloadPath`, a category's own incomplete folder, which drive `free_space_on_disk` speaks
for, Deluge's per-torrent `download_location` with `move_completed`, Transmission's
`incomplete-dir`, and the name of qBittorrent's pre-allocation preference (I10).

## Run it

```
scripts/dev/test-server.sh start                 # qBittorrent playground, :8081
scripts/dev/test-server.sh start --transmission  # :8085
scripts/dev/test-server.sh start --deluge        # :8086 (DELUGE_TEST_PASS in dev.env)
scripts/dev/research/271-temp-folder/check.py [qbt|deluge|transmission ...]
```

`check.py` reads every preference it changes first and puts it back at the end (it prints
`prefs restored: true`), removes the torrents it added with their files and the categories it
made, and keeps its folders under `~/.local/share/omaseed-test/research-271/` plus
`/tmp/omaseed-research-271/` (a second filesystem, a tmpfs, for the free-space question). Each
torrent is a one-file 64 KiB payload made with `transmission-create`, added stopped. It uses
`../153-engines-board47/rpc.py` for Transmission's and deluge-web's wires.

## What it printed (2026-11-19)

qBittorrent v5.3.0 (Web API 2.16.2), Deluge 2.2.0 (through deluge-web), Transmission 4.1.3
(RPC 6.0.1, JSON-RPC 2.0, snake_case names).

| # | Asked | Answer |
|---|---|---|
| 1 | qBittorrent `torrents/add`, session temp folder **on**, no download-path field | `download_path` = the session's `temp_path` |
| 1 | `useDownloadPath=true&downloadPath=X` | `download_path` = X |
| 1 | `useDownloadPath=false` | `download_path` = `""`: straight to `save_path` |
| 1 | `downloadPath=X` alone | `download_path` = X (the field implies the switch) |
| 1 | session temp folder **off**, `useDownloadPath=true&downloadPath=X` | `download_path` = X: a per-torrent temp folder works without the session's |
| 1 | session temp folder off, no field | `download_path` = `""` |
| 2 | `torrents/createCategory` with `downloadPathEnabled=true&downloadPath=X` | `torrents/categories` → `"download_path": "X"` |
| 2 | `downloadPathEnabled=false` | `"download_path": false` ("no") |
| 2 | neither field | `"download_path": null` ("default": the session's rule) |
| 2 | add into the X category, `autoTMM=true` | `download_path` = X, `save_path` = the category's |
| 2 | add into the "no" category, `autoTMM=true` | `download_path` = `""` even with the session's temp folder on |
| 2 | add into the "default" category, `autoTMM=true` | `download_path` = `temp_path/<category name>` (as `save_path` gets a subfolder) |
| 2 | any category, `autoTMM=false` | the category's folders are ignored: `download_path` = `temp_path`, `save_path` = the add's |
| 3 | `sync/maindata` `server_state.free_space_on_disk`, save path on home, temp folder on /tmp | the save path's filesystem (1.36 TB, /tmp had 31 GB) |
| 3 | the other way round | again the save path's (31 GB): never the temp folder's |
| 4 | Deluge `core.add_torrent_file` with `{download_location: T, move_completed: true, move_completed_path: F}` | all three honoured per torrent (`core.get_torrent_status` echoes them; `save_path` = T) |
| 4 | `{download_location: S, move_completed: false}` while the session's `move_completed` is on | honoured: `move_completed` false for this torrent, downloads to S |
| 4 | no options | the session's three values |
| 4 | `core.get_config_values(["download_location", "move_completed", "move_completed_path"])` | all three returned (playground: `move_completed` false) |
| 5 | Transmission `session_get` | `incomplete_dir` (a path) and `incomplete_dir_enabled` (bool) |
| 5 | `torrent_add` with an extra `incomplete_dir` | accepted without an error and **ignored**: the torrent has only `download_dir`, and `torrent_get` returns no `incomplete_dir` field |
| 6 | qBittorrent `app/preferences` | the key is `preallocate_all` (bool; false on the playground) |

So Deluge's "temp folder" is the other way round from qBittorrent's: the download goes to
`download_location`, and `move_completed_path` is where it ends. A torrent that downloads
straight to its place is `download_location` = that place with `move_completed` false.

## The exact requests

qBittorrent (form fields, `torrents` a multipart file):

```
POST /api/v2/app/setPreferences  json={"temp_path_enabled": true, "temp_path": "<root>/temp"}
POST /api/v2/torrents/add        torrents=@q.torrent stopped=true savepath=<root>/save [useDownloadPath=true|false] [downloadPath=X]
GET  /api/v2/torrents/info?hashes=<hash>                    -> download_path, save_path
POST /api/v2/torrents/createCategory  category=N savePath=P [downloadPathEnabled=true|false] [downloadPath=X]
GET  /api/v2/torrents/categories                            -> {N: {"download_path": "X" | false | null, ...}}
POST /api/v2/torrents/add        ... category=N autoTMM=true|false
POST /api/v2/app/setPreferences  json={"save_path": A, "temp_path": B, "temp_path_enabled": true}
GET  /api/v2/sync/maindata?rid=0                            -> server_state.free_space_on_disk (vs statvfs of A and B)
```

The free-space figure comes from qBittorrent's own periodic check, so the script polls
`sync/maindata` until it matches one of the two filesystems (it took one poll each time).

Deluge (deluge-web `/json`, after `auth.login` and `web.connect`):

```
core.get_config_values [["download_location", "move_completed", "move_completed_path", "pre_allocate_storage"]]
core.set_config        [{"move_completed": true, "move_completed_path": "<root>/dl-done"}]
core.add_torrent_file  ["q.torrent", <base64>, {"add_paused": true, "download_location": T, "move_completed": true, "move_completed_path": F}]
core.get_torrent_status [<hash>, ["download_location", "move_completed", "move_completed_path", "save_path", "move_on_completed", "move_on_completed_path"]]
```

Transmission (`/transmission/rpc`, JSON-RPC 2.0):

```
session_get {}                                       -> incomplete_dir, incomplete_dir_enabled
torrent_add {"metainfo": <base64>, "paused": true, "download_dir": D, "incomplete_dir": X}
torrent_get {"ids": [<hash>], "fields": ["download_dir", "incomplete_dir", "hash_string"]}
```
