#!/usr/bin/env python3
"""K10 (board 47 §3): does connecting deluge-web to a daemon switch it for deluge-web's other
users?

  python3 k10_deluge_daemon_switch.py

A second, empty deluged is started for this (its own config under $TMPDIR or /tmp, RPC on
127.0.0.1:58848, no torrents) and added to the :8086 deluge-web's host list. Two deluge-web
sessions sign in (A and B, two cookie jars, the same password). A connects to the second
daemon with web.connect; then B, which did nothing, is asked which daemon it is on (its
daemon_port) and how many torrents it sees. Printed before and after; then A connects back to
the test server's own daemon, the host is removed from the list and the second deluged
stopped.
"""

import os
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpc  # noqa: E402

SECOND_PORT = 58848


HOME_LOCATION = None


def where(session):
    """Which daemon `session` is on, told apart by where each saves (the second one keeps
    Deluge's default), and how many torrents it sees."""
    location = session.call("core.get_config_value", ["download_location"])
    count = len(session.call("core.get_torrents_status", [{}, ["name"]]))
    name = "the test server's daemon" if location == HOME_LOCATION else "the second daemon"
    return f"{name}, {count} torrents"


def main():
    global HOME_LOCATION
    a, b = rpc.Deluge(), rpc.Deluge()
    HOME_LOCATION = a.call("core.get_config_value", ["download_location"])
    hosts = a.call("web.get_hosts")
    home_id = next(h[0] for h in hosts if a.call("web.connected"))
    print("before: A", where(a), "· B", where(b))

    config = os.path.join(tempfile.gettempdir(), "omaseed-k10-deluged")
    shutil.rmtree(config, ignore_errors=True)
    os.makedirs(config)
    # A user of its own: deluge-web swaps a localhost host's "localclient" for the login in its
    # own config dir, which is the test server's daemon's.
    user, password = "omaseed-k10", secrets.token_urlsafe(12)
    fd = os.open(os.path.join(config, "auth"), os.O_WRONLY | os.O_CREAT, 0o600)
    with os.fdopen(fd, "w") as auth:
        auth.write(f"{user}:{password}:10\n")
    log = open(os.path.join(config, "deluged.log"), "w")
    daemon = subprocess.Popen(["deluged", "--do-not-daemonize", "--config", config, "--port", str(SECOND_PORT),
                               "--ui-interface", "127.0.0.1", "--interface", "127.0.0.1"],
                              stdout=log, stderr=subprocess.STDOUT)
    second_id = None
    try:
        rpc.wait(lambda: subprocess.run(["bash", "-c", f"exec 3<>/dev/tcp/127.0.0.1/{SECOND_PORT}"],
                                        stderr=subprocess.DEVNULL).returncode == 0, 30, 0.2, "the second daemon")
        added = a.call("web.add_host", ["127.0.0.1", SECOND_PORT, user, password])
        second_id = added[1] if isinstance(added, list) else added
        a.call("web.connect", [second_id])
        rpc.wait(lambda: a.call("web.connected"), 20, 0.2, "A connected to the second daemon")
        print("after A's web.connect to the second daemon: A", where(a), "· B", where(b))
        print("B's web.get_hosts statuses:",
              [b.call("web.get_host_status", [h[0]])[1] for h in b.call("web.get_hosts")])
    finally:
        try:
            a.call("web.connect", [home_id])
            rpc.wait(lambda: a.call("web.connected"), 20, 0.2, "back on the test server's daemon")
            if second_id:
                a.call("web.remove_host", [second_id])
            print("restored: A", where(a), "· B", where(b))
        finally:
            daemon.send_signal(signal.SIGTERM)
            daemon.wait(20)
            shutil.rmtree(config, ignore_errors=True)


if __name__ == "__main__":
    main()
