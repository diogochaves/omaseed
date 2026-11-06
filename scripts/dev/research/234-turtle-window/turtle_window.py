"""#234: what Transmission's turtle clock (alt_speed_time_*) does on a live daemon around midnight,
without waiting for a real midnight.

Usage: scripts/dev/test-server.sh start --transmission
       python3 -I scripts/dev/research/234-turtle-window/turtle_window.py

Each case sets a window relative to the daemon's clock (this computer's: the test server is a
user process) and reads alt_speed_enabled until it settles. Before a case the turtle is put in
the state opposite to the one omaseed's AltSchedule predicts (a window that surely contains
now, or one that surely doesn't), so the daemon has to act for the case to come out as omaseed
reads it; when it doesn't act, the state is watched past a minute boundary before it counts.
Then the clock: a window whose edge is the next minute, read across that minute (how long after
it the daemon moves), and a hand toggle against the schedule, watched past a minute. The
daemon's alt_speed_* settings are written back at the end.

Only the test server on :8085 (OMASEED_TRANSMISSION_RPC overrides; :8080 is refused).
"""

import datetime
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "153-engines-board47"))
from rpc import Transmission  # noqa: E402

KEYS = ["alt_speed_enabled", "alt_speed_time_enabled", "alt_speed_time_begin",
        "alt_speed_time_end", "alt_speed_time_day"]
SETTLE = 75  # seconds watched when the state doesn't move: one scheduler minute and more


def now_minute():
    at = datetime.datetime.now()
    return at.hour * 60 + at.minute, at


def bit(day):
    """Transmission's mask bit for a date: Sunday 1 << 0 … Saturday 1 << 6."""
    return 1 << ((day.weekday() + 1) % 7)


def turtle(rpc):
    return rpc.call("session_get", {"fields": ["alt_speed_enabled"]})["alt_speed_enabled"]


def window(rpc, begin, end, mask):
    rpc.call("session_set", {"alt_speed_time_begin": begin % 1440, "alt_speed_time_end": end % 1440,
                             "alt_speed_time_day": mask, "alt_speed_time_enabled": True})


def settle(rpc, want):
    """Reads until the turtle is `want` (seconds taken) or SETTLE seconds pass (None)."""
    start = time.monotonic()
    while time.monotonic() - start < SETTLE:
        if turtle(rpc) == want:
            return round(time.monotonic() - start, 1)
        time.sleep(1)
    return None


def next_minute():
    """Waits for a minute with at least 20 s left; returns (minute, the next one's start)."""
    while datetime.datetime.now().second > 40:
        time.sleep(1)
    n, at = now_minute()
    return n, at.replace(second=0, microsecond=0) + datetime.timedelta(minutes=1)


def across(rpc, name, begin, end, mask, before, after):
    """A window with an edge at the next minute: `before` now, `after` past the edge."""
    n, edge = next_minute()
    window(rpc, n + begin, n + end, mask)
    first = turtle(rpc)
    moved = None
    while datetime.datetime.now() < edge + datetime.timedelta(seconds=20):
        state = turtle(rpc)
        if state != first and moved is None:
            moved = (datetime.datetime.now() - edge).total_seconds()
        time.sleep(0.25)
    state = turtle(rpc)
    print(f"{name:58} {(n + begin) % 1440:5} {(n + end) % 1440:5} {mask:4}  "
          f"{'on' if before else 'off'}→{'on' if after else 'off':4} "
          f"{'on' if first else 'off'}→{'on' if state else 'off':4} "
          + (f"moved {moved:+.1f}s from the minute" if moved is not None else "didn't move"))


def main():
    rpc = Transmission()
    saved = rpc.call("session_get", {"fields": KEYS})
    version = rpc.call("session_get", {"fields": ["version"]})["version"]
    n, at = now_minute()
    if not 130 <= n <= 1440 - 130:
        raise SystemExit("run it at least 130 minutes from midnight: the cases offset now by up to 120")
    today, yesterday = bit(at), bit(at - datetime.timedelta(days=1))
    print(f"Transmission {version}, now {at:%a %H:%M} (minute {n}), today's bit {today}, "
          f"yesterday's {yesterday}")

    # (name, begin, end, mask, omaseed's reading of now), offsets from now in minutes
    cases = [
        ("wrap, now after midnight (yesterday's window), every day", 60, 30, 127, True),
        ("wrap, now after midnight, yesterday only", 60, 30, yesterday, True),
        ("wrap, now after midnight, today only", 60, 30, today, False),
        ("wrap, now before midnight (today's window), every day", -60, -120, 127, True),
        ("wrap, now before midnight, today only", -60, -120, today, True),
        ("wrap, now before midnight, yesterday only", -60, -120, yesterday, False),
        ("wrap, now outside it, every day", 60, -60, 127, False),
        ("begin == end, before now, every day", -30, -30, 127, True),
        ("begin == end, after now, yesterday only", 30, 30, yesterday, True),
        ("begin == end, after now, today only", 30, 30, today, False),
        ("same day, now inside, today only", -30, 30, today, True),
        ("same day, now inside, every day but today", -30, 30, 127 & ~today, False),
        ("same day, now inside, no day", -30, 30, 0, False),
    ]
    try:
        print(f"{'case':58} {'begin':>5} {'end':>5} {'mask':>4}  omaseed  daemon  took")
        for name, begin, end, mask, predicted in cases:
            n, _ = now_minute()
            # The opposite state first, by a window that surely says it.
            if predicted:
                window(rpc, n + 300, n + 360, 127)
            else:
                window(rpc, n - 30, n + 30, 127)
            if settle(rpc, not predicted) is None:
                raise SystemExit(f"{name}: the baseline didn't take")
            window(rpc, n + begin, n + end, mask)
            took = settle(rpc, predicted)
            state = turtle(rpc)
            print(f"{name:58} {(n + begin) % 1440:5} {(n + end) % 1440:5} {mask:4}  "
                  f"{'on' if predicted else 'off':7}  {'on' if state else 'off':6}  "
                  f"{'%ss' % took if took is not None else 'never (watched %ss)' % SETTLE}")

        print(f"\nby the clock{'':46} {'begin':>5} {'end':>5} {'mask':>4}  omaseed  daemon")
        across(rpc, "same day, begins next minute, every day", 1, 30, 127, False, True)
        across(rpc, "wrap, yesterday's ends next minute, every day", 60, 1, 127, True, False)
        across(rpc, "begin == end next minute, yesterday only", 1, 1, yesterday, True, False)
        across(rpc, "begin == end next minute, every day", 1, 1, 127, True, True)

        n, _ = now_minute()
        window(rpc, n - 30, n + 30, 127)
        settle(rpc, True)
        rpc.call("session_set", {"alt_speed_enabled": False})
        start = time.monotonic()
        back = None
        while time.monotonic() - start < SETTLE:
            if turtle(rpc):
                back = round(time.monotonic() - start, 1)
                break
            time.sleep(1)
        print(f"\nturned off by hand inside the window: "
              + (f"back on after {back}s" if back is not None else f"stays off ({SETTLE}s watched)"))
    finally:
        rpc.call("session_set", {k: v for k, v in saved.items() if k != "alt_speed_enabled"})
        rpc.call("session_set", {"alt_speed_enabled": saved["alt_speed_enabled"]})
        print("restored:", rpc.call("session_get", {"fields": KEYS}))


if __name__ == "__main__":
    main()
