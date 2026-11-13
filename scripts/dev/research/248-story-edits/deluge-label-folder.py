#!/usr/bin/env python3
"""#248 (board 58 §10, DECISIONS C43 Y7): what does a Deluge label with a folder do to a torrent?

  python3 scripts/dev/research/248-story-edits/deluge-label-folder.py check

Needs the Deluge playground (scripts/dev/test-server.sh start --deluge, :8086) and the
Transmission one (start --transmission, :8085) as the seeding peer.

1. A label `r248-far` with a folder (`apply_move_completed`, `move_completed`,
   `move_completed_path` = /tmp/omaseed-248/far) is put on a seeding torrent and on a paused
   unfinished one. Printed at 0, 1 and 3 s: each one's `save_path`, `move_completed`,
   `move_completed_path` and `state` (does the label copy its folder, and does a finished
   torrent move?).
2. The unfinished one is resumed and given Transmission as its peer. Printed every 0.2 s until
   it has landed (60 s cap): `state, progress, is_finished, save_path`; then whether a reading
   was finished with `save_path` still the old folder (the finish's transit, #248 design 9).
3. The label taken off one torrent (its options after), then `label.remove` while the other
   has it: in one `web.update_ui` reading, is the torrent's label "" and the label gone from
   the filter tree together?
Everything it added (torrents, the label, /tmp/omaseed-248) is removed at the end.
"""

import base64
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "153-engines-board47"))
from rpc import Deluge, Transmission, make_torrent, random_file, shown  # noqa: E402

WORK = "/tmp/omaseed-248"
FAR = os.path.join(WORK, "far")
LABEL = "r248-far"
FIELDS = ["name", "state", "progress", "is_finished", "save_path", "download_location",
          "move_completed", "move_completed_path", "label"]


def status(deluge, torrent_id):
    return deluge.call("core.get_torrent_status", [torrent_id, FIELDS])


def options(s):
    folder = shown(s["move_completed_path"]) or '""'
    return (f"save_path {s['save_path']}  move_completed {s['move_completed']}  "
            f"move_completed_path {folder}  state {s['state']}")


