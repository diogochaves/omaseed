#!/usr/bin/env bash
# omaseed — the hand-off walkthrough: a magnet link or a .torrent file opened with xdg-open,
# as a browser or a file manager does, lands in omaseed's add sheet, pre-filled, whether the
# app is closed, hidden in the tray, or already has that torrent (and, for a public one, merges
# the trackers the link brings); with the app quit, a link handed over through D-Bus activation
# (`gio open`, as GIO, the OpenURI portal and Chromium do) still reaches the sheet. Three links handed over one after another make one batch,
# added in one request (#40). Then, on a second box with its own qBittorrent, a magnet's file
# list before the add (docs/29, #44): the Sampler's magnet from the tray shows its tree, one file
# unticked is never written, and a private magnet goes through "fetching", "still looking" and
# "may never come" on a faster clock and is still added. Last, three .torrent files added one
# after another, "add stopped" double-clicked each time, the table sorted by date added and the
# pointer over its rows (#124): one add each, and each row on top as soon as it shows.
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
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
APP_ID=io.github.diogochaves.omaseed
BOX="${OMASEED_MAGNET_BOX:-magnet-walk}"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-magnet-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

TINY="tiny-notes.txt"
# Public, so a duplicate of it may merge trackers (step 4). Added stopped if the playground
# lacks it, and taken off again; otherwise only the two trackers the walk adds come off.
PUBLIC="Public Domain Sampler"
MERGE_A="udp://tracker.opentrackr.example:1337/announce"
MERGE_B="udp://open.demonii.example:1337/announce"
ADDED_PUBLIC=0
# #40: three magnets nobody seeds, handed over together; they stay at "metadata" and go in
# cleanup.
BATCH="omaseed-walk-batch"
BATCH_MAGNETS=()
for n in 1 2 3; do
    BATCH_MAGNETS+=("magnet:?xt=urn:btih:$(printf 'b47c%036d' "$n")&dn=$BATCH-$n")
done

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

# force_start NAME true|false: run NAME past the queue's download slots, or back in line.
force_start() {
    local hash
    hash="$(field "$1" hash)"
    [ -n "$hash" ] || return 0
    curl -fs -X POST "$PLAY/torrents/setForceStart" -d "hashes=$hash" -d "value=$2" >/dev/null
}

# hold_others: stop every other unfinished download on the playground, remembering which, so
# the torrent under test gets the seeder's upload to itself; release_others starts them again.
HELD=""
hold_others() {
    HELD="$(curl -fs "$PLAY/torrents/info" | python3 -c '
import json, sys
busy = {"downloading", "stalledDL", "queuedDL", "forcedDL", "metaDL", "forcedMetaDL", "checkingDL"}
print("|".join(t["hash"] for t in json.load(sys.stdin)
               if t["state"] in busy and sys.argv[1] not in t["name"]))' "$1")"
    [ -n "$HELD" ] || return 0
    curl -fs -X POST "$PLAY/torrents/stop" -d "hashes=$HELD" >/dev/null
}
release_others() {
    [ -n "$HELD" ] || return 0
    curl -fs -X POST "$PLAY/torrents/start" -d "hashes=$HELD" >/dev/null
    HELD=""
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
# three_in_batch: until the playground has the walk's three batch magnets.
three_in_batch() {
    for _ in $(seq 1 40); do
        [ "$(copies "$BATCH")" = 3 ] && return 0
        sleep 0.5
    done
    return 1
}
one_public() { [ "$(copies "$PUBLIC")" = 1 ]; }

# has_trackers HASH URL…: every URL is among HASH's trackers on the playground.
has_trackers() {
    local hash="$1"; shift
    curl -fs "$PLAY/torrents/trackers?hash=$hash" | python3 -c '
import json, sys
have = {t["url"] for t in json.load(sys.stdin)}
sys.exit(0 if all(u in have for u in sys.argv[1:]) else 1)' "$@"
}

keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

STEP=0
CHECKS=""
FAILED=0
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
    if "$@"; then mark="ok"; else FAILED=$((FAILED + 1)); fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}

