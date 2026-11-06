#!/usr/bin/env python3
"""#87 (board 47 §3, C25 K26): what Deluge 2.2's `core.prefetch_magnet_metadata` gives through
deluge-web before an add, how long it takes, what it answers on failure, and whether asking
again carries on where the last call stopped. Also: whether `core.get_torrents_status`'s
`diff=True` is safe through deluge-web (the engine's poll picks its call from this).

  python3 prefetch_files.py [--timeout S]

Needs the engines' test servers: `scripts/dev/test-server.sh start --deluge` (:8086) and
`start --transmission` (:8085, the seeder). It runs its own tracker (scripts/dev/swarm-tracker.py
on 127.0.0.1:26989, DHT is off everywhere) and removes everything it added at the end.

1. A fresh multi-file torrent (three files of random data, one in a subfolder), seeded by
   Transmission and announced to that tracker. Its magnet goes to the prefetch with a short
   timeout first (2 s), then again (--timeout, default 30 s): printed, how long each took and
   what came back; the metadata decoded (the info dictionary: name, files, sizes, piece
   length, private). Then once more: whether an answer already had is quick.
2. A magnet nobody has (5 s), a v2-only magnet and a link that isn't a magnet: what each
   answers.
3. diff=True from two deluge-web sessions: whether one session's call empties the other's.
"""

import argparse
import base64
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "153-engines-board47"))
import rpc  # noqa: E402

TRACKER_PORT = 26989


def bdecode(data, at=0):
    """One bencoded value at `at`: (value, next offset). Byte strings stay bytes."""
    kind = data[at:at + 1]
    if kind == b"i":
        end = data.index(b"e", at)
        return int(data[at + 1:end]), end + 1
    if kind == b"l":
        at += 1
        items = []
        while data[at:at + 1] != b"e":
            item, at = bdecode(data, at)
            items.append(item)
        return items, at + 1
    if kind == b"d":
        at += 1
        items = {}
        while data[at:at + 1] != b"e":
            key, at = bdecode(data, at)
            items[key], at = bdecode(data, at)
        return items, at + 1
    colon = data.index(b":", at)
    length = int(data[at:colon])
    return data[colon + 1:colon + 1 + length], colon + 1 + length


def describe(raw):
    """What an add sheet reads from the metadata: the info dictionary's name and files."""
    value, _ = bdecode(raw)
    keys = sorted(k.decode() for k in value)
    info = value.get(b"info", value)  # the info dictionary, or something wrapping it
    name = info.get(b"name", b"").decode()
    files = []
    if b"files" in info:
        for entry in info[b"files"]:
            files.append(("/".join(p.decode() for p in entry[b"path"]), entry[b"length"]))
    else:
        files.append((name, info.get(b"length", 0)))
    return (f"top-level keys {keys}; name {name!r}; piece length {info.get(b'piece length')}; "
            f"private {info.get(b'private', 0)}; files {files}")


def prefetch(deluge, magnet, timeout):
    began = time.monotonic()
    try:
        result = deluge.call("core.prefetch_magnet_metadata", [magnet, timeout], timeout=timeout + 60)
    except RuntimeError as error:
        return time.monotonic() - began, f"error {error}", b""
    took = time.monotonic() - began
    metadata = result[1] if isinstance(result, list) and len(result) > 1 else ""
    raw = base64.b64decode(metadata) if metadata else b""
    shown = [r if not isinstance(r, str) or len(r) < 60 else r[:16] + "…" for r in result] \
        if isinstance(result, list) else result
    return took, shown, raw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    work = os.path.join(tempfile.gettempdir(), "omaseed-87-prefetch")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    control = os.path.join(work, "control.json")
    with open(control, "w", encoding="utf-8") as out:
        out.write("{}")
    tracker = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "..", "..", "swarm-tracker.py"), str(TRACKER_PORT), control],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    announce = f"http://127.0.0.1:{TRACKER_PORT}/announce"

    transmission = rpc.Transmission()
    deluge = rpc.Deluge()
    print(f"Deluge {deluge.call('daemon.get_version')} (libtorrent {deluge.call('core.get_libtorrent_version')}) "
          f"through deluge-web; seeder: Transmission on :8085")

    pack = os.path.join(work, "prefetch-pack")
    rpc.random_file(os.path.join(pack, "part-1.bin"), 1 << 20)
    rpc.random_file(os.path.join(pack, "part-2.bin"), 3 << 19)
    rpc.random_file(os.path.join(pack, "extras", "notes.bin"), 4096)
    torrent = rpc.make_torrent(pack, os.path.join(work, "pack.torrent"), piece_kib=64, tracker=announce)
    info_hash = transmission.add(torrent, work)
    try:
        rpc.wait(lambda: transmission.torrent(info_hash, ["status"])["status"] == 6, 60, 0.2, "seeding")
        rpc.wait(lambda: transmission.torrent(info_hash, ["tracker_stats"])["tracker_stats"][0]
                 .get("last_announce_succeeded"), 60, 0.5, "the seeder's announce")
        magnet = (f"magnet:?xt=urn:btih:{info_hash}&dn=prefetch-pack"
                  f"&tr={urllib.parse.quote(announce, safe='')}")
        for label, timeout in (("short call", 2), ("asked again", args.timeout), ("once more", args.timeout)):
            took, shown, raw = prefetch(deluge, magnet, timeout)
            print(f"1. {label} (timeout {timeout} s): {took:.1f} s → {shown}")
            if raw:
                print(f"   {len(raw)} bytes: {describe(raw)}")
        listed = deluge.call("core.get_torrents_status", [{"id": info_hash}, ["name"]])
        print(f"   the daemon's torrents now hold it: {bool(listed)}")
    finally:
        transmission.remove(info_hash, delete=False)

    nobody = "magnet:?xt=urn:btih:" + "0123456789abcdef0123456789abcdef01234567" + "&dn=nobody-has-this"
    took, shown, _ = prefetch(deluge, nobody, 5)
    print(f"2. a magnet nobody has (timeout 5 s): {took:.1f} s → {shown}")
    v2 = "magnet:?xt=urn:btmh:1220" + "ab" * 32 + "&dn=v2-only"
    took, shown, _ = prefetch(deluge, v2, 5)
    print(f"2. a v2-only magnet: {took:.1f} s → {shown}")
    took, shown, _ = prefetch(deluge, "http://127.0.0.1:9/not-a-magnet.torrent", 5)
    print(f"2. a link that isn't a magnet: {took:.1f} s → {shown}")

    other = rpc.Deluge()
    keys = ["name", "state"]
    first = deluge.call("core.get_torrents_status", [{}, keys, True])
    again = deluge.call("core.get_torrents_status", [{}, keys, True])
    other.call("core.get_torrents_status", [{}, keys, True])
    deluge.call("core.pause_torrents", [list(first)[:1]])
    time.sleep(1)
    from_other = other.call("core.get_torrents_status", [{}, keys, True])
    from_first = deluge.call("core.get_torrents_status", [{}, keys, True])
    deluge.call("core.resume_torrents", [list(first)[:1]])
    print(f"3. diff=True: first call {len(first)} torrents, the same session again {len(again)}; "
          f"after a pause, session B sees {from_other!r}, then session A sees {from_first!r}")

    tracker.terminate()
    tracker.wait()
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
