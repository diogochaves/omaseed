#!/usr/bin/env python3
"""omaseed — what qBittorrent has saved about one torrent, read from its files on disk.

    python3 -I scripts/dev/fastresume.py BT_backup/<hash>[.fastresume|.torrent]

qBittorrent keeps each torrent as `<hash>.torrent` (the metainfo) and `<hash>.fastresume`
(libtorrent's resume data plus qBittorrent's own `qBt-*` keys) in its profile's `BT_backup`
folder. This prints, without qBittorrent running and without its Web API: the save paths and
flags, whether the torrent is v1, v2 or hybrid, each file's priority and how many of its pieces
are had, the piece priorities, and the times. Read-only: it never writes either file.

Useful when the API can't say why (a torrent in error with no message, a selection that changed
on its own, a "finished" torrent that isn't): found #115 that way. On a library that isn't a
throwaway, read a copy of the two files.
"""
import os
import sys
from collections import Counter


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


def text(v):
    return v.decode(errors="replace") if isinstance(v, bytes) else v


def main():
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if len(sys.argv) == 2 else 2
    base = sys.argv[1]
    for ext in (".fastresume", ".torrent"):
        if base.endswith(ext):
            base = base[: -len(ext)]
    with open(base + ".fastresume", "rb") as f:
        resume, _ = bdecode(f.read())
    torrent = {}
    if os.path.exists(base + ".torrent"):
        with open(base + ".torrent", "rb") as f:
            torrent, _ = bdecode(f.read())
    info = torrent.get(b"info", torrent) or resume.get(b"info", {})
    if not info:
        print("no metainfo: neither a .torrent beside it nor an info dict in the resume data")
        return 1

    v1 = b"pieces" in info
    v2 = b"file tree" in info or info.get(b"meta version") == 2
    print("name   ", text(info.get(b"name", b"?")))
    print("kind   ", "hybrid (v1+v2)" if v1 and v2 else "v2 only" if v2 else "v1 only")
    for k in (b"qBt-savePath", b"save_path", b"qBt-downloadPath", b"qBt-category", b"qBt-tags",
              b"qBt-stopped", b"paused", b"auto_managed", b"qBt-stopCondition", b"seed_mode",
              b"upload_mode", b"share_mode", b"allocation", b"qBt-ratioLimit",
              b"qBt-seedingTimeLimit", b"qBt-shareLimitAction", b"total_downloaded",
              b"total_uploaded"):
        if k in resume:
            print(f"{k.decode():24} {text(resume[k])}")

    plen = info.get(b"piece length", 0)
    files = info.get(b"files") or [{b"length": info.get(b"length", 0), b"path": [info.get(b"name", b"")]}]
    prio = resume.get(b"file_priority", [])
    pieces = resume.get(b"pieces", b"")
    have = sum(1 for x in pieces if x & 1)
    print(f"pieces  {have} of {len(pieces)} had, piece length {plen}")

    if plen and v1:
        print("files   (index, priority, MB, pieces had/in its range, path; pad files left out)")
        off = 0
        for n, f in enumerate(files):
            length = f[b"length"]
            p = prio[n] if n < len(prio) else "default"
            a, z = off // plen, max(off, off + length - 1) // plen
            got = sum(1 for k in range(a, z + 1) if k < len(pieces) and pieces[k] & 1)
            if not f.get(b"attr", b"").startswith(b"p"):
                path = "/".join(text(x) for x in f[b"path"])
                print(f"  {n:4} prio {p!s:7} {length / 1e6:10.1f} MB  {got}/{z - a + 1}  {path}")
            off += length
        wanted = sum(f[b"length"] for n, f in enumerate(files) if n < len(prio) and prio[n] > 0)
        print(f"wanted  {wanted / 1e6:.1f} MB in {sum(1 for p in prio if p > 0)} files")
    else:
        print("files   (v2 only: per-file pieces need the file tree; priorities only)")
        print("  ", Counter(prio))

    pp = resume.get(b"piece_priority", b"")
    if pp:
        print("piece priorities", dict(Counter(pp)))
        print("had and wanted", sum(1 for k in range(len(pp)) if pp[k] and k < len(pieces) and pieces[k] & 1),
              "· had and not wanted", sum(1 for k in range(len(pp)) if not pp[k] and k < len(pieces) and pieces[k] & 1))
    for k in (b"added_time", b"completed_time", b"last_download", b"last_upload", b"active_time",
              b"seeding_time", b"finished_time", b"qBt-seedStatus"):
        if k in resume:
            print(f"{k.decode():24} {resume[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