# Put the swarm back the way swarm.sh expects it (it works by category): the magnet's
# tiny-notes never gets its metadata, so it goes and `seed` re-adds it from its .torrent; the
# unicode torrent gets the swarm's category and folder back.
cleanup() {
    local hash
    release_others 2>/dev/null || true
    take_off "$TINY" 2>/dev/null || true
    hash="$(field "Spaces & Symbols" hash 2>/dev/null || true)"
    if [ -n "$hash" ]; then
        force_start "Spaces & Symbols" false 2>/dev/null || true
        curl -fs -X POST "$PLAY/torrents/setCategory" -d "hashes=$hash" -d category=swarm >/dev/null 2>&1 || true
        curl -fs -X POST "$PLAY/torrents/setLocation" -d "hashes=$hash" \
            --data-urlencode "location=$DOWNLOADS/swarm" >/dev/null 2>&1 || true
    fi
    if [ "$ADDED_PUBLIC" = 1 ]; then
        take_off "$PUBLIC" 2>/dev/null || true
    else
        hash="$(field "$PUBLIC" hash 2>/dev/null || true)"
        [ -z "$hash" ] || curl -fs -X POST "$PLAY/torrents/removeTrackers" -d "hash=$hash" \
            --data-urlencode "urls=$MERGE_A|$MERGE_B" >/dev/null 2>&1 || true
    fi
    for _ in 1 2 3; do take_off "$BATCH" 2>/dev/null || true; done
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
for _ in 1 2 3; do take_off "$BATCH"; done
if [ "$(copies "$PUBLIC")" = 0 ]; then
    PUBLIC_FILE="$(find "$TORRENTS" -name "$PUBLIC.torrent" -print -quit 2>/dev/null || true)"
    [ -f "$PUBLIC_FILE" ] || { echo "no $PUBLIC.torrent under $TORRENTS: run scripts/dev/swarm.sh seed" >&2; exit 1; }
    curl -fs -X POST "$PLAY/torrents/add" -F "torrents=@$PUBLIC_FILE" -F stopped=true \
        -F "savepath=$DOWNLOADS/walk" >/dev/null
    ADDED_PUBLIC=1
    on_playground "$PUBLIC" || { echo "the playground did not take $PUBLIC" >&2; exit 1; }
fi
mkdir -p "$DOWNLOADS"

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
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
# shellcheck disable=SC2016 # the box's $HOME, expanded inside the box
HOME_IN_BOX="$(omabox run -b "$BOX" -- sh -c 'printf %s "$HOME"')"
mkdir -p "$home/Downloads"
cp "$UNICODE_FILE" "$home/Downloads/unicode.torrent"

# ---- the walk -----------------------------------------------------------------------------
# shellcheck disable=SC2016 # expands in the inner sh, which gets the values as $1, $2
check "omaseed is the box's magnet: handler" \
    sh -c '[ "$(omabox run -b "$1" -- xdg-mime query default x-scheme-handler/magnet)" = "$2.desktop" ]' _ "$BOX" "$APP_ID"
# shellcheck disable=SC2016 # expands in the inner sh, which gets the values as $1, $2
check "and its .torrent handler" \
    sh -c '[ "$(omabox run -b "$1" -- xdg-mime query default application/x-bittorrent)" = "$2.desktop" ]' _ "$BOX" "$APP_ID"

# 1. Not running: the link starts the app, which opens straight into the sheet.
open_in_box "$MAGNET"
wait_window 15 || true
sleep 3 # the first sync, and the sheet's parse of the magnet
check "the magnet started omaseed" window_shown
step "Magnet, app closed" "The app opened with the add sheet on top: \"Add torrent\", \"tiny-notes.txt · 4 KB · from magnet link\", the magnet in the first field, the name filled in, \"add\" highlighted. Files says \"private tracker\": the playground's swarm torrents announce to the same tracker and are private, so no file list is asked for (docs/29 F5)."
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
# After the swarm or notify walk the playground's two download slots are busy for minutes
# and the other downloads take nearly all of the seeder's upload, so the new torrent would
# sit queued, then crawl. Force-start it and hold the others while it runs (cleanup takes
# the force off again; the others start straight after).
force_start "Spaces & Symbols" true || true
hold_others "Spaces & Symbols" || true
check "and it downloaded" finished "Spaces & Symbols"
release_others || true

# 3. Already there: the same magnet again finds the torrent instead of adding a second one,
# as soon as it arrives (docs/25 D1 a). It is still fetching its metadata, so whether it is
# private is not known yet, and nothing is merged.
sleep 2
open_in_box "$MAGNET"
sleep 3
step "Magnet it already has" "The duplicate sheet straight away, no add form behind it: tiny-notes.txt is already on this server; a dim line says its metadata hasn't arrived, so its trackers stay as they are; \"close\" and \"show it\", no merge."
check "no second copy on the playground" one_copy
check "still one omaseed process" one_process
keys Escape

# 3b. The same, with omaseed quit, the way a browser or Files hands it over (GIO, the OpenURI
# portal): the .desktop entry is DBusActivatable, so they call Open on omaseed's name and the
# bus starts the app, without the link, to answer it. The app has to have its object up before
# it takes the name, or the queued Open is refused and the window opens with no sheet; and the
# link lands before the library is in, so the sheet looks for the duplicate again once the first
# sync is (docs/25 D1 a), or it would show the add form until `add`.
omabox run -b "$BOX" -- "$repo/build/omaseed" --quit >/dev/null 2>&1 || true
omabox wait -b "$BOX" --timeout 15s window "$APP_ID" --gone >/dev/null 2>&1 || true
# shellcheck disable=SC2016 # expands in the inner sh, which gets the box as $1
check "omaseed quit" sh -c '! omabox run -b "$1" -- pgrep -x omaseed >/dev/null 2>&1' _ "$BOX"
GIO_LOG="$(omabox run -b "$BOX" -d -- gio open "$MAGNET" 2>&1 | sed -n 's/.*(log: \(.*\))$/\1/p' | head -n 1)"
wait_window 15 || true
# dup_sheet: until the duplicate sheet is up (the first sync comes first: the app just started).
dup_sheet() {
    for _ in $(seq 1 40); do
        omabox tree -b "$BOX" --find "Already on" 2>/dev/null | grep -q '^dialog "Already on' && return 0
        sleep 0.25
    done
    return 1
}
check "gio open started omaseed" window_shown
check "and handed it the link: the duplicate sheet came up" dup_sheet
# open_answered: gio said nothing about the call it made (it prints why an Open failed).
open_answered() { [ -z "$GIO_LOG" ] || ! grep -q "No such object path" "$GIO_LOG"; }
check "gio's Open was answered (no \"No such object path\")" open_answered
step "Magnet it already has, app quit" "The window opened by D-Bus activation, with the same duplicate sheet as the step before: tiny-notes.txt is already on this server, \"close\" and \"show it\"."
check "still one omaseed process" one_process
keys Escape

# 4. A public duplicate with trackers the torrent lacks (docs/25 §2, #26): merge them.
sleep 1
PUBLIC_HASH="$(field "$PUBLIC" hash)"
MERGE_MAGNET="magnet:?xt=urn:btih:$PUBLIC_HASH&dn=Public%20Domain%20Sampler&tr=$(printf %s "$MERGE_A" | python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.stdin.read(), safe=""))')&tr=$(printf %s "$MERGE_B" | python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.stdin.read(), safe=""))')"
open_in_box "$MERGE_MAGNET"
sleep 3
step "Public duplicate with new trackers" "The duplicate sheet for Public Domain Sampler: \"This source lists 2 trackers it doesn't have\", two green \"+ new\" hosts, the one it has dimmed, and \"merge 2 trackers\" highlighted."
keys Return
sleep 2
check "merge added both trackers on the playground" has_trackers "$PUBLIC_HASH" "$MERGE_A" "$MERGE_B"
check "and no second copy" one_public
step "Trackers merged" "Public Domain Sampler under the cursor, its details on the Trackers tab with the two new trackers \"not contacted\", and the footer saying \"2 trackers added to Public Domain Sampler\"."

# 5. Three links in a row (#40, docs/27 D7 a): the first fills the sheet, the next two join
# it, untouched as it is, and the sheet adds the three in one request.
keys Escape
sleep 1
for magnet in "${BATCH_MAGNETS[@]}"; do
    open_in_box "$magnet"
    sleep 1.5
done
sleep 2
step "Three links, one batch" "\"Add 3 torrents\", \"3 without a size yet\"; Torrents · 3 lists $BATCH-1 to -3, each \"magnet · size after metadata\"; no Name row; \"add 3\" highlighted."
keys Return
check "enter added all three to the playground" three_in_batch
step "Three added" "The sheet is gone; the three $BATCH rows are in the table, fetching metadata."

# ---- a magnet's files before the add (docs/29, #44) ---------------------------------------
# On a box with its own qBittorrent (app-box.sh --qbt), so the playground, which already holds
# the Sampler and the swarm's private torrents, stays put. That library starts empty, and the
# Sampler (public, seeded) goes first: the swarm's private torrents announce to the same
# 127.0.0.1 tracker, so once one is in the library the Sampler's magnet would read as a private
# tracker's (F5). First an add pressed before the Sampler's list is in (#193). The lookup's clock runs 6 times faster (OMASEED_LOOKUP_CLOCK_SCALE), so the
# private magnet's "still looking" (10 s) and "may never come" (60 s) come in 2 and 10 s.
SEEDER="http://127.0.0.1:8083/api/v2"
# seeder_field NAME KEY: KEY of the seeder's torrent called NAME.
seeder_field() {
    curl -fs "$SEEDER/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"] == sys.argv[1]:
        print(t[sys.argv[2]]); break' "$1" "$2"
}
PRIVATE_NAME="Field Recordings Vol. 3 [FLAC]"
SAMPLER_MAGNET="$(seeder_field "$PUBLIC" magnet_uri)"
SAMPLER_HASH="$(seeder_field "$PUBLIC" hash)"
PRIVATE_MAGNET="$(seeder_field "$PRIVATE_NAME" magnet_uri)"
[ -n "$SAMPLER_MAGNET" ] && [ -n "$PRIVATE_MAGNET" ] ||
    { echo "the seeder lacks $PUBLIC or $PRIVATE_NAME: run scripts/dev/swarm.sh seed" >&2; exit 1; }

