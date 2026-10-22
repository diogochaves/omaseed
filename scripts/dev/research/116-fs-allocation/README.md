# #116: what each filesystem does with the files a torrent starts writing

**Question:** a 3-second misclick (every file of a torrent wanted, then skipped again) left
~78 GB allocated on an NTFS disk, and qBittorrent stalled for minutes. Which filesystems do
that, and what should omaseed say about them?

**Answer (2026-10-21, kernel 7.2, qBittorrent 5.2.4, libtorrent 2.1.2):** wanting a file makes
libtorrent create it at its full length at once, even on a stopped torrent. On `ntfs3`, `exfat`
and `vfat` that allocates the whole file, and skipping it later keeps it; on ext4, btrfs and the
newer in-kernel `ntfs` driver the file is sparse and costs nothing. `ntfs3`'s `prealloc` is not
the cause. On `ntfs3`, the first write near a file's end then writes zeros over everything before
it (4.3 GB of zeros for one 16 KB write), which is the stall. FAT32 refuses a file of 4 GiB or
more within seconds of the tick; exFAT refuses one bigger than the free space at once. Board 44
took this as DECISIONS C17 D8–D9; the full table is on #116.

## The scripts

- `fs-allocation.sh LABEL MKFS_KIND [MOUNT_TYPE] [MOUNT_OPTIONS]`: one filesystem, three cases
  (A: some files from the start; Cstop: the torrent stopped, all wanted 3 s; C: finished, all
  wanted ~3 s), measured in allocated bytes. A loop image mounted through udisks, so no root;
  everything is removed at the end. Needs the swarm (`scripts/dev/swarm.sh start`) and the
  filesystem's mkfs tool.
- `fstype.cpp`: prints `QStorageInfo::fileSystemType()` for paths, the string omaseed sees.
  `g++ -fPIC fstype.cpp -o fstype $(pkg-config --cflags --libs Qt6Core)`. It matched findmnt's
  type on every filesystem tried.

```sh
D=scripts/dev/research/116-fs-allocation
$D/fs-allocation.sh ntfs3 ntfs ntfs3
$D/fs-allocation.sh ntfs3-noprealloc ntfs ntfs3 prealloc=0
$D/fs-allocation.sh ntfs ntfs ntfs
$D/fs-allocation.sh exfat exfat
$D/fs-allocation.sh vfat vfat
$D/fs-allocation.sh ext4 ext4
$D/fs-allocation.sh btrfs btrfs
```

## What it printed

Open Movie Night (9 files, 751 MB), a 3 GB image each:

| Filesystem | A | Cstop | C |
|---|---|---|---|
| `ntfs3`, `ntfs3 prealloc=0`, `exfat`, `vfat` | 0.3 MB | 750.8 MB | 751.1 MB, kept |
| `ntfs`, `ext4`, `btrfs` | 0.3 MB | 0 (sparse) | 0.3 MB |

Not measured: `ntfs-3g` (`fuseblk`), xfs (tools not installed then), CIFS and NFS.
