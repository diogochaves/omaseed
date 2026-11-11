#!/usr/bin/env python3
"""What Transmission 4.1's RPC can do for an engine-neutral export and import (#88).

    scripts/dev/test-server.sh start --transmission
    scripts/dev/research/88-engines-backup/transmission.py [BIG_MIB]

Makes its own private fixture torrent (lib.fixture) under ~/.cache/omaseed-research-88, adds it
to the playground on :8085 several ways, prints what the daemon answers, and removes what it
added. BIG_MIB (default 256) sizes the first file, for the check's timing.
"""

import os
import shutil
import sys
import time
import urllib.parse

from lib import WORK, Transmission, b64, bdecode, fixture, say, wait

BIG = int(sys.argv[1]) if len(sys.argv) > 1 else 256
t = Transmission()
session = t.call("session_get")
say("daemon", f"{session['version']} rpc {session['rpc_version_semver']}")
if "omaseed-test" not in session["config_dir"]:
    raise SystemExit("not the playground")
torrent, ih, data = fixture(big_mib=BIG)
NAME = "omaseed-88-fixture"
added = set()


def add(params):
    answer = t.raw("torrent_add", params)
    result = answer.get("result", {})
    for key in ("torrent_added", "torrent_duplicate"):
        if key in result:
            added.add(result[key]["hash_string"])
    return answer


def remove(info_hash):
    t.call("torrent_remove", {"ids": [info_hash], "delete_local_data": False})
    wait(lambda: t.get(info_hash, ["id"]) is None, 10, what="removed")
    added.discard(info_hash)


def check(info_hash, label):
    """Polls from now until the torrent is neither checking nor waiting to; says what it saw."""
    seen, start, first = [], time.monotonic(), None
    while time.monotonic() - start < 120:
        s = t.get(info_hash, ["status", "recheck_progress", "percent_done", "left_until_done", "error_string"])
        if s["status"] not in seen:
            seen.append(s["status"])
        if s["status"] in (1, 2):
            first = first or time.monotonic()
        elif first or time.monotonic() - start > 1.5:
            break
        time.sleep(0.02)
    took = f"{time.monotonic() - first:.2f} s checking" if first else "never seen checking"
    say(label, f"statuses {seen} · {took} · percent_done {s['percent_done']} · left {s['left_until_done']} · error '{s['error_string']}'")
    return s


