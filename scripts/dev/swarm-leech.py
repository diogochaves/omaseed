#!/usr/bin/env python3
"""omaseed — the local swarm's churn loop: keeps the playground (:8081) uploading.

scripts/dev/swarm.sh leech start runs it in the background; do not run it by hand. It drives
the leecher qBittorrent (:8084): it adds the fixtures the playground has finished (from the
same .torrent files the seeder shares), lets them download, deletes each one with its files
when it completes and adds it again after a random pause, forever. The tracker shows the
leecher only the playground (control key "isolate"), so every byte comes from :8081.

How busy it is changes in phases of one to four minutes: 0 to 3 downloads at once, each with a
per-torrent limit picked at random, so the playground's upload moves between torrents and has
quiet stretches. It exits on its own when the playground, the seeder or the leecher has not
answered for 30 s, or when its pid file no longer names it, and then shuts the leecher down.

    swarm-leech.py PIDFILE TORRENTS_DIR CATEGORY LEECHER_API PLAY_API SEEDER_API
"""
import json
import os
import random
import sys
import time
import urllib.parse
import urllib.request

TICK = 3            # seconds between looks
GONE_AFTER = 30     # seconds a server may be silent before the loop gives up
SEEDING = {"uploading", "stalledUP", "forcedUP", "queuedUP"}  # the playground can serve these
KIB = 1024

opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # loopback, never a proxy


def log(message):
    print(f"{time.strftime('%H:%M:%S')} {message}", flush=True)


def call(base, path, data=None, body=None, headers=None):
    if data is not None:
        body = urllib.parse.urlencode(data).encode()
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
    request = urllib.request.Request(base + path, data=body, headers=headers or {})
    with opener.open(request, timeout=10) as reply:
        return reply.read()


def get_json(base, path):
    return json.loads(call(base, path))


def answers(base):
    try:
        call(base, "/app/version")
        return True
    except OSError:
        return False


def add(leecher, torrent_file, dl_limit):
    boundary = "omaseed-leech-%016x" % random.getrandbits(64)
    with open(torrent_file, "rb") as f:
        payload = f.read()
    parts = [
        (b'Content-Disposition: form-data; name="torrents"; filename="fixture.torrent"\r\n'
         b"Content-Type: application/x-bittorrent\r\n\r\n" + payload),
        b'Content-Disposition: form-data; name="dlLimit"\r\n\r\n' + str(dl_limit).encode(),
    ]
    body = b"".join(b"--" + boundary.encode() + b"\r\n" + p + b"\r\n" for p in parts)
    body += b"--" + boundary.encode() + b"--\r\n"
    call(leecher, "/torrents/add", body=body,
         headers={"Content-Type": "multipart/form-data; boundary=" + boundary})


def ours(pidfile):
    try:
        with open(pidfile) as f:
            return f.read().strip() == str(os.getpid())
    except OSError:
        return False


def tick(rng, cooldown, target, phase_end, now, torrents, category, leecher, play):
    """One look: drop what finished, follow the phase, add or hold to match it."""
    pool = {t["name"]: t for t in get_json(play, f"/torrents/info?category={category}")
            if t["progress"] >= 1 and t["state"] in SEEDING
            and os.path.isfile(os.path.join(torrents, t["name"] + ".torrent"))}
    mine = get_json(leecher, "/torrents/info")

    for t in list(mine):
        done = t["progress"] >= 1
        if done or t["name"] not in pool or t["state"] in ("error", "missingFiles"):
            call(leecher, "/torrents/delete", {"hashes": t["hash"], "deleteFiles": "true"})
            pause = rng.uniform(5, 45)
            cooldown[t["name"]] = now + pause
            mine.remove(t)
            log(f"{'finished' if done else 'dropped '} {t['name'][:40]}: again in {pause:.0f} s")

    if now >= phase_end:
        target = rng.choices([0, 1, 2, 3], weights=[2, 4, 3, 2])[0]
        phase_end = now + rng.uniform(60, 240)
        log(f"phase: {target} at once for {phase_end - now:.0f} s")

    running = [t for t in mine if not t["state"].startswith("stopped")]
    stopped = [t for t in mine if t["state"].startswith("stopped")]
    rng.shuffle(running)
    rng.shuffle(stopped)
    if len(running) > target:
        extra = running[target:]
        call(leecher, "/torrents/stop", {"hashes": "|".join(t["hash"] for t in extra)})
        log("held " + ", ".join(t["name"][:30] for t in extra))
    wanted = target - len(running)
    for t in stopped[:max(wanted, 0)]:
        call(leecher, "/torrents/start", {"hashes": t["hash"]})
        log(f"resumed {t['name'][:40]}")
        wanted -= 1
    held = {t["name"] for t in mine}
    free = [n for n in pool if n not in held and cooldown.get(n, 0) <= now]
    rng.shuffle(free)
    for name in free[:max(wanted, 0)]:
        limit = rng.choice([0, 256, 512, 1024, 1536])
        try:
            add(leecher, os.path.join(torrents, name + ".torrent"), limit * KIB)
            log(f"added   {name[:40]} ({f'{limit} KiB/s' if limit else 'no own limit'})")
        except OSError as e:
            cooldown[name] = now + 30
            log(f"adding {name[:40]} failed: {e}")
    return target, phase_end


def main():
    pidfile, torrents, category, leecher, play, seeder = sys.argv[1:7]
    with open(pidfile, "w") as f:
        f.write(f"{os.getpid()}\n")
    rng = random.Random()
    cooldown = {}           # fixture name -> earliest time it may be added again
    target, phase_end = 0, 0.0
    silent_since = None
    log("churn loop up")

    while ours(pidfile):
        now = time.time()
        down = [name for name, base in (("playground", play), ("seeder", seeder), ("leecher", leecher))
                if not answers(base)]
        if down:
            silent_since = silent_since or now
            if now - silent_since > GONE_AFTER:
                log(f"{', '.join(down)} gone for {GONE_AFTER} s: stopping")
                break
            time.sleep(TICK)
            continue
        silent_since = None

        try:
            target, phase_end = tick(rng, cooldown, target, phase_end, now,
                                     torrents, category, leecher, play)
        except (OSError, ValueError) as e:
            log(f"skipped a tick: {e}")
        time.sleep(TICK)

    if ours(pidfile):
        os.remove(pidfile)
        try:
            call(leecher, "/app/shutdown", {})
        except OSError:
            pass
    log("churn loop down")


if __name__ == "__main__":
    main()
