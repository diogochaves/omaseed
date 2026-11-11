#!/usr/bin/env python3
"""What Deluge 2.2 can do, through deluge-web's /json, for an engine-neutral export and
import (#88).

    scripts/dev/test-server.sh start --deluge
    scripts/dev/research/88-engines-backup/deluge.py [BIG_MIB] [--trap]

Makes its own private fixture torrent (lib.fixture) under ~/.cache/omaseed-research-88, adds it
to the playground on :8086 several ways, prints what the daemon answers, and removes what it
added (its labels too). The password is DELUGE_TEST_PASS (environment or dev.env), never
printed. BIG_MIB (default 256) sizes the first file, for the check's timing.
"""

import os
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from lib import DELUGE, WORK, Deluge, b64, bdecode, fixture, say, wait

BIG = next((int(a) for a in sys.argv[1:] if a.isdigit()), 256)
d = Deluge()
config = d.call("core.get_config")
say("daemon", f"{d.call('daemon.get_version')} · libtorrent {d.call('core.get_libtorrent_version')}")
if "omaseed-test" not in config["download_location"]:
    raise SystemExit("not the playground")
STATE = os.path.join(os.path.dirname(config["download_location"]), "state")
torrent, ih, data = fixture(big_mib=BIG)
NAME = "omaseed-88-fixture"
added, labels = set(), []


def status(tid, keys):
    return d.call("core.get_torrent_status", [tid, keys])


def remove(tid):
    if d.tried("core.remove_torrent", [tid, False]) is not True:
        print("!! core.remove_torrent answered", d.tried("core.remove_torrent", [tid, False]))
    wait(lambda: not d.call("core.get_torrents_status", [{"id": [tid]}, ["name"]]), 10, what="removed")
    added.discard(tid)


def add(options, blob=None, name="x.torrent"):
    answer = d.tried("core.add_torrent_file", [name, b64(blob or torrent), options])
    if isinstance(answer, str) and not answer.startswith("ERROR"):
        added.add(answer)
        wait(lambda: status(answer, ["state"]), 5, what="listed")
    return answer


def check(tid, label, timeout=120):
    seen, start, first = [], time.monotonic(), None
    while time.monotonic() - start < timeout:
        s = status(tid, ["state", "progress", "paused", "total_done", "message"])
        if s["state"] not in seen:
            seen.append(s["state"])
        if s["state"] in ("Checking", "Allocating"):
            first = first or time.monotonic()
        elif first or time.monotonic() - start > 2:
            break
        time.sleep(0.02)
    took = f"{time.monotonic() - first:.2f} s checking" if first else "never seen checking"
    say(label, f"states {seen} · {took} · progress {s['progress']:.1f} · done {s['total_done']} · '{s['message']}'")


def on_disk():
    return sorted(os.path.relpath(os.path.join(p, f), data) for p, _, fs in os.walk(data) for f in fs)