try:
    # ---------------------------------------------------------------- import: the add
    say("IMPORT: add paused, into the folder with the data, labels, file choices")
    answer = add({"metainfo": b64(torrent), "paused": True, "download_dir": data,
                  "labels": ["films", "kept"], "files_unwanted": [2], "priority_high": [1],
                  "sequential_download": True, "bandwidth_priority": 1,
                  "download_limit": 111, "download_limited": True, "seed_ratio_limit": 3.5,
                  "seed_ratio_mode": 1, "queue_position": 0})
    say("torrent_add answers", answer)
    after = t.get(ih, ["status", "percent_done", "recheck_progress", "labels", "download_dir", "wanted",
                       "priorities", "sequential_download", "bandwidth_priority", "download_limit",
                       "download_limited", "seed_ratio_limit", "seed_ratio_mode", "queue_position",
                       "is_private", "added_date", "done_date", "have_valid", "have_unchecked"])
    say("right after the add", after)
    check(ih, "did the add check by itself?")
    say("torrent_verify", t.raw("torrent_verify", {"ids": [ih]}))
    check(ih, f"after torrent_verify ({BIG + 3} MiB)")
    say("still paused after the check?", t.get(ih, ["status", "done_date", "is_finished"]))

    say("IMPORT: limits, set right after (torrent_set)")
    say("torrent_set", t.raw("torrent_set", {
        "ids": [ih], "download_limit": 222, "download_limited": True, "upload_limit": 333,
        "upload_limited": True, "honors_session_limits": False, "seed_ratio_limit": 1.5,
        "seed_ratio_mode": 1, "seed_idle_limit": 44, "seed_idle_mode": 1, "labels": ["music"],
        "files_wanted": [2], "priority_low": [0], "sequential_download": False}))
    say("reads back", t.get(ih, ["download_limit", "download_limited", "upload_limit", "upload_limited",
                                "honors_session_limits", "seed_ratio_limit", "seed_ratio_mode",
                                "seed_idle_limit", "seed_idle_mode", "labels", "wanted", "priorities",
                                "sequential_download"]))

    say("IMPORT: the same torrent again (duplicate)")
    say("torrent_add again, other folder and labels", add({"metainfo": b64(torrent), "paused": True,
                                                         "download_dir": "/nowhere", "labels": ["dup"]}))
    say("did the second add change it?", t.get(ih, ["download_dir", "labels"]))

    # ---------------------------------------------------------------- export
    say("EXPORT: the .torrent and the magnet")
    e = t.get(ih, ["torrent_file", "magnet_link", "metainfo", "name", "is_private", "comment", "creator"])
    say("torrent_file", e["torrent_file"])
    say("a 'metainfo' field?", "metainfo" in e)
    path = e["torrent_file"]
    say("readable here, same bytes as added?",
        os.path.exists(path) and open(path, "rb").read() == torrent)
    magnet = urllib.parse.urlsplit(e["magnet_link"])
    say("magnet_link carries", {k: v for k, v in urllib.parse.parse_qs(magnet.query).items()})
    say("is_private", e["is_private"])

    say("EXPORT: trackers changed on the daemon")
    say("torrent_set tracker_list", t.raw("torrent_set", {
        "ids": [ih], "tracker_list": "http://127.0.0.1:9/new/announce\n\nhttp://127.0.0.1:9/backup/announce"}))
    r = t.get(ih, ["tracker_list", "trackers", "torrent_file", "magnet_link"])
    say("tracker_list", r["tracker_list"])
    say("trackers (tier, announce)", [(x["tier"], x["announce"]) for x in r["trackers"]])
    meta, _ = bdecode(open(r["torrent_file"], "rb").read())
    say("the .torrent on disk now announces to", [[u.decode() for u in tier] for tier in meta[b"announce-list"]])
    say("same info hash, same bytes as added?", open(r["torrent_file"], "rb").read() == torrent)

    say("EXPORT: renames (torrent_rename_path), then what a record can read")
    say("rename a file", t.raw("torrent_rename_path", {"ids": [ih], "path": f"{NAME}/sub/b.bin", "name": "B renamed.bin"}))
    say("rename the top folder", t.raw("torrent_rename_path", {"ids": [ih], "path": NAME, "name": "Top Renamed"}))
    r = t.get(ih, ["name", "files", "file_stats", "download_dir", "status", "queue_position", "added_date",
                   "done_date", "start_date", "activity_date", "edit_date", "seconds_seeding", "group",
                   "peer_limit", "is_stalled", "percent_done", "metadata_percent_complete",
                   "primary_mime_type", "file_count", "total_size", "size_when_done", "torrent_file"])
    say("name", r["name"])
    say("files[].name", [f["name"] for f in r["files"]])
    say("file_stats (wanted, priority)", [(f["wanted"], f["priority"]) for f in r["file_stats"]])
    meta, _ = bdecode(open(r["torrent_file"], "rb").read())
    say(".torrent on disk still names", [meta[b"info"][b"name"].decode()] +
        ["/".join(p.decode() for p in f[b"path"]) for f in meta[b"info"][b"files"]])
    say("on disk", sorted(os.path.relpath(os.path.join(d, f), data) for d, _, fs in os.walk(data) for f in fs))
    say("dates, queue, the rest", {k: r[k] for k in ("added_date", "done_date", "start_date", "edit_date",
                                                      "queue_position", "status", "group", "total_size",
                                                      "size_when_done", "metadata_percent_complete")})
    say("asking only for incomplete_dir, category, tags, save_path",
        t.raw("torrent_get", {"ids": [ih], "fields": ["incomplete_dir", "category", "tags", "save_path"]}).get("error"))
    say("session incomplete_dir", {k: session[k] for k in ("incomplete_dir", "incomplete_dir_enabled")})
    remove(ih)

    # ---------------------------------------------------------------- import: renames before the check
    say("IMPORT: data on disk under the RENAMED names, torrent added paused, then renamed")
    say("on disk before the add", sorted(os.path.relpath(os.path.join(d, f), data) for d, _, fs in os.walk(data) for f in fs))
    add({"metainfo": b64(torrent), "paused": True, "download_dir": data})
    check(ih, "added over renamed data, no rename yet")
    say("rename the top folder (old absent, new on disk)",
        t.raw("torrent_rename_path", {"ids": [ih], "path": NAME, "name": "Top Renamed"}))
    say("rename the file (old absent, new on disk)",
        t.raw("torrent_rename_path", {"ids": [ih], "path": "Top Renamed/sub/b.bin", "name": "B renamed.bin"}))
    say("files[].name", [f["name"] for f in t.get(ih, ["files"])["files"]])
    t.call("torrent_verify", {"ids": [ih]})
    check(ih, "verify after the renames")
    say("on disk after", sorted(os.path.relpath(os.path.join(d, f), data) for d, _, fs in os.walk(data) for f in fs))

    say("IMPORT: a rename when both the old and the new name are on disk")
    os.makedirs(os.path.join(data, "Top Renamed", "other"), exist_ok=True)
    open(os.path.join(data, "Top Renamed", "taken.bin"), "wb").write(b"x" * 10)
    say("file onto an existing name", t.raw("torrent_rename_path", {
        "ids": [ih], "path": "Top Renamed/a.bin", "name": "taken.bin"}))
    say("taken.bin is still the 10 bytes?", os.path.getsize(os.path.join(data, "Top Renamed", "taken.bin")) == 10)
    say("the torrent's files now", [f["name"] for f in t.get(ih, ["files"])["files"]])
    say("on disk now", sorted(os.path.relpath(os.path.join(p, f), data) for p, _, fs in os.walk(data) for f in fs))
    say("into a subfolder (a path as the name)", t.raw("torrent_rename_path", {
        "ids": [ih], "path": "Top Renamed/a.bin", "name": "other/a.bin"}))

    say("IMPORT: queue order")
    other, ih2, _ = fixture(name="omaseed-88-second", big_mib=1, private=False)
    add({"metainfo": b64(other), "paused": True, "download_dir": data, "queue_position": 0})
    say("positions as added (the second asked for 0 in its add)", [(x["name"], x["queue_position"]) for x in t.call(
        "torrent_get", {"ids": [ih, ih2], "fields": ["name", "queue_position"]})["torrents"]])
    say("torrent_set queue_position 0 on the second", t.raw("torrent_set", {"ids": [ih2], "queue_position": 0}))
    say("positions now", [(x["name"], x["queue_position"]) for x in t.call(
        "torrent_get", {"ids": [ih, ih2], "fields": ["name", "queue_position"]})["torrents"]])
    say("public torrent's magnet_link", t.get(ih2, ["magnet_link"])["magnet_link"][:200])
    remove(ih2)
    remove(ih)

    # ---------------------------------------------------------------- a magnet without metadata
    say("EXPORT: a magnet that never got its metadata")
    link = "magnet:?xt=urn:btih:" + "88" * 20 + "&dn=never+there&tr=http%3A%2F%2F127.0.0.1%3A9%2Fannounce"
    answer = add({"filename": link, "paused": True, "labels": ["m"]})
    say("torrent_add answers", answer)
    mh = "88" * 20
    m = t.get(mh, ["name", "metadata_percent_complete", "torrent_file", "magnet_link", "total_size",
                   "files", "labels", "tracker_list", "status", "is_private"])
    say("reads", m)
    say("its torrent_file exists?", os.path.exists(m["torrent_file"]) if m["torrent_file"] else "empty")
    remove(mh)

    say("IMPORT: wrong folder (no data), paused, verify")
    empty = os.path.join(WORK, "empty")
    os.makedirs(empty, exist_ok=True)
    add({"metainfo": b64(torrent), "paused": True, "download_dir": empty})
    t.call("torrent_verify", {"ids": [ih]})
    check(ih, "verify with nothing on disk")
    say("files made in the empty folder?", os.listdir(empty))
    say("torrent_set_location move=false to the data", t.raw("torrent_set_location", {"ids": [ih], "location": data, "move": False}))
    time.sleep(0.5)
    check(ih, "after set_location (no verify asked)")
    remove(ih)
finally:
    for left in list(added):
        try:
            remove(left)
        except Exception as error:  # noqa: BLE001
            print("cleanup:", left, error)
    shutil.rmtree(WORK, ignore_errors=True)
    say("left on the daemon", len(t.call("torrent_get", {"fields": ["id"]})["torrents"]))
