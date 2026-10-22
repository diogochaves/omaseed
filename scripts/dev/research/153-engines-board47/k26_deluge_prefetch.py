#!/usr/bin/env python3
"""K26 (board 47 §3): does Deluge's metadata prefetch work through deluge-web, and does a
long prefetch hold deluge-web's other answers?

  python3 k26_deluge_prefetch.py [--timeout S]

1. A fresh single-file torrent (2 MiB of random data), announced to the offline swarm's
   tracker (scripts/dev/swarm.sh, http://127.0.0.1:26996/announce; DHT is off everywhere), is
   seeded by the :8085 Transmission test server. Its magnet goes to
   `core.prefetch_magnet_metadata(magnet, 30)` through the :8086 deluge-web, twice: once with
   the seeder as an `x.pe` peer hint only, once with the tracker (`tr=`). Printed: what came
   back, how long it took, and the file name its metadata holds.
2. A magnet nobody has (a made-up hash), with the same call and --timeout (default 10 s),
   while a second deluge-web session asks `daemon.get_version` every 100 ms: printed, how
   long the prefetch took to give up and the other session's longest answer meanwhile.
The seeded torrent and its data are removed at the end; Deluge never adds either magnet.
"""

import argparse
import base64
import os
import shutil
import statistics
import sys
import tempfile
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpc  # noqa: E402


def bdecode_names(data):
    """The file names a torrent's info dictionary holds (enough of bencode for that)."""
    text = data.decode("latin-1")
    names = []
    marker = "4:name"
    at = text.find(marker)
    while at >= 0:
        rest = text[at + len(marker):]
        length, _, tail = rest.partition(":")
        if length.isdigit():
            names.append(tail[: int(length)])
        at = text.find(marker, at + 1)
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, default=10)
    parser.add_argument("--tracker", default="http://127.0.0.1:26996/announce")
    args = parser.parse_args()

    transmission = rpc.Transmission()
    seeder_port = transmission.call("session_get", {"fields": ["peer_port"]})["peer_port"]
    deluge = rpc.Deluge()
    print(f"Deluge {deluge.call('daemon.get_version')} through deluge-web; seeder: Transmission on 127.0.0.1:{seeder_port}")

    folder = os.path.join(tempfile.gettempdir(), "omaseed-k26")
    shutil.rmtree(folder, ignore_errors=True)
    data = os.path.join(folder, "k26-prefetch-sample.bin")
    rpc.random_file(data, 2 << 20)
    torrent = rpc.make_torrent(data, os.path.join(folder, "k26.torrent"), piece_kib=256, tracker=args.tracker)
    info_hash = transmission.add(torrent, folder)
    try:
        rpc.wait(lambda: transmission.torrent(info_hash, ["status"])["status"] == 6, 60, 0.2, "seeding")
        rpc.wait(lambda: transmission.torrent(info_hash, ["tracker_stats"])["tracker_stats"][0]
                 .get("last_announce_succeeded"), 60, 0.5, "the seeder's announce")
        hint = urllib.parse.quote(f"127.0.0.1:{seeder_port}")
        base = f"magnet:?xt=urn:btih:{info_hash}&dn=k26-prefetch-sample.bin"
        for label, magnet in (("x.pe peer hint only", f"{base}&x.pe={hint}"),
                              ("the tracker", f"{base}&tr={urllib.parse.quote(args.tracker, safe='')}")):
            began = time.monotonic()
            result = deluge.call("core.prefetch_magnet_metadata", [magnet, 30], timeout=60)
            took = time.monotonic() - began
            metadata = result[1] if isinstance(result, list) and len(result) > 1 else ""
            raw = base64.b64decode(metadata) if metadata else b""
            print(f"1. a seeded magnet, {label}: answered after {took:.1f} s with {len(raw)} bytes "
                  f"of metadata{', names ' + str(bdecode_names(raw)) if raw else ''}")
    finally:
        transmission.remove(info_hash, delete=False)
        shutil.rmtree(folder, ignore_errors=True)

    # 2. Nobody has this one: the call waits its whole timeout.
    other = rpc.Deluge()
    samples = []
    stop = threading.Event()

    def poll():
        while not stop.is_set():
            at = time.monotonic()
            other.call("daemon.get_version", timeout=120)
            samples.append(time.monotonic() - at)
            time.sleep(0.1)

    poller = threading.Thread(target=poll)
    poller.start()
    time.sleep(0.3)
    began = time.monotonic()
    nobody = "magnet:?xt=urn:btih:" + "0123456789abcdef0123456789abcdef01234567" + "&dn=nobody-has-this"
    result = deluge.call("core.prefetch_magnet_metadata", [nobody, args.timeout], timeout=args.timeout + 60)
    took = time.monotonic() - began
    stop.set()
    poller.join()
    print(f"2. a magnet nobody has, timeout {args.timeout} s: answered after {took:.1f} s with "
          f"{[r if not isinstance(r, str) or len(r) < 60 else r[:20] + '…' for r in result] if isinstance(result, list) else result!r}")
    print(f"   the other session meanwhile: {len(samples)} answers, median "
          f"{statistics.median(samples) * 1000:.0f} ms, longest {max(samples) * 1000:.0f} ms")


if __name__ == "__main__":
    main()
