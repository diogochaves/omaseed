#!/usr/bin/env python3
"""omaseed — the local swarm's BitTorrent tracker (HTTP, BEP 3 / BEP 23 compact peers).

Listens on 127.0.0.1 only, so the swarm never reaches the network. qBittorrent's own
embedded tracker binds every interface, which is why this exists.

It answers /announce like any tracker, plus two things a test needs, both read from a JSON
control file on every request (scripts/dev/swarm.sh writes it):

    {"fail": {"<info hash hex>": "message", ...},   # answer with a failure reason
     "exemptPorts": [26997],                       # ...except to these peers (the seeder)
     "extraSeeds": 0, "extraLeechers": 0}          # added to complete/incomplete

A hybrid torrent announces twice, under its v1 hash and its v2 hash cut to 20 bytes, so a
failure has to name both.

    swarm-tracker.py PORT CONTROL_FILE
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

INTERVAL = 10  # asked for; libtorrent announces every 5 min at most anyway (min_announce_interval)
PEER_TTL = 3600  # so a peer is kept until it says "stopped"; swarm.sh forces reannounces

peers = {}  # info hash bytes -> {peer id bytes: (ip, port, left, seen)}


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


def read_control(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


class Handler(BaseHTTPRequestHandler):
    control_path = ""

    def log_message(self, *args):
        pass

    def note(self, info_hash, event, left, outcome):
        # One line per announce on stderr (swarm.sh sends it to tracker.log).
        print(f"{time.strftime('%H:%M:%S')} {info_hash.hex()[:12]} {event or '-'} left={left} {outcome}",
              file=sys.stderr, flush=True)

    def reply(self, payload):
        body = bencode(payload)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path != "/announce":
            self.send_error(404)
            return
        # info_hash and peer_id are raw bytes, percent-encoded: decode as latin-1 to keep them.
        query = parse_qs(url.query, encoding="latin-1")
        try:
            info_hash = query["info_hash"][0].encode("latin-1")
            peer_id = query["peer_id"][0].encode("latin-1")
            port = int(query["port"][0])
        except (KeyError, ValueError):
            self.reply({"failure reason": "missing info_hash, peer_id or port"})
            return
        left = int(query.get("left", ["0"])[0])
        event = query.get("event", [""])[0]

        control = read_control(self.control_path)
        failure = control.get("fail", {}).get(info_hash.hex())
        if failure and port not in control.get("exemptPorts", []):
            self.note(info_hash, event, left, "fail")
            self.reply({"failure reason": failure})
            return

        now = time.time()
        swarm = peers.setdefault(info_hash, {})
        for pid in [p for p, v in swarm.items() if now - v[3] > PEER_TTL]:
            del swarm[pid]
        if event == "stopped":
            swarm.pop(peer_id, None)
        else:
            swarm[peer_id] = (self.client_address[0], port, left, now)

        others = [v for p, v in swarm.items() if p != peer_id]
        compact = b"".join(
            bytes(int(x) for x in ip.split(".")) + p.to_bytes(2, "big")
            for ip, p, _, _ in others
        )
        seeds = sum(1 for v in swarm.values() if v[2] == 0)
        self.note(info_hash, event, left, f"ok peers={len(others)}")
        self.reply({
            "interval": INTERVAL,
            "min interval": INTERVAL,
            "complete": seeds + int(control.get("extraSeeds", 0)),
            "incomplete": len(swarm) - seeds + int(control.get("extraLeechers", 0)),
            "peers": compact,
        })


def main():
    port, Handler.control_path = int(sys.argv[1]), sys.argv[2]
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
