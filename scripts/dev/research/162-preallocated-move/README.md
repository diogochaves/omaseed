# 162 — How big is a torrent's file at its new place while qBittorrent moves it?

**Question (#162).** omaseed measured a single torrent's move by the size of its files at the
destination (`QbtMoveFeed::bytesAt`). On an ntfs3 drive the band read 100 % from the first
second of an 18 GB move. Does qBittorrent make the destination file at full length before it
copies, and on which filesystems?

**How.** `watch-move.sh` in a box with its own qBittorrent: a single-file torrent added as
complete in the box's home (btrfs here), moved to `/tmp/r162` (the box's tmpfs, so qBittorrent
copies), the destination's `st_size` and `st_blocks` read every ~0.25 s. A copy to the tmpfs
takes a fraction of a second, so `--throttle` stops and continues qBittorrent (SIGSTOP 0.8 s,
SIGCONT 0.02 s) to watch it; move-walk.sh's big step does the same.

```sh
scripts/dev/app-box.sh up --qbt -b b033-probe
scripts/dev/research/162-preallocated-move/watch-move.sh -b b033-probe --mib 2048
scripts/dev/research/162-preallocated-move/watch-move.sh -b b033-probe --mib 2048 --throttle
scripts/dev/app-box.sh down -b b033-probe
```

**What it printed** (qBittorrent 5.3.0, 2026-10-22; the seconds column of the throttled run):

```
   0.1s  dest size 2147483648  dest 512-byte blocks 1047872  source 2147483648
   0.3s  dest size 2147483648  dest 512-byte blocks 3843488  source gone

   0.1s  dest size 2147483648  dest 512-byte blocks 618560  source 2147483648   (--throttle)
   2.7s  dest size 2147483648  dest 512-byte blocks 1299008  source 2147483648
   6.3s  dest size 2147483648  dest 512-byte blocks 2448608  source 2147483648
  10.0s  dest size 2147483648  dest 512-byte blocks 3396736  source 2147483648
  12.4s  dest size 2147483648  dest 512-byte blocks 4172512  source 2147483648
```

**Answer.** The destination file has its full length from the first reading, on a tmpfs as on
ntfs3: the copy fills a file already that long (its blocks grow; its size never does). So the
size at the destination says nothing about a move's progress. Blocks would on a filesystem that
keeps the file sparse, but ntfs3 allocates the full length at once (#116), so they don't help
there either. omaseed now treats a first reading that is already the torrent's whole size as no
measurement, and the band says how long the torrent has been moving instead (#162, option a).
