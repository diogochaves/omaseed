#!/usr/bin/env python3
"""Generates deterministic .torrent fixtures for tst_torrentfile.cpp and the live acceptance
test. Re-running this script must produce byte-identical output (fixed creation date, no
randomness, no wall-clock or filesystem-order dependence), so the fixtures can be committed
and regenerated to verify they still match.

Writes one .torrent file per fixture plus fixtures.json, which records each fixture's expected
fields computed independently of omaseed's C++ decoder (this is a from-scratch bencode
encoder, not a re-use of Bencode.cpp).
"""

import hashlib
import json
import os

FIXED_CREATION_DATE = 1700000000  # 2023-11-14T22:13:20Z, arbitrary but fixed


def encode(value):
    """Minimal, from-scratch bencode encoder. Dict keys are always sorted by raw byte value,
    matching the canonical form omaseed::core::bencode::encode() produces."""
    if isinstance(value, bool):
        raise TypeError("use int, not bool")
    if isinstance(value, int):
        return b"i" + str(value).encode("ascii") + b"e"
    if isinstance(value, bytes):
        return str(len(value)).encode("ascii") + b":" + value
    if isinstance(value, str):
        return encode(value.encode("utf-8"))
    if isinstance(value, list):
        return b"l" + b"".join(encode(v) for v in value) + b"e"
    if isinstance(value, dict):
        def key_bytes(k):
            return k if isinstance(k, bytes) else k.encode("utf-8")

        items = sorted(value.items(), key=lambda kv: key_bytes(kv[0]))
        out = b"d"
        for k, v in items:
            out += encode(key_bytes(k)) + encode(v)
        out += b"e"
        return out
    raise TypeError(f"unsupported bencode type: {type(value)!r}")


def fake_pieces(count, label):
    """A deterministic stand-in for real SHA-1 piece hashes: this fixture has no real file
    content on disk, so the "pieces" field's exact bytes don't matter for the infohash test,
    only their length (20 bytes per piece) and that decode/encode round-trip them intact."""
    return b"".join(hashlib.sha1(f"{label}-piece-{i}".encode("ascii")).digest() for i in range(count))


def ceil_div(a, b):
    return (a + b - 1) // b


