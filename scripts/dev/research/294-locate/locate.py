#!/usr/bin/env python3
"""#294: can each engine re-point a torrent at a folder that already holds its files, moving
nothing? (DECISIONS C50 GT2, `do locate`; EngineClient::pointAt.)

Usage: scripts/dev/research/294-locate/locate.py [qbt|transmission|deluge ...]
       (no argument: all three)

Needs the servers of scripts/dev/test-server.sh up: the playground qBittorrent on :8081
(`start`), Transmission on :8085 (`start --transmission`), Deluge on :8086 (`start --deluge`,
DELUGE_TEST_PASS from the environment or ~/.config/omaseed/dev.env). Never :8080.

Two cases per engine, each on a fresh two-file torrent made here and verified complete at
OLD (the files are already there, so it seeds at once, no swarm needed):

  gone: the files are moved by hand from OLD to NEW (so OLD is missing), the engine is told
        (a recheck: what a missing-files torrent looks like), then the re-point call is sent
        with NEW. Read: the answer, the state before a recheck, the save path; then a recheck
        and its progress. Every file at NEW keeps its inode, mtime and sha256; nothing comes
        back at OLD.
  both: the files are at OLD and a copy at NEW (different bytes in one file at NEW, so an
        overwrite would show), then the re-point call. Neither side may lose or change a
        file: OLD's and NEW's inodes, mtimes and sha256 are compared before and after.

The calls: qBittorrent `torrents/setLocation`, Transmission `torrent_set_location` with
`move: false`, Deluge `core.move_storage`. Every torrent it adds is removed (without its data,
then its folders deleted), all under ~/.local/share/omaseed-test/research-294/. Prints what
each server answered and what the disk showed, as JSON lines; `verdict` lines sum it up.
"""

import base64
import json
import os
import shutil
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "153-engines-board47"))
import rpc  # noqa: E402  (Transmission's and deluge-web's clients, make_torrent, wait)

QBT = os.environ.get("OMASEED_QBT", "http://127.0.0.1:8081")
if ":8080" in QBT:
    raise SystemExit("refusing :8080, the real library")

ROOT = os.path.expanduser("~/.local/share/omaseed-test/research-294")
FILES = {"a.bin": 192 << 10, "sub/b.bin": 96 << 10}


def say(label, value):
    print(f"{label}: {json.dumps(value, sort_keys=True)}", flush=True)


def shown(value):
    return value.replace(ROOT, "<root>") if isinstance(value, str) else value


def make(case):
    """A two-file torrent `<case>` complete at ROOT/<case>/old, its .torrent beside it."""
    base = os.path.join(ROOT, case)
    old, new = os.path.join(base, "old"), os.path.join(base, "new")
    content = os.path.join(old, case)
    for rel, size in FILES.items():
        rpc.random_file(os.path.join(content, rel), size)
    torrent = rpc.make_torrent(content, os.path.join(base, case + ".torrent"), piece_kib=16)
    return old, new, torrent


def snapshot(folder, case):
    """inode, mtime_ns and sha256 of each payload file under folder/case, or None if absent."""
    seen = {}
    for rel in FILES:
        path = os.path.join(folder, case, rel)
        if os.path.exists(path):
            st = os.stat(path)
            seen[rel] = [st.st_ino, st.st_mtime_ns, rpc.sha256(path)]
        else:
            seen[rel] = None
    return seen


def poll(view, done, timeout=10):
    """view() until done(view) holds or timeout passes (the engines' state lags their calls by
    a second or two); returns the last view either way."""
    deadline = time.monotonic() + timeout
    seen = view()
    while not done(seen) and time.monotonic() < deadline:
        time.sleep(0.25)
        seen = view()
    return seen


def move_by_hand(old, new, case):
    os.makedirs(new, exist_ok=True)
    os.rename(os.path.join(old, case), os.path.join(new, case))


def copy_by_hand(old, new, case):
    """NEW gets a copy of OLD's files, with one byte of a.bin changed: an overwrite would show."""
    shutil.copytree(os.path.join(old, case), os.path.join(new, case))
    path = os.path.join(new, case, "a.bin")
    with open(path, "r+b") as f:
        first = f.read(1)
        f.seek(0)
        f.write(bytes([first[0] ^ 0xFF]))


