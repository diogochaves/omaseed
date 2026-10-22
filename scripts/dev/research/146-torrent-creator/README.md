# 146: qBittorrent's torrent creator through the Web API

**Question.** What do `torrentcreator/addTask`, `status`, `torrentFile` and `deleteTask` really
do, for board 49 (create a torrent from a folder, #146)? Which defaults, which piece sizes, which
words when it fails, what a cancel leaves, and does "the site's copy" of a private torrent seed
from the original folder without downloading, with the same info hash and with a different one?

**Run.** On the playground (or any auth-bypassed throwaway qBittorrent), under its lock:

```
flock -o /tmp/claude-$UID/omaseed-playground.lock \
  python3 -I scripts/dev/research/146-torrent-creator/creator.py [WORKDIR]
```

It makes folders under `WORKDIR` (default `/tmp/omaseed-146`, sparse files for the big ones),
deletes every task it made and every torrent it added (never their files), then `WORKDIR`. The
announce URL carries a made-up passkey, printed masked.

## What it printed (2026-10-22, qBittorrent 5.3.0, Web API 2.16.2, on :8081)

- **Defaults** with only `sourcePath`: `format` hybrid, `pieceSize` 0 (automatic),
  `ignoreDotfiles` true, `private` false, and **`startSeeding` true**: the torrent is added to
  the session, seeding from where the files are (`save_path` the folder's parent, progress 1, no
  check). With `startSeeding=false` nothing is added.
- **Private v1 with `source`:** the info dict holds `private` 1 and `source`; the top level
  holds `announce` (the URL as given, passkey included), `comment`, `created by`
  "qBittorrent v5.3.0", `creation date`. No `announce-list` with one tracker. The task's
  `status` reply echoes `trackers` in full, **passkey included**: omaseed must never log it.
- **Trackers:** `|` between URLs puts them in one tier; an empty entry (`||`) starts a new tier.
  Newlines are not separators: the field came out empty. `urlSeeds` the same way, into `url-list`.
- **Ids:** a hybrid or v2 torrent is in the session under its truncated v2 hash, not its v1.
- **Automatic piece size** (v1 and hybrid alike): 5.5 MB → 32 KiB, 1 GiB → 512 KiB,
  4 GiB → 1 MiB, 16 GiB → 2 MiB. While a task runs its `pieceSize` reads 0; the size is known
  only when it finishes. `progress` is a whole percent.
- **Speed** (sparse files on tmpfs, so this is the hashing alone): 16 GiB in 6.8 s as v1,
  13.5–14.3 s as hybrid (hybrid hashes twice). A spinning disk reads far slower: a band is needed.
- **Failures** (`status` "Failed", `errorMessage`):
  - an unreadable folder, a missing folder and an empty one all say *"Create new torrent file
    failed. Reason: All files have been filtered out by 'Ignore dotfiles' option."*: the words are
    wrong for two of the three, so omaseed must look first (`app/getDirectoryContent`) and word it
    itself;
  - an unreadable file inside: *"… Reason: Permission denied [system:13]."*;
  - the same torrent already in the session with `startSeeding` true: *"Failed to start
    seeding."*, and `torrentFile` then answers **409 "Torrent creation failed."**: the .torrent is
    lost although it was made. Creating with `startSeeding=false` and adding it afterwards
    (`torrents/add` with `seedMode=true`, which came back `stalledUP` progress 1) keeps the file
    whatever the add does.
- **Cancel:** `deleteTask` on a running task answers 200 and the task is gone (`status` 404).
  `torrentFile` on a running task: 409 "Torrent creation is still unfinished."
- **`torrentFilePath`:** the .torrent is also written there on the server; `torrentFile` still
  returns it.
- **The site's copy, added back** at the folder's parent: with only the announce changed, the
  info hash is the same (`c28cd967…` both); with `source` changed inside the info, it differs
  (`da2f6cec…`). Both checked to progress 1 with nothing downloaded: the files on disk are the
  files, whatever the hash. qBittorrent accepts both at once over the same folder (two torrents,
  one set of files), so omaseed must stop that itself.

Not tested: a task across a qBittorrent restart; several tasks at once (qBittorrent queues them).
