"""Shared by transmission.py and deluge.py beside this file (#88): a bencoder, a fixture
torrent made in Python (private, two tracker tiers, a top folder with three files), and the
two wires. Standard library only. Only the test servers of scripts/dev/test-server.sh
(--transmission on :8085, --deluge on :8086) are meant.
"""

import base64
import hashlib
import http.cookiejar
import json
import os
import shutil
import time
import urllib.error
import urllib.request

TRANSMISSION = os.environ.get("OMASEED_TRANSMISSION_RPC", "http://127.0.0.1:8085/transmission/rpc")
DELUGE = os.environ.get("OMASEED_DELUGE_JSON", "http://127.0.0.1:8086/json")
for _url in (TRANSMISSION, DELUGE):
    if ":8080" in _url:
        raise SystemExit("refusing :8080, the real library")

WORK = os.environ.get("OMASEED_88_WORK", os.path.expanduser("~/.cache/omaseed-research-88"))
TRACKERS = [["http://127.0.0.1:9/announce?passkey=SECRET88"], ["http://127.0.0.1:9/backup/announce"]]


def bencode(value):
    if isinstance(value, int):
        return b"i%de" % value
    if isinstance(value, str):
        value = value.encode()
    if isinstance(value, bytes):
        return b"%d:%s" % (len(value), value)
    if isinstance(value, list):
        return b"l" + b"".join(bencode(v) for v in value) + b"e"
    if isinstance(value, dict):
        items = sorted((k.encode() if isinstance(k, str) else k, v) for k, v in value.items())
        return b"d" + b"".join(bencode(k) + bencode(v) for k, v in items) + b"e"
    raise TypeError(type(value))


def bdecode(data, at=0):
    lead = data[at:at + 1]
    if lead == b"i":
        end = data.index(b"e", at)
        return int(data[at + 1:end]), end + 1
    if lead == b"l":
        at += 1
        out = []
        while data[at:at + 1] != b"e":
            item, at = bdecode(data, at)
            out.append(item)
        return out, at + 1
    if lead == b"d":
        at += 1
        out = {}
        while data[at:at + 1] != b"e":
            key, at = bdecode(data, at)
            out[key], at = bdecode(data, at)
        return out, at + 1
    colon = data.index(b":", at)
    size = int(data[at:colon])
    return data[colon + 1:colon + 1 + size], colon + 1 + size


def fixture(name="omaseed-88-fixture", big_mib=8, private=True, piece=1 << 18):
    """Makes WORK/data/<name>/{a.bin, sub/b.bin, sub/c.txt} and its .torrent.
    Returns (torrent bytes, info hash, data folder holding <name>)."""
    data = os.path.join(WORK, "data")
    top = os.path.join(data, name)
    shutil.rmtree(top, ignore_errors=True)
    os.makedirs(os.path.join(top, "sub"))
    files = [(["a.bin"], big_mib << 20), (["sub", "b.bin"], 3 << 20), (["sub", "c.txt"], 5000)]
    stream = b""
    for path, size in files:
        blob = os.urandom(size)
        with open(os.path.join(top, *path), "wb") as out:
            out.write(blob)
        stream += blob
    pieces = b"".join(hashlib.sha1(stream[i:i + piece]).digest() for i in range(0, len(stream), piece))
    info = {"name": name, "piece length": piece, "pieces": pieces,
            "files": [{"path": path, "length": size} for path, size in files]}
    if private:
        info["private"] = 1
    meta = {"announce": TRACKERS[0][0], "announce-list": TRACKERS, "info": info,
            "comment": "omaseed research #88", "created by": "lib.py"}
    return bencode(meta), hashlib.sha1(bencode(info)).hexdigest(), data


def b64(blob):
    return base64.b64encode(blob).decode()


class Transmission:
    def __init__(self, url=TRANSMISSION):
        self.url = url
        self.session = ""

    def raw(self, method, params=None, timeout=120):
        """The whole JSON-RPC answer, error or not."""
        body = json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode()
        for _ in range(2):
            request = urllib.request.Request(self.url, body, {
                "Content-Type": "application/json", "X-Transmission-Session-Id": self.session})
            try:
                with urllib.request.urlopen(request, timeout=timeout) as reply:
                    return json.load(reply)
            except urllib.error.HTTPError as error:
                if error.code == 409:
                    self.session = error.headers.get("X-Transmission-Session-Id", "")
                    continue
                raise
        raise RuntimeError("no session id")

    def call(self, method, params=None):
        answer = self.raw(method, params)
        if "error" in answer:
            raise RuntimeError(f"{method}: {answer['error']}")
        return answer.get("result", {})

    def get(self, info_hash, fields):
        torrents = self.call("torrent_get", {"ids": [info_hash], "fields": fields})["torrents"]
        return torrents[0] if torrents else None


class Deluge:
    def __init__(self, url=DELUGE):
        self.url = url
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))
        self.id = 0
        if not self.call("auth.login", [deluge_password()]):
            raise SystemExit("deluge-web refused DELUGE_TEST_PASS")

    def raw(self, method, params=None, timeout=120):
        self.id += 1
        body = json.dumps({"method": method, "params": params or [], "id": self.id}).encode()
        request = urllib.request.Request(self.url, body, {"Content-Type": "application/json"})
        with self.opener.open(request, timeout=timeout) as reply:
            return json.load(reply)

    def call(self, method, params=None):
        answer = self.raw(method, params)
        if answer.get("error"):
            raise RuntimeError(f"{method}: {answer['error']}")
        return answer["result"]

    def tried(self, method, params=None):
        """The result, or the error's message cut short."""
        answer = self.raw(method, params)
        if answer.get("error"):
            lines = str(answer["error"].get("message", answer["error"])).strip().splitlines()
            return f"ERROR (code {answer['error'].get('code')}): " + lines[0].rsplit(">: ", 1)[-1][:200]
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
                password = value.strip("'\"")
    except FileNotFoundError:
        pass
    if not password:
        raise SystemExit("no DELUGE_TEST_PASS: scripts/dev/test-server.sh start --deluge makes one")
    return password


def wait(condition, timeout, step=0.05, what="condition"):
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        value = condition()
        if value:
            return value
        time.sleep(step)
    raise TimeoutError(f"{what} not reached within {timeout} s")


def say(title, value=""):
    if isinstance(value, (dict, list)):
        value = json.dumps(value, ensure_ascii=False, sort_keys=True)
    print(f"{title:<46} {value}" if value != "" else f"\n== {title}")
