# 87-deluge-moves: what Deluge does with a move through deluge-web

Board 47 §E/§F (DECISIONS C25 K21, K24), E6 step 11c (#87). What omaseed reads from Deluge
while it moves a torrent, when the move fails in words, and when it fails without one.

    scripts/dev/test-server.sh start --deluge
    python3 scripts/dev/research/87-deluge-moves/moves.py check

`stage`, `throttle on|off` and `clean` set the same torrents and folders up for a box (the
step's shots); see the script's head.

## What it printed (2026-11-06, Deluge 2.2.0, libtorrent 2.0.14.0, throttle 0.5 s of 0.6 s)

    1. into a folder it can't make: answer None; states seen ['Paused', 'Seeding']; save_path unchanged
    2. into a read-only folder: Error, “Failed to move download folder: Permission denied”, paused True
       resumed: Error, paused False
       rechecked: Queued, “OK”
    3. 16 files, 1 GiB, throttled: Moving 3 readings in 2 s, progress [6, 50, 94]; then Seeding, save_path /tmp/omaseed-87-moves/fast, move_completed False
    3. one file, 256 MiB, throttled: Moving 1 readings in 1 s, progress [100]; then Seeding, save_path /tmp/omaseed-87-moves/fast, move_completed False

1. A move Deluge can't make the folder for is answered like any other and never happens: the
   torrent never reads Moving and stays where it was. Only deluged's log says why ("could not
   create the directory"). omaseed reads it as failed two polls and 3 s after the answer.
2. A move that fails while copying is Deluge's forced Error, paused. A resume clears `paused`,
   not the error; a recheck clears it. omaseed offers recheck first.
3. Deluge's % while Moving is the size of the torrent's files at the target over its bytes done.
   libtorrent makes each file full length there before it copies it (as #162 found for
   qBittorrent), so the % rises file by file, and a one-file torrent reads 100 % from the start.
   The band keeps #162's rule: a first reading already at full size says how long it has been
   moving instead. A move turns the torrent's move-when-done (`move_completed`) off.
