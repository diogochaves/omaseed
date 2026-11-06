#!/usr/bin/env python3
"""A BitTorrent peer that has one torrent's metadata and nothing else, on 127.0.0.1 (#87).

It answers the extension protocol's ut_metadata requests (BEP 9, BEP 10) for the .torrent it
is given and holds no pieces, so a client asked for that torrent's magnet learns its name and
files from it and can download nothing. The Deluge walk (scripts/dev/deluge-walk.sh) points a
magnet at it with x.pe=127.0.0.1:PORT, so Deluge's prefetch reads the files before the add
without a tracker, DHT or a second client: everything stays on this machine.

    scripts/dev/metadata-peer.py TORRENT-FILE PORT   # runs until killed; prints its magnet

Only v1 torrents (the handshake's info hash is the SHA-1 of the info dictionary).
"""

import hashlib
import os
import socket
import struct
import sys
import threading
import urllib.parse

PIECE = 16 * 1024
OUR_UT_METADATA = 3


def decode(data, at=0):
    """One bencoded value from `data` at `at`: (value, the index after it)."""
    head = data[at:at + 1]
    if head == b"i":
        end = data.index(b"e", at)
        return int(data[at + 1:end]), end + 1
    if head == b"l":
        at += 1
        items = []
        while data[at:at + 1] != b"e":
            item, at = decode(data, at)
            items.append(item)
        return items, at + 1
    if head == b"d":
        at += 1
        items = {}
        while data[at:at + 1] != b"e":
            key, at = decode(data, at)
            items[key], at = decode(data, at)
        return items, at + 1
    colon = data.index(b":", at)
    size = int(data[at:colon])
    return data[colon + 1:colon + 1 + size], colon + 1 + size


def encode(value):
    if isinstance(value, int):
        return b"i%de" % value
    if isinstance(value, bytes):
        return b"%d:%s" % (len(value), value)
    if isinstance(value, str):
        return encode(value.encode())
    if isinstance(value, list):
        return b"l" + b"".join(encode(v) for v in value) + b"e"
    if isinstance(value, dict):
        items = sorted((k.encode() if isinstance(k, str) else k, v) for k, v in value.items())
        return b"d" + b"".join(encode(k) + encode(v) for k, v in items) + b"e"
    raise TypeError(value)


def info_bytes(torrent):
    """The info dictionary exactly as the .torrent holds it (its hash is the info hash)."""
    if torrent[:1] != b"d":
        raise SystemExit("not a .torrent")
    at = 1
    while torrent[at:at + 1] != b"e":
        key, at = decode(torrent, at)
        start = at
        _, at = decode(torrent, at)
        if key == b"info":
            return torrent[start:at]
    raise SystemExit("no info dictionary in the .torrent")


def read_exact(conn, size):
    data = b""
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise ConnectionError("closed")
        data += chunk
    return data


def send_extended(conn, ext_id, payload):
    conn.sendall(struct.pack(">IBB", len(payload) + 2, 20, ext_id) + payload)


def serve(conn, info, info_hash):
    with conn:
        try:
            shake = read_exact(conn, 68)
            if shake[28:48] != info_hash:
                return
            reserved = bytearray(8)
            reserved[5] |= 0x10  # BEP 10: the extension protocol
            conn.sendall(b"\x13BitTorrent protocol" + bytes(reserved) + info_hash
                         + b"-OM0001-" + os.urandom(12))
            send_extended(conn, 0, encode({"m": {"ut_metadata": OUR_UT_METADATA},
                                           "metadata_size": len(info), "v": "omaseed walk"}))
            theirs = None
            while True:
                (size,) = struct.unpack(">I", read_exact(conn, 4))
                if size == 0:
                    continue
                message = read_exact(conn, size)
                if message[0] != 20:
                    continue
                body, _ = decode(message, 2)
                if message[1] == 0:
                    theirs = body.get(b"m", {}).get(b"ut_metadata")
                elif message[1] == OUR_UT_METADATA and theirs and body.get(b"msg_type") == 0:
                    piece = body[b"piece"]
                    chunk = info[piece * PIECE:(piece + 1) * PIECE]
                    send_extended(conn, theirs, encode({"msg_type": 1, "piece": piece,
                                                        "total_size": len(info)}) + chunk)
        except (ConnectionError, OSError, ValueError, KeyError):
            return


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__.strip())
    with open(sys.argv[1], "rb") as f:
        torrent = f.read()
    port = int(sys.argv[2])
    info = info_bytes(torrent)
    info_hash = hashlib.sha1(info).digest()
    name = decode(info)[0].get(b"name", b"").decode()
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(8)
    print("magnet:?xt=urn:btih:%s&dn=%s&x.pe=127.0.0.1:%d"
          % (info_hash.hex(), urllib.parse.quote(name), port), flush=True)
    while True:
        conn, _ = listener.accept()
        threading.Thread(target=serve, args=(conn, info, info_hash), daemon=True).start()


if __name__ == "__main__":
    main()
