#!/usr/bin/env bash
# omaseed — the real-data walkthrough: every torrent state the swarm can produce, shown in
# the app, one step at a time. Run it again after any change to the table, the details pane
# or the sync code.
#
#   scripts/dev/swarm-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                              # OUT_DIR/index.md says what to look for in each
#   scripts/dev/swarm-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                              # step sets the state up, says what to check, and
#                                              # waits for enter
#
# Each state is seen twice: in the table (the details tab that tells it), then in the rows
# display (DECISIONS C24 H3, the library grouped under the five questions), and the walk ends on
# the shelves.
#
# It resets the swarm first (scripts/dev/swarm.sh reset + seed), so the downloads start from
# zero, at 1 MiB/s so they are still running while the walk looks at them. The seeder's rate
# goes back to 4 MiB/s at the end. Everything runs inside an omabox and against :8081 and
# :8083 only; the app's server list is written before it starts, so its first-run page never
# offers :8080, and the box's network reaches :8081 alone (omabox --net isolated), so the app
# could not touch :8080 even by mistake.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PLAY_PORT="${OMASEED_TEST_PORT:-8081}"
PLAY="http://127.0.0.1:$PLAY_PORT/api/v2"
refuse_home "$PLAY"
DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
BOX=swarm-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-swarm-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

# wait_state NAME REGEX [SECONDS]: until the playground's torrent NAME is in a matching state.
wait_state() {
    local name="$1" pattern="$2" limit="${3:-120}" state=""
    for _ in $(seq 1 $((limit * 2))); do
        state="$(curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if sys.argv[1].lower() in t["name"].lower():
        print(t["state"], round(t["progress"], 3)); break
' "$name" 2>/dev/null || true)"
        if [[ "$state" =~ $pattern ]]; then return 0; fi
        sleep 0.5
    done
    echo "  (waited ${limit}s: \"$name\" is \"$state\", not /$pattern/; showing it anyway)" >&2
}

# launch: (re)start the app in the box and wait for its window and first sync.
launch() {
    omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
    omabox run -b "$BOX" --pass OMASEED_DISPLAY -d -- ./build/omaseed >/dev/null
    for _ in $(seq 1 40); do
        omabox hyprctl -b "$BOX" -j clients | grep -q '"class": "io.github.diogochaves.omaseed"' && break
        sleep 0.25
    done
    sleep 2.5
}

# show NAME TAB: put the cursor on NAME (the jump palette) and open details tab TAB: 1 story,
# 2 files, 3 swarm, 4 trackers, 5 facts (DECISIONS C24 H6).
# Each step restarts the app so it starts from the same state whatever the last step did.
show() {
    launch
    omabox keys -b "$BOX" ctrl+k -t "$1" Return l "$2" >/dev/null
    sleep 1.5
}

# rows TITLE WHAT: the same moment in the rows display (C24 H3): the app again with every scope
# on rows, the library on show; interactive, switch with the title bar's display tool.
rows() {
    [ "$INTERACTIVE" = 1 ] || OMASEED_DISPLAY=rows launch
    step "$1 in rows" "$2"
}

STEP=0
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

