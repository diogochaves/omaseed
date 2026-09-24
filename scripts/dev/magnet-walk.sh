#!/usr/bin/env bash
# omaseed — the hand-off walkthrough: a magnet link or a .torrent file opened with xdg-open,
# as a browser or a file manager does, lands in omaseed's add sheet, pre-filled, whether the
# app is closed, hidden in the tray, or already has that torrent.
#
#   scripts/dev/magnet-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                               # OUT_DIR/index.md says what to look for in each
#                                               # and lists the checks
#   scripts/dev/magnet-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                               # script still opens, types and closes, then says
#                                               # what to check and waits for enter
#
# omaseed is registered as the box's magnet: and .torrent handler (install-desktop.sh
# --set-default, which touches only the box's HOME). Two small swarm torrents are taken off the
# playground first (tiny-notes.txt as a magnet, the unicode one as a .torrent file), so the
# walk adds them for real. The .torrent one downloads again from the local seeder; the magnet
# stays at "metadata", since the swarm's torrents are private and libtorrent never sends a
# private torrent's metadata to a magnet. Afterwards swarm.sh's bookkeeping is put back (see
# cleanup). The box's network reaches :8081 alone.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PLAY_PORT="${OMASEED_TEST_PORT:-8081}"
PLAY="http://127.0.0.1:$PLAY_PORT/api/v2"
refuse_home "$PLAY"
DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
APP_ID=io.github.diogochaves.omaseed
BOX=magnet-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-magnet-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

TINY="tiny-notes.txt"

# field NAME KEY: KEY of the playground's torrent whose name contains NAME ("" if none).
field() {
    curl -fs "$PLAY/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if sys.argv[1] in t["name"]:
        print(t[sys.argv[2]]); break' "$1" "$2"
}

# copies NAME: how many torrents on the playground have NAME in their name.
copies() {
    curl -fs "$PLAY/torrents/info" | python3 -c '
import json, sys
print(sum(1 for t in json.load(sys.stdin) if sys.argv[1] in t["name"]))' "$1"
}

# on_playground NAME [SECONDS]: until the playground has NAME.
on_playground() {
    local limit="${2:-20}"
    for _ in $(seq 1 $((limit * 2))); do
        [ "$(copies "$1")" -ge 1 ] && return 0
        sleep 0.5
    done
    return 1
}

# finished NAME [SECONDS]: until NAME is complete on the playground.
finished() {
    local limit="${2:-60}"
    for _ in $(seq 1 $((limit * 2))); do
        [ "$(field "$1" progress)" = 1 ] && return 0
        sleep 0.5
    done
    return 1
}

# take_off NAME: delete NAME from the playground with its files, and wait until it is gone.
take_off() {
    local hash
    hash="$(field "$1" hash)"
    [ -n "$hash" ] || return 0
    curl -fs -X POST "$PLAY/torrents/delete" -d "hashes=$hash" -d deleteFiles=true >/dev/null
    for _ in $(seq 1 20); do
        [ "$(copies "$1")" = 0 ] && return 0
        sleep 0.5
    done
}

window_shown() { omabox hyprctl -b "$BOX" -j clients | grep -q "\"class\": \"$APP_ID\""; }
window_hidden() { ! window_shown; }

# wait_window [SECONDS]: until the app's window is mapped.
wait_window() {
    local limit="${1:-10}"
    for _ in $(seq 1 $((limit * 4))); do
        window_shown && return 0
        sleep 0.25
    done
    return 1
}

# one_process: exactly one omaseed runs in the box (a second launch hands its URI over and
# exits).
one_process() { [ "$(omabox run -b "$BOX" -- pgrep -xc omaseed 2>/dev/null || echo 0)" = 1 ]; }

# open_in_box ARG: what a browser or file manager does with a link or a downloaded file.
open_in_box() { omabox run -b "$BOX" -d -- xdg-open "$1" >/dev/null; }

one_copy() { [ "$(copies "$TINY")" = 1 ]; }

keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

STEP=0
CHECKS=""
# step "title" "what to look for": a screenshot (headless) or a pause (interactive).
step() {
    STEP=$((STEP + 1))
    local file
    file="$(printf '%02d' "$STEP")-$(echo "$1" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-').png"
    if [ "$INTERACTIVE" = 1 ]; then
        printf '\n\033[1m%02d %s\033[0m\n    %s\n    enter for the next step ' "$STEP" "$1" "$2"
        read -r _
    else
        omabox shot -b "$BOX" -o "$OUT/$file" >/dev/null
        printf -- '- **%02d %s**: %s\n  ![](%s)\n' "$STEP" "$1" "$2" "$file" >>"$OUT/index.md"
        echo "$(printf '%02d' "$STEP") $1 -> $OUT/$file"
    fi
}

# check "what" CONDITION…: a pass/fail line for what a screenshot cannot show.
check() {
    local what="$1"; shift
    local mark="FAIL"
    if "$@"; then mark="ok"; fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}

