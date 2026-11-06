#!/usr/bin/env python3
"""omaseed — invented search plugins for qBittorrent, answering with the offline swarm.

    python3 -I scripts/dev/search-plugins.py make OUTDIR [--torrents DIR] [--slow SECONDS]

Writes four nova3-style search plugins into OUTDIR, for the search view's tests (#145,
DECISIONS C26). None of them touches a network: every row is made from the offline swarm's own
`.torrent` files (scripts/dev/swarm.sh, `$OMASEED_SWARM_PROFILE/torrents` by default), so a
search can be run end to end and a result added and downloaded on the playground or in a box.
Their names, sites and pages are invented (`*.invalid`), and none is ever contacted: a link to
a `.torrent` is answered by the plugin's own `download_torrent`, from bytes written into it.

    omaseed_plugin_a   "plugin a"  answers at once, with seeds; magnets for the swarm's public
                                   torrents, `.torrent` links for its private ones, and one row
                                   with a library torrent's name and size but another hash
    omaseed_plugin_b   "plugin b"  fails: raises, so qBittorrent logs its traceback
    omaseed_plugin_c   "plugin c"  answers after 4 s and reports no seeds (-1): its copy of the
                                   public torrent carries one more tracker (it folds into a's),
                                   and one row nobody seeds (0)
    omaseed_plugin_d   "plugin d"  answers after --slow seconds (45 by default): past omaseed's
                                   30 s, so it reads "slow"

A row is returned when every word of the query is in its name (case folded, `.`, `_`, `-` as
spaces), or for every row when the query is `*`. Install a plugin with qBittorrent's own
`search/installPlugin` and the file's path (the server reads it on its own machine).
"""
import base64
import hashlib
import os
import sys

PLUGINS = {
    "a": {"full": "plugin a", "cats": ["movies", "music", "software"], "delay": 0, "seeds": True},
    "b": {"full": "plugin b", "cats": ["movies", "tv"], "delay": 0, "fail": True},
    "c": {"full": "plugin c", "cats": ["music", "books", "movies"], "delay": 4, "seeds": False},
    "d": {"full": "plugin d", "cats": ["books", "software"], "delay": None, "seeds": True},
}
# Seeds plugin a reports, in the order of the swarm's torrents (sorted by name): fixed, so a
# walk's order is the same every run.
SEEDS = [41, 23, 17, 12, 9, 8, 3, 2, 1, 0]
EXTRA_TRACKER = "udp://127.0.0.1:9/announce"  # nobody listens there (swarm.sh's dead tracker)


def bdecode(b, i=0):
    c = b[i:i + 1]
    if c == b"i":
        j = b.index(b"e", i)
        return int(b[i + 1:j]), j + 1
    if c == b"l":
        i += 1
        out = []
        while b[i:i + 1] != b"e":
            v, i = bdecode(b, i)
            out.append(v)
        return out, i + 1
    if c == b"d":
        i += 1
        out = {}
        while b[i:i + 1] != b"e":
            k, i = bdecode(b, i)
            v, i = bdecode(b, i)
            out[k] = v
        return out, i + 1
    j = b.index(b":", i)
    n = int(b[i:j])
    return b[j + 1:j + 1 + n], j + 1 + n


def info_span(data):
    """The bencoded `info` dictionary's bytes, as they are in the file."""
    i = 1
    while data[i:i + 1] != b"e":
        key, i = bdecode(data, i)
        start = i
        _, i = bdecode(data, i)
        if key == b"info":
            return data[start:i]
    raise ValueError("no info dictionary")


def read_torrent(path):
    with open(path, "rb") as f:
        data = f.read()
    meta, _ = bdecode(data)
    info = meta[b"info"]
    raw = info_span(data)
    v1 = b"pieces" in info
    size = info.get(b"length")
    if size is None and b"files" in info:
        size = sum(f[b"length"] for f in info[b"files"])
    if size is None:  # v2 only: the file tree's lengths
        def walk(tree):
            total = 0
            for key, value in tree.items():
                if key == b"":
                    total += value.get(b"length", 0)
                else:
                    total += walk(value)
            return total
        size = walk(info.get(b"file tree", {}))
    trackers = []
    if b"announce" in meta:
        trackers.append(meta[b"announce"].decode())
    for tier in meta.get(b"announce-list", []):
        for url in tier:
            if url.decode() not in trackers:
                trackers.append(url.decode())
    return {
        "name": info[b"name"].decode(errors="replace"),
        "size": size,
        "private": info.get(b"private", 0) == 1,
        "v1": hashlib.sha1(raw).hexdigest() if v1 else "",
        "v2": hashlib.sha256(raw).hexdigest() if info.get(b"meta version") == 2 else "",
        "trackers": trackers,
        "bytes": data,
    }


def magnet(t, extra=()):
    from urllib.parse import quote
    xt = f"xt=urn:btih:{t['v1']}" if t["v1"] else f"xt=urn:btmh:1220{t['v2']}"
    parts = [xt, "dn=" + quote(t["name"])]
    parts += ["tr=" + quote(url, safe="") for url in list(t["trackers"]) + list(extra)]
    return "magnet:?" + "&".join(parts)