cleanup() {
    "$SWARM" fix >/dev/null 2>&1 || true
    "$SWARM" rate 4096 >/dev/null 2>&1 || true
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "resetting the swarm (the playground's swarm downloads start over)"
"$SWARM" start >/dev/null
"$SWARM" reset >/dev/null
OMASEED_SWARM_RATE=1024 "$SWARM" start >/dev/null
"$SWARM" rate 1024 >/dev/null
"$SWARM" seed >/dev/null
mkdir -p "$DOWNLOADS"

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Swarm walk %s\n\nBuild `%s`. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
mkdir -p "$home/.config/omaseed" && chmod 700 "$home/.config/omaseed"
printf '{"version":1,"defaultId":"playground","servers":[{"id":"playground","name":"playground","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' "$PLAY_PORT" \
    >"$home/.config/omaseed/servers.json"
chmod 600 "$home/.config/omaseed/servers.json"
omabox run -b "$BOX" -- scripts/dev/install-desktop.sh --set-default >/dev/null 2>&1
launch

# ---- the walk -----------------------------------------------------------------------------
wait_state "Open Movie" "^downloading 0\.0[2-9]|^downloading 0\.[1-9]" 90
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 1
step "Downloading" "Open Movie Night: status downloading, a moving speed and ETA; its details open on the story: the time to ready large in blue with \"to ready\", the sentence \"N % of SIZE · from N seeds at SPEED.\", the pieces bar partly filled, what happened (Added, into swarm). The status line's ↓ near 1 MiB/s. Two downloads run; the rest are queued."
rows "Downloading" "The rows display, grouped: under ARRIVING the two downloads say their time to ready (\"to ready\") and \"N % of SIZE · from N seeds at SPEED\", the queued ones \"next\" or \"queued\" in line; heads in the rail's order, each with its count and a short line."
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 2
step "Files tree" "Open Movie Night's Files tab: the film, extras/, subs/ (english, português (Brasil), 日本語) and sample/, with per-file progress."
[ "$INTERACTIVE" = 1 ] || show "photo-archive" 2
step "Many files" "photo-archive: 1 208 files in 92 folders by year and month; scrolling and folding stay smooth."
wait_state "tiny-notes" "UP|^uploading" 30
[ "$INTERACTIVE" = 1 ] || show "tiny-notes" 1
step "Seeding" "tiny-notes.txt: finished, seeding (also when nobody is downloading from it), done 100 %, ETA —, the rail counts it under landed today (giving back once local midnight has passed). Its story: the time it landed in green, \"landed\", and what happened: Added (Finished downloading under it once it took a second or more). Each finished download raises a \"Download complete\" notification in the box (often caught by the first shots)."
rows "Seeding" "tiny-notes.txt under LANDED TODAY: the time it landed and \"landed\", \"in swarm · 4 KB · given back 0.0× so far\"; the head says \"since midnight\"."
wait_state "Spaces & Symbols" "UP|^uploading" 60
[ "$INTERACTIVE" = 1 ] || show "Spaces & Symbols" 5
step "Long unicode name" "The v2-only torrent: its long unicode name elides in the table and the details head; facts shows a v2 hash and no v1."
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 3
step "Swarm" "Open Movie Night's swarm tab: the seeder at 127.0.0.1:26997, qBittorrent 5.2.3, sending."

"$SWARM" break crowd >/dev/null
sleep 6
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 5
step "Crowded swarm" "Facts' seeds and peers: 1 connected of 13 seeds, 40 more peers than there are (the tracker's extra counts)."

"$SWARM" break tracker >/dev/null
sleep 6
[ "$INTERACTIVE" = 1 ] || show "Spaces & Symbols" 4
step "Tracker error" "The unicode torrent: the trackers tab says \"unregistered torrent (omaseed swarm)\", the tab carries its error badge, the table row shows the tracker error."

"$SWARM" break dead >/dev/null
# until qBittorrent has tried 127.0.0.1:9 and marked it not working (status 4)
hash="$(curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
print([t["hash"] for t in json.load(sys.stdin) if t["name"].startswith("distro")][0])')"
for _ in $(seq 1 60); do
    curl -fs "$PLAY/torrents/trackers?hash=$hash" | python3 -c '
import json, sys
sys.exit(0 if any(t["url"].endswith(":9/announce") and t["status"] == 4 for t in json.load(sys.stdin)) else 1)' && break
    sleep 0.5
done
[ "$INTERACTIVE" = 1 ] || show "distro" 4
step "Dead tracker" "distro ISO's trackers tab: 127.0.0.1:26996 working, 127.0.0.1:9 not working (connection refused)."

"$SWARM" break error >/dev/null
wait_state "unwritable" "^error" 30
[ "$INTERACTIVE" = 1 ] || show "unwritable" 1
step "Error" "unwritable-fixture.bin: status error (its save path is read-only); the rail counts it under needs you, in red. Its story opens with the red error block (Can't write to its save folder), the fact \"no access\" in red over \"needs you\"."
rows "Error" "unwritable-fixture.bin first, under NEEDS YOU in red: its short cause on the right in red over \"needs you\", the long cause under the name, and its fix (\"retry\") as a button."

"$SWARM" break stall >/dev/null
sleep 8
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 1
step "Stalled" "The seeder stopped: running downloads read stalled, speeds fall to 0, ETA ∞."
rows "Stalled" "The stalled downloads under ARRIVING: \"waiting\" over \"no seed\", \"no seed online · …\"."

"$SWARM" fix >/dev/null
"$SWARM" break missing >/dev/null
wait_state "tiny-notes" "^missingFiles" 60
sleep 4 # the app reconnects to the restarted playground
[ "$INTERACTIVE" = 1 ] || show "tiny-notes" 1
step "Missing files" "tiny-notes.txt: status missing files (red, counted under needs you), ETA —, after the playground restarted; the app reconnected on its own (no banner left)."
rows "Missing files" "tiny-notes.txt under NEEDS YOU: \"files missing\" in red, and where its files were."

"$SWARM" fix >/dev/null
sleep 10
[ "$INTERACTIVE" = 1 ] || show "Open Movie" 1
step "Fixed" "After swarm.sh fix: no errored rows, nothing under needs you, downloads moving again (a stalled one can take a minute to reconnect)."
rows "Fixed" "Every torrent back under its question: nothing under needs you, the downloads arriving, the rest landed today, giving back or resting."
[ "$INTERACTIVE" = 1 ] || OMASEED_DISPLAY=overview launch
step "Shelves" "The overview's shelves: one per category (swarm, fixtures…, no category last), a cover per torrent, its grid filled as far as it has arrived, two letters, the name and its fact under it."

echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
