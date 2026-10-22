# 153 — Board 47 §3: four things the engines' sources suggested and nobody had seen

**Questions (#153, board 47 §3; DECISIONS C25 K10, K22, K23, K26).**

- K22: does Transmission's RPC answer while a long move runs?
- K23: does a Transmission move replace a file already at the destination?
- K26: does Deluge's metadata prefetch (`core.prefetch_magnet_metadata`) work through
  deluge-web, and does a long one hold deluge-web's other answers?
- K10: does connecting deluge-web to a daemon switch it for deluge-web's other users?

**How.** Against the engines' test servers, `scripts/dev/test-server.sh start --transmission`
(:8085) and `start --deluge` (:8086, its password `DELUGE_TEST_PASS` in dev.env). K26 also uses
the offline swarm's tracker (`scripts/dev/swarm.sh start`, :26996). Every file is generic random
data made by the script; each script removes what it added.

```sh
cd scripts/dev/research/153-engines-board47
python3 k22_transmission_busy_move.py          # --gib 4: a tmpfs → btrfs move, session_get every 50 ms
python3 k23_transmission_move_replaces.py      # same filesystem, then across
python3 k26_deluge_prefetch.py                 # needs the swarm's tracker
python3 k10_deluge_daemon_switch.py            # starts and stops a second, empty deluged on :58848
```

`rpc.py` holds the two small clients (Transmission's 409 session dance, deluge-web's cookie).

**What they printed** (Transmission 4.1.3, RPC 6.0.1; Deluge 2.2.0 with libtorrent 2.0.14;
2026-10-22):

```
== k22_transmission_busy_move.py
Transmission 4.1.3 (838877323f) · RPC 6.0.1
making 4 GiB of random data in /tmp/omaseed-k22-src …
seeding; moving it from /tmp/omaseed-k22-src to ~/.local/share/omaseed-test/transmission/k22-dest (st_dev 56 → 41)
set_location answered after 2.70 s
the move took 2.70 s; download_dir now ~/.local/share/omaseed-test/transmission/k22-dest
session_get before the move: 10 answers, median 1 ms
session_get asked during the move: 1 answers, median 2688 ms, longest 2.69 s
(8 GiB: set_location answered after 2.98 s, the one session_get asked meanwhile after 2.97 s)
== k23_transmission_move_replaces.py
same filesystem (st_dev 41 → 41):
  the file at the destination is the torrent's
  the source file is gone
  download_dir ~/.local/share/omaseed-test/transmission/k23-dest, status 6, error 0 ''
  a verify then: 100 % complete, status 6 ''
across filesystems (st_dev 41 → 56):
  the file at the destination is the torrent's
  the source file is gone
  download_dir /tmp/omaseed-k23-dest, status 6, error 0 ''
  a verify then: 100 % complete, status 6 ''
== k26_deluge_prefetch.py
Deluge 2.2.0 through deluge-web; seeder: Transmission on 127.0.0.1:26994
1. a seeded magnet, x.pe peer hint only: answered after 30.0 s with 0 bytes of metadata
1. a seeded magnet, the tracker: answered after 5.1 s with 246 bytes of metadata, names ['k26-prefetch-sample.bin']
2. a magnet nobody has, timeout 10 s: answered after 10.0 s with ['0123456789abcdef0123456789abcdef01234567', '']
   the other session meanwhile: 102 answers, median 2 ms, longest 38 ms
== k10_deluge_daemon_switch.py
before: A the test server's daemon, 3 torrents · B the test server's daemon, 3 torrents
after A's web.connect to the second daemon: A the second daemon, 0 torrents · B the second daemon, 0 torrents
B's web.get_hosts statuses: ['Online', 'Connected']
restored: A the test server's daemon, 3 torrents · B the test server's daemon, 3 torrents
```

**What it means.**

- K22 **holds**: a move runs in Transmission's session thread and every RPC answer waits for it,
  set_location's own included, for as long as the copy takes (here 2.7 s for 4 GiB from tmpfs
  to an NVMe btrfs; a slow disk or a NAS makes it minutes). Board 47's "busy moving" band
  applies.
- K23 **replaces**: on the same filesystem (a rename) and across (a copy) the torrent's file
  takes the place of the one that was there, which is gone, with no error; the torrent then
  verifies complete. Board 47's "Transmission would replace that file" line applies.
- K26 **works** through deluge-web, by repeated calls with a timeout each: a magnet whose peers
  come from a tracker answered in 5 s with its metadata; one nobody has answered empty after
  its timeout. A long call holds nothing: another deluge-web session kept answering in
  milliseconds meanwhile. Deluge 2.2.0 ignores a magnet's `x.pe` peer hints (no metadata in
  30 s, and a plain add with only `x.pe` never connects until `core.connect_peer`).
- K10 **switches everyone**: deluge-web keeps one daemon connection for all its sessions;
  `web.connect` from one session moves the others too. The daemon step must say so before it
  connects (board 47 §3's note). Also seen: deluge-web replaces a local host's `localclient`
  login with the one in its own config dir, so a second local daemon needs a user of its own.
- Also found while setting up: deluge-web 2.2.0 doesn't pass `daemon.info` on ("Unknown
  method"); `daemon.get_version` names the daemon's version (the recognizer uses that).
