#!/usr/bin/env python3
"""#271: the temp-folder facts board 64 / DECISIONS C47 rests on, asked of the playgrounds.

Usage: scripts/dev/research/271-temp-folder/check.py [qbt|deluge|transmission ...]
       (no argument: all three)

Needs the servers of scripts/dev/test-server.sh up: the playground qBittorrent on :8081
(`start`), Transmission on :8085 (`start --transmission`), Deluge on :8086 (`start --deluge`,
DELUGE_TEST_PASS from the environment or ~/.config/omaseed/dev.env). Never :8080.

Every preference it changes is read first and put back at the end, every torrent it adds is
removed with its files, and its folders live under ~/.local/share/omaseed-test/research-271/
(the payloads and the save folders) and /tmp/omaseed-research-271/ (a temp folder on another
filesystem, for the free-space question). Prints what each server answered, as JSON lines.
"""

import base64
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "153-engines-board47"))
import rpc  # noqa: E402  (Transmission's and deluge-web's clients, make_torrent, shown)

QBT = os.environ.get("OMASEED_QBT", "http://127.0.0.1:8081")
if ":8080" in QBT:
    raise SystemExit("refusing :8080, the real library")

ROOT = os.path.expanduser("~/.local/share/omaseed-test/research-271")
OTHER_FS = "/tmp/omaseed-research-271"


def say(label, value):
    print(f"{label}: {json.dumps(value, sort_keys=True)}")


def payload(name):
    """A one-file torrent of 64 KiB of random data, its own hash each run."""
    source = os.path.join(ROOT, "payload", name)
    os.makedirs(os.path.join(ROOT, "torrents"), exist_ok=True)
    rpc.random_file(source, 64 << 10)
    return rpc.make_torrent(source, os.path.join(ROOT, "torrents", name + ".torrent"), piece_kib=16)


def df(path):
    stat = os.statvfs(path)
    return stat.f_bavail * stat.f_frsize


# ---------------------------------------------------------------- qBittorrent


