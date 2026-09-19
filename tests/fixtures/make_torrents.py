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


def main():
    fixtures_dir = os.path.dirname(os.path.abspath(__file__))
    builders = [
        build_single_file_v1,
        build_multi_file_v1_unicode,
        build_private_announce_list,
        build_v2_stub,
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
            total_size = sum(f["length"] for f in info["files"])
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