def judge(engine, case, before_new, after_new, before_old=None, after_old=None):
    untouched_new = before_new == after_new
    verdict = {"new untouched": untouched_new}
    if before_old is not None:
        verdict["old untouched"] = before_old == after_old
    else:
        verdict["nothing back at old"] = all(v is None for v in after_old.values())
    verdict["safe"] = all(verdict.values())
    say(f"verdict {engine} {case}", verdict)
    return verdict["safe"]


# ---------------------------------------------------------------- qBittorrent


def qbt(path, data=None, files=None):
    url = QBT + "/api/v2/" + path
    if files is None:
        body = urllib.parse.urlencode(data).encode() if data is not None else None
        request = urllib.request.Request(url, body)
    else:
        boundary = "omaseed294"
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
        status, text = reply.status, reply.read().decode()
    try:
        return json.loads(text)
    except ValueError:
        return text or f"HTTP {status}"


def qbt_row(digest):
    rows = qbt(f"torrents/info?hashes={digest}")
    return rows[0] if rows else None


def qbt_view(digest):
    row = qbt_row(digest)
    return {"state": row["state"], "progress": row["progress"], "save_path": shown(row["save_path"]),
            "auto_tmm": row["auto_tmm"]}


def qbt_calm(view):
    return not view["state"].startswith(("checking", "moving", "metaDL", "allocating"))


def qbt_until(digest, progress=None, timeout=10):
    return poll(lambda: qbt_view(digest),
                lambda v: qbt_calm(v) and (progress is None or v["progress"] == progress), timeout)


def qbt_add(torrent, save):
    answer = qbt("torrents/add", {"savepath": save, "stopped": "true", "paused": "true", "autoTMM": "false"},
                 {"torrents": torrent})
    name = os.path.splitext(os.path.basename(torrent))[0]

    def found():
        for row in qbt("torrents/info"):
            if row["name"] == name:
                return row["hash"]
        return None
    digest = rpc.wait(found, 15, 0.2, "the add")
    qbt("torrents/recheck", {"hashes": digest})
    say("qbt added", {"answer": answer, **qbt_until(digest, progress=1)})
    return digest


def qbt_case(case):
    old, new, torrent = make(f"qbt-{case}")
    name = f"qbt-{case}"
    digest = qbt_add(torrent, old)
    try:
        if case in ("gone", "rechecked"):
            move_by_hand(old, new, name)
            if case == "gone":
                qbt("torrents/start", {"hashes": digest})
                say("qbt gone: started with the files gone",
                    poll(lambda: qbt_view(digest), lambda v: v["state"] in ("missingFiles", "error"), 5))
                qbt("torrents/stop", {"hashes": digest})
                say("qbt gone: stopped", poll(lambda: qbt_view(digest), lambda v: v["state"].startswith("stopped")))
            else:
                qbt("torrents/recheck", {"hashes": digest})
                say("qbt rechecked: after the hand move + recheck", qbt_until(digest, progress=0))
            say(f"qbt {case}: old after that", snapshot(old, name))
            before_new = snapshot(new, name)
            answer = qbt("torrents/setLocation", {"hashes": digest, "location": new})
            say(f"qbt {case}: setLocation answer", answer)
            say(f"qbt {case}: before a recheck",
                poll(lambda: qbt_view(digest), lambda v: v["save_path"].endswith("/new") and qbt_calm(v)))
            say(f"qbt {case}: disk after setLocation", {"new same": snapshot(new, name) == before_new,
                                                    "old": snapshot(old, name)})
            qbt("torrents/recheck", {"hashes": digest})
            say(f"qbt {case}: after a recheck", qbt_until(digest, progress=1))
            return judge("qbt", case, before_new, snapshot(new, name), after_old=snapshot(old, name))
        copy_by_hand(old, new, name)
        before_old, before_new = snapshot(old, name), snapshot(new, name)
        answer = qbt("torrents/setLocation", {"hashes": digest, "location": new})
        say("qbt both: setLocation answer", answer)
        say("qbt both: before a recheck",
            poll(lambda: qbt_view(digest), lambda v: v["save_path"].endswith("/new") and qbt_calm(v)))
        verdict = judge("qbt", case, before_new, snapshot(new, name), before_old, snapshot(old, name))
        qbt("torrents/recheck", {"hashes": digest})
        say("qbt both: after a recheck (a.bin differs at new)",
            poll(lambda: qbt_view(digest), lambda v: qbt_calm(v) and v["progress"] < 1, 8))
        return verdict
    finally:
        qbt("torrents/delete", {"hashes": digest, "deleteFiles": "false"})