echo "starting a second box, with its own qBittorrent"
[ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
BOX="${OMASEED_MAGNET_BOX:-magnet-walk}-qbt"
qbt_args=(up -b "$BOX" --qbt --handlers)
[ "$INTERACTIVE" = 1 ] && qbt_args+=(--interactive)
OMASEED_LOOKUP_CLOCK_SCALE=6 "$repo/scripts/dev/app-box.sh" "${qbt_args[@]}" >/dev/null
SAMPLER_DIR=/home/sbx/Torrents/$PUBLIC

# box_files HASH: the file priorities the box's qBittorrent holds for HASH, comma-separated.
box_files() {
    omabox run -b "$BOX" -- curl -fs "http://127.0.0.1:8090/api/v2/torrents/files?hash=$1" |
        python3 -c 'import json, sys; print(",".join(str(f["priority"]) for f in json.load(sys.stdin)))'
}
# box_priorities_are HASH LIST [SECONDS]: until HASH's priorities read LIST.
box_priorities_are() {
    for _ in $(seq 1 $((${3:-10} * 2))); do
        [ "$(box_files "$1" 2>/dev/null)" = "$2" ] && return 0
        sleep 0.5
    done
    return 1
}
# box_progress HASH: HASH's progress on the box's qBittorrent ("" when it has none).
box_progress() {
    omabox run -b "$BOX" -- curl -fs "http://127.0.0.1:8090/api/v2/torrents/info?hashes=$1" |
        python3 -c 'import json, sys; t = json.load(sys.stdin); print(t[0]["progress"] if t else "")'
}
box_finished() {
    for _ in $(seq 1 60); do
        [ "$(box_progress "$1" 2>/dev/null)" = 1 ] && return 0
        sleep 0.5
    done
    return 1
}
# box_appears HASH: until the box's qBittorrent has HASH.
box_appears() {
    for _ in $(seq 1 20); do
        [ -n "$(box_progress "$1" 2>/dev/null)" ] && return 0
        sleep 0.5
    done
    return 1
}
no_mkv() { omabox run -b "$BOX" -- test ! -e "$SAMPLER_DIR/sampler.mkv"; }
has_srt() { omabox run -b "$BOX" -- test -f "$SAMPLER_DIR/sampler.srt"; }

# 5b. #193: add pressed before the list is in. qBittorrent 5.3.0 crashes on the next sync once
# the metadata arrives for a magnet added while its lookup still runs, so the app holds the add
# until the list is in. The Sampler's list takes about a second to come from the seeder; enter
# goes as soon as the sheet shows its name. Then the Sampler comes off again (files too), so
# step 6 finds an empty library.
sheet_named() {
    for _ in $(seq 1 40); do
        omabox tree -b "$BOX" --find "$1" 2>/dev/null | grep -q '^dialog "Add torrent"' && return 0
        sleep 0.25
    done
    return 1
}
box_up() { omabox run -b "$BOX" -- curl -fs "http://127.0.0.1:8090/api/v2/app/version" >/dev/null; }
app_log() { cat "$(omabox path -b "$BOX")"/home/run-*.log 2>/dev/null; }
open_in_box "$SAMPLER_MAGNET"
check "the sheet came up for the Sampler's magnet" sheet_named "$PUBLIC"
keys Return
check "the early add went in" box_appears "$SAMPLER_HASH"
check "and finished" box_finished "$SAMPLER_HASH"
sleep 4 # a few more syncs: 5.3.0 fell over on the first one after the metadata came
check "the box's qBittorrent is still up after an add before the list (#193)" box_up
if app_log | grep -q "waits for its file list first"; then
    check "the add waited for the list" true
else
    # The list came before enter: the step saw nothing to hold, and says so instead of failing.
    CHECKS+="- skipped: the list came before enter, so the early add wasn't held this time"$'\n'
    echo "  check skipped: the list came before enter"
fi
omabox run -b "$BOX" -- curl -fs -X POST "http://127.0.0.1:8090/api/v2/torrents/delete" \
    -d "hashes=$SAMPLER_HASH" -d deleteFiles=true >/dev/null
gone_from_box() {
    for _ in $(seq 1 20); do
        [ -z "$(box_progress "$1" 2>/dev/null)" ] && return 0
        sleep 0.5
    done
    return 1
}
check "the Sampler is off the box again" gone_from_box "$SAMPLER_HASH"
step "An add before the list" "The sheet is gone; Public Domain Sampler was a row, seeding, before the walk took it off again: the table is empty and the app is connected (the server is up)."

# 6. The Sampler's magnet with the app in the tray: the window comes back with the sheet, and
# the tree arrives in it (F2).
omabox hyprctl -b "$BOX" dispatch "hl.dsp.window.close(\"class:$APP_ID\")" >/dev/null
sleep 1
check "closing the window hides it to the tray" window_hidden
open_in_box "$SAMPLER_MAGNET"
wait_window 10 || true
sleep 4 # the lookup: about a second on the swarm, a little more on a box just started
check "the magnet brought the hidden window back" window_shown
# #63 (DECISIONS C28): once the metadata is in, the line under the tree names the extra.
# The metadata can take longer than the sleep on a box just started: look for up to 20 s.
in_sheet() {
    local _
    for _ in $(seq 20); do
        omabox tree -b "$BOX" --find "$1" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}
check "the line under the tree says README.txt is an extra, still ticked" \
    in_sheet "1 extra: a text file"
step "A magnet's files, from the tray" "The add sheet for Public Domain Sampler, \"24.0 MB · from magnet link\"; under the switches, Files \"3 files · 24.0 MB\": README.txt (a dim \"extra\" on its row), sampler.mkv, sampler.srt, all ticked; under the tree, dim: \"1 extra: a text file (…). Ticked, so the torrent seeds whole.\"; the free line needs 24.0 MB."

# From the field, shift+tab goes back past add, add stopped and cancel to the tree.
keys shift+Tab shift+Tab shift+Tab shift+Tab j space
sleep 0.5
step "sampler.mkv unticked" "The tree has the keyboard: sampler.mkv unticked and \"skip\", Files \"2 of 3 files · 43 KB\", the free line \"needs 43 KB of 24.0 MB\"."
keys Return
check "added in one call with sampler.mkv skipped (priorities 1,0,1 at once)" \
    box_priorities_are "$SAMPLER_HASH" "1,0,1"
check "and it finished" box_finished "$SAMPLER_HASH"
check "sampler.mkv was never written" no_mkv
check "sampler.srt was" has_srt
step "The Sampler added" "The sheet is gone; Public Domain Sampler is a row, 43 KB, 100 %, seeding."

# 7. A private swarm magnet: no peer ever sends its list, so the sheet goes through F1, F3 and
# F4 (on the walk's clock), and enter still adds it.
open_in_box "$PRIVATE_MAGNET"
sleep 1.2
step "A magnet looking for its list" "The sheet for $PRIVATE_NAME, 104 MB; Files: \"fetching · 7 s\" (or so) on the right, a box \"Asking qBittorrent for the file list\", add highlighted."
sleep 3
step "Still looking" "Files: \"still looking\" and its seconds; a yellow ◷, \"No peer has sent the file list yet\", and a dim line naming enter and shift+enter."
sleep 7
step "May never come" "Files: \"no list after 1 min …\" in yellow; a yellow \"!\", \"The file list may never come\", and \"qBittorrent goes on looking until it restarts, even after you close this sheet.\""
keys Return
PRIVATE_HASH="$(seeder_field "$PRIVATE_NAME" hash)"
check "enter added it, still fetching its metadata" box_appears "$PRIVATE_HASH"
step "The private magnet added" "The sheet is gone; $PRIVATE_NAME is a row at \"metadata\", beside the Sampler."

# 8. Adding in a row (#124, DECISIONS C20): three .torrent files opened one after another, the
# table sorted by date added, newest first, and the pointer over its rows (which holds the row
# order, #51 D9). Each "add stopped" is double-clicked: the sheet closes on qBittorrent's OK, the
# second click sends nothing (or lands on the table, the sheet already gone), and each new row
# shows at the top on the sync that brings it, never at the end first.
# rows_in_order: the table's rows on screen, top to bottom, one name a line.
rows_in_order() {
    omabox tree -b "$BOX" 2>/dev/null | python3 -c '
import re, sys
rows, depth = [], None
for line in sys.stdin:
    indent = len(line) - len(line.lstrip())
    if depth is None:
        if re.match(r"\s*list \"torrents\"", line):
            depth = indent
        continue
    if indent <= depth:
        break
    m = re.match(r"\s*list item \"(.*)\" @\d+,(\d+)", line)
    if m:
        rows.append((int(m.group(2)), m.group(1)))
print("\n".join(name for _, name in sorted(rows)))'
}
sheet_up() { omabox tree -b "$BOX" --find "Add torrent" 2>/dev/null | grep -q 'dialog "Add torrent"'; }
# sheet_for NAME: until the add sheet is up with NAME in it.
sheet_for() {
    for _ in $(seq 1 40); do
        omabox tree -b "$BOX" --find "$1" 2>/dev/null | grep -q '^dialog "Add torrent"' && return 0
        sleep 0.25
    done
    return 1
}
sheet_gone() {
    for _ in $(seq 1 20); do
        sheet_up || return 0
        sleep 0.25
    done
    return 1
}
# lands_on_top NAME: watch the table until NAME shows; true when it first shows on top.
lands_on_top() {
    local rows
    for _ in $(seq 1 40); do
        rows="$(rows_in_order)"
        if grep -qxF "$1" <<<"$rows"; then
            [ "$(head -n1 <<<"$rows")" = "$1" ] && return 0
            echo "    $1 first showed at row $(grep -nxF "$1" <<<"$rows" | cut -d: -f1)" >&2
            return 1
        fi
        sleep 0.2
    done
    return 1
}
# box_count NAME: how many torrents the box's qBittorrent has called NAME.
box_count() {
    omabox run -b "$BOX" -- curl -fs "http://127.0.0.1:8090/api/v2/torrents/info" |
        python3 -c 'import json, sys; print(sum(1 for t in json.load(sys.stdin) if t["name"] == sys.argv[1]))' "$1"
}
# double_click_add_stopped: two clicks, as quick as omabox sends them, on the sheet's "add
# stopped" (the tree's places are the window's).
double_click_add_stopped() {
    local x y w h
    read -r x y w h < <(omabox tree -b "$BOX" --find "add stopped" 2>/dev/null |
        sed -n 's/.*button "add stopped" @\([0-9]*\),\([0-9]*\) \([0-9]*\)x\([0-9]*\).*/\1 \2 \3 \4/p' |
        head -n1)
    [ -n "${h:-}" ] || return 1
    omabox click -b "$BOX" --window omaseed $((x + w / 2)) $((y + h / 2)) >/dev/null
    omabox click -b "$BOX" --window omaseed $((x + w / 2)) $((y + h / 2)) >/dev/null
}
# no_refused_add: no "add failed" in the box's run logs (the app's among them: `omabox log
# run` reads only the latest run, an xdg-open here).
no_refused_add() { ! cat "$(omabox path -b "$BOX")"/home/run-*.log 2>/dev/null | grep -q 'add failed'; }

keys s
sleep 0.5
keys -t added
keys Return
sleep 0.5
# The pointer over the table's first row: the row order is held while it stays there.
omabox pointer -b "$BOX" --window omaseed -- move 700 115 >/dev/null
for name in "distro-2026.09-x86_64.iso" "photo-archive-2019-2025" "tiny-notes.txt"; do
    open_in_box "$HOME_IN_BOX/Downloads/$name.torrent"
    check "the sheet came up for $name" sheet_for "$name"
    check "add stopped, double-clicked" double_click_add_stopped
    check "it closed on qBittorrent's OK" sheet_gone
    check "$name took its sorted place at once: the top" lands_on_top "$name"
    check "and was added once" test "$(box_count "$name")" = 1
done
check "the second presses sent nothing (no refused add in the log)" no_refused_add
step "Three added in a row" "Sorted by added, newest first: tiny-notes.txt, photo-archive-2019-2025 and distro-2026.09-x86_64.iso on top, in that order, all stopped; the sheet is gone."

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
# A failed check fails the walk, so a caller need not read index.md to know.
if [ "$FAILED" -gt 0 ]; then
    echo "$FAILED check(s) failed" >&2
    exit 1
fi
