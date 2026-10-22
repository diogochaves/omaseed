# #107: does the Web API tell a stopped errored torrent apart?

**Question:** qBittorrent ranks `hasError()` above `isStopped()` in `TorrentImpl::updateState`,
so an errored torrent reports `state: error` whether it is stopped or not. Does anything else
in `torrents/info` or `torrents/properties` say it is stopped, so omaseed can show
"error · stopped" (DECISIONS C20) without remembering the stop it sent?

**Answer (2026-10-22, qBittorrent 5.2.4):** no field does, but a filter does.

- `state` stays `error` through the stop and through a start that fails again.
- No field says "stopped". What changes is what follows from it: `dlspeed` and `eta`
  (8640000, "infinite"), `force_start` (false), `num_incomplete`, `peers_total`. None of them
  is a reliable flag: an errored torrent with no peers looks the same running or stopped.
- `torrents/info?filter=stopped` lists it only while it is stopped, and `filter=running` only
  while it is not: the filters test `isStopped()` itself, not `state`. (`paused` and `resumed`,
  the 4.x names, list it both ways: 5.x no longer knows them and reads them as "all".)

So omaseed could ask `torrents/info?filter=stopped&hashes=<the errored ones>` beside its
`sync/maindata` polls and see a stop sent from anywhere, across restarts. As built for v0.3.0,
the app remembers the stop it sent (per server, by hash, in memory), as C20 says; the filter is
what `scripts/dev/error-walk.sh` checks the stop with.

## The script

`probe.sh` starts a throwaway qBittorrent (its own profile in a temporary folder, port 8095
by default), adds the swarm's unwritable fixture into a read-only folder, downloads from the
swarm's seeder until it errors, then stops and starts it, printing the fields that changed and
which filters list it after each step. It needs the swarm's seeder
(`scripts/dev/swarm.sh start`) and never touches the playground or the strict twin.

```sh
scripts/dev/research/107-errored-stopped/probe.sh
```

## What it printed

```
qBittorrent v5.2.4, state error
errored: filters all paused active resumed running errored
after torrents/stop: state error, filters all stopped paused inactive resumed errored
changed fields, errored -> errored and stopped:
   filters: ['all', 'paused', 'active', 'resumed', 'running', 'errored'] -> ['all', 'stopped', 'paused', 'inactive', 'resumed', 'errored']
   info.dlspeed: 1904 -> 0
   info.eta: 1097 -> 8640000
   info.force_start: True -> False
   info.num_incomplete: 1 -> 0
   props.dl_speed: 1904 -> 0
   props.eta: 1097 -> 8640000
   props.peers_total: 1 -> 0
after torrents/start: state error, filters all paused active resumed running errored
```
