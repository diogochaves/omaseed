#!/usr/bin/env python3
"""#87 (board 47 §E/§F, C25 K21, K24): what Deluge 2.2 does with a move through deluge-web, and
the stage for E6 step 11c's shots of it in the app.

  python3 moves.py check            # the three moves below, printed, then everything removed
  python3 moves.py stage            # leaves them ready for a box: torrents and folders
  python3 moves.py throttle on|off  # deluged stopped 0.9 s of every 0.95 s, so a copy lasts
  python3 moves.py clean            # removes what `stage` added

Needs the Deluge test server (`scripts/dev/test-server.sh start --deluge`, :8086). Its files go
under /tmp/omaseed-87-moves (a tmpfs) and Deluge's profile (the home disk), so a move between
them is a copy.

check:
1. Into a folder Deluge would have to make and can't (under a 0555 folder): core.move_storage
   answers null; 5 s later the torrent never read Moving, its save_path is unchanged, and its
   state is what it was. deluged's log alone says why.
2. Into an existing 0555 folder: state Error, message "Failed to move download folder:
   Permission denied", paused; a resume leaves the Error (paused false); a recheck clears it.
3. Two torrents from the home disk to the tmpfs, deluged throttled: 16 files of 64 MiB, and one
   file of 256 MiB. State Moving, its `progress` (bytes at the destination over bytes done)
   read every 0.3 s; then save_path is the new folder and move_completed is off.

stage: "Move Big" (16 files, 1 GiB, seeding, in Deluge's profile, move-when-done on to a
folder), "Move One.bin" (256 MiB, one file, beside it),
"Move Small.bin" (seeding in /tmp/omaseed-87-moves/home), the 0555 folders
/tmp/omaseed-87-moves/locked (empty) and /tmp/omaseed-87-moves/readonly, and
/tmp/omaseed-87-moves/fast for the big one's move.
"""

import base64
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "153-engines-board47"))
import rpc  # noqa: E402

WORK = "/tmp/omaseed-87-moves"
PROFILE = os.path.expanduser("~/.local/share/omaseed-test/deluge")
BIG_HOME = os.path.join(PROFILE, "research-87-moves")
THROTTLE = os.path.join(WORK, "throttle.pid")
FIELDS = ["name", "state", "progress", "save_path", "message", "paused", "move_completed"]


def deluged_pid():
    with open(os.path.join(PROFILE, "omaseed-deluged.pid"), encoding="utf-8") as f:
        return int(f.read().split(";")[0].strip())


