#!/usr/bin/env python3
"""omaseed #146: what qBittorrent's torrent creator really does, through its Web API.

    python3 -I creator.py [WORKDIR]

    QBT_URL  the qBittorrent's Web API (default the playground, http://127.0.0.1:8081/api/v2;
             an auth-bypassed localhost one: this script sends no credential)

Makes throwaway folders under WORKDIR (default /tmp/omaseed-146), creates torrents from them
in each way board 49 draws (private v1 with a source tag, public hybrid that seeds at once, v2,
automatic piece sizes, an unreadable folder, a missing one, a cancel), then adds "the site's
copy" of the private one back twice: once with only the announce changed (same info hash) and
once with the source tag changed inside the info (a different hash), each pointed at the
original folder, and prints what qBittorrent made of each. Deletes every task it made and every
torrent it added (never their files), then WORKDIR.

The announce URL carries a made-up passkey; it is printed masked, as omaseed must show it.
Run under the playground's lock: flock -o /tmp/claude-$UID/omaseed-playground.lock ...
"""
import hashlib, json, os, sys, time, urllib.error, urllib.parse, urllib.request, uuid

Q = os.environ.get("QBT_URL", "http://127.0.0.1:8081/api/v2").rstrip("/")
if ":8080" in Q:
    sys.exit("creator.py: :8080 is the real library's port; refusing")
WORK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/omaseed-146"
PASSKEY = "0f" * 16  # made up
ANNOUNCE = f"https://tracker-a.example/announce/{PASSKEY}"
TASKS, HASHES = [], []


def mask(s):
    return s.replace(PASSKEY, PASSKEY[:4] + "•" * 8) if isinstance(s, str) else s


def call(path, data=None, files=None, raw=False):
    url = f"{Q}/{path}"
    body, headers = None, {}
    if files is not None:
        b = uuid.uuid4().hex
        parts = []
        for k, v in (data or {}).items():
            parts.append(f'--{b}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n{v}\r\n'.encode())
        for k, (name, blob) in files.items():
            parts.append(f'--{b}\r\nContent-Disposition: form-data; name="{k}"; filename="{name}"\r\n'
                         f"Content-Type: application/x-bittorrent\r\n\r\n".encode() + blob + b"\r\n")
        body = b"".join(parts) + f"--{b}--\r\n".encode()
        headers["Content-Type"] = f"multipart/form-data; boundary={b}"
    elif data is not None:
        body = urllib.parse.urlencode(data).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(url, body, headers), timeout=30) as r:
            out = r.read()
            return r.status, out if raw else out.decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")


# ---------- a small bencode, enough for one .torrent ----------
def bdec(b, i=0):
    c = b[i:i + 1]
    if c == b"i":
        j = b.index(b"e", i)
        return int(b[i + 1:j]), j + 1
    if c in (b"l", b"d"):
        i += 1
        out = [] if c == b"l" else {}
        while b[i:i + 1] != b"e":
            if c == b"l":
                v, i = bdec(b, i)
                out.append(v)
            else:
                k, i = bdec(b, i)
                v, i = bdec(b, i)
                out[k] = v
        return out, i + 1
    j = b.index(b":", i)
    n = int(b[i:j])
    return b[j + 1:j + 1 + n], j + 1 + n


def benc(v):
    if isinstance(v, int):
        return b"i%de" % v
    if isinstance(v, bytes):
        return b"%d:%s" % (len(v), v)
    if isinstance(v, str):
        return benc(v.encode())
    if isinstance(v, list):
        return b"l" + b"".join(benc(x) for x in v) + b"e"
    return b"d" + b"".join(benc(k) + benc(v[k]) for k in sorted(v)) + b"e"


def describe(blob):
    t, _ = bdec(blob)
    info = t[b"info"]
    keys = sorted(k.decode() for k in info)
    d = {
        "top-level keys": sorted(k.decode() for k in t),
        "info keys": keys,
        "announce": mask(t.get(b"announce", b"").decode()),
        "announce-list": [[mask(u.decode()) for u in tier] for tier in t.get(b"announce-list", [])],
        "url-list": [u.decode() for u in t.get(b"url-list", [])] if isinstance(t.get(b"url-list"), list) else t.get(b"url-list", b"").decode(),
        "comment": t.get(b"comment", b"").decode(),
        "created by": t.get(b"created by", b"").decode(),
        "private": info.get(b"private"),
        "source": info.get(b"source", b"").decode() or None,
        "piece length": info.get(b"piece length"),
        "meta version": info.get(b"meta version"),
        "has v1 pieces": b"pieces" in info,
        "has piece layers": b"piece layers" in t,
    }
    if b"pieces" in info:
        d["info hash v1"] = hashlib.sha1(benc(info)).hexdigest()
    if b"meta version" in info:
        d["info hash v2 (truncated)"] = hashlib.sha256(benc(info)).hexdigest()[:40]
    return t, d


