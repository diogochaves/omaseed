#!/usr/bin/env python3
"""Records deluge-web's answers for the Deluge engine's unit tests (#87, E6):
tests/fixtures/deluge/*.json, read by tst_delugetranslate and tst_delugeengine.

Only the engines' test servers are meant: `scripts/dev/test-server.sh start --deluge` (deluged
2.2 with deluge-web on 127.0.0.1:8086, the Label plugin on; its password DELUGE_TEST_PASS in
~/.config/omaseed/dev.env) and `start --transmission` (:8085), which seeds one torrent to
Deluge so a download with a real peer can be recorded. The script makes its own torrents from
generic bytes with transmission-create, under Deluge's profile, puts them in the states the
tests read (seeding with a label, file priorities and two dead trackers; a paused one with its
own limits; an error after a move into a read-only folder, and that error after a resume; a
magnet waiting for its metadata; one downloading from Transmission; one queued behind it),
records the answers, and removes everything it added: torrents, labels, the config it changed.
The seeded torrents (`test-server.sh seed --deluge`) are left alone and left out of the answers.

Paths in the answers are rewritten so no real folder is committed: Deluge's profile reads
/srv/deluge, the home folder /home/user. Free space is a made-up round number (it would describe
this machine's disk). deluge-web's session cookie never appears in an answer.

    scripts/dev/deluge-fixtures.py [OUT_DIR]   # default tests/fixtures/deluge
"""

import base64
import hashlib
import http.cookiejar
import json
import os
import shutil
import stat
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DELUGE = os.environ.get("OMASEED_DELUGE_JSON", "http://127.0.0.1:8086/json")
TRANSMISSION = os.environ.get("OMASEED_TRANSMISSION_RPC", "http://127.0.0.1:8085/transmission/rpc")
for url in (DELUGE, TRANSMISSION):
    if ":8080" in url:
        raise SystemExit("refusing :8080, the real library")
PROFILE = os.path.expanduser(
    os.environ.get("OMASEED_DELUGE_PROFILE", "~/.local/share/omaseed-test/deluge"))
HOME = os.path.expanduser("~")
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "tests", "fixtures", "deluge")
WORK = os.path.join(PROFILE, "fixtures-recording")
DEAD_TRACKER = "http://127.0.0.1:9/announce"  # discard port: refused at once
DEAD_UDP_TRACKER = "udp://127.0.0.1:9/announce"
FAKE_FREE = 400_000_000_000

# The poll's keys: DelugeTranslate::listFields() (keep the two alike).
LIST_FIELDS = [
    "name", "state", "total_size", "total_wanted", "progress", "download_payload_rate",
    "upload_payload_rate", "eta", "num_seeds", "total_seeds", "num_peers", "total_peers", "ratio",
    "label", "save_path", "move_completed", "move_completed_path", "is_finished", "paused",
    "time_added", "completed_time", "time_since_transfer", "tracker", "tracker_status", "queue",
    "sequential_download", "prioritize_first_last", "super_seeding", "is_auto_managed", "private",
    "max_download_speed", "max_upload_speed", "stop_at_ratio", "stop_ratio", "remove_at_ratio",
    "distributed_copies", "total_remaining", "total_done", "all_time_download", "total_uploaded",
    "total_payload_upload", "active_time", "seeding_time", "message", "num_pieces",
]
# What the details read besides: DelugeTranslate's other field lists.
DETAIL_FIELDS = LIST_FIELDS + [
    "hash", "trackers", "files", "file_priorities", "file_progress", "piece_length", "pieces",
    "peers", "comment", "creator", "num_files", "max_connections", "last_seen_complete",
    "next_announce", "total_payload_download", "download_location",
]
SESSION_KEYS = ["payload_download_rate", "payload_upload_rate", "total_payload_download",
                "total_payload_upload", "dht_nodes", "num_peers", "has_incoming_connections"]
CONFIG_KEYS = ["max_download_speed", "max_upload_speed", "download_location", "listen_ports",
               "dht", "stop_seed_at_ratio", "stop_seed_ratio", "remove_seed_at_ratio",
               "move_completed", "move_completed_path", "max_active_downloading"]


