#!/usr/bin/env bash
# omaseed — a throwaway desktop for manual tests: omaseed installed as a user would have it,
# its own qBittorrent, and torrents and magnet links to open with a double click.
#
#   scripts/dev/manual-box.sh               # Diogo: the box is a window on workspace 9
#   scripts/dev/manual-box.sh --headless    # agents: the same box without a window (drive it
#                                           # with omabox keys / click / shot -b manual-box)
#   scripts/dev/manual-box.sh down          # throw it all away
#
# What is in the box:
# - omaseed from build-box/ (Debug, tests off), installed with `cmake --install` into the box's
#   ~/.local: the binary, the .desktop entry, the D-Bus service and the icons. Nothing is
#   registered for magnet: or .torrent: first run's "links" step does that, as for a user.
# - qbittorrent-nox, with a config as a user's own (~/.config/qBittorrent), its Web UI on
#   127.0.0.1:BOX_QBT_PORT with a login (admin and a random password, printed at the end and
#   kept in the box's ~/qbittorrent-login.txt). First run finds it: the process, then the port
#   from that config.
# - ~/Downloads: four of the swarm's .torrent files, and links.html with the Public Domain
#   Sampler's magnet (the only swarm torrent that is not private, so the magnet gets its
#   metadata). Open them from Files (Nautilus) and Chromium, both in the box.
#
# The box's network is its own (--net isolated): its 127.0.0.1 is not this machine's, and the
# only ports it reaches here are the swarm's tracker and seeder, so the box's qBittorrent
# downloads for real, offline. The real library cannot be reached from inside. `down`, or
# closing the window, deletes everything: the config, the keyring, the downloads.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
BOX=manual-box
BUILD=build-box
BOX_QBT_PORT="${OMASEED_BOX_QBT_PORT:-8090}"
BOX_BT_PORT=26990
TRACKER_PORT=26996
SEEDER_PORT=8083
SEEDER_BT_PORT=26997
SEEDER="http://127.0.0.1:$SEEDER_PORT/api/v2"
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
BOX_HOME=/home/sbx
# Not 8080 even inside the box: the rule is about the port, and a box started without
# isolation by mistake would put this server beside the real library.
refuse_home "$BOX_QBT_PORT" "$SEEDER"

MODE=interactive
case "${1:-}" in
"") ;;
--headless) MODE=headless ;;
down) omabox down -b "$BOX"; exit 0 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) echo "manual-box.sh: unknown argument $1 (see --help)" >&2; exit 2 ;;
esac

echo "building omaseed for the box ($BUILD/, installed under $BOX_HOME/.local)"
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
        -DCMAKE_INSTALL_PREFIX="$BOX_HOME/.local" >/dev/null
fi
cmake --build "$BUILD" >/dev/null

echo "starting the swarm (tracker :$TRACKER_PORT, seeder :$SEEDER_PORT)"
"$SWARM" start >/dev/null
"$SWARM" seed >/dev/null

# magnet_of NAME: the seeder's magnet for NAME (its tracker included).
magnet_of() {
    curl -fs "$SEEDER/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"] == sys.argv[1]:
        print(t["magnet_uri"]); break' "$1"
}
SAMPLER_MAGNET="$(magnet_of "Public Domain Sampler")"
[ -n "$SAMPLER_MAGNET" ] || { echo "the seeder has no Public Domain Sampler: run scripts/dev/swarm.sh seed" >&2; exit 1; }

echo "starting the box ($MODE)"
omabox down -b "$BOX" >/dev/null 2>&1 || true
# XDG_DATA_HOME as the host's session has it: the box's dbus-daemon only reads
# ~/.local/share/dbus-1/services when it is set, and the launcher starts omaseed through
# D-Bus (DBusActivatable), so without it the app installed in ~/.local never starts.
up_args=(--net isolated --allow "$TRACKER_PORT,$SEEDER_BT_PORT" --env "XDG_DATA_HOME=$BOX_HOME/.local/share")
if [ "$MODE" = interactive ]; then
    omabox up "$BOX" --interactive "${up_args[@]}" >/dev/null
else
    omabox up "$BOX" --idle 0 "${up_args[@]}" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
# shellcheck disable=SC2016 # the box's $HOME, expanded inside the box
[ "$(omabox run -b "$BOX" -- sh -c 'printf %s "$HOME"')" = "$BOX_HOME" ] ||
    { echo "the box's HOME is not $BOX_HOME; the install's paths would be wrong" >&2; exit 1; }

