#!/usr/bin/env python3
"""K22 (board 47 §3): does Transmission's RPC answer while a long move runs?

  python3 k22_transmission_busy_move.py [--gib N] [--src DIR] [--dest DIR]

A single-file torrent of N GiB of random data (default 4) is made in --src (default a tmpfs
folder under $TMPDIR or /tmp), added to the :8085 test server as complete, and once it seeds
it is moved (torrent_set_location, move true) to --dest (default beside the test server's
profile, on another filesystem), so Transmission copies it. Meanwhile another thread asks
session_get every 50 ms and notes how long each answer took. Printed: how long the
set_location call took, how long the move took (until download_dir changed and the file
left the source), and the answers' latencies inside that window. The torrent and both
folders are removed at the end.
"""

import argparse
import os
import shutil
import sys
import statistics
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpc  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gib", type=float, default=4)
    parser.add_argument("--src", default=os.path.join(tempfile.gettempdir(), "omaseed-k22-src"))
    parser.add_argument("--dest", default=os.path.expanduser("~/.local/share/omaseed-test/transmission/k22-dest"))
    args = parser.parse_args()

    client = rpc.Transmission()
    version = client.call("session_get", {"fields": ["version", "rpc_version_semver"]})
    print(f"Transmission {version['version']} · RPC {version['rpc_version_semver']}")
    size = int(args.gib * (1 << 30))
    data = os.path.join(args.src, "k22-move-sample.bin")
    print(f"making {args.gib:g} GiB of random data in {rpc.shown(args.src)} …")
    rpc.random_file(data, size)
    torrent = rpc.make_torrent(data, os.path.join(args.src, "k22.torrent"), piece_kib=16384)
    info_hash = client.add(torrent, args.src)
    try:
        rpc.wait(lambda: (t := client.torrent(info_hash, ["status", "percent_done"]))
                 and t["percent_done"] == 1 and t["status"] == 6, 300, 0.5, "seeding")
        os.makedirs(args.dest, exist_ok=True)
        print(f"seeding; moving it from {rpc.shown(args.src)} to {rpc.shown(args.dest)} (st_dev {os.stat(args.src).st_dev} → "
              f"{os.stat(args.dest).st_dev})")

        samples = []
        stop = threading.Event()

        def poll():
            asker = rpc.Transmission()
            while not stop.is_set():
                began = time.monotonic()
                try:
                    asker.call("session_get", {"fields": ["version"]}, timeout=120)
                    samples.append((began, time.monotonic() - began))
                except Exception as error:  # noqa: BLE001 - a research script: say what happened
                    samples.append((began, float("nan")))
                    print("poll failed:", error)
                time.sleep(0.05)

        poller = threading.Thread(target=poll)
        poller.start()
        time.sleep(0.5)
        began = time.monotonic()
        client.call("torrent_set_location", {"ids": [info_hash], "location": args.dest, "move": True})
        call_took = time.monotonic() - began
        moved_file = os.path.join(args.dest, "k22-move-sample.bin")
        rpc.wait(lambda: not os.path.exists(data) and os.path.exists(moved_file), 600, 0.02, "the file moved")
        moved = time.monotonic()
        time.sleep(0.5)
        stop.set()
        poller.join()
        location = client.torrent(info_hash, ["download_dir"])["download_dir"]

        inside = [took for at, took in samples if began <= at <= moved]
        before = [took for at, took in samples if at < began]
        print(f"set_location answered after {call_took:.2f} s")
        print(f"the move took {moved - began:.2f} s; download_dir now {rpc.shown(location)}")
        if before:
            print(f"session_get before the move: {len(before)} answers, median {statistics.median(before) * 1000:.0f} ms")
        if inside:
            print(f"session_get asked during the move: {len(inside)} answers, median "
                  f"{statistics.median(inside) * 1000:.0f} ms, longest {max(inside):.2f} s")
        else:
            print("no session_get was asked during the move")
    finally:
        client.remove(info_hash, delete=True)
        shutil.rmtree(args.src, ignore_errors=True)
        shutil.rmtree(args.dest, ignore_errors=True)


if __name__ == "__main__":
    main()
