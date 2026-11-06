#!/usr/bin/env python3
"""Records Transmission's answers for the engine's unit tests (#85, E4):
tests/fixtures/transmission/*.json, read by tst_transmissiontranslate and tst_transmissionengine.

Only the engine test server is meant: `scripts/dev/test-server.sh start --transmission`
(transmission-daemon 4.1 on 127.0.0.1:8085, no login). The script makes its own torrents from
generic bytes with transmission-create, under the daemon's profile, puts them in the states
the tests read (seeding with labels, trackers that fail, a web seed and file priorities; a
stopped one with its own limits; a magnet waiting for metadata; one in a local error after a
move into a read-only folder; one removed), records the answers, and removes everything it
added. The seeded torrents (`test-server.sh seed --transmission`) are left alone.

Paths in the answers are rewritten so no real folder is committed: the daemon's profile reads
/srv/transmission, the home folder /home/user. The session id is a fixed word, and free_space's
sizes are made-up round numbers (they would describe this machine's disk).

    scripts/dev/transmission-fixtures.py [OUT_DIR]   # default tests/fixtures/transmission
"""

import base64
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import time
import urllib.error
import urllib.request

RPC = os.environ.get("OMASEED_TRANSMISSION_RPC", "http://127.0.0.1:8085/transmission/rpc")
if ":8080" in RPC:
    raise SystemExit("refusing :8080, the real library")
PROFILE = os.path.expanduser(
    os.environ.get("OMASEED_TRANSMISSION_PROFILE", "~/.local/share/omaseed-test/transmission"))
HOME = os.path.expanduser("~")
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "tests", "fixtures", "transmission")
WORK = os.path.join(PROFILE, "fixtures-recording")
SESSION_WORD = "FIXTURE-SESSION-ID"
DEAD_TRACKER = "http://127.0.0.1:9/announce"  # discard port: refused at once
DEAD_UDP_TRACKER = "udp://127.0.0.1:9/announce"
WEB_SEED = "http://127.0.0.1:9/seed/"

# Every torrent_get field of the 4.1 spec (docs/rpc-spec.md §3.3) but the deprecated one.
ALL_FIELDS = [
    "activity_date", "added_date", "availability", "bandwidth_priority", "bytes_completed",
    "comment", "corrupt_ever", "creator", "date_created", "desired_available", "done_date",
    "download_dir", "downloaded_ever", "download_limit", "download_limited", "edit_date",
    "error", "error_string", "eta", "eta_idle", "file_count", "files", "file_stats", "group",
    "hash_string", "have_unchecked", "have_valid", "honors_session_limits", "id",
    "is_finished", "is_private", "is_stalled", "labels", "left_until_done", "magnet_link",
    "max_connected_peers", "metadata_percent_complete", "name", "peer_limit", "peers",
    "peers_connected", "peers_from", "peers_getting_from_us", "peers_sending_to_us",
    "percent_complete", "percent_done", "pieces", "piece_count", "piece_size", "priorities",
    "primary_mime_type", "queue_position", "rate_download", "rate_upload", "recheck_progress",
    "seconds_downloading", "seconds_seeding", "seed_idle_limit", "seed_idle_mode",
    "seed_ratio_limit", "seed_ratio_mode", "sequential_download",
    "sequential_download_from_piece", "size_when_done", "start_date", "status",
    "torrent_file", "total_size", "trackers", "tracker_list", "tracker_stats",
    "uploaded_ever", "upload_limit", "upload_limited", "upload_ratio", "wanted", "webseeds",
    "webseeds_sending_to_us",
]


class Rpc:
    def __init__(self):
        self.session = ""
        self.next_id = 0

    def post(self, payload):
        """One POST with the 409 dance; returns (status, headers, parsed JSON or text)."""
        body = json.dumps(payload).encode()
        for _ in range(2):
            request = urllib.request.Request(RPC, body, {
                "Content-Type": "application/json", "X-Transmission-Session-Id": self.session})
            try:
                with urllib.request.urlopen(request, timeout=60) as reply:
                    return reply.status, dict(reply.headers), json.load(reply)
            except urllib.error.HTTPError as error:
                if error.code == 409 and not self.session_retry_used(error):
                    continue
                return error.code, dict(error.headers), error.read().decode("latin-1")
        raise SystemExit("no session id from the daemon")

    def session_retry_used(self, error):
        self.session = error.headers.get("X-Transmission-Session-Id", "")
        return not self.session

    def request(self, method, params=None):
        self.next_id += 1
        payload = {"jsonrpc": "2.0", "method": method, "id": self.next_id}
        if params is not None:
            payload["params"] = params
        return payload

    def answer(self, method, params=None):
        """The whole JSON-RPC answer, error or not."""
        return self.post(self.request(method, params))[2]

    def call(self, method, params=None):
        answer = self.answer(method, params)
        if "error" in answer:
            raise SystemExit(f"{method}: {answer['error']}")
        return answer.get("result", {})