def qbt_main():
    say("qbt version", {"app": qbt("app/version"), "api": qbt("app/webapiVersion")})
    return [qbt_case("gone"), qbt_case("rechecked"), qbt_case("both")]


# ---------------------------------------------------------------- Transmission


TR_FIELDS = ["status", "percent_done", "error", "error_string", "download_dir", "have_valid", "left_until_done"]


def tr_view(t, digest):
    row = t.torrent(digest, TR_FIELDS)
    return {k: shown(v) for k, v in row.items()}


def tr_until(t, digest, done=lambda v: True, timeout=10):
    # status 1 = check wait, 2 = checking
    return poll(lambda: tr_view(t, digest), lambda v: v["status"] not in (1, 2) and done(v), timeout)


def tr_case(t, case):
    name = f"tr-{case}"
    old, new, torrent = make(name)
    meta = base64.b64encode(open(torrent, "rb").read()).decode()
    result = t.call("torrent_add", {"metainfo": meta, "paused": True, "download_dir": old})
    digest = (result.get("torrent_added") or result.get("torrent_duplicate"))["hash_string"]
    try:
        t.call("torrent_verify", {"ids": [digest]})
        say("tr added", tr_until(t, digest, lambda v: v["percent_done"] == 1))
        if case in ("gone", "rechecked"):
            move_by_hand(old, new, name)
            if case == "gone":
                t.call("torrent_start", {"ids": [digest]})
                say("tr gone: started with the files gone", tr_until(t, digest, lambda v: v["error"] != 0, 5))
                t.call("torrent_stop", {"ids": [digest]})
                say("tr gone: stopped", tr_until(t, digest, lambda v: v["status"] == 0))
            else:
                t.call("torrent_verify", {"ids": [digest]})
                say("tr rechecked: after the hand move + verify",
                    tr_until(t, digest, lambda v: v["percent_done"] < 1))
            say(f"tr {case}: old after that", snapshot(old, name))
            before_new = snapshot(new, name)
            started = time.monotonic()
            answer = t.call("torrent_set_location", {"ids": [digest], "location": new, "move": False})
            say(f"tr {case}: set_location move:false answer",
                {"answer": answer, "seconds": round(time.monotonic() - started, 3)})
            say(f"tr {case}: before a verify", tr_view(t, digest))
            say(f"tr {case}: disk after set_location", {"new same": snapshot(new, name) == before_new,
                                                       "old": snapshot(old, name)})
            t.call("torrent_verify", {"ids": [digest]})
            say(f"tr {case}: after a verify", tr_until(t, digest, lambda v: v["percent_done"] == 1))
            return judge("transmission", case, before_new, snapshot(new, name), after_old=snapshot(old, name))
        copy_by_hand(old, new, name)
        before_old, before_new = snapshot(old, name), snapshot(new, name)
        answer = t.call("torrent_set_location", {"ids": [digest], "location": new, "move": False})
        say("tr both: set_location move:false answer", answer)
        say("tr both: before a verify", tr_view(t, digest))
        verdict = judge("transmission", case, before_new, snapshot(new, name), before_old, snapshot(old, name))
        t.call("torrent_verify", {"ids": [digest]})
        say("tr both: after a verify (a.bin differs at new)", tr_until(t, digest, lambda v: v["percent_done"] < 1, 8))
        return verdict
    finally:
        t.remove(digest, delete=False)


def transmission_main():
    t = rpc.Transmission()
    session = t.call("session_get", {})
    say("transmission version", {"version": session.get("version"), "rpc": session.get("rpc_version_semver")})
    return [tr_case(t, "gone"), tr_case(t, "rechecked"), tr_case(t, "both")]


# ---------------------------------------------------------------- Deluge


DE_FIELDS = ["state", "progress", "save_path", "download_location", "message", "is_finished"]