# Put the swarm back the way swarm.sh expects it (it works by category): the magnet's
# tiny-notes never gets its metadata, so it goes and `seed` re-adds it from its .torrent; the
# unicode torrent gets the swarm's category and folder back.
cleanup() {
    local hash
    take_off "$TINY" 2>/dev/null || true
    hash="$(field "Spaces & Symbols" hash 2>/dev/null || true)"
    if [ -n "$hash" ]; then
        curl -fs -X POST "$PLAY/torrents/setCategory" -d "hashes=$hash" -d category=swarm >/dev/null 2>&1 || true
        curl -fs -X POST "$PLAY/torrents/setLocation" -d "hashes=$hash" \
            --data-urlencode "location=$DOWNLOADS/swarm" >/dev/null 2>&1 || true
    fi
    "$SWARM" seed >/dev/null 2>&1 || true
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "preparing the swarm (tiny-notes.txt and the unicode torrent come off the playground)"
"$SWARM" start >/dev/null
"$SWARM" seed >/dev/null
# Looked for only now: on a machine never seeded (or after swarm.sh reset) the seed above is
# what writes it.
UNICODE_FILE="$(find "$TORRENTS" -name '*Spaces & Symbols*.torrent' -print -quit 2>/dev/null || true)"
[ -f "$UNICODE_FILE" ] || { echo "no unicode .torrent under $TORRENTS: run scripts/dev/swarm.sh seed" >&2; exit 1; }
MAGNET="$(field "$TINY" magnet_uri)"
[ -n "$MAGNET" ] || { echo "the playground has no $TINY: run scripts/dev/swarm.sh seed" >&2; exit 1; }
take_off "$TINY"
take_off "Spaces & Symbols"
mkdir -p "$DOWNLOADS"

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
else
    mkdir -p "$OUT"
    printf '# Magnet walk %s\n\nBuild `%s`. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
mkdir -p "$home/.config/omaseed" && chmod 700 "$home/.config/omaseed"
printf '{"version":1,"defaultId":"playground","servers":[{"id":"playground","name":"playground","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' "$PLAY_PORT" \
    >"$home/.config/omaseed/servers.json"
chmod 600 "$home/.config/omaseed/servers.json"
omabox run -b "$BOX" -- scripts/dev/install-desktop.sh --set-default >/dev/null 2>&1
# A browser's download, where the box's own file manager would find it. `home` is that
# directory from the host; inside the box it is the box's $HOME.
HOME_IN_BOX="$(omabox run -b "$BOX" -- sh -c 'printf %s "$HOME"')"
mkdir -p "$home/Downloads"
cp "$UNICODE_FILE" "$home/Downloads/unicode.torrent"

# ---- the walk -----------------------------------------------------------------------------
check "omaseed is the box's magnet: handler" \
    sh -c '[ "$(omabox run -b "$1" -- xdg-mime query default x-scheme-handler/magnet)" = "$2.desktop" ]' _ "$BOX" "$APP_ID"
check "and its .torrent handler" \
    sh -c '[ "$(omabox run -b "$1" -- xdg-mime query default application/x-bittorrent)" = "$2.desktop" ]' _ "$BOX" "$APP_ID"

# 1. Not running: the link starts the app, which opens straight into the sheet.
open_in_box "$MAGNET"
wait_window 15 || true
sleep 3 # the first sync, and the sheet's parse of the magnet
check "the magnet started omaseed" window_shown
step "Magnet, app closed" "The app opened with the add sheet on top: \"Add torrent\", \"tiny-notes.txt · 4 KB · from browser\", the magnet in the first field, the name filled in, \"add\" highlighted."
check "one omaseed process" one_process
keys Return
# It stays at "metadata" here: the swarm's torrents are private, and libtorrent never hands a
# private torrent's metadata to a magnet. The .torrent case below downloads for real.
check "enter added the magnet to the playground" on_playground "$TINY"
sleep 2
step "Magnet added" "The sheet is gone; tiny-notes.txt is a row again, fetching its metadata (it cannot finish: a private torrent's metadata is never sent to a magnet), without the swarm category."

# 2. Hidden in the tray: a downloaded .torrent brings the window back with the sheet.
omabox hyprctl -b "$BOX" dispatch "hl.dsp.window.close(\"class:$APP_ID\")" >/dev/null
sleep 1
check "closing the window hides it to the tray" window_hidden
open_in_box "$HOME_IN_BOX/Downloads/unicode.torrent"
wait_window 10 || true
sleep 2
check "the .torrent brought the hidden window back" window_shown
check "still one omaseed process (the second launch handed the file over)" one_process
step "Torrent file, app in the tray" "The window is back with the sheet: the unicode torrent's long name (elided, not cut mid-glyph), 5.00 MB, from browser, and its three files listed."
keys Return
check "enter added the file to the playground" on_playground "Spaces & Symbols"
check "and it downloaded" finished "Spaces & Symbols"

# 3. Already there: the same magnet again finds the torrent instead of adding a second one.
sleep 2
open_in_box "$MAGNET"
sleep 3
keys Return
sleep 2
step "Magnet it already has" "The duplicate sheet instead of an add: tiny-notes.txt is already on this server, with \"show it\"."
check "no second copy on the playground" one_copy
check "still one omaseed process" one_process
keys Escape

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
