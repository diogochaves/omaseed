#!/usr/bin/env python3
"""Write a made-up omaseed status file (plugin/STATUS.md, schema v2) for the bar widget.

    scripts/dev/widget-status.py STATE [OUT] [--keep] [--name=NAME] [--servers]

STATE is one of: moving, seeding, idle, alt, error, unreachable, firewalled.
--name=NAME names the current server ("home" by default). --servers adds a second server
and the app's shared menu (docs/33 §11): the popup's head gets ⋯ and its server line ▾.
OUT is the file to write, $XDG_RUNTIME_DIR/omaseed/status.json by default (run it
inside an omabox: `omabox run -d -- scripts/dev/widget-status.py moving --keep`); the parent
directory is made 0700 and the file 0600, written atomically like the app does.
With --keep it rewrites the file every 5 s (so the widget never sees it go stale)
until killed. Example data throughout: the docs/19 drawing's names and numbers.
"""

import copy
import json
import os
import sys
import time

G = 1024 ** 3
M = 1024 ** 2
K = 1024


def base(now):
    return {
        "v": 2,
        "t": now,
        "pid": 48213,
        "app": {"version": "0.1.0", "window": "hidden"},
        "server": {"name": "home", "url": "127.0.0.1:8081", "version": "5.2.3",
                   "reachable": True, "error": "", "retryIn": 0},
        "reachable": "connected",
        "speed": {"dl": 1237319, "up": 2422210},
        "alt": {"on": False, "dlLimit": 1 * M, "upLimit": 200 * K},
        "counts": {"all": 142, "downloading": 2, "seeding": 118, "completed": 121,
                   "stopped": 12, "stalled": 3, "checking": 0, "errored": 0},
        "disk": {"free": 1978765344768},
        "share": {"sessionUp": int(14.2 * G), "sessionDown": int(3.1 * G), "sessionRatio": 4.58,
                  "allTimeUp": int(9.2 * 1024 * G), "allTimeDown": int(4.0 * 1024 * G),
                  "allTimeRatio": 2.31, "onOffer": int(2.1 * 1024 * G)},
        "uploadHour": [int(2.0 * M + 1.2 * M * abs(((i * 37) % 23) - 11) / 11) for i in range(60)],
        "week": [{"day": time.strftime("%Y-%m-%d", time.localtime(now - (6 - i) * 86400)), "up": int(u * G)}
                 for i, u in enumerate([9.1, 14.8, 10.2, 19.7, 13.3, 22.5, 18.1])],
        "alive": {
            "buckets": [{"seeds": 1, "count": 2, "waiting": 2, "peers": 11, "quiet": 1},
                        {"seeds": 2, "count": 5, "waiting": 3, "peers": 30, "quiet": 2},
                        {"seeds": 3, "count": 9, "waiting": 2, "peers": 6, "quiet": 0},
                        {"seeds": 4, "count": 12, "waiting": 1, "peers": 3, "quiet": 4},
                        {"seeds": 5, "count": 7, "waiting": 1, "peers": 1, "quiet": 0}],
            "rows": [{"hash": "a1", "name": "Prelinger Archives: Duck and Cover (1951)", "seeds": 1, "waiting": 9},
                     {"hash": "a2", "name": "debian-3.0r0-i386-binary-1.iso", "seeds": 1, "waiting": 2},
                     {"hash": "a3", "name": "Field Recordings Vol. 3 [FLAC]", "seeds": 2, "waiting": 14},
                     {"hash": "a4", "name": "Night of the Living Dead (1968)", "seeds": 3, "waiting": 5}],
        },
        "topSeeders": [{"hash": "t1", "name": "Big Buck Bunny 4K (CC-BY)", "uploaded": int(5.4 * G), "ratio": 8.21},
                       {"hash": "t2", "name": "archlinux-2026.09.01-x86_64.iso", "uploaded": int(3.3 * G), "ratio": 3.4}],
        "active": [{"hash": "f1", "name": "archlinux-2026.10.01-x86_64.iso", "progress": 0.489, "dl": 1029379, "eta": 160},
                   {"hash": "f2", "name": "Cosmos Laundromat 2K (CC-BY)", "progress": 0.872, "dl": 207940, "eta": 425}],
        "recent": [{"kind": "completed", "hash": "r1", "name": "ubuntu-26.04.1-desktop-amd64.iso", "at": now - 720,
                    "size": int(6.11 * G), "category": "linux",
                    "path": "/data/torrents/linux/ubuntu-26.04.1-desktop-amd64.iso"},
                   {"kind": "milestone", "hash": "r2", "name": "Tears of Steel 4K (CC-BY)", "at": now - 3600,
                    "ratio": 1, "path": "/data/torrents/movies/Tears of Steel"},
                   {"kind": "completed", "hash": "r3", "name": "Sintel 4K (CC-BY)", "at": now - 2 * 86400,
                    "size": int(4.2 * G), "category": "movies", "path": ""}],
        "categories": [{"name": "linux", "savePath": "/data/torrents/linux"},
                       {"name": "movies", "savePath": ""},
                       {"name": "music", "savePath": "/data/music"}],
        "add": {"savePath": "/data/torrents", "autoTmm": True, "category": "linux"},
        "downloads": [{"path": os.path.expanduser("~/Downloads/debian-13.1.0-amd64-DVD-1.iso.torrent"),
                       "name": "debian-13.1.0-amd64-DVD-1.iso", "size": int(3.9 * G), "at": now - 300,
                       "inLibrary": False},
                      {"path": os.path.expanduser("~/Downloads/Sintel 4K (CC-BY).torrent"),
                       "name": "Sintel 4K (CC-BY).torrent", "size": int(4.2 * G), "at": now - 4000,
                       "inLibrary": True}],
    }