def throttle(on):
    if os.path.exists(THROTTLE):
        with open(THROTTLE, encoding="utf-8") as f:
            pid = int(f.read())
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        os.remove(THROTTLE)
        os.kill(deluged_pid(), signal.SIGCONT)
    if on:
        os.makedirs(WORK, exist_ok=True)
        loop = f"while kill -STOP {deluged_pid()}; do sleep 0.9; kill -CONT {deluged_pid()}; sleep 0.05; done"
        child = subprocess.Popen(["bash", "-c", loop], start_new_session=True,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(THROTTLE, "w", encoding="utf-8") as f:
            f.write(str(child.pid))


def add(deluge, source, name, size, where, options=None, files=1):
    """A torrent of `size` bytes named `name` in `where` (one file, or a folder of `files`
    equal files), added seeding (seed_mode)."""
    path = os.path.join(where, name)
    for i in range(files):
        one = path if files == 1 else os.path.join(path, f"part-{i + 1:02}.bin")
        if not os.path.exists(one) or os.path.getsize(one) != size // files:
            rpc.random_file(one, size // files)
    torrent = rpc.make_torrent(path, os.path.join(source, name + ".torrent"), piece_kib=4096)
    with open(torrent, "rb") as f:
        meta = base64.b64encode(f.read()).decode()
    opts = {"download_location": where, "seed_mode": True}
    opts.update(options or {})
    return deluge.call("core.add_torrent_file", [name + ".torrent", meta, opts])


def status(deluge, tid):
    return deluge.call("core.get_torrent_status", [tid, FIELDS])


def folders():
    for sub in ("home", "fast", "locked", "readonly", "torrents", "done"):
        os.makedirs(os.path.join(WORK, sub), exist_ok=True)
    os.makedirs(BIG_HOME, exist_ok=True)
    os.chmod(os.path.join(WORK, "locked"), 0o555)
    os.chmod(os.path.join(WORK, "readonly"), 0o555)


def stage(deluge):
    folders()
    big = add(deluge, os.path.join(WORK, "torrents"), "Move Big", 1 << 30, BIG_HOME,
              {"move_completed": True, "move_completed_path": os.path.join(WORK, "done")}, files=16)
    one = add(deluge, os.path.join(WORK, "torrents"), "Move One.bin", 256 << 20, BIG_HOME)
    small = add(deluge, os.path.join(WORK, "torrents"), "Move Small.bin", 4 << 20,
                os.path.join(WORK, "home"))
    return big, small, one


def clean(deluge):
    throttle(False)
    for tid, t in deluge.call("core.get_torrents_status", [{}, ["name"]]).items():
        if t["name"].startswith("Move "):
            deluge.call("core.remove_torrent", [tid, True])
    for sub in ("locked", "readonly"):
        path = os.path.join(WORK, sub)
        if os.path.isdir(path):
            os.chmod(path, 0o755)
    subprocess.run(["rm", "-rf", WORK, BIG_HOME], check=False)


def check(deluge):
    print(f"Deluge {deluge.call('daemon.get_version')} "
          f"(libtorrent {deluge.call('core.get_libtorrent_version')}) through deluge-web")
    big, small, one = stage(deluge)
    try:
        before = status(deluge, small)
        answer = deluge.call("core.move_storage", [[small], os.path.join(WORK, "locked", "new")])
        seen = set()
        for _ in range(25):
            seen.add(status(deluge, small)["state"])
            time.sleep(0.2)
        after = status(deluge, small)
        print(f"1. into a folder it can't make: answer {answer!r}; states seen {sorted(seen)}; "
              f"save_path {'unchanged' if after['save_path'] == before['save_path'] else 'changed'}")

        deluge.call("core.move_storage", [[small], os.path.join(WORK, "readonly")])
        failed = rpc.wait(lambda: (s := status(deluge, small))["state"] == "Error" and s, 10, 0.2, "Error")
        print(f"2. into a read-only folder: {failed['state']}, “{failed['message']}”, paused {failed['paused']}")
        deluge.call("core.resume_torrent", [[small]])
        time.sleep(1)
        resumed = status(deluge, small)
        print(f"   resumed: {resumed['state']}, paused {resumed['paused']}")
        deluge.call("core.force_recheck", [[small]])
        cleared = rpc.wait(lambda: (s := status(deluge, small))["state"] not in ("Error", "Checking") and s,
                           30, 0.2, "the recheck")
        print(f"   rechecked: {cleared['state']}, “{cleared['message']}”")

        for label, tid in (("16 files, 1 GiB", big), ("one file, 256 MiB", one)):
            throttle(True)
            deluge.call("core.move_storage", [[tid], os.path.join(WORK, "fast")])
            readings = []
            start = time.monotonic()
            while time.monotonic() - start < 120:
                s = status(deluge, tid)
                if s["state"] == "Moving":
                    readings.append(round(s["progress"]))
                elif readings or s["save_path"].startswith(os.path.join(WORK, "fast")):
                    break
                time.sleep(0.3)
            throttle(False)
            s = status(deluge, tid)
            print(f"3. {label}, throttled: Moving {len(readings)} readings in "
                  f"{time.monotonic() - start:.0f} s, progress {readings}; then {s['state']}, "
                  f"save_path {rpc.shown(s['save_path'])}, move_completed {s['move_completed']}")
    finally:
        clean(deluge)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    what = sys.argv[1]
    if what == "throttle":
        throttle(sys.argv[2:3] == ["on"])
        return
    deluge = rpc.Deluge()
    if what == "check":
        check(deluge)
    elif what == "stage":
        for name, tid in zip(("Move Big", "Move Small.bin", "Move One.bin"), stage(deluge)):
            print(name, tid)
    elif what == "clean":
        clean(deluge)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
