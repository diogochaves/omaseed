#!/usr/bin/env bash
# omaseed — a throwaway desktop for manual tests: omaseed installed as a user would have it,
# its own qBittorrent, and torrents and magnet links to open with a double click.
#
#   scripts/dev/manual-box.sh               # Diogo: the box is a window on workspace 9
#   scripts/dev/manual-box.sh --headless    # agents: the same box without a window (drive it
#                                           # with omabox keys / click / shot -b manual-box)
#   scripts/dev/manual-box.sh down          # throw it all away
#   scripts/dev/app-box.sh api -b manual-box torrents/info   # ask the box's qBittorrent
#
# What is in the box:
# - omaseed from build-box/ (Debug, tests off), installed with `cmake --install` into the box's
#   ~/.local: the binary, the .desktop entry, the D-Bus service and the icons. Nothing is
#   registered for magnet: or .torrent: first run's "links" step does that, as for a user.
# - qbittorrent-nox, with a config as a user's own (~/.config/qBittorrent), its Web UI on
#   127.0.0.1:BOX_QBT_PORT with a login (admin and a random password, printed at the end and
#   kept in the box's ~/qbittorrent-login.txt). First run finds it: the process, then the port
#   from that config.
# - ~/Downloads: five of the swarm's .torrent files, and links.html with the Public Domain
#   Sampler's magnet (the only swarm torrent that is not private, so the magnet gets its
#   metadata). Open them from Files (Nautilus) and Chromium, both in the box.
#
# The box's network is its own (--net isolated): its 127.0.0.1 is not this machine's, and the
# only ports it reaches here are the swarm's tracker and seeder, so the box's qBittorrent
# downloads for real, offline. The real library cannot be reached from inside. `down`, or
# closing the window, deletes everything: the config, the keyring, the downloads.
#
# The setup itself lives in scripts/dev/app-box.sh (`up --manual`); OMASEED_MANUAL_BOX names
# another box, so an agent can try this script without touching the one Diogo is using.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app_box="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_MANUAL_BOX:-manual-box}"

case "${1:-}" in
"") exec "$app_box" up --manual --interactive -b "$BOX" ;;
--headless) exec "$app_box" up --manual --headless -b "$BOX" ;;
down) exec "$app_box" down -b "$BOX" ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) echo "manual-box.sh: unknown argument $1 (see --help)" >&2; exit 2 ;;
esac