def sanitized(value):
    text = json.dumps(value, ensure_ascii=False)
    text = text.replace(PROFILE, "/srv/transmission").replace(HOME, "/home/user")
    value = json.loads(text)
    result = value.get("result") if isinstance(value, dict) else None
    if isinstance(result, dict):
        if "session_id" in result:
            result["session_id"] = SESSION_WORD
        if "size_bytes" in result and result["size_bytes"] >= 0:
            result["size_bytes"], result["total_size"] = 400_000_000_000, 1_000_000_000_000
    return value


def save(name, value):
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8") as out:
        json.dump(sanitized(value), out, indent=1, ensure_ascii=False, sort_keys=True)
        out.write("\n")
    print("wrote", os.path.relpath(path, REPO))


def pattern(size, seed):
    """Generic bytes, the same every run."""
    block = hashlib.sha256(seed.encode()).digest()
    return (block * (size // len(block) + 1))[:size]


def write(path, size, seed):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(pattern(size, seed))


def create(target, outfile, trackers=(), webseeds=()):
    command = ["transmission-create", "--anonymize", "-s", "16", "-c", "omaseed fixture",
               "-o", outfile]
    for tracker in trackers:
        command += ["-t", tracker]
    for seed in webseeds:
        command += ["-w", seed]
    subprocess.run(command + [target], check=True, capture_output=True)
    with open(outfile, "rb") as torrent:
        return base64.b64encode(torrent.read()).decode()


def wait_for(rpc, ids, test, what, seconds=30):
    deadline = time.time() + seconds
    while time.time() < deadline:
        torrents = rpc.call("torrent_get", {"ids": ids, "fields": ALL_FIELDS})["torrents"]
        if torrents and all(test(t) for t in torrents):
            return torrents
        time.sleep(0.5)
    raise SystemExit(f"timed out waiting for {what}")


def main():
    if shutil.which("transmission-create") is None:
        raise SystemExit("transmission-create is missing (transmission-cli)")
    os.makedirs(OUT, exist_ok=True)
    rpc = Rpc()

    # The first answer: 409 with the session id and the RPC version (K5, K8).
    status, headers, _ = rpc.post(rpc.request("session_get"))
    if status != 409 and not rpc.session:
        raise SystemExit(f"expected a 409 first, got {status}")
    rpc_version = headers.get("X-Transmission-Rpc-Version") or rpc.call(
        "session_get", {"fields": ["rpc_version_semver"]})["rpc_version_semver"]
    save("409.json", {"status": 409, "headers": {
        "X-Transmission-Session-Id": SESSION_WORD, "X-Transmission-Rpc-Version": rpc_version}})

    shutil.rmtree(WORK, ignore_errors=True)
    data = os.path.join(WORK, "data")
    added = []
    try:
        # The pack: three files in two folders, two dead trackers in two tiers, a web seed.
        pack = os.path.join(data, "Fixture Pack")
        write(os.path.join(pack, "disc 1", "track-01.bin"), 40 * 1024, "track-01")
        write(os.path.join(pack, "track-02.bin"), 20 * 1024, "track-02")
        write(os.path.join(pack, "notes.txt"), 3000, "notes")
        pack_meta = create(pack, os.path.join(WORK, "pack.torrent"),
                           [DEAD_TRACKER, DEAD_UDP_TRACKER], [WEB_SEED])
        # A single file whose data is not there: stopped at 0 %, with its own limits.
        single_meta = create(write_temp(WORK, "fixture-single.bin", 48 * 1024),
                             os.path.join(WORK, "single.torrent"), [DEAD_TRACKER])
        # A single file that will go into a local error.
        errdata = os.path.join(WORK, "errdata")
        write(os.path.join(errdata, "fixture-error.bin"), 24 * 1024, "fixture-error")
        error_meta = create(os.path.join(errdata, "fixture-error.bin"),
                            os.path.join(WORK, "error.torrent"))
        # One to remove, for recently_active's `removed`.
        gone_meta = create(write_temp(WORK, "fixture-gone.bin", 8 * 1024),
                           os.path.join(WORK, "gone.torrent"))

        answer = rpc.answer("torrent_add", {
            "metainfo": pack_meta, "download_dir": data, "labels": ["fixtures", "pack"],
            "paused": False})
        save("torrent_add.json", answer)
        pack_id = answer["result"]["torrent_added"]["id"]
        added.append(pack_id)
        save("torrent_add_duplicate.json",
             rpc.answer("torrent_add", {"metainfo": pack_meta, "download_dir": data}))

        single_id = rpc.call("torrent_add", {
            "metainfo": single_meta, "download_dir": os.path.join(WORK, "empty"),
            "labels": ["fixtures"], "paused": True})["torrent_added"]["id"]
        added.append(single_id)
        error_id = rpc.call("torrent_add", {
            "metainfo": error_meta, "download_dir": errdata, "paused": False})["torrent_added"]["id"]
        added.append(error_id)
        magnet_hash = hashlib.sha1(b"omaseed fixture magnet").hexdigest()
        magnet = (f"magnet:?xt=urn:btih:{magnet_hash}&dn=fixture-magnet"
                  f"&tr={urllib.request.quote(DEAD_TRACKER, safe='')}")
        magnet_id = rpc.call("torrent_add", {"filename": magnet, "paused": False,
                                             "download_dir": os.path.join(WORK, "magnet")}
                             )["torrent_added"]["id"]
        added.append(magnet_id)

        # The pack's files: one high, one low, the notes not wanted.
        rpc.call("torrent_set", {"ids": [pack_id], "priority_high": [0], "priority_low": [1],
                                 "files_unwanted": [2]})
        # The single's own limits: 250 kB/s down, ratio 1.5, no idle limit, sequential.
        rpc.call("torrent_set", {"ids": [single_id], "download_limited": True,
                                 "download_limit": 250, "seed_ratio_mode": 1,
                                 "seed_ratio_limit": 1.5, "seed_idle_mode": 2,
                                 "sequential_download": True})

        wait_for(rpc, [pack_id, error_id], lambda t: t["status"] == 6, "the pack and the "
                 "error torrent to seed")
        # Its trackers fail at once (nothing listens on the discard port).
        wait_for(rpc, [pack_id], lambda t: any(
            s["has_announced"] for s in t["tracker_stats"]), "the pack's first announce")

        # The local error: a move into a folder nobody can write (C25 K24).
        readonly = os.path.join(WORK, "readonly")
        os.makedirs(readonly)
        os.chmod(readonly, stat.S_IRUSR | stat.S_IXUSR)
        rpc.call("torrent_set_location", {"ids": [error_id],
                                          "location": os.path.join(readonly, "inside"),
                                          "move": True})
        wait_for(rpc, [error_id], lambda t: t["error"] == 3, "the local error")

        gone_id = rpc.call("torrent_add", {"metainfo": gone_meta, "paused": True,
                                           "download_dir": os.path.join(WORK, "empty")}
                           )["torrent_added"]["id"]
        mine = [pack_id, single_id, error_id, magnet_id]

        save("torrent_get_all.json",
             rpc.answer("torrent_get", {"ids": mine, "fields": ALL_FIELDS}))
        save("torrent_get_table.json", rpc.answer("torrent_get", {
            "ids": mine, "format": "table",
            "fields": ["id", "hash_string", "name", "status", "error", "error_string",
                       "labels", "percent_done"]}))
        rpc.call("torrent_remove", {"ids": [gone_id], "delete_local_data": False})
        # recently_active over this script's torrents only (the seeded ones may be active too).
        recent = rpc.answer("torrent_get", {"ids": "recently_active",
                                            "fields": ["id", "hash_string", "name", "status"]})
        recent["result"]["torrents"] = [
            t for t in recent["result"]["torrents"] if t["id"] in mine]
        recent["result"]["removed"] = [r for r in recent["result"]["removed"] if r == gone_id]
        save("torrent_get_recent.json", recent)

        save("session_get.json", rpc.answer("session_get"))
        save("session_stats.json", rpc.answer("session_stats"))
        save("free_space.json", rpc.answer("free_space", {"path": data}))
        save("free_space_missing.json",
             rpc.answer("free_space", {"path": os.path.join(WORK, "not-there")}))
        save("torrent_rename_path.json", rpc.answer("torrent_rename_path", {
            "ids": [pack_id], "path": "Fixture Pack/notes.txt", "name": "readme.txt"}))
        save("error_comma_label.json",
             rpc.answer("torrent_set", {"ids": [pack_id], "labels": ["a,b"]}))
        save("error_not_absolute.json", rpc.answer("torrent_set_location", {
            "ids": [pack_id], "location": "relative/path", "move": True}))
        save("error_method_not_found.json", rpc.answer("torrent_frobnicate"))
        save("group_get.json", rpc.answer("group_get"))
    finally:
        if added:
            rpc.call("torrent_remove", {"ids": added, "delete_local_data": False})
        readonly = os.path.join(WORK, "readonly")
        if os.path.isdir(readonly):
            os.chmod(readonly, stat.S_IRWXU)
        shutil.rmtree(WORK, ignore_errors=True)


def write_temp(work, name, size):
    """A file made only to build a .torrent from; its torrent is added where it isn't."""
    path = os.path.join(work, "sources", name)
    write(path, size, name)
    return path


if __name__ == "__main__":
    main()