def task_status(tid):
    code, out = call("torrentcreator/status?" + urllib.parse.urlencode({"taskID": tid}))
    if code != 200:
        return code, out
    rows = json.loads(out)
    return code, rows[0] if rows else None


def wait(tid, limit=120):
    for _ in range(limit * 4):
        code, st = task_status(tid)
        if code != 200 or st is None or st.get("status") in ("Finished", "Failed"):
            return code, st
        time.sleep(0.25)
    return code, st


def show(title, obj):
    print(f"\n== {title}")
    if isinstance(obj, dict):
        for k, v in obj.items():
            print(f"   {k}: {mask(v) if isinstance(v, str) else v}")
    else:
        print("  ", mask(str(obj)))


def create(label, **params):
    params.setdefault("startSeeding", "false")
    code, out = call("torrentcreator/addTask", params)
    show(f"{label}: addTask -> {code}", out)
    if code != 200:
        return None, None
    tid = json.loads(out)["taskID"]
    TASKS.append(tid)
    code, st = wait(tid)
    show(f"{label}: status", st if isinstance(st, dict) else f"{code} {st}")
    if not isinstance(st, dict) or st.get("status") != "Finished":
        return tid, None
    code, blob = call("torrentcreator/torrentFile?" + urllib.parse.urlencode({"taskID": tid}), raw=True)
    if code != 200:
        show(f"{label}: torrentFile -> {code}", blob)
        return tid, None
    t, d = describe(blob)
    show(f"{label}: the .torrent ({len(blob)} bytes)", d)
    return tid, (blob, t, d)


def torrent(h):
    code, out = call("torrents/info?" + urllib.parse.urlencode({"hashes": h}))
    rows = json.loads(out) if code == 200 else []
    return rows[0] if rows else None


def tid_of(d):
    """qBittorrent's id for a torrent: the v1 hash, or the truncated v2 hash for v2 and hybrids."""
    return d.get("info hash v2 (truncated)") or d["info hash v1"]


def settle(h, limit=30):
    """Wait until the torrent is in the session and not checking for two reads in a row."""
    calm = 0
    for _ in range(limit * 2):
        time.sleep(0.5)
        t = torrent(h)
        busy = not t or t["state"] in ("checkingUP", "checkingDL", "checkingResumeData", "metaDL", "allocating", "moving")
        calm = 0 if busy else calm + 1
        if calm >= 2:
            return t
    return torrent(h)


def brief(t):
    if not t:
        return "not in the session"
    return {k: t.get(k) for k in ("name", "state", "progress", "save_path", "content_path", "private",
                                  "infohash_v1", "infohash_v2", "downloaded", "amount_left")}