def check():
    deluge = Deluge()
    transmission = Transmission()
    print("Deluge", deluge.call("daemon.get_version"), "· libtorrent",
          deluge.call("core.get_libtorrent_version"), "· Transmission",
          transmission.call("session_get", {"fields": ["version"]})["version"])
    if "Label" not in deluge.call("core.get_enabled_plugins"):
        deluge.call("core.enable_plugin", ["Label"])
        time.sleep(1)
    shutil.rmtree(WORK, ignore_errors=True)
    added, seeder = [], None
    label_made = False
    try:
        # A seeding torrent: its data is in place, so the check finds it complete.
        done_dir = os.path.join(WORK, "seeding")
        random_file(os.path.join(done_dir, "r248-done.bin"), 2 << 20)
        done_meta = make_torrent(os.path.join(done_dir, "r248-done.bin"), os.path.join(WORK, "done.torrent"), 256)
        # An unfinished one: Transmission has its data, Deluge none.
        peer_dir = os.path.join(WORK, "peer")
        random_file(os.path.join(peer_dir, "r248-stream.bin"), 16 << 20)
        stream_meta = make_torrent(os.path.join(peer_dir, "r248-stream.bin"), os.path.join(WORK, "stream.torrent"), 256)

        def b64(path):
            return base64.b64encode(open(path, "rb").read()).decode()

        done_id = deluge.call("core.add_torrent_file", ["done.torrent", b64(done_meta), {
            "download_location": done_dir, "add_paused": False}])
        added.append(done_id)
        stream_dir = os.path.join(WORK, "downloads")
        os.makedirs(stream_dir, exist_ok=True)
        stream_id = deluge.call("core.add_torrent_file", ["stream.torrent", b64(stream_meta), {
            "download_location": stream_dir, "add_paused": True, "auto_managed": False}])
        added.append(stream_id)
        start = time.monotonic()
        while status(deluge, done_id)["state"] != "Seeding":
            if time.monotonic() - start > 30:
                raise SystemExit("the seeding torrent never seeded")
            time.sleep(0.2)

        deluge.call("label.add", [LABEL])
        label_made = True
        deluge.call("label.set_options", [LABEL, {
            "apply_move_completed": True, "move_completed": True, "move_completed_path": FAR}])
        print(f"\n1. label {LABEL}: move_completed_path {FAR}")
        for name, tid in (("seeding", done_id), ("unfinished", stream_id)):
            print(f"   {name} before: {options(status(deluge, tid))}")
        deluge.call("label.set_torrent", [done_id, LABEL])
        deluge.call("label.set_torrent", [stream_id, LABEL])
        labelled = time.monotonic()
        for at in (0, 1, 3):
            time.sleep(max(0.0, labelled + at - time.monotonic()))
            for name, tid in (("seeding", done_id), ("unfinished", stream_id)):
                print(f"   {at} s {name}: {options(status(deluge, tid))}")

        print("\n2. the unfinished one resumed, Transmission its peer")
        added_peer = transmission.call("torrent_add", {"metainfo": b64(stream_meta), "download_dir": peer_dir})
        seeder = (added_peer.get("torrent_added") or added_peer.get("torrent_duplicate"))["hash_string"]
        transmission.call("torrent_set", {"ids": [seeder], "upload_limited": True, "upload_limit": 2048})
        peer_port = transmission.call("session_get", {"fields": ["peer_port"]})["peer_port"]
        start = time.monotonic()
        while transmission.torrent(seeder, ["status"])["status"] != 6:
            if time.monotonic() - start > 30:
                raise SystemExit("Transmission never seeded")
            time.sleep(0.3)
        deluge.call("core.resume_torrent", [stream_id])
        deluge.call("core.connect_peer", [stream_id, "127.0.0.1", peer_port])
        start = time.monotonic()
        last, transit = None, []
        while time.monotonic() - start < 60:
            s = status(deluge, stream_id)
            reading = (s["state"], round(s["progress"], 1), s["is_finished"], s["save_path"])
            if reading != last:
                print(f"   {time.monotonic() - start:5.1f} s  state {s['state']}  progress "
                      f"{s['progress']:.1f}  is_finished {s['is_finished']}  save_path {s['save_path']}")
                last = reading
            if s["is_finished"] and s["save_path"] != FAR:
                transit.append(s["save_path"])
            if s["is_finished"] and s["save_path"] == FAR and s["state"] == "Seeding":
                break
            time.sleep(0.2)
        else:
            print("   not landed within 60 s")
        print(f"   finished readings with save_path still the old folder: {len(transit)}"
              + (f" ({transit[0]})" if transit else ""))
        print(f"   after: {options(status(deluge, stream_id))}")

        print("\n3. the label off, then removed")
        deluge.call("label.set_torrent", [done_id, ""])
        time.sleep(1)
        print(f"   seeding, label off: label {status(deluge, done_id)['label']!r}  "
              f"{options(status(deluge, done_id))}")
        deluge.call("label.remove", [LABEL])
        label_made = False
        ui = deluge.call("web.update_ui", [["label", "save_path"], {}])
        tree = [name for name, _count in ui["filters"].get("label", [])]
        print(f"   label.remove, one web.update_ui reading: the unfinished one's label "
              f"{ui['torrents'][stream_id]['label']!r}; {LABEL} in the filter tree {LABEL in tree}")
        print(f"   its options after: {options(status(deluge, stream_id))}")
    finally:
        if added:
            deluge.call("core.remove_torrents", [added, True])
        if label_made:
            deluge.call("label.remove", [LABEL])
        if seeder:
            transmission.remove(seeder, delete=False)
        shutil.rmtree(WORK, ignore_errors=True)


if __name__ == "__main__":
    if sys.argv[1:] != ["check"]:
        raise SystemExit(__doc__)
    check()