try:
    # ---------------------------------------------------------------- import: the add
    say("IMPORT: add paused, into the folder with the data, with options")
    options = {"add_paused": True, "download_location": data, "file_priorities": [1, 7, 0],
               "max_download_speed": 111, "max_upload_speed": 222, "stop_at_ratio": True,
               "stop_ratio": 3.5, "remove_at_ratio": False, "sequential_download": True,
               "prioritize_first_last_pieces": True, "move_completed": False, "label": "films"}
    tid = add(options)
    say("core.add_torrent_file answers", tid)
    keys = ["state", "paused", "progress", "save_path", "download_location", "file_priorities",
            "max_download_speed", "max_upload_speed", "stop_at_ratio", "stop_ratio", "remove_at_ratio",
            "sequential_download", "prioritize_first_last_pieces", "move_completed", "move_completed_path",
            "label", "queue", "is_auto_managed", "private", "time_added", "completed_time"]
    say("right after the add", status(tid, keys))
    check(tid, "did the add check by itself?")
    say("core.force_recheck", d.tried("core.force_recheck", [[tid]]))
    check(tid, f"after force_recheck ({BIG + 3} MiB)")
    say("after the check", status(tid, ["state", "paused", "progress", "completed_time", "is_finished", "queue"]))

    say("IMPORT: the label (plugin Label)")
    say("label.set_torrent to a label that isn't there", d.tried("label.set_torrent", [tid, "o88-films"]))
    say("label.add", d.tried("label.add", ["o88-films"]))
    labels.append("o88-films")
    say("label.set_torrent", d.tried("label.set_torrent", [tid, "o88-films"]))
    say("label reads", status(tid, ["label"]))
    say("label.add with capitals and a space", d.tried("label.add", ["O88 Films"]))
    say("label.get_labels", [x for x in d.call("label.get_labels") if "88" in x])
    for made in [x for x in d.call("label.get_labels") if "88" in x and x not in labels]:
        labels.append(made)
    say("label.get_options", d.tried("label.get_options", ["o88-films"]))

    say("IMPORT: limits set right after (core.set_torrent_options)")
    say("set_torrent_options", d.tried("core.set_torrent_options", [[tid], {
        "max_download_speed": 55, "max_upload_speed": -1, "stop_at_ratio": True, "stop_ratio": 1.5,
        "remove_at_ratio": True, "sequential_download": False, "file_priorities": [1, 4, 0],
        "move_completed": True, "move_completed_path": "/tmp/o88-done"}]))
    say("reads back", status(tid, ["max_download_speed", "max_upload_speed", "stop_at_ratio", "stop_ratio",
                                   "remove_at_ratio", "sequential_download", "file_priorities",
                                   "move_completed", "move_completed_path"]))
    d.call("core.set_torrent_options", [[tid], {"move_completed": False, "remove_at_ratio": False}])

    say("IMPORT: the same torrent again (duplicate)")
    say("add_torrent_file again", add({"add_paused": True, "download_location": "/nowhere"}))
    say("core.add_torrent_files (the batch) with it", d.tried("core.add_torrent_files", [[["x.torrent", b64(torrent), {}]]]))

    # ---------------------------------------------------------------- export
    say("EXPORT: every key core.get_torrent_status has")
    everything = status(tid, [])
    say("keys", sorted(everything))
    say("EXPORT: the .torrent and the magnet")
    state_file = os.path.join(STATE, tid + ".torrent")
    say("state/<id>.torrent readable here, same bytes as added?",
        os.path.exists(state_file) and open(state_file, "rb").read() == torrent)
    magnet = d.call("core.get_magnet_uri", [tid])
    say("core.get_magnet_uri carries", urllib.parse.parse_qs(urllib.parse.urlsplit(magnet).query))
    say("web.get_torrent_info(state file)", str(d.tried("web.get_torrent_info", [state_file]))[:300])
    say("web.get_torrent_files", str(d.tried("web.get_torrent_files", [tid]))[:200])
    for path in ("/torrents/" + tid, "/download/" + tid, "/state/" + tid + ".torrent"):
        try:
            code = urllib.request.urlopen(DELUGE.rsplit("/", 1)[0] + path, timeout=5).status
        except urllib.error.HTTPError as error:
            code = error.code
        say(f"GET {path[:24]}…", code)
    say("methods that might hand the metainfo", [m for m in d.call("system.listMethods")
                                                if any(w in m for w in ("export", "metainfo", "filedump", "torrent_file"))])

    say("EXPORT: trackers changed on the daemon")
    say("core.set_torrent_trackers", d.tried("core.set_torrent_trackers", [tid, [
        {"url": "http://127.0.0.1:9/new/announce", "tier": 0},
        {"url": "http://127.0.0.1:9/backup/announce", "tier": 1}]]))
    time.sleep(0.5)
    say("trackers (tier, url)", [(x["tier"], x["url"]) for x in status(tid, ["trackers"])["trackers"]])
    meta, _ = bdecode(open(state_file, "rb").read())
    say("state/<id>.torrent still announces to", [[u.decode() for u in tier] for tier in meta[b"announce-list"]])
    say("magnet now", urllib.parse.parse_qs(urllib.parse.urlsplit(d.call("core.get_magnet_uri", [tid])).query).get("tr"))

    say("EXPORT: renames, then what a record can read")
    say("core.rename_files", d.tried("core.rename_files", [tid, [[1, f"{NAME}/sub/B renamed.bin"]]]))
    say("core.rename_folder", d.tried("core.rename_folder", [tid, NAME + "/", "Top Renamed/"]))
    wait(lambda: status(tid, ["files"])["files"][2]["path"].startswith("Top Renamed/"), 10, what="renamed")
    r = status(tid, ["name", "files", "orig_files", "file_priorities", "save_path", "download_location",
                     "queue", "paused", "state", "time_added", "completed_time", "max_download_speed",
                     "max_upload_speed", "stop_at_ratio", "stop_ratio", "remove_at_ratio",
                     "sequential_download", "private", "label", "is_auto_managed", "total_wanted",
                     "total_size", "storage_mode", "seed_mode", "super_seeding", "shared", "owner"])
    say("name", r["name"])
    say("files[].path", [f["path"] for f in r["files"]])
    say("orig_files[].path", [f["path"] for f in r["orig_files"]])
    say("on disk", on_disk())
    say("the rest", {k: v for k, v in r.items() if k not in ("files", "orig_files", "name")})
    say("incomplete folder: none per torrent; config", {k: config[k] for k in ("move_completed", "move_completed_path", "download_location")})
    remove(tid)

    # ---------------------------------------------------------------- import: renames before the check
    say("IMPORT: data on disk under the RENAMED names; add paused, rename, recheck")
    say("on disk before the add", on_disk())
    tid = add({"add_paused": True, "download_location": data})
    check(tid, "added over renamed data, no rename yet")
    say("made on disk by the add?", on_disk())
    say("core.rename_folder (old absent, new on disk)", d.tried("core.rename_folder", [tid, NAME + "/", "Top Renamed/"]))
    time.sleep(1)
    say("core.rename_files (old absent, new on disk)", d.tried("core.rename_files", [tid, [[1, "Top Renamed/sub/B renamed.bin"]]]))
    time.sleep(1)
    say("files[].path", [f["path"] for f in status(tid, ["files"])["files"]])
    d.call("core.force_recheck", [[tid]])
    check(tid, "recheck after the renames")
    say("on disk after", on_disk())
    remove(tid)

    say("IMPORT: the names given in the add (mapped_files, name)")
    say("mapped_files (JSON keys are strings)", add({"add_paused": True, "download_location": data,
        "mapped_files": {"0": "Top Renamed/a.bin", "1": "Top Renamed/sub/B renamed.bin"}}))
    say("mapped_files as a list of pairs", add({"add_paused": True, "download_location": data,
        "mapped_files": [[0, "Top Renamed/a.bin"]]}))
    tid = add({"add_paused": True, "download_location": data, "name": "Only A Name"})
    r = status(tid, ["name", "files"])
    say("option name alone: name, files", [r["name"]] + [f["path"] for f in r["files"]])
    remove(tid)

    say("IMPORT: seed_mode (no check at all), a new fixture under its own names")
    torrent, ih, data = fixture(big_mib=BIG)
    tid = add({"add_paused": True, "download_location": data, "seed_mode": True})
    check(tid, "add with seed_mode")
    say("reads", status(tid, ["state", "progress", "seed_mode", "is_finished", "completed_time", "queue"]))

    say("IMPORT: queue order")
    other, ih2, _ = fixture(name="omaseed-88-second", big_mib=1, private=False)
    third, ih3, _ = fixture(name="omaseed-88-third", big_mib=1, private=False)
    empty = os.path.join(WORK, "empty")
    os.makedirs(empty, exist_ok=True)
    for blob in (other, third):
        add({"add_paused": True, "download_location": empty}, blob)
    say("queue as added (complete one, two incomplete)", [status(x, ["queue"])["queue"] for x in (tid, ih2, ih3)])
    say("core.queue_top on the third", d.tried("core.queue_top", [[ih3]]))
    time.sleep(0.3)
    say("queue now", [status(x, ["queue"])["queue"] for x in (tid, ih2, ih3)])
    say("public torrent's magnet", d.call("core.get_magnet_uri", [ih2])[:220])
    say("files made in the empty folder by a paused add?", os.listdir(empty))
    for x in (tid, ih2, ih3):
        remove(x)

    # ---------------------------------------------------------------- a magnet without metadata
    say("EXPORT: a magnet that never got its metadata")
    mh = "88" * 20
    link = "magnet:?xt=urn:btih:" + mh + "&dn=never+there&tr=http%3A%2F%2F127.0.0.1%3A9%2Fannounce"
    answer = d.tried("core.add_torrent_magnet", [link, {"add_paused": True}])
    say("core.add_torrent_magnet answers", answer)
    if answer == mh:
        added.add(mh)
        say("reads", status(mh, ["name", "state", "total_size", "num_files", "files", "trackers", "has_metadata",
                                 "progress", "private"]))
        say("core.get_magnet_uri", d.tried("core.get_magnet_uri", [mh]))
        say("state/<id>.torrent exists?", os.path.exists(os.path.join(STATE, mh + ".torrent")))
        remove(mh)

    if "--trap" in sys.argv:
        # A complete torrent with a skipped file, then that file wanted again: Deluge takes it
        # out of "finished" but not back into its queue, and core.remove_torrent fails from
        # then on. Only a restart of the playground gets rid of it, so this is asked for.
        say("TRAP: a skipped file wanted again after the check (needs a daemon restart after)")
        torrent, ih, data = fixture(big_mib=BIG)
        tid = add({"add_paused": True, "download_location": data, "file_priorities": [1, 1, 0]})
        d.call("core.force_recheck", [[tid]])
        check(tid, "recheck, file 2 skipped")
        say("before", status(tid, ["state", "progress", "is_finished", "queue"]))
        d.call("core.set_torrent_options", [[tid], {"file_priorities": [1, 1, 1]}])
        time.sleep(2)
        say("after file_priorities [1, 1, 1]", status(tid, ["state", "progress", "is_finished", "queue"]))
        say("core.remove_torrent", d.tried("core.remove_torrent", [tid, False]))
        time.sleep(1)
        say("core.remove_torrent again", d.tried("core.remove_torrent", [tid, False]))
        added.discard(tid)
        say("now", "scripts/dev/test-server.sh stop --deluge; start --deluge; then remove " + tid)
finally:
    for left in list(added):
        try:
            remove(left)
        except Exception as error:  # noqa: BLE001
            print("cleanup:", left, error)
    for label in labels:
        d.tried("label.remove", [label])
    shutil.rmtree(WORK, ignore_errors=True)
    say("left on the daemon", len(d.call("core.get_torrents_status", [{}, ["name"]])))
    say("labels left", d.call("label.get_labels"))
