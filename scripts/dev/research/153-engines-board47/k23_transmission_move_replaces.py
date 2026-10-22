#!/usr/bin/env python3
"""K23 (board 47 §3): does a Transmission move replace a file already at the destination?

  python3 k23_transmission_move_replaces.py

Twice, on the :8085 test server: once with source and destination on the same filesystem
(both beside the test server's profile, where Transmission renames), once across filesystems
(the destination on a tmpfs under $TMPDIR or /tmp, where it copies). Each time a single-file
torrent of 8 MiB of random data is added as complete, a different file of the same name and
size is put at the destination, and the torrent is moved there. Printed: whether the file at
the destination afterwards is the torrent's or the one that was there, whether the source is
still there, the torrent's download_dir, status and error, and what a verify then says.
"""

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpc  # noqa: E402

NAME = "k23-sample.bin"


def run(client, label, src, dest):
    shutil.rmtree(src, ignore_errors=True)
    shutil.rmtree(dest, ignore_errors=True)
    data = os.path.join(src, NAME)
    rpc.random_file(data, 8 << 20)
    rpc.random_file(os.path.join(dest, NAME), 8 << 20)
    ours, theirs = rpc.sha256(data), rpc.sha256(os.path.join(dest, NAME))
    torrent = rpc.make_torrent(data, os.path.join(tempfile.gettempdir(), "k23.torrent"), piece_kib=256)
    info_hash = client.add(torrent, src)
    try:
        rpc.wait(lambda: client.torrent(info_hash, ["status"])["status"] == 6, 60, 0.2, "seeding")
        client.call("torrent_set_location", {"ids": [info_hash], "location": dest, "move": True})
        time.sleep(1)
        state = client.torrent(info_hash, ["download_dir", "status", "error", "error_string", "percent_done"])
        at_dest = rpc.sha256(os.path.join(dest, NAME))
        print(f"{label} (st_dev {os.stat(src).st_dev} → {os.stat(dest).st_dev}):")
        print(f"  the file at the destination is {'the torrent' if at_dest == ours else 'the one that was there' if at_dest == theirs else 'neither'}'s")
        print(f"  the source file is {'still there' if os.path.exists(data) else 'gone'}")
        print(f"  download_dir {rpc.shown(state['download_dir'])}, status {state['status']}, error {state['error']} {state['error_string']!r}")
        client.call("torrent_verify", {"ids": [info_hash]})
        rpc.wait(lambda: client.torrent(info_hash, ["status"])["status"] not in (1, 2), 60, 0.2, "verify")
        time.sleep(0.5)
        after = client.torrent(info_hash, ["percent_done", "status", "error_string"])
        print(f"  a verify then: {after['percent_done'] * 100:.0f} % complete, status {after['status']} {after['error_string']!r}")
    finally:
        client.remove(info_hash, delete=False)
        shutil.rmtree(src, ignore_errors=True)
        shutil.rmtree(dest, ignore_errors=True)


def main():
    client = rpc.Transmission()
    version = client.call("session_get", {"fields": ["version"]})["version"]
    print(f"Transmission {version}")
    home = os.path.expanduser("~/.local/share/omaseed-test/transmission")
    run(client, "same filesystem", os.path.join(home, "k23-src"), os.path.join(home, "k23-dest"))
    run(client, "across filesystems", os.path.join(home, "k23-src"),
        os.path.join(tempfile.gettempdir(), "omaseed-k23-dest"))


if __name__ == "__main__":
    main()
