# #115: a recheck after deleting files puts a hybrid torrent in error

**Question:** after a misclick wanted every file of a torrent for a few seconds, the skipped
files were deleted on disk to free their space. A recheck then put the torrent in `error`, stuck,
with nothing in qBittorrent's log or Web API. Why, and what is safe?

**Answer (2026-10-21, qBittorrent 5.2.4, libtorrent 2.1.2):** a libtorrent bug with **hybrid**
(v1+v2) torrents: `torrent_inconsistent_hashes` (error 217, "v1 and v2 hashes do not describe
the same data"). It needs a hybrid torrent that was *downloaded*, one of its files with
downloaded pieces missing from disk, and a recheck. v1-only and v2-only torrents are fine; the
filesystem and skipping are irrelevant. Only removing the torrent (keeping files) and adding it
again clears it. The full findings, the recovery table and the rule board 44 took from them
(DECISIONS C17 D3) are on #115.

## The scripts

| Script | What it does |
|---|---|
| `repro_217.py` | The minimal reproduction: two libtorrent sessions on loopback, a 3-file torrent downloaded, one file deleted, a recheck. `hybrid` errors with 217; `v1` and `v2` don't. |
| `lt_pair.py` | The same harness with variations: random or zero data, which files are kept, and what follows the delete (recheck, stop+start, re-want, restart from resume data, clear_error, re-add). Used to map what triggers it and what recovers. |
| `lt_resume.py` | Loads a *copy* of a qBittorrent `.fastresume` and `.torrent` into a standalone libtorrent session and rechecks: to see a real torrent's state offline. |
| `qbt-steps.sh` | The same steps through a real qBittorrent's Web API, on a swarm fixture (the Nested Library Sampler is hybrid). |

All need the Python bindings of libtorrent 2.x (`python-libtorrent` on Arch) except
`qbt-steps.sh`, which needs the swarm (`scripts/dev/swarm.sh start`).

```sh
python3 -I scripts/dev/research/115-hybrid-recheck/repro_217.py hybrid   # errc=217
python3 -I scripts/dev/research/115-hybrid-recheck/repro_217.py v1       # errc=0
python3 -I scripts/dev/research/115-hybrid-recheck/lt_pair.py hybrid random /tmp/lt115 readd
NOCOPY=1 PEER=127.0.0.1:26997 scripts/dev/research/115-hybrid-recheck/qbt-steps.sh \
    /tmp/q115 'Docs/' stopstart-recheck
```

## What they printed

`repro_217.py`, libtorrent 2.1.2.0 (re-run 2026-10-21 from this folder):

```
2.1.2.0 hybrid deleted s.txt: state=checking_files errc=217 'v1 and v2 hashes do not describe the same data' have=0/52
2.1.2.0 v1 deleted s.txt: state=downloading errc=0 'Success' have=50/51
```

`qbt-steps.sh` on qBittorrent 5.2.4, the fixture downloaded from the swarm, all but `Docs/`
skipped and deleted, stop, start, recheck: `error 0.004`, no pieces had. The same with the
content copied in (checked from disk, not downloaded): `stalledUP`, clean.