def de_view(web, digest):
    status = web.call("core.get_torrent_status", [digest, DE_FIELDS])
    return {k: shown(v) for k, v in status.items()}


def de_until(web, digest, done=lambda v: True, timeout=10):
    return poll(lambda: de_view(web, digest),
                lambda v: v["state"] not in ("Checking", "Moving", "Allocating") and done(v), timeout)


def de_case(web, case):
    name = f"de-{case}"
    old, new, torrent = make(name)
    meta = base64.b64encode(open(torrent, "rb").read()).decode()
    digest = web.call("core.add_torrent_file", [os.path.basename(torrent), meta,
                                                {"add_paused": True, "download_location": old}])
    try:
        web.call("core.force_recheck", [[digest]])
        say("de added", de_until(web, digest, lambda v: v["progress"] == 100))
        if case in ("gone", "rechecked", "error"):
            move_by_hand(old, new, name)
            if case == "error":
                # OLD unreadable makes the recheck fail with a file error: Deluge's Error state.
                # Readable again before the re-point, so the error is the torrent's, not the move's.
                os.makedirs(os.path.join(old, name), exist_ok=True)
                os.chmod(old, 0)
                try:
                    web.call("core.force_recheck", [[digest]])
                    say("de error: recheck with OLD unreadable", de_until(web, digest, lambda v: v["state"] == "Error"))
                finally:
                    os.chmod(old, 0o755)
                os.rmdir(os.path.join(old, name))
            elif case == "gone":
                web.call("core.resume_torrent", [[digest]])
                say("de gone: resumed with the files gone", de_until(web, digest, lambda v: v["state"] == "Error", 8))
                web.call("core.pause_torrent", [[digest]])
                say("de gone: paused", de_until(web, digest, lambda v: v["state"] in ("Paused", "Error"), 5))
            else:
                web.call("core.force_recheck", [[digest]])
                say("de rechecked: after the hand move + recheck", de_until(web, digest, lambda v: v["progress"] < 100))
            say(f"de {case}: old after that", snapshot(old, name))
            before_new = snapshot(new, name)
            answer = web.call("core.move_storage", [[digest], new])
            say(f"de {case}: move_storage answer", answer)
            say(f"de {case}: before a recheck",
                de_until(web, digest, lambda v: v["save_path"].endswith("/new")))
            say(f"de {case}: disk after move_storage", {"new same": snapshot(new, name) == before_new,
                                                       "old": snapshot(old, name)})
            web.call("core.force_recheck", [[digest]])
            say(f"de {case}: after a recheck", de_until(web, digest, lambda v: v["progress"] == 100, 40))
            return judge("deluge", case, before_new, snapshot(new, name), after_old=snapshot(old, name))
        copy_by_hand(old, new, name)
        before_old, before_new = snapshot(old, name), snapshot(new, name)
        answer = web.call("core.move_storage", [[digest], new])
        say("de both: move_storage answer", answer)
        say("de both: before a recheck", de_until(web, digest, lambda v: v["save_path"].endswith("/new")))
        verdict = judge("deluge", case, before_new, snapshot(new, name), before_old, snapshot(old, name))
        web.call("core.force_recheck", [[digest]])
        say("de both: after a recheck (a.bin differs at new)", de_until(web, digest, lambda v: v["progress"] < 100, 8))
        return verdict
    finally:
        web.call("core.remove_torrent", [digest, False])


def deluge_main():
    web = rpc.Deluge()
    if not web.call("web.connected"):
        hosts = web.call("web.get_hosts")
        web.call("web.connect", [hosts[0][0]])
    say("deluge version", {"daemon": web.call("daemon.get_version")})
    return [de_case(web, "gone"), de_case(web, "rechecked"), de_case(web, "error"), de_case(web, "both")]


def main():
    wanted = sys.argv[1:] or ["qbt", "transmission", "deluge"]
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT, exist_ok=True)
    safe = True
    try:
        for name in wanted:
            safe &= all({"qbt": qbt_main, "transmission": transmission_main, "deluge": deluge_main}[name]())
    finally:
        shutil.rmtree(ROOT, ignore_errors=True)
    sys.exit(0 if safe else 1)


if __name__ == "__main__":
    main()
