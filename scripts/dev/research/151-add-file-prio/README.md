# 151: are a .torrent's file choices lost after a stopped add?

`tst_app_add`'s `addsWithFileChoicesRenameAndLimits` failed 4 of 4 against qBittorrent 5.3.0
(Web API 2.16.2): a file read back at priority 1 where 0 or 7 was set, and the log said
`POST torrents/filePrio: cancelled`. A cancelled priority in the add flow could mean that files
unticked in the add sheet download after all. Is it the server, the app or the test?

## The probe

`probe.sh [URL] [VARIANT...]` adds `tests/fixtures/open_movies_v1.torrent` stopped and sends
what `App::addTorrent` sends for a `.torrent` whose files were chosen in the sheet: the add
(stopped, a name, a download limit, first and last pieces first), the folder and file renames,
one `torrents/filePrio` per priority (ids 3 and 4 skipped, id 7 maximal), then `torrents/start`.
It prints the state and every file's priority after each step and for seven seconds after. The
variants leave out the renames (`norename`), first and last pieces first (`noflp`) or the start
(`nostart`), or wait for the torrent to leave `checkingResumeData` before the priorities (`wait`).

```
flock -o /tmp/claude-$UID/omaseed-playground.lock scripts/dev/research/151-add-file-prio/probe.sh
```

Against the playground, 2026-10-23 (abridged: `full` and `norename`; `noflp`, `nostart` and
`wait` read the same, `nostart` ending in `stoppedDL`):

```
qBittorrent v5.3.0, Web API 2.16.2
== full
  torrents/add           200
  added                  stoppedDL            [1, 1, 1, 1, 1, 1, 1, 1]
  torrents/renameFolder  200
  torrents/renameFile    200
  torrents/filePrio      200
  torrents/filePrio      200
  after filePrio         stoppedDL            [1, 1, 1, 0, 0, 1, 1, 7]
  torrents/start         200
  at once                stoppedDL            [1, 1, 1, 0, 0, 1, 1, 7]
  +1s                    stalledDL            [1, 1, 1, 0, 0, 1, 1, 7]
  +4s                    stalledDL            [1, 1, 1, 0, 0, 1, 1, 7]
== norename
  torrents/add           200
  added                  checkingResumeData   [1, 1, 1, 1, 1, 1, 1, 1]
  torrents/filePrio      200
  torrents/filePrio      200
  after filePrio         checkingResumeData   [1, 1, 1, 0, 0, 1, 1, 7]
  torrents/start         200
  +1s                    stalledDL            [1, 1, 1, 0, 0, 1, 1, 7]
  +4s                    stalledDL            [1, 1, 1, 0, 0, 1, 1, 7]
```

5.3.0 takes every priority, keeps it through `checkingResumeData` and the start, and answers
each `filePrio` with 200 as soon as it holds it.

## What the test saw

With every request `QbtClient` sends traced (a temporary `qCDebug` in `send()` and
`onJobReplyFinished()`, not committed), a failing run read:

```
send POST torrents/filePrio  id=3|4  priority=0
send GET  torrents/files                         <- the test's read
done      torrents/filePrio 200
send POST torrents/filePrio  id=7    priority=7
done      torrents/filePrio 200
send POST torrents/start
done      torrents/files 200                     <- index 7 still 1
FAIL!  Compared lists differ at index 7
done      torrents/start 0                       <- "cancelled": the test destroyed App
```

The test waited for the torrent to be "not stopped" before reading the priorities, and a
torrent added stopped is `checkingResumeData` for its first moment, which is not "stopped": the
wait passed at once and the read went out while the app was still sending the priorities. The
test then ended, destroying `App` and its `QbtClient`, which cancels what is still in flight
(`QbtClient::~QbtClient` calls `cancelAll()`): `filePrio` or `torrents/start`, whichever was
last. Nothing in the app cancelled a request; every `filePrio` it sent was answered 200.

## The answer

The test, not the app. The app sends the priorities one after another, each after the last is
answered, and starts the torrent only once all of them are; 5.3.0 keeps them. The test now waits
for the torrent to be started (a state that is neither stopped nor `checkingResumeData`), which
the app asks for only after the last priority is in, and then reads the list once, exactly.

Real adds on 5.3 keep the sheet's choices. Had a follow-up been cut short (the app quit within a
fraction of a second of the add), the torrent stays stopped, because the start is the last step:
nothing downloads that was not chosen.