class Deluge:
    """One deluge-web session, its cookie in its own jar."""

    def __init__(self):
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))
        self.next_id = 0

    def answer(self, method, params=None, timeout=60):
        """The whole answer, error or not."""
        self.next_id += 1
        body = json.dumps({"method": method, "params": params or [], "id": self.next_id}).encode()
        request = urllib.request.Request(DELUGE, body, {"Content-Type": "application/json"})
        with self.opener.open(request, timeout=timeout) as reply:
            return json.load(reply)

    def call(self, method, params=None, timeout=60):
        answer = self.answer(method, params, timeout)
        if answer.get("error"):
            raise SystemExit(f"{method}: {answer['error']}")
        return answer["result"]


def transmission(method, params=None, session=[""]):  # noqa: B006 (the session id is kept)
    body = json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode()
    for _ in range(2):
        request = urllib.request.Request(TRANSMISSION, body, {
            "Content-Type": "application/json", "X-Transmission-Session-Id": session[0]})
        try:
            with urllib.request.urlopen(request, timeout=60) as reply:
                answer = json.load(reply)
        except urllib.error.HTTPError as error:
            if error.code == 409:
                session[0] = error.headers.get("X-Transmission-Session-Id", "")
                continue
            raise
        if "error" in answer:
            raise SystemExit(f"Transmission {method}: {answer['error']}")
        return answer.get("result", {})
    raise SystemExit("no session id from Transmission")


def password():
    value = os.environ.get("DELUGE_TEST_PASS", "")
    if value:
        return value
    path = os.environ.get("OMASEED_DEV_ENV", os.path.expanduser("~/.config/omaseed/dev.env"))
    try:
        for line in open(path, encoding="utf-8"):
            key, _, found = line.strip().removeprefix("export ").partition("=")
            if key == "DELUGE_TEST_PASS":
                value = found
    except FileNotFoundError:
        pass
    if not value:
        raise SystemExit("no DELUGE_TEST_PASS: scripts/dev/test-server.sh start --deluge makes one")
    return value


def sanitized(value, free_space=False):
    text = json.dumps(value, ensure_ascii=False)
    text = text.replace(PROFILE, "/srv/deluge").replace(HOME, "/home/user")
    value = json.loads(text)
    result = value.get("result") if isinstance(value, dict) else None
    if free_space and isinstance(result, int) and result >= 0:
        value["result"] = FAKE_FREE
    if isinstance(result, dict):
        stats = result.get("stats")
        if isinstance(stats, dict) and isinstance(stats.get("free_space"), int) and stats["free_space"] >= 0:
            stats["free_space"] = FAKE_FREE
    return value


def save(name, value):
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8") as out:
        json.dump(sanitized(value, name.startswith("core_get_free_space")), out, indent=1, ensure_ascii=False, sort_keys=True)
        out.write("\n")
    print("wrote", os.path.relpath(path, REPO))


def only(answer, hashes):
    """An answer whose result is keyed by hash, kept to `hashes` (the seeded torrents go)."""
    result = answer.get("result")
    if isinstance(result, dict):
        torrents = result.get("torrents") if "torrents" in result else result
        if isinstance(torrents, dict):
            for key in list(torrents):
                if key not in hashes:
                    del torrents[key]
    return answer


