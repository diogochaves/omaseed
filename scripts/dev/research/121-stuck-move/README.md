# #121: a stuck move, and what makes qBittorrent try it again

**Question:** a finished torrent's move out of qBittorrent's incomplete folder failed because
the destination was full (#117). Once room is made, what makes qBittorrent move it? Board 44
(D13) aimed "Set location…" at the same destination.

**Answer (2026-10-22, qBittorrent 5.3.0):** a recheck. A `torrents/setLocation` to the save
path it already has does nothing (the save path never changed, so there is nothing to move);
a recheck, once it checks complete, moves the files to the save path. A move to another folder
works as any move does. #117's "a recheck changes nothing" was with the destination still full.

## The script

`retry.sh` stages the stuck move as `scripts/dev/stuck-walk.sh` does: a 24 MiB FAT image
through `udisksctl` (no root) holding a 14 MiB filler, a throwaway qBittorrent of its own
(`test-server.sh` with its own profile and port), and a 12 MiB torrent made on the spot whose
files are already in the incomplete folder. Added with that folder as its download path and
the image as its save path, it checks complete and the move fails. Then it frees the image (the
filler and the partial copies) and tries, printing the state, save path and content path after
each: `setLocation` to the same save path; a recheck; `setDownloadPath` to nothing; a
`setLocation` to another folder and back.

## What it printed

```
qBittorrent v5.3.0, destination /run/media/<user>/OMASEEDRSCH
stuck: state stalledUP, save /run/media/<user>/OMASEEDRSCH, content …/incomplete/Stuck Move Sampler
destination freed: 24M free
1. setLocation to the same save path: state stalledUP, save /run/media/<user>/OMASEEDRSCH, content …/incomplete/Stuck Move Sampler
2. recheck: state stalledUP, save /run/media/<user>/OMASEEDRSCH, content /run/media/<user>/OMASEEDRSCH/Stuck Move Sampler
3. setDownloadPath to nothing: (unchanged: already moved)
4a. setLocation to another folder: content …/elsewhere/Stuck Move Sampler
4b. and back to the destination: content /run/media/<user>/OMASEEDRSCH/Stuck Move Sampler
```

The failed move's line, as 5.3.0 writes it: the source is the incomplete folder, and
libtorrent's reason names a file in it, not the full destination:

```
Failed to move torrent. Torrent: "Stuck Move Sampler". Source: "…/incomplete". Destination:
"/run/media/<user>/OMASEEDFULL". Reason: "1dca4d storage move failed. file_truncate
(…/incomplete/Stuck Move Sampler/part-08.bin): No space left on device"
```
