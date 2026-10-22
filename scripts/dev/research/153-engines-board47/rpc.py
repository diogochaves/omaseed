"""Small clients for the research scripts beside this file (board 47 §3, #153): Transmission's
JSON-RPC 2.0 with its 409 session dance, and deluge-web's JSON-RPC v1 with its cookie.

Only the test servers from scripts/dev/test-server.sh (--transmission on :8085, --deluge on
:8086) are meant; DELUGE_TEST_PASS comes from the environment or ~/.config/omaseed/dev.env.
"""

import base64
import hashlib
import http.cookiejar
import json
import os
import subprocess
import time
import urllib.error
import urllib.request

TRANSMISSION = os.environ.get("OMASEED_TRANSMISSION_RPC", "http://127.0.0.1:8085/transmission/rpc")
DELUGE = os.environ.get("OMASEED_DELUGE_JSON", "http://127.0.0.1:8086/json")
for url in (TRANSMISSION, DELUGE):
    if ":8080" in url:
        raise SystemExit("refusing :8080, the real library")


class Transmission:
    def __init__(self, url=TRANSMISSION):
        self.url = url
        self.session = ""

    def call(self, method, params=None, timeout=120):
        body = json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode()
        for _ in range(2):
            request = urllib.request.Request(self.url, body, {
                "Content-Type": "application/json", "X-Transmission-Session-Id": self.session})
            try:
                with urllib.request.urlopen(request, timeout=timeout) as reply:
                    answer = json.load(reply)
            except urllib.error.HTTPError as error:
                if error.code == 409:
                    self.session = error.headers.get("X-Transmission-Session-Id", "")
                    continue
                raise
            if "error" in answer:
                raise RuntimeError(f"{method}: {answer['error']}")
            return answer.get("result", {})
        raise RuntimeError("no session id")

    def add(self, torrent_path, download_dir):
        meta = base64.b64encode(open(torrent_path, "rb").read()).decode()
        result = self.call("torrent_add", {"metainfo": meta, "download_dir": download_dir})
        added = result.get("torrent_added") or result.get("torrent_duplicate")
        return added["hash_string"]

    def torrent(self, info_hash, fields):
        torrents = self.call("torrent_get", {"ids": [info_hash], "fields": fields})["torrents"]
        return torrents[0] if torrents else None

    def remove(self, info_hash, delete=True):
        self.call("torrent_remove", {"ids": [info_hash], "delete_local_data": delete})


class Deluge:
    """One deluge-web session: its own cookie jar, so two of these are two users."""

    def __init__(self, url=DELUGE, password=None):
        self.url = url
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))
        self.id = 0
        password = password if password is not None else deluge_password()
        if not self.call("auth.login", [password]):
            raise SystemExit("deluge-web refused DELUGE_TEST_PASS")

    def call(self, method, params=None, timeout=120):
        self.id += 1
        body = json.dumps({"method": method, "params": params or [], "id": self.id}).encode()
        request = urllib.request.Request(self.url, body, {"Content-Type": "application/json"})
        with self.opener.open(request, timeout=timeout) as reply:
            answer = json.load(reply)
        if answer.get("error"):
            raise RuntimeError(f"{method}: {answer['error']}")
        return answer["result"]


def deluge_password():
    password = os.environ.get("DELUGE_TEST_PASS", "")
    if password:
        return password
    path = os.environ.get("OMASEED_DEV_ENV", os.path.expanduser("~/.config/omaseed/dev.env"))
    try:
        for line in open(path, encoding="utf-8"):
            key, _, value = line.strip().removeprefix("export ").partition("=")
            if key == "DELUGE_TEST_PASS":
                password = value
    except FileNotFoundError:
        pass
    if not password:
        raise SystemExit("no DELUGE_TEST_PASS: scripts/dev/test-server.sh start --deluge makes one")
    return password


def random_file(path, size):
    """`size` bytes of random data at `path`, written in 64 MiB pieces."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        left = size
        while left > 0:
            piece = min(left, 64 << 20)
            out.write(os.urandom(piece))
            left -= piece


def make_torrent(source, torrent_path, piece_kib=4096, tracker=None):
    """A .torrent of `source` (no tracker unless one is given), made with transmission-create."""
    if os.path.exists(torrent_path):
        os.remove(torrent_path)
    command = ["transmission-create", "-s", str(piece_kib), "-c", "omaseed research", "-o", torrent_path]
    if tracker:
        command += ["-t", tracker]
    subprocess.run(command + [source], check=True, stdout=subprocess.DEVNULL)
    return torrent_path


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()[:16]


def wait(condition, timeout, step=0.1, what="condition"):
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        value = condition()
        if value:
            return value
        time.sleep(step)
    raise TimeoutError(f"{what} not reached within {timeout} s")


def shown(path):
    """`path` as printed: the home folder as ~."""
    home = os.path.expanduser("~")
    return "~" + path[len(home):] if path.startswith(home) else path