def pattern(size, seed):
    """Generic bytes, the same every run."""
    block = hashlib.sha256(seed.encode()).digest()
    return (block * (size // len(block) + 1))[:size]


def write(path, size, seed):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(pattern(size, seed))
    return path


def create(target, outfile, trackers=(), piece_kib=16):
    command = ["transmission-create", "--anonymize", "-s", str(piece_kib), "-c", "omaseed fixture",
               "-o", outfile]
    for tracker in trackers:
        command += ["-t", tracker]
    subprocess.run(command + [target], check=True, capture_output=True)
    with open(outfile, "rb") as torrent:
        return base64.b64encode(torrent.read()).decode()


def wait_for(deluge, hashes, test, what, seconds=60):
    deadline = time.time() + seconds
    while time.time() < deadline:
        status = deluge.call("core.get_torrents_status", [{"id": hashes}, DETAIL_FIELDS])
        if len(status) == len(hashes) and all(test(status[h]) for h in hashes):
            return status
        time.sleep(0.5)
    raise SystemExit(f"timed out waiting for {what}")


def main():
    if shutil.which("transmission-create") is None:
        raise SystemExit("transmission-create is missing (transmission-cli)")
    os.makedirs(OUT, exist_ok=True)
    deluge = Deluge()

    # Before the sign-in: the one method deluge-web answers, and what any other gets.
    save("auth_check_session.json", deluge.answer("auth.check_session"))
    save("not_authenticated.json", deluge.answer("web.connected"))
    save("auth_login_refused.json", deluge.answer("auth.login", ["not-the-password"]))
    save("auth_login.json", deluge.answer("auth.login", [password()]))
    save("web_connected.json", deluge.answer("web.connected"))
    hosts = deluge.answer("web.get_hosts")
    save("web_get_hosts.json", hosts)
    host_id = hosts["result"][0][0]
    save("web_get_host_status.json", deluge.answer("web.get_host_status", [host_id]))
    save("daemon_get_version.json", deluge.answer("daemon.get_version"))
    save("core_get_libtorrent_version.json", deluge.answer("core.get_libtorrent_version"))
    save("core_get_enabled_plugins.json", deluge.answer("core.get_enabled_plugins"))
    save("unknown_method.json", deluge.answer("core.frobnicate"))

    config_before = deluge.call("core.get_config_values", [CONFIG_KEYS])
    shutil.rmtree(WORK, ignore_errors=True)
    data = os.path.join(WORK, "data")
    added = []
    transmission_hash = None
    labels_added = []
    try:
        # The pack: three files in two folders, two dead trackers in two tiers.
        pack = os.path.join(data, "Fixture Pack")
        write(os.path.join(pack, "disc 1", "track-01.bin"), 40 * 1024, "track-01")
        write(os.path.join(pack, "track-02.bin"), 20 * 1024, "track-02")
        write(os.path.join(pack, "notes.txt"), 3000, "notes")
        pack_meta = create(pack, os.path.join(WORK, "pack.torrent"), [DEAD_TRACKER, DEAD_UDP_TRACKER])
        # A single file whose data is not there: paused at 0 %, with its own limits.
        single_meta = create(write(os.path.join(WORK, "sources", "fixture-single.bin"), 48 * 1024,
                                   "fixture-single"), os.path.join(WORK, "single.torrent"),
                             [DEAD_TRACKER])
        # A single file that will go into an error.
        errdata = os.path.join(WORK, "errdata")
        error_meta = create(write(os.path.join(errdata, "fixture-error.bin"), 24 * 1024,
                                  "fixture-error"), os.path.join(WORK, "error.torrent"))
        # A file Transmission seeds and Deluge downloads, slowly enough to be seen doing it.
        seed_dir = os.path.join(WORK, "seeder")
        stream = write(os.path.join(seed_dir, "fixture-stream.bin"), 6 << 20, "fixture-stream")
        stream_meta = create(stream, os.path.join(WORK, "stream.torrent"), piece_kib=64)
        # One more without data, queued behind the download.
        queued_meta = create(write(os.path.join(WORK, "sources", "fixture-queued.bin"), 32 * 1024,
                                   "fixture-queued"), os.path.join(WORK, "queued.torrent"),
                             [DEAD_TRACKER])

        # Labels: one with a folder for torrents that finish, one with none (C25 B).
        deluge.call("label.add", ["fixtures"])
        labels_added.append("fixtures")
        deluge.call("label.set_options", ["fixtures", {
            "apply_move_completed": True, "move_completed": True,
            "move_completed_path": os.path.join(WORK, "done")}])
        deluge.call("label.add", ["empty"])
        labels_added.append("empty")
        save("label_get_labels.json", deluge.answer("label.get_labels"))
        save("label_get_options.json", deluge.answer("label.get_options", ["fixtures"]))
        save("label_get_options_empty.json", deluge.answer("label.get_options", ["empty"]))
        save("error_label_invalid.json", deluge.answer("label.add", ["Not Valid!"]))
        save("error_label_unknown.json", deluge.answer("label.set_torrent", ["0" * 40, "nolabel"]))

        answer = deluge.answer("core.add_torrent_file", ["pack.torrent", pack_meta, {
            "download_location": data, "add_paused": False}])
        save("core_add_torrent_file.json", answer)
        pack_id = answer["result"]
        added.append(pack_id)
        save("error_add_duplicate.json", deluge.answer("core.add_torrent_file", [
            "pack.torrent", pack_meta, {"download_location": data}]))
        deluge.call("label.set_torrent", [pack_id, "fixtures"])
        # The pack's files: one high, one low, the notes skipped.
        deluge.call("core.set_torrent_file_priorities", [pack_id, [7, 1, 0]])

        single_id = deluge.call("core.add_torrent_file", ["single.torrent", single_meta, {
            "download_location": os.path.join(WORK, "empty"), "add_paused": True}])
        added.append(single_id)
        # The single's own limits: 250 KiB/s down, stop at ratio 1.5 and remove (keeping the
        # files), sequential.
        deluge.call("core.set_torrent_options", [[single_id], {
            "max_download_speed": 250.0, "stop_at_ratio": True, "stop_ratio": 1.5,
            "remove_at_ratio": True, "sequential_download": True}])

        error_id = deluge.call("core.add_torrent_file", ["error.torrent", error_meta, {
            "download_location": errdata, "add_paused": False}])
        added.append(error_id)
        magnet_hash = hashlib.sha1(b"omaseed fixture magnet").hexdigest()
        magnet = (f"magnet:?xt=urn:btih:{magnet_hash}&dn=fixture-magnet"
                  f"&tr={urllib.parse.quote(DEAD_TRACKER, safe='')}")
        answer = deluge.answer("core.add_torrent_magnet", [magnet, {
            "download_location": os.path.join(WORK, "magnet")}])
        save("core_add_torrent_magnet.json", answer)
        magnet_id = answer["result"]
        added.append(magnet_id)
        # Started past the queue (matrix §4.5: auto_managed off), so the queue below leaves it.
        deluge.call("core.set_torrent_options", [[magnet_id], {"auto_managed": False}])
        save("error_add_v2_magnet.json", deluge.answer("core.add_torrent_magnet", [
            "magnet:?xt=urn:btmh:1220" + "ab" * 32 + "&dn=v2-only", {}]))

        wait_for(deluge, [pack_id, error_id], lambda t: t["state"] == "Seeding",
                 "the pack and the error torrent to seed")
        wait_for(deluge, [pack_id], lambda t: t["tracker_status"].startswith("Error"),
                 "the pack's first announce")

        # The error: a move into a folder that is there but nobody can write (C25 K24, §F).
        # Into one Deluge would have to make, it refuses before moving and says so only in
        # its log: the torrent stays where it was (research/87-deluge-prefetch).
        readonly = os.path.join(WORK, "readonly")
        os.makedirs(readonly)
        os.chmod(readonly, stat.S_IRUSR | stat.S_IXUSR)
        deluge.call("core.move_storage", [[error_id], readonly])
        wait_for(deluge, [error_id], lambda t: t["state"] == "Error", "the move's error")

        # Transmission seeds the stream, slowly; Deluge downloads it from there.
        seeder = transmission("torrent_add", {"metainfo": stream_meta, "download_dir": seed_dir})
        transmission_hash = (seeder.get("torrent_added") or seeder.get("torrent_duplicate"))["hash_string"]
        transmission("torrent_set", {"ids": [transmission_hash], "upload_limited": True,
                                     "upload_limit": 96})
        peer_port = transmission("session_get", {"fields": ["peer_port"]})["peer_port"]
        deadline = time.time() + 30
        while transmission("torrent_get", {"ids": [transmission_hash], "fields": ["status"]}
                           )["torrents"][0]["status"] != 6:
            if time.time() > deadline:
                raise SystemExit("Transmission never seeded the stream")
            time.sleep(0.3)
        stream_id = deluge.call("core.add_torrent_file", ["stream.torrent", stream_meta, {
            "download_location": os.path.join(WORK, "stream"), "auto_managed": False}])
        added.append(stream_id)
        deluge.call("core.connect_peer", [stream_id, "127.0.0.1", peer_port])
        wait_for(deluge, [stream_id], lambda t: t["state"] == "Downloading" and t["peers"]
                 and t["download_payload_rate"] > 0 and t["progress"] > 5, "the download", 120)
        # No download slot for anyone the queue manages: the next one waits.
        deluge.call("core.set_config", [{"max_active_downloading": 0}])
        queued_id = deluge.call("core.add_torrent_file", ["queued.torrent", queued_meta, {
            "download_location": os.path.join(WORK, "empty")}])
        added.append(queued_id)
        wait_for(deluge, [queued_id], lambda t: t["state"] == "Queued", "the queue")

        mine = [pack_id, single_id, error_id, magnet_id, stream_id, queued_id]
        save("web_update_ui.json", only(deluge.answer("web.update_ui", [LIST_FIELDS, {}]), mine))
        save("core_get_torrents_status.json",
             deluge.answer("core.get_torrents_status", [{"id": mine}, DETAIL_FIELDS]))
        save("core_get_torrent_status_unknown.json",
             deluge.answer("core.get_torrent_status", ["0" * 40, ["name"]]))
        save("core_get_session_status.json", deluge.answer("core.get_session_status", [SESSION_KEYS]))
        save("core_get_config_values.json", deluge.answer("core.get_config_values", [CONFIG_KEYS]))
        save("core_get_free_space.json", deluge.answer("core.get_free_space", [data]))
        save("core_get_free_space_missing.json",
             deluge.answer("core.get_free_space", [os.path.join(WORK, "not-there")]))
        save("core_glob.json", deluge.answer("core.glob", [os.path.join(data, "*")]))
        save("core_get_path_size_missing.json",
             deluge.answer("core.get_path_size", [os.path.join(WORK, "not-there")]))
        save("core_remove_torrents_missing.json",
             deluge.answer("core.remove_torrents", [["0" * 40], False]))
        save("core_prefetch_empty.json", deluge.answer("core.prefetch_magnet_metadata", [
            "magnet:?xt=urn:btih:" + hashlib.sha1(b"nobody has this").hexdigest(), 2]))
        save("error_prefetch_v2.json", deluge.answer("core.prefetch_magnet_metadata", [
            "magnet:?xt=urn:btmh:1220" + "ab" * 32, 2]))

        # A resume doesn't clear a failed move's error (matrix §4.4: only a recheck does).
        deluge.call("core.resume_torrents", [[error_id]])
        time.sleep(2)
        save("core_get_torrent_status_error_resumed.json", deluge.answer(
            "core.get_torrent_status", [error_id, ["state", "message", "paused"]]))

        # The Label plugin turned off: its key and filter go (C25 B, K15).
        deluge.call("core.disable_plugin", ["Label"])
        time.sleep(1)
        save("web_update_ui_no_label.json",
             only(deluge.answer("web.update_ui", [LIST_FIELDS, {}]), mine))
        save("core_get_enabled_plugins_none.json", deluge.answer("core.get_enabled_plugins"))
        deluge.call("core.enable_plugin", ["Label"])
        time.sleep(1)

        # deluge-web with no daemon: the poll says so (a core call would never answer).
        deluge.call("web.disconnect")
        save("web_update_ui_disconnected.json", deluge.answer("web.update_ui", [LIST_FIELDS, {}]))
        save("web_connected_false.json", deluge.answer("web.connected"))
        connect = deluge.answer("web.connect", [host_id])
        connect["result"] = sorted(connect["result"])
        save("web_connect.json", connect)
    finally:
        deluge.call("core.set_config", [{"max_active_downloading": config_before["max_active_downloading"]}])
        if added:
            deluge.call("core.remove_torrents", [added, True])
        for label in labels_added:
            deluge.answer("label.remove", [label])
        if transmission_hash:
            transmission("torrent_remove", {"ids": [transmission_hash], "delete_local_data": False})
        readonly = os.path.join(WORK, "readonly")
        if os.path.isdir(readonly):
            os.chmod(readonly, stat.S_IRWXU)
        shutil.rmtree(WORK, ignore_errors=True)


if __name__ == "__main__":
    main()
