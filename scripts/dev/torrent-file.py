#!/usr/bin/env python3
"""omaseed — read a .torrent, or make "the site's copy" of one, for the create walk (#146).

    python3 -I scripts/dev/torrent-file.py show FILE [--full]
    python3 -I scripts/dev/torrent-file.py site FILE OUT [--announce URL] [--source TAG]

`show` prints one JSON object: name, private, source, piece_length, pieces, files, total,
format (v1, v2, hybrid), info_hash_v1, created_by, announce, tiers (the announce-list, one list
per tier) and url_list. Every URL's path and query come out as dots, as omaseed shows a private
tracker's announce (core/AnnounceMask), unless --full.

`site` writes what a private tracker's site hands back after an upload: the same file with its
own announce URL (the info, and so the info hash, unchanged), or, with --source, the source tag
changed inside the info (another info hash, the same files).

Pure Python, no third-party module; read-only on FILE.
"""
import hashlib
import json
import re
import sys


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


def bencode(v):
    if isinstance(v, int):
        return b"i%de" % v
    if isinstance(v, bytes):
        return b"%d:%s" % (len(v), v)
    if isinstance(v, str):
        return bencode(v.encode())
    if isinstance(v, list):
        return b"l" + b"".join(bencode(x) for x in v) + b"e"
    if isinstance(v, dict):
        return b"d" + b"".join(bencode(k) + bencode(v[k]) for k in sorted(v)) + b"e"
    raise TypeError(type(v))


def text(v):
    return v.decode("utf-8", "replace") if isinstance(v, bytes) else v


def masked(url, full):
    url = text(url)
    if full:
        return url
    m = re.match(r"^([a-z]+://[^/?#]+)([^?#]*)(\?.*)?$", url, re.I)
    if not m:
        return "••••••••"
    out, hidden = m.group(1), False
    for part in [p for p in m.group(2).split("/") if p]:
        if part.lower() in ("announce", "announce.php", "scrape", "scrape.php"):
            out, hidden = out + "/" + part, False
        elif not hidden:
            out, hidden = out + "/••••••••", True
    return out + ("?••••••••" if m.group(3) else "")


def show(path, full):
    data = open(path, "rb").read()
    top, _ = bdecode(data)
    info = top[b"info"]
    files = info.get(b"files")
    if files is None and b"file tree" not in info:
        listed = [(text(info[b"name"]), info.get(b"length", 0))]
    elif files is not None:
        listed = [("/".join(text(p) for p in f[b"path"]), f[b"length"])
                  for f in files if b"attr" not in f or b"p" not in f[b"attr"]]
    else:
        listed = []
    v2 = info.get(b"meta version") == 2
    v1 = b"pieces" in info
    start = data.index(b"4:info") + 6
    _, end = bdecode(data, start)
    print(json.dumps({
        "name": text(info[b"name"]),
        "private": info.get(b"private", 0),
        "source": text(info.get(b"source", b"")),
        "piece_length": info[b"piece length"],
        "pieces": len(info.get(b"pieces", b"")) // 20,
        "files": len(listed),
        "total": sum(size for _, size in listed),
        "format": "hybrid" if v1 and v2 else "v2" if v2 else "v1",
        "info_hash_v1": hashlib.sha1(data[start:end]).hexdigest() if v1 else "",
        "created_by": text(top.get(b"created by", b"")),
        "announce": masked(top.get(b"announce", b""), full),
        "tiers": [[masked(u, full) for u in tier] for tier in top.get(b"announce-list", [])],
        "url_list": [text(u) for u in (top.get(b"url-list") or [])] if isinstance(top.get(b"url-list"), list)
        else ([text(top[b"url-list"])] if top.get(b"url-list") else []),
        "comment": text(top.get(b"comment", b"")),
    }, ensure_ascii=False))


def site(path, out, announce, source):
    top, _ = bdecode(open(path, "rb").read())
    if announce:
        top[b"announce"] = announce.encode()
        top.pop(b"announce-list", None)
    if source is not None:
        top[b"info"][b"source"] = source.encode()
    top[b"comment"] = b"the site's copy"
    open(out, "wb").write(bencode(top))


def main():
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "show":
        show(args[1], "--full" in args)
    elif len(args) >= 3 and args[0] == "site":
        opts = dict(zip(args[3::2], args[4::2]))
        site(args[1], args[2], opts.get("--announce"), opts.get("--source"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
