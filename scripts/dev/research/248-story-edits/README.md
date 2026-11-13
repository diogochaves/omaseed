# 248-story-edits: what the engines show when a torrent's place changes

Board 58 (DECISIONS C43 Y6, Y7, #248). The story tells a torrent's place changed outside omaseed
("Moved to …", "Will land in …") from what a poll sees, so two things had to be known first:
what a Deluge label with a folder does to a torrent, and when qBittorrent's `save_path` changes
while it moves one.

    scripts/dev/test-server.sh start --deluge
    scripts/dev/test-server.sh start --transmission     # the seeding peer
    python3 scripts/dev/research/248-story-edits/deluge-label-folder.py check

    scripts/dev/app-box.sh up --qbt -b r248             # a box with its own qBittorrent
    scripts/dev/research/248-story-edits/qbt-save-path.sh -b r248 [--throttle]
    scripts/dev/app-box.sh down -b r248

## Deluge: a label with a folder (2026-11-13, Deluge 2.2.0, libtorrent 2.0.14.0, Transmission 4.1.3)

    1. label r248-far: move_completed_path /tmp/omaseed-248/far
       seeding before: save_path /tmp/omaseed-248/seeding  move_completed False  move_completed_path ~/Downloads  state Seeding
       unfinished before: save_path /tmp/omaseed-248/downloads  move_completed False  move_completed_path ~/Downloads  state Paused
       0 s seeding: save_path /tmp/omaseed-248/seeding  move_completed True  move_completed_path /tmp/omaseed-248/far  state Seeding
       0 s unfinished: save_path /tmp/omaseed-248/downloads  move_completed True  move_completed_path /tmp/omaseed-248/far  state Paused
       1 s seeding: save_path /tmp/omaseed-248/seeding  move_completed True  move_completed_path /tmp/omaseed-248/far  state Seeding
       1 s unfinished: save_path /tmp/omaseed-248/downloads  move_completed True  move_completed_path /tmp/omaseed-248/far  state Paused
       3 s seeding: save_path /tmp/omaseed-248/seeding  move_completed True  move_completed_path /tmp/omaseed-248/far  state Seeding
       3 s unfinished: save_path /tmp/omaseed-248/downloads  move_completed True  move_completed_path /tmp/omaseed-248/far  state Paused

    2. the unfinished one resumed, Transmission its peer
         0.0 s  state Downloading  progress 0.0  is_finished False  save_path /tmp/omaseed-248/downloads
        10.7 s  state Downloading  progress 6.1  is_finished False  save_path /tmp/omaseed-248/downloads
        ... (a line every 0.5 s, 6 % each)
        18.2 s  state Downloading  progress 97.6  is_finished False  save_path /tmp/omaseed-248/downloads
        18.6 s  state Moving  progress 100.0  is_finished False  save_path /tmp/omaseed-248/downloads
        18.8 s  state Seeding  progress 100.0  is_finished True  save_path /tmp/omaseed-248/far
       finished readings with save_path still the old folder: 0
       after: save_path /tmp/omaseed-248/far  move_completed False  move_completed_path /tmp/omaseed-248/far  state Seeding

    3. the label off, then removed
       seeding, label off: label ''  save_path /tmp/omaseed-248/seeding  move_completed False  move_completed_path ~/Downloads  state Seeding
       label.remove, one web.update_ui reading: the unfinished one's label ''; r248-far in the filter tree False
       its options after: save_path /tmp/omaseed-248/far  move_completed False  move_completed_path /tmp/omaseed-248/far  state Seeding

A second run, the same but for the timing, read no `Moving` (its 0.2 s readings went from
Downloading at 97 % straight to Seeding in `far`).

1. The label copies its folder into the torrent's options at once (`move_completed` on,
   `move_completed_path` the label's exact path, no label subfolder), on a finished torrent too.
   Only a download that finishes moves: the finished one labelled later stays where it is.
2. While it finishes, `save_path` is the old folder up to and through `Moving` with
   `is_finished` still false; the first reading with `is_finished` true already has the new
   folder. Deluge then turns `move_completed` off.
3. Taking the label off puts `move_completed` and its path back to the daemon's defaults.
   `label.remove` takes the label off its torrents and out of the filter tree in the same
   reading; it leaves the torrent's options as they were.

## qBittorrent: save_path during a move (2026-11-13, qBittorrent v5.3.0, WebUI API 2.16.2)

A 1 GiB file, the box's `~/Torrents` (btrfs) and `/tmp` (tmpfs): each move is a copy.

    == (no throttle)
    1. setLocation /tmp/r248
       before: save_path /home/sbx/Torrents/r248  download_path ""  state stalledUP  auto_tmm false
          0.0s  save_path /home/sbx/Torrents/r248  download_path ""  state moving  auto_tmm false
          0.2s  save_path /tmp/r248  download_path ""  state stalledUP  auto_tmm false

    2. category r248cat (savePath /home/sbx/Torrents/r248cat), setCategory, then setAutoManagement on
       after setCategory: save_path /tmp/r248  download_path ""  state stalledUP  auto_tmm false
          0.0s  save_path /home/sbx/Torrents/r248cat  download_path ""  state moving  auto_tmm true
          0.2s  save_path /home/sbx/Torrents/r248cat  download_path ""  state stalledUP  auto_tmm true

    3. setLocation /tmp/r248loc on the auto-managed torrent
          0.0s  save_path /home/sbx/Torrents/r248cat  download_path ""  state moving  auto_tmm false
          0.2s  save_path /tmp/r248loc  download_path ""  state stalledUP  auto_tmm false
    == --throttle
    1. setLocation /tmp/r248
       before: save_path /home/sbx/Torrents/r248  download_path ""  state stalledUP  auto_tmm false
          0.0s  save_path /home/sbx/Torrents/r248  download_path ""  state moving  auto_tmm false
          6.5s  save_path /tmp/r248  download_path ""  state stalledUP  auto_tmm false

    2. category r248cat (savePath /home/sbx/Torrents/r248cat), setCategory, then setAutoManagement on
       after setCategory: save_path /tmp/r248  download_path ""  state stalledUP  auto_tmm false
          0.0s  save_path /home/sbx/Torrents/r248cat  download_path ""  state moving  auto_tmm true
          7.3s  save_path /home/sbx/Torrents/r248cat  download_path ""  state stalledUP  auto_tmm true

    3. setLocation /tmp/r248loc on the auto-managed torrent
          0.0s  save_path /home/sbx/Torrents/r248cat  download_path ""  state moving  auto_tmm false
          7.3s  save_path /tmp/r248loc  download_path ""  state stalledUP  auto_tmm false

1. A set location (`torrents/setLocation`) leaves `save_path` on the old folder while it moves
   and changes it when the move has landed, with the state leaving `moving` in the same reading.
2. Automatic management's move is the other way round: `save_path` is the category's folder
   from the first reading, while the state is still `moving`.
3. A set location on an auto-managed torrent turns `auto_tmm` off at once, before the move ends.
   A move within one filesystem (a rename) is over before the next 0.1 s reading either way.

## What it means for #248

- **Y7 (Deluge), as the plan expects.** The label's folder is its exact path, copied at once;
  labelling a finished torrent never moves it. Through `DelugeTranslate` (an unfinished torrent
  with `move_completed` on has the label's folder as its save path, where it waits as its
  download path) labelling the unfinished torrent reads as save path → the label's folder with a
  download path: **Will land in**, by its label. Labelling a finished one changes no save path:
  no place line, as the plan has it.
- **No transit reading (design 9).** Deluge's first finished reading already has the new
  `save_path`, and omaseed's save path for the torrent was that folder all along, so a finish
  out of the waiting folder changes no save path. Keep the transit rule (harmless) with its
  synthetic test, as the plan says for this case.
- **Taking a Deluge label off** (seen here on a finished torrent only; for an unfinished one
  this follows from 3, not run) while unfinished makes the save path the waiting folder again
  (`move_completed` off): a place change by the server with no label. Step D: that is
  Elsewhere by the rules as written (no label to name); a "by its label" cause can't apply.
  Worth a look in D, not a change here.
- **qBittorrent, a set location: the path changes at the end, not the start.** The plan expected
  the start; the echo is unchanged (the running job or the 60 s record covers it), but step D
  must make sure `ownsMoveOf` is still true in the poll where the path lands, when the move job
  may be finishing in that same poll: the history record (finishedAt ≤ 60 s) or the running job
  must be in place before `torrentsEdited` is classified, whichever order the two are handled in.
  An outside set location then reads "Moved to …" only once it has landed, which is right.
- **qBittorrent, automatic management: the path changes at the start** (while `moving`), with
  `auto_tmm` true: Server by category, as design 10 says; the line appears when the move
  begins (accepted, Y6).
- **WebUI setLocation turns auto-TMM off**, before the path changes: design 10's rule
  (`after.autoTmm` → Server) holds; no `autoManagedFolder` comparison needed.