def status(state, now):
    s = base(now)
    if state == "moving":
        pass
    elif state == "seeding":
        s["speed"] = {"dl": 0, "up": 2422210}
        s["active"] = []
        s["counts"]["downloading"] = 0
    elif state == "idle":
        s["speed"] = {"dl": 0, "up": 0}
        s["active"] = []
        s["counts"]["downloading"] = 0
        s["uploadHour"] = [0] * 60
        s["downloads"] = []
    elif state == "alt":
        s["alt"]["on"] = True
        s["speed"] = {"dl": 1 * M, "up": 200 * K}
    elif state == "error":
        s["counts"]["errored"] = 2
    elif state == "unreachable":
        s["server"].update(reachable=False, error="connection refused", retryIn=7)
        s["speed"] = {"dl": 0, "up": 0}
    elif state == "firewalled":
        s["reachable"] = "firewalled"
    else:
        raise SystemExit(f"unknown state {state!r}")
    return copy.deepcopy(s)


def with_servers(s, now):
    name = s["server"]["name"]
    s["servers"] = [{"id": "{s-1}", "name": name, "current": True, "state": "ok",
                     "count": s["counts"]["all"], "seen": now, "reason": ""},
                    {"id": "{s-2}", "name": "private", "current": False, "state": "ok",
                     "count": 4, "seen": now - 3600, "reason": ""}]

    def row(id, label, kind="action", checked=False, enabled=True, detail=""):
        return {"id": id, "label": label, "kind": kind, "checked": checked, "enabled": enabled,
                "detail": detail, "urgent": False}

    s["menu"] = [row("servers", "Servers", "caption"),
                 row("server:{s-1}", name, "radio", True, detail=f"{s['counts']['all']} torrents"),
                 row("server:{s-2}", "private", "radio", detail="4 torrents"),
                 row("separator:servers", "", "separator"),
                 row("alt-speed", "Alternative Speed Limits", "check", s["alt"]["on"]),
                 row("stop-all", "Stop All"), row("start-all", "Start All"),
                 row("separator:actions", "", "separator"),
                 row("add", "Add Torrent…"), row("settings", "Settings…"),
                 row("separator:quit", "", "separator"),
                 row("quit", "Quit omaseed")]
    return s


def write(path, data):
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    tmp = path + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as out:
        json.dump(data, out, indent=1)
    os.replace(tmp, path)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) not in (1, 2):
        raise SystemExit(__doc__)
    state = args[0]
    out = args[1] if len(args) == 2 else os.path.join(os.environ["XDG_RUNTIME_DIR"], "omaseed", "status.json")
    keep = "--keep" in sys.argv
    name = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--name=")), "")
    servers = "--servers" in sys.argv
    while True:
        now = int(time.time())
        s = status(state, now)
        if name:
            s["server"]["name"] = name
        if servers:
            s = with_servers(s, now)
        write(out, s)
        if not keep:
            return
        time.sleep(5)


if __name__ == "__main__":
    main()
