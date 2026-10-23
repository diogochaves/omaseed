# #182: freeing a skipped file leaves a v2-only torrent short of 100 %

**Question:** on a v2-only torrent, "free the space" deleted a skipped file that held
downloaded pieces and rechecked; afterwards the torrent read 98.6 % and never completed. Why,
is a v1-only torrent hit too, and does "add it again" (what a hybrid gets, C17 D3) bring a
v2-only torrent back to 100 %?

**Answer (2026-10-23, libtorrent 2.1.2):** after a recheck that finds a skipped file's pieces
gone, libtorrent counts a v2-only torrent's wanted bytes piece by piece at the full piece size
(each file's short last piece included) but counts only the real bytes as done, and since it
reads the torrent as `finished` it never downloads anything to close the gap. With the file
still there, a recheck reads 100 %. A v1-only torrent reads short too, but `downloading`: the
pieces it shared with a wanted file are fetched again, so it completes. Adding the torrent again
(removed keeping its files, the file deleted, added from its own .torrent) counts from the real
bytes and reads 100 %, whether the priorities come with the add or right after it, as
`AddAgainJob` sends them. So DECISIONS C29: a v2-only torrent is a hybrid in the free plan, and
"add it again" is offered.

## The script

`lt_free.py` (needs `python-libtorrent` 2.x): two libtorrent sessions on loopback, offline, a
three-file torrent downloaded whole, one file skipped, then:

| Mode | What follows the skip |
|---|---|
| `keep-recheck` | the file stays; recheck |
| `delete-recheck` | stop, delete it, start, recheck (what `FreeSkippedJob` does on v1) |
| `readd-after` | remove the torrent, delete it, add it again stopped with every file wanted, skip it, start (`AddAgainJob`) |
| `readd-at` | the same with the priorities in the add itself |

```sh
cd scripts/dev/research/182-v2-free
for k in v2 v1 hybrid; do for m in keep-recheck delete-recheck readd-after readd-at; do
    for s in 0 1 2; do python3 -I -W ignore lt_free.py $k /tmp/lt182 $m $s | grep -v ' downloaded \| skipped '; done
done; done
```

## What it printed

The middle file (`b.nfo`, 70 001 bytes) skipped; the first and the last file read the same way
(`progress` is qBittorrent's: done / wanted, 1 when they are equal):

```
v2     keep-recheck   skip=Fixture/b.nfo   recheck      state=seeding     pieces=111/111 wanted=3570004 done=3570004 progress=1.0000
v2     delete-recheck skip=Fixture/b.nfo   recheck      state=finished    pieces=108/111 wanted=3538944 done=3500003 progress=0.9890
v2     readd-after    skip=Fixture/b.nfo   added again  state=finished    pieces=108/111 wanted=3500003 done=3500003 progress=1.0000
v2     readd-at       skip=Fixture/b.nfo   added again  state=finished    pieces=108/111 wanted=3500003 done=3500003 progress=1.0000
v1     delete-recheck skip=Fixture/b.nfo   recheck      state=downloading pieces=106/109 wanted=3537236 done=3471700 progress=0.9815
hybrid delete-recheck skip=Fixture/b.nfo   recheck      state=checking_files errc=217 pieces=0/111 progress=0.0000
hybrid readd-after    skip=Fixture/b.nfo   added again  state=finished    pieces=108/111 wanted=3500003 done=3500003 progress=1.0000
```

`3538944` is 108 pieces of 32 KiB: the wanted pieces at full size. A recheck after the re-add
reads the same 100 %.