echo "installing omaseed in the box"
stage="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-manual-box-XXXX")"
DESTDIR="$stage" cmake --install "$BUILD" >/dev/null
cp -a "$stage$BOX_HOME/.local/." "$home/.local/"
rm -rf -- "$stage"
omabox run -b "$BOX" -- update-desktop-database "$BOX_HOME/.local/share/applications" >/dev/null 2>&1 || true
# The bus skipped ~/.local/share/dbus-1/services at start (it did not exist yet) and does not
# notice it appear; a reload makes it read the new service file.
omabox run -b "$BOX" -- dbus-send --session --dest=org.freedesktop.DBus --type=method_call \
    /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig >/dev/null

echo "setting up the box's qBittorrent (Web UI 127.0.0.1:$BOX_QBT_PORT)"
password="$(python3 -c 'import secrets; print(secrets.token_urlsafe(12))')"
hash="$(PASS="$password" python3 -c '
import base64, hashlib, os
salt = os.urandom(16)
key = hashlib.pbkdf2_hmac("sha512", os.environ["PASS"].encode(), salt, 100000, dklen=64)
print(base64.b64encode(salt).decode() + ":" + base64.b64encode(key).decode())')"
mkdir -p "$home/.config/qBittorrent" "$home/Downloads"
# MultiConnectionsPerIp: every peer of this swarm is 127.0.0.1 (the seeder, the playground,
# this box). With it off, libtorrent keeps one peer per IP, the last port the tracker named,
# so the box could be left trying only the playground's port, closed in here (M15).
cat >"$home/.config/qBittorrent/qBittorrent.conf" <<EOF
[LegalNotice]
Accepted=true

[BitTorrent]
Session\\Port=$BOX_BT_PORT
Session\\DefaultSavePath=$BOX_HOME/Downloads
Session\\MultiConnectionsPerIp=true

[Preferences]
WebUI\\Enabled=true
WebUI\\Address=127.0.0.1
WebUI\\Port=$BOX_QBT_PORT
WebUI\\Username=admin
WebUI\\Password_PBKDF2="@ByteArray($hash)"
EOF
printf 'qBittorrent Web UI  http://127.0.0.1:%s\nusername  admin\npassword  %s\n' \
    "$BOX_QBT_PORT" "$password" >"$home/qbittorrent-login.txt"
chmod 600 "$home/qbittorrent-login.txt"
omabox run -b "$BOX" -d -- qbittorrent-nox >/dev/null
for _ in $(seq 1 40); do
    code="$(omabox run -b "$BOX" -- curl -s -o /dev/null -w '%{http_code}' \
        "http://127.0.0.1:$BOX_QBT_PORT/api/v2/app/version" 2>/dev/null || true)"
    [ "$code" = 403 ] || [ "$code" = 200 ] && break
    sleep 0.5
done
[ "$code" = 403 ] || [ "$code" = 200 ] ||
    { echo "the box's qBittorrent did not answer on :$BOX_QBT_PORT (last: ${code:-none})" >&2; exit 1; }

echo "filling ~/Downloads"
for name in "tiny-notes.txt" "Field Recordings Vol. 3 [FLAC]" "distro-2026.09-x86_64.iso" \
    "Ünïcødé ✓ Spaces & Symbols"; do
    # A plain prefix match: [FLAC] would be a character class to find -name.
    for file in "$TORRENTS"/*.torrent; do
        [[ "$(basename "$file")" == "$name"* ]] && cp "$file" "$home/Downloads/"
    done
done
MAGNET="$SAMPLER_MAGNET" python3 - "$home/Downloads/links.html" <<'PY'
import html, os, sys
magnet = html.escape(os.environ["MAGNET"])
open(sys.argv[1], "w").write(f"""<!doctype html>
<meta charset="utf-8"><title>omaseed test links</title>
<style>body{{font:16px system-ui;margin:3em;max-width:40em}} a{{display:block;margin:1em 0}}</style>
<h1>omaseed test links</h1>
<a href="{magnet}">Public Domain Sampler (magnet, 24 MB)</a>
<p>Clicking it asks to open xdg-open; omaseed should take it from there once first run
registered it. The .torrent files are in this folder: open them from Files.</p>
""")
PY

echo
echo "The box is up: $BOX."
if [ "$MODE" = interactive ]; then
    echo "  It is a window on workspace 9. SUPER+ALT+ESC sends SUPER keys to it."
fi
echo "  Start omaseed from the launcher (SUPER+SPACE). First run should find the box's qBittorrent."
echo "  qBittorrent login: admin / $password  (also in the box's ~/qbittorrent-login.txt)"
echo "  ~/Downloads: .torrent files for Files, and links.html (open it in Chromium) with a magnet."
echo "  Throw it away: scripts/dev/manual-box.sh down (or close the window)."
