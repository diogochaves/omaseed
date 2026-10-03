#!/usr/bin/env bash
# omaseed — a throwaway desktop for manual tests: omaseed installed as a user would have it,
# two qBittorrents of its own, and torrents and magnet links to open with a double click.
#
#   scripts/dev/manual-box.sh               # Diogo: the box is a window on workspace 9
#   scripts/dev/manual-box.sh --headless    # agents: the same box without a window (drive it
#                                           # with omabox keys / click / shot -b manual-box)
#   scripts/dev/manual-box.sh [--headless] --stock-bar   # Omarchy's default bar, not a copy of yours
#   scripts/dev/manual-box.sh down          # throw it all away
#   scripts/dev/app-box.sh api -b manual-box torrents/info   # ask the box's "public" qBittorrent
#   scripts/dev/app-box.sh api -b manual-box --server private torrents/info   # and "private"
#
# What is in the box:
# - omaseed from build-box/ (Debug, tests off), installed with `cmake --install` into the box's
#   ~/.local: the binary, the .desktop entry, the D-Bus service and the icons. Nothing is
#   registered for magnet: or .torrent: first run's "links" step does that, as for a user.
# - Two qbittorrent-nox, as people who keep public and private trackers apart run them, each
#   with its own login (admin and a random password, printed at the end; both in the box's
#   ~/qbittorrent-login.txt):
#   - "public" on 127.0.0.1:8090, with a config as a user's own (~/.config/qBittorrent),
#     saving to ~/Downloads, empty. First run finds it: the process, then the port from that
#     config. The Public Domain Sampler (links.html) is its torrent.
#   - "private" on 127.0.0.1:8091, its own --profile (~/.local/share/qbittorrent-private),
#     ports and save path (~/Downloads/private), already downloading four of the swarm's
#     private torrents. Add it in Settings › Servers (a) to switch between the two.
# - ~/Downloads: five of the swarm's .torrent files, and links.html with the Public Domain
#   Sampler's magnet (the only swarm torrent that is not private, so the magnet gets its
#   metadata). Open them from Files (Nautilus) and Chromium, both in the box.
#
# The box's network is its own (--net isolated): its 127.0.0.1 is not this machine's, and the
# only ports it reaches here are the swarm's tracker and seeder, so the box's qBittorrents
# download for real, offline. The real library cannot be reached from inside. `down`, or
# closing the window, deletes everything: the config, the keyring, the downloads.
#
# The setup itself lives in scripts/dev/app-box.sh (`up --manual`); OMASEED_MANUAL_BOX names
# another box, so an agent can try this script without touching the one Diogo is using.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app_box="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_MANUAL_BOX:-manual-box}"

bar=()
for arg in "$@"; do [ "$arg" = --stock-bar ] && bar=(--stock-bar); done
case "${1:-}" in
"" | --stock-bar)
    [ "${2:-}" = --headless ] && exec "$app_box" up --manual --headless -b "$BOX" "${bar[@]}"
    exec "$app_box" up --manual --interactive -b "$BOX" "${bar[@]}" ;;
--headless) exec "$app_box" up --manual --headless -b "$BOX" "${bar[@]}" ;;
down) exec "$app_box" down -b "$BOX" ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) echo "manual-box.sh: unknown argument $1 (see --help)" >&2; exit 2 ;;
esac