def rows_for(key, torrents):
    """`[(row dict, torrent bytes or None)]` plugin `key` answers with, before the query."""
    site = f"http://plugin-{key}.invalid"
    out = []
    if key == "a":
        for i, t in enumerate(torrents):
            if t["private"]:
                link = f"{site}/get/{i}.torrent"
                body = t["bytes"]
            else:
                link = magnet(t)
                body = None
            out.append(({"name": t["name"], "size": t["size"], "seeds": SEEDS[i % len(SEEDS)],
                         "leech": i % 4, "link": link, "desc": f"{site}/t/{i}", "days": 3 + 2 * i},
                        body))
        # Same name and size as a library torrent, another hash: "likely on disk" (SR7).
        if torrents:
            t = torrents[-1]
            fake = dict(t, v1=hashlib.sha1(b"another copy of " + t["name"].encode()).hexdigest(),
                        v2="")
            out.append(({"name": t["name"], "size": t["size"], "seeds": 5, "leech": 1,
                         "link": magnet(fake), "desc": f"{site}/t/copy", "days": 30}, None))
    elif key == "c":
        for i, t in enumerate(torrents):
            if t["private"]:
                continue
            out.append(({"name": t["name"].replace(" ", "."), "size": t["size"], "seeds": -1,
                         "leech": -1, "link": magnet(t, [EXTRA_TRACKER]), "desc": f"{site}/{i}",
                         "days": None}, None))
        nobody = {"name": "sampler commentary track", "size": 220 * 2**20, "private": False,
                  "v1": hashlib.sha1(b"nobody seeds this").hexdigest(), "v2": "",
                  "trackers": [EXTRA_TRACKER]}
        out.append(({"name": nobody["name"], "size": nobody["size"], "seeds": 0, "leech": 0,
                     "link": magnet(nobody), "desc": f"{site}/nobody", "days": 760}, None))
    elif key == "d":
        for i, t in enumerate(torrents[:2]):
            if t["private"]:
                continue
            out.append(({"name": t["name"] + " (late)", "size": t["size"], "seeds": 6,
                         "leech": 0, "link": magnet(dict(t, v1=hashlib.sha1(
                             b"late " + t["name"].encode()).hexdigest(), v2="")),
                         "desc": f"{site}/{i}", "days": 1}, None))
    return out


TEMPLATE = '''# VERSION: 1.0
# AUTHORS: omaseed's tests (invented; never contacts a site)
# Made by omaseed's scripts/dev/search-plugins.py. MIT.

import base64
import os
import tempfile
import time
import urllib.parse

from novaprinter import prettyPrinter

ROWS = {rows!r}
FILES = {files!r}
DELAY = {delay!r}
FAIL = {fail!r}


def fold(text):
    out = text.lower()
    for ch in "._-":
        out = out.replace(ch, " ")
    return out.split()


class {cls}(object):
    url = {site!r}
    name = {full!r}
    supported_categories = {cats!r}

    def search(self, what, cat="all"):
        if FAIL:
            raise RuntimeError("the invented site answered nothing")
        if DELAY:
            time.sleep(DELAY)
        words = fold(urllib.parse.unquote(what))
        now = int(time.time())
        for row in ROWS:
            if words != ["*"] and not all(w in " ".join(fold(row["name"])) for w in words):
                continue
            prettyPrinter({{
                "link": row["link"],
                "name": row["name"],
                "size": str(row["size"]),
                "seeds": row["seeds"],
                "leech": row["leech"],
                "engine_url": self.url,
                "desc_link": row["desc"],
                "pub_date": now - row["days"] * 86400 if row["days"] is not None else -1,
            }})

    def download_torrent(self, info):
        body = FILES.get(info)
        if body is None:
            raise RuntimeError("no such link")
        handle, path = tempfile.mkstemp(suffix=".torrent")
        with os.fdopen(handle, "wb") as out:
            out.write(base64.b64decode(body))
        print(path + " " + info)
'''


def make(outdir, torrents_dir, slow):
    paths = sorted(os.path.join(torrents_dir, n) for n in os.listdir(torrents_dir)
                   if n.endswith(".torrent") and not n.startswith("unwritable"))
    torrents = [read_torrent(p) for p in paths]
    os.makedirs(outdir, exist_ok=True)
    for key, spec in PLUGINS.items():
        site = f"http://plugin-{key}.invalid"
        rows, files = [], {}
        if not spec.get("fail"):
            for row, body in rows_for(key, torrents):
                rows.append(row)
                if body is not None:
                    files[row["link"]] = base64.b64encode(body).decode()
        cats = {"all": "0"}
        cats.update({c: str(n + 1) for n, c in enumerate(spec["cats"])})
        cls = f"omaseed_plugin_{key}"
        delay = slow if spec["delay"] is None else spec["delay"]
        with open(os.path.join(outdir, cls + ".py"), "w") as f:
            f.write(TEMPLATE.format(rows=rows, files=files, delay=delay, fail=bool(spec.get("fail")),
                                    cls=cls, site=site, full=spec["full"], cats=cats))
        print(os.path.join(outdir, cls + ".py"))


def main(argv):
    if len(argv) < 3 or argv[1] != "make":
        print(__doc__.strip())
        return 0 if len(argv) > 1 and argv[1] in ("-h", "--help") else 2
    outdir = argv[2]
    torrents_dir = os.path.join(os.environ.get("OMASEED_SWARM_PROFILE",
                                               os.path.expanduser("~/.local/share/omaseed-swarm")),
                                "torrents")
    slow = 45
    rest = argv[3:]
    while rest:
        if rest[0] == "--torrents" and len(rest) > 1:
            torrents_dir, rest = rest[1], rest[2:]
        elif rest[0] == "--slow" and len(rest) > 1:
            slow, rest = int(rest[1]), rest[2:]
        else:
            print(f"unknown argument: {rest[0]}", file=sys.stderr)
            return 2
    if not os.path.isdir(torrents_dir):
        print(f"no swarm torrents in {torrents_dir}: scripts/dev/swarm.sh seed first", file=sys.stderr)
        return 1
    make(outdir, torrents_dir, slow)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