def qbt(path, data=None, files=None):
    url = QBT + "/api/v2/" + path
    if files is None:
        body = urllib.parse.urlencode(data).encode() if data is not None else None
        request = urllib.request.Request(url, body)
    else:
        boundary = "omaseed271"
        parts = []
        for key, value in (data or {}).items():
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
        for key, filename in files.items():
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"; '
                         f'filename="{os.path.basename(filename)}"\r\n'
                         'Content-Type: application/x-bittorrent\r\n\r\n'.encode()
                         + open(filename, "rb").read() + b"\r\n")
        parts.append(f"--{boundary}--\r\n".encode())
        request = urllib.request.Request(url, b"".join(parts),
                                         {"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(request, timeout=30) as reply:
        text = reply.read().decode()
    try:
        return json.loads(text)
    except ValueError:
        return text


def info_hash(torrent):
    out = subprocess.run(["transmission-show", torrent], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if "Hash v1:" in line or line.strip().startswith("Hash:"):
            return line.split(":", 1)[1].strip().lower()
    raise RuntimeError("no hash in transmission-show")


def qbt_add(name, **fields):
    torrent = payload(name)
    fields.setdefault("stopped", "true")
    fields.setdefault("savepath", os.path.join(ROOT, "save"))
    answer = qbt("torrents/add", fields, {"torrents": torrent})
    digest = info_hash(torrent)
    row = rpc.wait(lambda: (qbt(f"torrents/info?hashes={digest}") or [None])[0], 10, what="the add")
    return digest, answer, row


def qbt_main():
    version = qbt("app/version")
    api = qbt("app/webapiVersion")
    say("qbt version", {"app": version, "api": api})
    prefs = qbt("app/preferences")
    keep = {k: prefs[k] for k in ("temp_path_enabled", "temp_path", "save_path", "auto_tmm_enabled")}
    say("qbt prefs before (Q6: preallocate_all present?)",
        {"preallocate_all present": "preallocate_all" in prefs, "preallocate_all": prefs.get("preallocate_all"), **{
            k: rpc.shown(v) if isinstance(v, str) else v for k, v in keep.items()}})
    added, categories = [], []
    temp = os.path.join(ROOT, "temp")
    try:
        os.makedirs(os.path.join(ROOT, "save"), exist_ok=True)
        qbt("app/setPreferences", {"json": json.dumps({"temp_path_enabled": True, "temp_path": temp})})

        # Q1: torrents/add's useDownloadPath / downloadPath, temp folder on in the session.
        cases = {
            "q1 no params": {},
            "q1 useDownloadPath=true downloadPath=X": {
                "useDownloadPath": "true", "downloadPath": os.path.join(ROOT, "own-temp")},
            "q1 useDownloadPath=false": {"useDownloadPath": "false"},
            "q1 downloadPath=X alone": {"downloadPath": os.path.join(ROOT, "own-temp-alone")},
        }
        for n, (label, fields) in enumerate(cases.items()):
            digest, answer, row = qbt_add(f"q1-{n}", **fields)
            added.append(digest)
            say(label, {"answer": answer, "download_path": rpc.shown(row["download_path"]),
                        "save_path": rpc.shown(row["save_path"])})
        props = qbt(f"torrents/properties?hash={added[0]}")
        say("q1 properties of 'no params'", {"download_path": rpc.shown(props.get("download_path", "?"))})

        # Q1 with the session's temp folder off: does useDownloadPath=true still give one?
        qbt("app/setPreferences", {"json": json.dumps({"temp_path_enabled": False})})
        digest, answer, row = qbt_add("q1-off", useDownloadPath="true",
                                      downloadPath=os.path.join(ROOT, "own-temp-off"))
        added.append(digest)
        say("q1 session temp off, useDownloadPath=true downloadPath=X",
            {"download_path": rpc.shown(row["download_path"])})
        digest, answer, row = qbt_add("q1-off-none")
        added.append(digest)
        say("q1 session temp off, no params", {"download_path": rpc.shown(row["download_path"])})
        qbt("app/setPreferences", {"json": json.dumps({"temp_path_enabled": True})})

        # Q2: categories in three states, and an add into each (auto TMM on and off).
        states = {
            "omaseed271-own": {"savePath": os.path.join(ROOT, "cat-save"), "downloadPathEnabled": "true",
                               "downloadPath": os.path.join(ROOT, "cat-temp")},
            "omaseed271-no": {"savePath": os.path.join(ROOT, "cat-save"), "downloadPathEnabled": "false"},
            "omaseed271-default": {"savePath": os.path.join(ROOT, "cat-save")},
        }
        for name, fields in states.items():
            categories.append(name)
            say(f"q2 createCategory {name}", qbt("torrents/createCategory", {"category": name, **fields}))
        listed = qbt("torrents/categories")
        for name in states:
            say(f"q2 categories[{name}]", {k: rpc.shown(v) if isinstance(v, str) else v
                                            for k, v in listed[name].items()})
        for name in states:
            for tmm in ("true", "false"):
                digest, answer, row = qbt_add(f"q2-{name}-{tmm}", category=name, autoTMM=tmm)
                added.append(digest)
                say(f"q2 add into {name} autoTMM={tmm}",
                    {"download_path": rpc.shown(row["download_path"]), "save_path": rpc.shown(row["save_path"]),
                     "auto_tmm": row["auto_tmm"]})

        # Q3: which path's free space server_state.free_space_on_disk reports.
        os.makedirs(OTHER_FS, exist_ok=True)
        for label, save, tmp in (("save on home, temp on /tmp", os.path.join(ROOT, "save"), OTHER_FS),
                                 ("save on /tmp, temp on home", OTHER_FS, temp)):
            qbt("app/setPreferences", {"json": json.dumps(
                {"save_path": save, "temp_path": tmp, "temp_path_enabled": True})})
            seen = None
            deadline = time.monotonic() + 75
            while time.monotonic() < deadline:  # the checker runs on its own clock, not per request
                seen = qbt("sync/maindata?rid=0")["server_state"].get("free_space_on_disk")
                if abs(seen - df(save)) < 512 << 20 or abs(seen - df(tmp)) < 512 << 20:
                    if abs(df(save) - df(tmp)) > 1 << 30:
                        break
                time.sleep(5)
            say(f"q3 {label}", {"free_space_on_disk": seen, "df(save)": df(save), "df(temp)": df(tmp),
                                "matches": "save" if abs(seen - df(save)) < 512 << 20 else
                                "temp" if abs(seen - df(tmp)) < 512 << 20 else "neither"})
    finally:
        if added:
            qbt("torrents/delete", {"hashes": "|".join(added), "deleteFiles": "true"})
        if categories:
            qbt("torrents/removeCategories", {"categories": "\n".join(categories)})
        qbt("app/setPreferences", {"json": json.dumps(keep)})
        after = qbt("app/preferences")
        say("qbt prefs restored", all(after[k] == v for k, v in keep.items()))


# ---------------------------------------------------------------- Deluge


def deluge_main():
    web = rpc.Deluge()
    if not web.call("web.connected"):
        hosts = web.call("web.get_hosts")
        web.call("web.connect", [hosts[0][0]])
    say("deluge version", {"daemon": web.call("daemon.get_version")})
    keys = ["download_location", "move_completed", "move_completed_path", "pre_allocate_storage"]
    keep = web.call("core.get_config_values", [keys])
    say("deluge session prefs before", {k: rpc.shown(v) if isinstance(v, str) else v for k, v in keep.items()})
    fields = ["download_location", "move_completed", "move_completed_path", "save_path", "move_on_completed",
              "move_on_completed_path"]
    added = []
    try:
        web.call("core.set_config", [{"move_completed": True, "move_completed_path": os.path.join(ROOT, "dl-done")}])
        cases = {
            "q4 no options (session move_completed on)": {},
            "q4 download_location + move_completed + path": {
                "download_location": os.path.join(ROOT, "dl-temp"), "move_completed": True,
                "move_completed_path": os.path.join(ROOT, "dl-final")},
            "q4 download_location, move_completed=false": {
                "download_location": os.path.join(ROOT, "dl-straight"), "move_completed": False},
        }
        for n, (label, options) in enumerate(cases.items()):
            torrent = payload(f"q4-{n}")
            meta = base64.b64encode(open(torrent, "rb").read()).decode()
            digest = web.call("core.add_torrent_file", [os.path.basename(torrent), meta,
                                                        {"add_paused": True, **options}])
            added.append(digest)
            status = web.call("core.get_torrent_status", [digest, fields])
            say(label, {k: rpc.shown(v) if isinstance(v, str) else v for k, v in status.items()})
    finally:
        for digest in added:
            web.call("core.remove_torrent", [digest, True])
        web.call("core.set_config", [keep])
        say("deluge prefs restored", web.call("core.get_config_values", [keys]) == keep)


# ---------------------------------------------------------------- Transmission


def transmission_main():
    t = rpc.Transmission()
    session = t.call("session_get", {})
    say("transmission version", {"version": session.get("version"), "rpc": session.get("rpc_version_semver")})
    say("q5 session_get incomplete keys", {k: rpc.shown(v) if isinstance(v, str) else v
                                           for k, v in session.items() if "incomplete" in k})
    torrent = payload("q5")
    meta = base64.b64encode(open(torrent, "rb").read()).decode()
    added = None
    try:
        try:
            result = t.call("torrent_add", {"metainfo": meta, "paused": True,
                                            "download_dir": os.path.join(ROOT, "tr-save"),
                                            "incomplete_dir": os.path.join(ROOT, "tr-own-temp")})
            added = (result.get("torrent_added") or result.get("torrent_duplicate"))["hash_string"]
            say("q5 torrent_add with incomplete_dir", "accepted (no error)")
        except RuntimeError as error:
            say("q5 torrent_add with incomplete_dir", f"refused: {error}")
        if added:
            row = t.torrent(added, ["download_dir", "incomplete_dir", "hash_string"])
            say("q5 torrent_get download_dir, incomplete_dir", {k: rpc.shown(v) if isinstance(v, str) else v
                                                                for k, v in row.items()})
    finally:
        if added:
            t.remove(added)


def main():
    wanted = sys.argv[1:] or ["qbt", "deluge", "transmission"]
    os.makedirs(ROOT, exist_ok=True)
    try:
        for name in wanted:
            {"qbt": qbt_main, "deluge": deluge_main, "transmission": transmission_main}[name]()
    finally:
        shutil.rmtree(ROOT, ignore_errors=True)
        shutil.rmtree(OTHER_FS, ignore_errors=True)


if __name__ == "__main__":
    main()