def build_single_file_v1():
    piece_length = 16384
    length = 50000
    piece_count = ceil_div(length, piece_length)
    info = {
        "name": "Single.bin",
        "length": length,
        "piece length": piece_length,
        "pieces": fake_pieces(piece_count, "single"),
    }
    torrent = {
        "announce": "http://tracker1.example.com/announce",
        "comment": "omaseed fixture: single-file v1",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "single_file_v1.torrent", torrent


def build_multi_file_v1_unicode():
    piece_length = 16384
    files = [
        {"length": 120, "path": ["café", "notes.txt"]},
        {"length": 4096, "path": ["日本語", "sub フォルダ", "ファイル.bin"]},
        {"length": 10, "path": ["root.txt"]},
    ]
    total = sum(f["length"] for f in files)
    piece_count = ceil_div(total, piece_length)
    info = {
        "name": "Unicode Pack",
        "piece length": piece_length,
        "pieces": fake_pieces(piece_count, "unicode"),
        "files": files,
    }
    torrent = {
        "announce": "http://tracker.example.com/announce",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "multi_file_v1_unicode.torrent", torrent


def build_private_announce_list():
    piece_length = 32768
    length = 100000
    piece_count = ceil_div(length, piece_length)
    info = {
        "name": "Private.iso",
        "length": length,
        "piece length": piece_length,
        "pieces": fake_pieces(piece_count, "private"),
        "private": 1,
    }
    torrent = {
        "announce": "http://tracker1.example.com/announce",
        "announce-list": [
            ["http://tracker1.example.com/announce"],
            ["http://tracker2.example.com/announce", "http://tracker3.example.com/announce"],
        ],
        "comment": "Test fixture for omaseed",
        "created by": "omaseed test fixtures",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "private_announce_list.torrent", torrent


def build_v2_stub():
    """A v2-only (BEP-52) stub: single file exactly one piece long, so its merkle "pieces
    root" is simply the SHA-256 of that one (synthetic, deterministic) piece with no padding
    needed. A full v1+v2 hybrid needs a real padded merkle tree per file for v1's flat piece
    list to agree with v2's layered one; this stub keeps the parser path (meta version 2,
    file tree, no legacy "pieces"/"length"/"files") exercised without that machinery."""
    piece_length = 16384
    content = bytes((i * 7 + 3) % 256 for i in range(piece_length))
    pieces_root = hashlib.sha256(content).digest()
    info = {
        "name": "V2Stub.bin",
        "piece length": piece_length,
        "meta version": 2,
        "file tree": {
            "V2Stub.bin": {
                "": {
                    "length": piece_length,
                    "pieces root": pieces_root,
                }
            }
        },
    }
    torrent = {
        "announce": "http://tracker.example.com/announce",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "v2_stub.torrent", torrent


def build_multi_file_padded_v1():
    """A v1 multi-file torrent with a BEP-47 pad file between its two real files, the way
    hybrid (v1+v2) torrents and some v1 creators align files to pieces. qBittorrent leaves pad
    files out of its file list and its `torrents/filePrio` ids, so the parser must too, or
    every id after the first pad points at the wrong file."""
    piece_length = 16384
    files = [
        {"length": 20000, "path": ["disc", "one.bin"]},
        {"attr": "p", "length": 12768, "path": [".pad", "12768"]},
        {"length": 5000, "path": ["notes.txt"]},
    ]
    total = sum(f["length"] for f in files)
    piece_count = ceil_div(total, piece_length)
    info = {
        "name": "Padded Pack",
        "piece length": piece_length,
        "pieces": fake_pieces(piece_count, "padded"),
        "files": files,
    }
    torrent = {
        "announce": "http://tracker.example.com/announce",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "multi_file_padded_v1.torrent", torrent


def build_open_movies_v1():
    """The add sheet's file picker fixture (D1): a nested tree of a few folders, sizes in
    the gigabytes so the sheet's sizes read like a real library, and a big piece length so
    the fake piece list stays small on disk."""
    piece_length = 16 * 1024 * 1024
    gib = 1024 * 1024 * 1024
    files = [
        {"length": int(3.43 * gib), "path": ["Big Buck Bunny", "bbb_sunflower_2160p_60fps.mp4"]},
        {"length": 48213, "path": ["Big Buck Bunny", "subs", "bbb.en.srt"]},
        {"length": 51877, "path": ["Big Buck Bunny", "subs", "bbb.de.srt"]},
        {"length": int(4.12 * gib), "path": ["Sintel", "sintel_4k.mkv"]},
        {"length": 61022, "path": ["Sintel", "subs", "sintel.en.srt"]},
        {"length": int(6.02 * gib), "path": ["Tears of Steel", "tears_of_steel_4k.mov"]},
        {"length": int(1.37 * gib), "path": ["Cosmos Laundromat", "cosmos_laundromat_2k.mp4"]},
        {"length": 2911, "path": ["README.txt"]},
    ]
    total = sum(f["length"] for f in files)
    piece_count = ceil_div(total, piece_length)
    info = {
        "name": "Blender Open Movies",
        "piece length": piece_length,
        "pieces": fake_pieces(piece_count, "open-movies"),
        "files": files,
    }
    torrent = {
        "announce": "http://tracker.example.com/announce",
        "comment": "omaseed fixture: the add sheet's file picker",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "open_movies_v1.torrent", torrent


def nested_library_files():
    """The Nested Library Sampler's tree: 113 files in two seasons with a Subs folder each,
    extras a folder per day, artwork, a soundtrack and one folder seven levels deep, with
    unicode, brackets and spaces in the names. scripts/dev/swarm.sh generates the same tree
    (same seed, same order) and seeds it for real; keep the two in step."""
    import random

    mb, kb = 1024 * 1024, 1024
    rng = random.Random(29)
    files = []
    for season, eps in ((1, 10), (2, 8)):
        for e in range(1, eps + 1):
            base = f"Season {season:02d}/S{season:02d}E{e:02d} - Episode {e}"
            files.append((f"{base}.mkv", rng.randint(2, 5) * mb + rng.randint(0, 99999)))
            for lang in ("en", "pt-BR", "ja"):
                files.append((f"Season {season:02d}/Subs/S{season:02d}E{e:02d}.{lang}.srt",
                              rng.randint(30, 60) * kb))
    for day in range(1, 6):
        for c in range(1, rng.randint(3, 6)):
            files.append((f"Extras/Behind the Scenes/Day {day:02d}/clip {c:02d}.mkv",
                          rng.randint(1, 3) * mb))
    for who in ("Director", "Composer", "Lead Actor", "Café Owner (cameo)"):
        files.append((f"Extras/Interviews/{who}.mp4", rng.randint(2, 4) * mb))
    for i in range(1, 13):
        files.append((f"Artwork/Stills/still_{i:03d}.png", rng.randint(200, 900) * kb))
    for p in ("Poster [A]", "Poster [B]", "ポスター (Japan)"):
        files.append((f"Artwork/Posters/{p}.jpg", rng.randint(300, 1200) * kb))
    files += [
        ("Docs/README.md", 4321), ("Docs/Changelog.txt", 2100),
        ("Docs/licenses/LICENSE-CC-BY-4.0.txt", 18_000), ("Docs/licenses/LICENSE-MIT.txt", 1_070),
        ("deep/a/b/c/d/e/f/the deepest file.txt", 512),
        ("Soundtrack/01 - Opening.flac", 3 * mb), ("Soundtrack/02 - Main Theme.flac", 4 * mb),
        ("Soundtrack/03 - Credits (Extended Mix).flac", 5 * mb), ("Soundtrack/cover.jpg", 400 * kb),
        ("sample.mkv", 2 * mb), ("checksums.sha256", 9000),
    ]
    return files


def build_nested_library_v1():
    """Many files in deep, nested folders (the add sheet's tree with the mouse, M17, and
    renaming, M18): the Nested Library Sampler's own tree, private, fake pieces."""
    piece_length = 256 * 1024
    files = [{"length": size, "path": rel.split("/")} for rel, size in nested_library_files()]
    total = sum(f["length"] for f in files)
    info = {
        "name": "Nested Library Sampler (2026)",
        "piece length": piece_length,
        "pieces": fake_pieces(ceil_div(total, piece_length), "nested-library"),
        "private": 1,
        "files": files,
    }
    torrent = {
        "announce": "http://127.0.0.1:26996/announce",
        "comment": "omaseed fixture: many files in nested folders",
        "creation date": FIXED_CREATION_DATE,
        "info": info,
    }
    return "nested_library_v1.torrent", torrent


def main():
    fixtures_dir = os.path.dirname(os.path.abspath(__file__))
    builders = [
        build_single_file_v1,
        build_multi_file_v1_unicode,
        build_private_announce_list,
        build_v2_stub,
        build_multi_file_padded_v1,
        build_open_movies_v1,
        build_nested_library_v1,
    ]

    manifest = []
    for builder in builders:
        filename, torrent = builder()
        info = torrent["info"]
        info_bytes = encode(info)
        info_hash_v1 = hashlib.sha1(info_bytes).hexdigest()
        info_hash_v2 = ""
        if info.get("meta version") == 2:
            info_hash_v2 = hashlib.sha256(info_bytes).hexdigest()

        raw = encode(torrent)
        path = os.path.join(fixtures_dir, filename)
        with open(path, "wb") as fh:
            fh.write(raw)

        total_size = info.get("length")
        if total_size is None and "files" in info:
            # BEP-47 pad files are not content: qBittorrent leaves them out, and so do we.
            total_size = sum(f["length"] for f in info["files"] if "p" not in f.get("attr", ""))
        if total_size is None and "file tree" in info:
            def sum_tree(node):
                total = 0
                for _key, child in node.items():
                    if "" in child:
                        total += child[""]["length"]
                    else:
                        total += sum_tree(child)
                return total

            total_size = sum_tree(info["file tree"])

        manifest.append(
            {
                "file": filename,
                "name": info.get("name", ""),
                "totalSize": total_size,
                "infoHashV1": info_hash_v1,
                "infoHashV2": info_hash_v2,
                "isPrivate": bool(info.get("private", 0)),
                "pieceLength": info.get("piece length", 0),
                "metaVersion": info.get("meta version", 1),
            }
        )

    manifest_path = os.path.join(fixtures_dir, "fixtures.json")
    with open(manifest_path, "w") as fh:
        json.dump(manifest, fh, indent=2, sort_keys=True)
        fh.write("\n")

    for entry in manifest:
        print(f"{entry['file']}: infohash_v1={entry['infoHashV1']} totalSize={entry['totalSize']}")


if __name__ == "__main__":
    main()