def main():
    code, ver = call("app/version")
    _, api = call("app/webapiVersion")
    print(f"qBittorrent {ver} · Web API {api} · {Q}")
    if code != 200:
        sys.exit("no qBittorrent there")
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "sample-folder")
    os.makedirs(os.path.join(src, "notes"), exist_ok=True)
    for name, size in (("track-01.flac", 3_000_000), ("track-02.flac", 2_500_000), ("notes/readme.txt", 1200)):
        with open(os.path.join(src, name), "wb") as f:
            f.write(hashlib.sha256(name.encode()).digest() * (size // 32) + b"x" * (size % 32))
    with open(os.path.join(src, ".hidden-cache"), "wb") as f:
        f.write(b"cache" * 100)
    before = {t["hash"] for t in json.loads(call("torrents/info")[1])}
    print(f"session holds {len(before)} torrents before")

    # A. private, v1, source tag, automatic piece size, not added
    tidA, A = create("A private v1", sourcePath=src, private="true", format="v1", pieceSize="0",
                     trackers=ANNOUNCE, source="TA", comment="made by board 49's research", ignoreDotfiles="true")
    show("A: session afterwards", f"{len(json.loads(call('torrents/info')[1]))} torrents (startSeeding=false adds nothing)")

    # B. public, hybrid, two trackers in two tiers ("|" between URLs), a web seed, seeds at once
    tidB, B = create("B public hybrid, startSeeding", sourcePath=src, private="false", format="hybrid",
                     trackers="udp://tracker.one.example:6969/announce|https://tracker.two.example/announce",
                     urlSeeds="https://mirror.example/files/", startSeeding="true")
    if B:
        h = tid_of(B[2])
        HASHES.append(h)
        show("B: in the session (its id is the truncated v2 hash)", brief(settle(h, 8)))
        show("B: by its v1 hash", brief(torrent(B[2]["info hash v1"])))

    # B2. how trackers make tiers: "|" between URLs, an empty entry between tiers?
    for label, trk in (("B2 a|b", "udp://t1.example:1/a|udp://t2.example:1/a"),
                       ("B2 a||b", "udp://t1.example:1/a||udp://t2.example:1/a"),
                       ("B2 a\\nb", "udp://t1.example:1/a\nudp://t2.example:1/a"),
                       ("B2 a\\n\\nb", "udp://t1.example:1/a\n\nudp://t2.example:1/a")):
        code, out = call("torrentcreator/addTask", {"sourcePath": src, "format": "v1", "trackers": trk, "startSeeding": "false"})
        tid = json.loads(out)["taskID"]
        TASKS.append(tid)
        _, st = wait(tid)
        if st.get("status") != "Finished":
            print(f"\n== {label}: {st.get('status')} {st.get('errorMessage')}")
            continue
        _, blob = call("torrentcreator/torrentFile?" + urllib.parse.urlencode({"taskID": tid}), raw=True)
        print(f"\n== {label}: announce-list {describe(blob)[1]['announce-list']}")

    # B3. only the folder and v1: startSeeding left out, does it add the torrent?
    n0 = len(json.loads(call("torrents/info")[1]))
    _, st = wait(json.loads(call("torrentcreator/addTask", {"sourcePath": src, "format": "v1"})[1])["taskID"])
    TASKS.append(st["taskID"])
    show("B3 defaults", {k: st.get(k) for k in ("format", "pieceSize", "private", "ignoreDotfiles", "optimizeAlignment", "paddedFileSizeLimit", "status", "errorMessage")})
    code, blob = call("torrentcreator/torrentFile?" + urllib.parse.urlencode({"taskID": st["taskID"]}), raw=True)
    if code == 200:
        h3 = tid_of(describe(blob)[1])
        time.sleep(1.5)
        if torrent(h3):
            HASHES.append(h3)
        show("B3 startSeeding left out", f"torrents {n0} -> {len(json.loads(call('torrents/info')[1]))}; the created one in the session: {torrent(h3) is not None}")

    # B4. the folder again as a hybrid (format left out) while B's identical hybrid already seeds
    _, st = wait(json.loads(call("torrentcreator/addTask", {"sourcePath": src})[1])["taskID"])
    TASKS.append(st["taskID"])
    show("B4 same torrent, already in the session", {k: st.get(k) for k in ("format", "status", "errorMessage")})
    code, out = call("torrentcreator/torrentFile?" + urllib.parse.urlencode({"taskID": st["taskID"]}))
    show("B4 torrentFile ->", f"{code} {out[:80]}")

    # B5. the seedMode add omaseed would make itself: A's .torrent added at the folder's parent
    if A:
        code, out = call("torrents/add", {"savepath": WORK, "seedMode": "true", "stopped": "false"}, files={"torrents": ("a.torrent", A[0])})
        HASHES.append(A[2]["info hash v1"])
        show("B5 A added with seedMode ->", f"{code} {out}")
        show("B5 in the session", brief(settle(A[2]["info hash v1"], 8)))
        call("torrents/delete", {"hashes": A[2]["info hash v1"], "deleteFiles": "false"})
        HASHES.remove(A[2]["info hash v1"])
        time.sleep(1)

    # C. v2 only
    create("C v2", sourcePath=src, format="v2")

    # D. automatic piece size for bigger contents (sparse files: hashing zeros)
    for gib in (1, 4, 16):
        big = os.path.join(WORK, f"big-{gib}g")
        os.makedirs(big, exist_ok=True)
        with open(os.path.join(big, "disk.img"), "wb") as f:
            f.truncate(gib * 1024 ** 3)
        for fmt in ("v1", "hybrid"):
            code, out = call("torrentcreator/addTask", {"sourcePath": big, "format": fmt, "startSeeding": "false"})
            tid = json.loads(out)["taskID"]
            TASKS.append(tid)
            t0 = time.time()
            _, st = wait(tid, 600)
            print(f"\n== D {gib} GiB {fmt}: pieceSize {st.get('pieceSize')} ({st.get('pieceSize', 0) // 1024} KiB), "
                  f"{st.get('status')} in {time.time() - t0:.1f} s")
            call("torrentcreator/deleteTask", {"taskID": tid})
            TASKS.remove(tid)

    # E. an unreadable folder, F. a missing one, G. a file rather than a folder
    locked = os.path.join(WORK, "locked")
    os.makedirs(locked, exist_ok=True)
    with open(os.path.join(locked, "a.bin"), "wb") as f:
        f.write(b"a" * 4096)
    os.chmod(locked, 0)
    create("E unreadable folder", sourcePath=locked)
    os.chmod(locked, 0o755)
    os.chmod(os.path.join(locked, "a.bin"), 0)
    create("E2 unreadable file inside", sourcePath=locked)
    os.chmod(os.path.join(locked, "a.bin"), 0o644)
    create("F missing folder", sourcePath=os.path.join(WORK, "nowhere"))
    create("G one file", sourcePath=os.path.join(src, "track-01.flac"))
    os.makedirs(os.path.join(WORK, "empty"), exist_ok=True)
    create("G2 empty folder", sourcePath=os.path.join(WORK, "empty"))

    # H. cancel while it hashes
    code, out = call("torrentcreator/addTask", {"sourcePath": os.path.join(WORK, "big-16g"), "format": "hybrid"})
    tid = json.loads(out)["taskID"]
    TASKS.append(tid)
    time.sleep(1.5)
    _, st = task_status(tid)
    show("H while hashing", {k: st.get(k) for k in ("status", "progress", "pieceSize", "timeStarted")})
    code, out = call("torrentcreator/torrentFile?" + urllib.parse.urlencode({"taskID": tid}))
    show("H torrentFile while running ->", f"{code} {out[:120]}")
    code, out = call("torrentcreator/deleteTask", {"taskID": tid})
    show("H deleteTask while running ->", f"{code} {out[:120]}")
    TASKS.remove(tid)
    time.sleep(0.5)
    show("H status afterwards ->", task_status(tid))
    show("H unknown task id ->", task_status("no-such-task"))

    # I. torrentFilePath: saved on the server
    out_path = os.path.join(WORK, "saved-on-server.torrent")
    create("I torrentFilePath", sourcePath=src, format="v1", torrentFilePath=out_path)
    show("I file on the server", f"{os.path.exists(out_path)} ({os.path.getsize(out_path) if os.path.exists(out_path) else 0} bytes)")

    # J. the site's copy of A, added back pointed at the original folder
    if A:
        blob, t, d = A
        site = dict(t)
        site[b"announce"] = ANNOUNCE.replace("tracker-a", "tracker-a").replace(PASSKEY, "ab" * 16).encode()
        site.pop(b"announce-list", None)
        same = benc(site)
        _, d1 = describe(same)
        show("J1 site's copy, announce only", {"info hash v1": d1["info hash v1"], "same as A": d1["info hash v1"] == d["info hash v1"]})
        code, out = call("torrents/add", {"savepath": WORK, "stopped": "true"}, files={"torrents": ("site.torrent", same)})
        show("J1 add ->", f"{code} {out}")
        HASHES.append(d1["info hash v1"])
        settle(d1["info hash v1"])
        call("torrents/recheck", {"hashes": d1["info hash v1"]})
        show("J1 after its check", brief(settle(d1["info hash v1"])))
        changed = dict(t)
        changed[b"info"] = dict(t[b"info"])
        changed[b"info"][b"source"] = b"TA-site"
        diff = benc(changed)
        _, d2 = describe(diff)
        show("J2 site's copy, source changed in the info", {"info hash v1": d2["info hash v1"], "same as A": d2["info hash v1"] == d["info hash v1"]})
        code, out = call("torrents/add", {"savepath": WORK, "stopped": "true"}, files={"torrents": ("site2.torrent", diff)})
        show("J2 add ->", f"{code} {out}")
        HASHES.append(d2["info hash v1"])
        settle(d2["info hash v1"])
        call("torrents/recheck", {"hashes": d2["info hash v1"]})
        show("J2 after its check (same files, other hash)", brief(settle(d2["info hash v1"])))

    show("tasks listed now", [s["taskID"] for s in json.loads(call("torrentcreator/status")[1]) if s["taskID"] in TASKS])


try:
    main()
finally:
    for tid in TASKS:
        call("torrentcreator/deleteTask", {"taskID": tid})
    if HASHES:
        call("torrents/delete", {"hashes": "|".join(HASHES), "deleteFiles": "false"})
    time.sleep(1)
    for root, dirs, files in os.walk(WORK):
        for n in dirs + files:
            try:
                os.chmod(os.path.join(root, n), 0o755)
            except OSError:
                pass
    import shutil
    shutil.rmtree(WORK, ignore_errors=True)
    left = [s["taskID"] for s in json.loads(call("torrentcreator/status")[1]) if s["taskID"] in TASKS]
    print(f"\ncleaned: {len(TASKS)} tasks deleted, {len(HASHES)} torrents removed (files kept, then {WORK} removed); left: {left}")
