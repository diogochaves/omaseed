# Research reproductions

Scripts that answered a question once and can answer it again: when qBittorrent, libtorrent or
a filesystem changes, run them and compare with what they printed then. Each folder is one
issue, with a README saying what it reproduces, how to run it and what it printed.

| Folder | Question | Issue |
|---|---|---|
| [115-hybrid-recheck](115-hybrid-recheck/) | Why does a recheck after deleting skipped files put a torrent in error? | #115 |
| [116-fs-allocation](116-fs-allocation/) | What does each filesystem do with the files a torrent starts writing? | #116 |
| [117-open-file-move](117-open-file-move/) | Does qBittorrent's move of a finished file fail while someone has it open? | #117 |
| [121-stuck-move](121-stuck-move/) | Once a finished torrent's move into a full disk failed, what makes qBittorrent try it again? | #121 |
| [107-errored-stopped](107-errored-stopped/) | Does the Web API tell a stopped errored torrent from a running one? | #107 |
| [146-torrent-creator](146-torrent-creator/) | What does qBittorrent's torrent creator do through the API, and does a tracker's copy seed from the original folder? | #146 |
| [162-preallocated-move](162-preallocated-move/) | How big is a torrent's file at its new place while qBittorrent moves it? | #162 |
| [151-add-file-prio](151-add-file-prio/) | Are a .torrent's file choices kept when they are set right after a stopped add? | #151 |
| [182-v2-free](182-v2-free/) | Why does freeing a skipped file leave a v2-only torrent short of 100 %, and does adding it again fix it? | #182 |

They talk only to throwaway qBittorrents (the playground on `:8081`, or a profile made with
`scripts/dev/test-server.sh`) and the offline swarm (`scripts/dev/swarm.sh`), never to a real
library. A general tool that came out of this work lives one level up:
`scripts/dev/fastresume.py`, which reads what qBittorrent saved about a torrent.

A new one: a folder named `<issue>-<topic>`, the scripts with their usage at the top, and a
README with the question, the commands, and what they printed (versions included).
