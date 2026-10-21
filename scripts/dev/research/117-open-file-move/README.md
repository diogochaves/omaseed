# #117: a finished file moved out of the incomplete folder while it is open

**Question:** with qBittorrent's "keep incomplete torrents in" folder on another disk, a person
opens a file (a video) before it finishes; it finishes and qBittorrent moves it. Does the move
fail, as it would on Windows?

**Answer (2026-10-21, qBittorrent 5.2.4):** no. Linux doesn't lock open files: a move across
disks is a copy and an unlink, and the reader keeps reading the old (now deleted) copy, whose
space comes back when it closes. On one disk it's a rename and the open file follows. The real
risk is a full destination: the move fails with "No space left on device" naming the *source*,
partial copies stay in the destination, and the torrent stays `stalledUP`, seeding from the
incomplete folder, with no error state. Board 44 dropped suggesting an incomplete folder (D10)
and added a notice for that half-move (D13, DECISIONS C17). Details on #117.

## The scripts

- `hold-open.py PATH SECONDS`: waits for PATH to exist, opens it and holds the descriptor,
  printing its link count, size, allocation and `/proc/self/fd` target whenever they change;
  at the end it reads the whole file through the held descriptor and prints its sha1.
- `watch-torrent.py API HASH SECONDS`: prints a torrent's state, `save_path`, `download_path`,
  `content_path` and progress whenever they change, every 0.25 s.

How they were used: a throwaway qBittorrent (`test-server.sh` with its own profile and port, so
the incomplete folder could be turned on without touching the playground), a swarm fixture added
with `useDownloadPath=true`, `downloadPath` on btrfs and `savepath` on another filesystem,
`hold-open.py` started on the file while it downloaded (`swarm.sh rate` or a per-torrent limit
slows it), `watch-torrent.py` beside it. Then the same with the destination a small full loop
image (udisks) for the half-move.

## What it printed

A 503 MB file held open from 2 %, btrfs to `ntfs3`: the move took 0.25 s in `moving`; the
reader's descriptor turned into `… (deleted)`, link count 0; its sha1 matched the moved copy;
the incomplete disk's 503 MB stayed in use until the reader closed. A 38.9 MB torrent into a
disk with 22 MB free: 699 partial files left in the destination, the torrent `stalledUP` at 100 %.
