#!/usr/bin/env bash
# omaseed — the stuck-move walkthrough (docs/44 §5, D13, #121): a qBittorrent that keeps
# running downloads in an incomplete folder finishes a torrent into a destination that is
# full. qBittorrent logs "Failed to move torrent … No space left on device", leaves partial
# copies in the destination and keeps the torrent stalledUP, seeding from the incomplete folder,
# with no error. omaseed says so: the row's status reads "move failed", The story opens with a
# red block naming the destination, the log's reason, where it is seeding from and the partial
# copies when it can see them, "show both folders", "c recheck" and "set location…". Then room
# is made and the block's `c recheck` moves it (DECISIONS C28, #158;
# scripts/dev/research/121-stuck-move: a move to the save path it already has does nothing),
# and the block goes.
#
#   scripts/dev/stuck-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                              # OUT_DIR/index.md says what to look for in each
#                                              # and lists the checks
#   scripts/dev/stuck-walk.sh --interactive    # Diogo: the box is a window; each step sets the
#                                              # state up, says what to check, waits for enter
#
# What it sets up, all of it thrown away at the end (also on ^C), none of it root:
# - a small FAT image (24 MiB, 14 of them a filler), attached and mounted with udisksctl, the
#   full destination;
# - a throwaway qBittorrent of its own (scripts/dev/test-server.sh with its own profile, port
#   $PORT), never the playground, so no lock is needed and nothing shared changes;
# - a torrent made here whose 12 MiB of files are already in the incomplete folder: added with
#   that folder as its download path and the image as its save path, it checks as complete
#   and qBittorrent moves it at once, into a disk with less room than it needs.
# The box mounts the profile read-only at its own path and reaches only that qBittorrent's port.
# It can't mount the image (omabox refuses anything under /run, where udisks mounts), so omaseed
# can't see the destination there and says partial copies "may be left"; tst_partialcopies and
# tst_Details check the named case.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
cd "$repo"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PORT="${OMASEED_STUCK_PORT:-8097}"
BT_PORT="${OMASEED_STUCK_BT_PORT:-26987}"
API="http://127.0.0.1:$PORT/api/v2"
refuse_home "$API" "$BT_PORT"
WORK="${OMASEED_STUCK_DIR:-$HOME/.local/share/omaseed-stuck}"
PROFILE="$WORK/qbt"
INCOMPLETE="$WORK/incomplete"
IMAGE="$WORK/full.img"
NAME="Stuck Move Sampler"
APP_ID=io.github.diogochaves.omaseed
BOX="${OMASEED_STUCK_BOX:-stuck-walk}"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-stuck-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }
for tool in udisksctl mkfs.vfat qbittorrent-nox python3 curl; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

server() {
    OMASEED_TEST_PROFILE="$PROFILE" OMASEED_TEST_PORT="$PORT" OMASEED_TEST_BT_PORT="$BT_PORT" \
        scripts/dev/test-server.sh "$@"
}

LOOP=""
MOUNT=""

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

cleanup() {
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
    server stop >/dev/null 2>&1 || true
    if [ -n "$LOOP" ]; then
        udisksctl unmount --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
        udisksctl loop-delete --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$WORK"
}
trap cleanup EXIT

# torrent_info FIELD: the walk's torrent's FIELD in torrents/info, "" when it isn't there.
torrent_info() {
    curl -fs "$API/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"] == sys.argv[1]:
        print(t.get(sys.argv[2], "")); break' "$NAME" "$1" 2>/dev/null || true
}

# stuck: finished, not moving, its content still under the download path (D13's rule).
stuck() {
    local state progress content
    state="$(torrent_info state)"
    progress="$(torrent_info progress)"
    content="$(torrent_info content_path)"
    [ "$progress" = 1 ] && [ "$state" != moving ] && [ "$state" != checkingUP ] &&
        [[ "$content" == "$INCOMPLETE"/* ]]
}

move_failed_logged() {
    curl -fs "$API/log/main?normal=false&info=false&warning=true&critical=true" | python3 -c '
import json, sys
sys.exit(0 if any(e["message"].startswith("Failed to move torrent.") and "\"" + sys.argv[1] + "\""
                  in e["message"] for e in json.load(sys.stdin)) else 1)' "$NAME"
}

# wait_for WHAT SECONDS CONDITION…
wait_for() {
    local what="$1" limit="$2"; shift 2
    for _ in $(seq 1 $((limit * 2))); do
        "$@" && return 0
        sleep 0.5
    done
    echo "  (waited ${limit}s: $what)" >&2
    return 1
}

partial_copies() { [ -n "$(find "$MOUNT/$NAME" -type f 2>/dev/null | head -1)" ]; }
destination_full() {
    [ "$(df --output=avail -B1 "$MOUNT" | tail -1 | tr -d ' ')" -lt 1048576 ]
}

# in_tree TEXT: a node of the app's accessibility tree named TEXT, or starting with it.
in_tree() {
    omabox tree -b "$BOX" --window "$APP_ID" --json 2>/dev/null | python3 -c '
import json, sys
sys.exit(0 if any((n.get("name") or "").startswith(sys.argv[1]) for n in json.load(sys.stdin)) else 1)' "$1"
}
# click_text TEXT: click the first node of the tree whose name starts with TEXT.
click_text() {
    local at
    at="$(omabox tree -b "$BOX" --window "$APP_ID" --json 2>/dev/null | python3 -c '
import json, sys
for node in json.load(sys.stdin):
    if (node.get("name") or "").startswith(sys.argv[1]) and node.get("w", 0) > 0:
        print(node["x"] + node["w"] // 2, node["y"] + node["h"] // 2)
        break' "$1")"
    [ -n "$at" ] || return 1
    # shellcheck disable=SC2086 # "X Y"
    omabox click -b "$BOX" --window "$APP_ID" $at >/dev/null
}
app_logged() { omabox log -b "$BOX" run 2>/dev/null | grep -qF -- "$1"; }

# ---- the full destination -----------------------------------------------------------------
rm -rf -- "$WORK"
mkdir -p "$INCOMPLETE/$NAME"
truncate -s 24M "$IMAGE"
mkfs.vfat -n OMASEEDFULL "$IMAGE" >/dev/null
LOOP="$(udisksctl loop-setup --no-user-interaction -f "$IMAGE" | sed -n 's/.* as \(\/dev\/loop[0-9]*\)\.$/\1/p')"
[ -n "$LOOP" ] || { echo "udisksctl loop-setup gave no device" >&2; exit 1; }
# A desktop automounter may mount it first; either way, its mount point.
for _ in $(seq 1 20); do
    MOUNT="$(lsblk -no MOUNTPOINT "$LOOP" 2>/dev/null | head -1)"
    [ -n "$MOUNT" ] && break
    udisksctl mount --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
    sleep 0.25
done
[ -n "$MOUNT" ] && [ -w "$MOUNT" ] || { echo "the image is not mounted writable ($LOOP)" >&2; exit 1; }
head -c $((14 * 1048576)) /dev/zero >"$MOUNT/filler.bin"
echo "full destination: $MOUNT ($(df -h --output=avail "$MOUNT" | tail -1 | tr -d ' ') free)"

# ---- the torrent: its files already in the incomplete folder ------------------------------
for n in $(seq -w 1 12); do
    head -c 1048576 /dev/urandom >"$INCOMPLETE/$NAME/part-$n.bin"
done
python3 - "$INCOMPLETE/$NAME" "$WORK/stuck.torrent" <<'PY'
import hashlib, os, sys
root, out = sys.argv[1], sys.argv[2]
piece = 262144
files = sorted(os.listdir(root))
data = b"".join(open(os.path.join(root, f), "rb").read() for f in files)
pieces = b"".join(hashlib.sha1(data[i:i + piece]).digest() for i in range(0, len(data), piece))
def enc(v):
    if isinstance(v, int): return b"i%de" % v
    if isinstance(v, str): v = v.encode()
    if isinstance(v, bytes): return b"%d:%s" % (len(v), v)
    if isinstance(v, list): return b"l" + b"".join(enc(x) for x in v) + b"e"
    return b"d" + b"".join(enc(k) + enc(v[k]) for k in sorted(v)) + b"e"
info = {"name": os.path.basename(root), "piece length": piece, "pieces": pieces, "private": 1,
        "files": [{"length": os.path.getsize(os.path.join(root, f)), "path": [f]} for f in files]}
open(out, "wb").write(enc({"announce": "http://127.0.0.1:9/announce", "info": info}))
PY

# ---- the qBittorrent ----------------------------------------------------------------------
server start >/dev/null
curl -fs "$API/torrents/add" -F "torrents=@$WORK/stuck.torrent" -F "savepath=$MOUNT" \
    -F "useDownloadPath=true" -F "downloadPath=$INCOMPLETE" -F "autoTMM=false" >/dev/null
check "qBittorrent logged the failed move" wait_for "a Failed to move torrent line" 60 move_failed_logged
check "finished, not moving, still in the incomplete folder" wait_for "the torrent stuck" 30 stuck
# The line itself, for the record (and for a session checking ProblemLine against it).
curl -fs "$API/log/main?normal=false&info=false&warning=true&critical=true" | python3 -c '
import json, sys
for e in json.load(sys.stdin):
    if e["message"].startswith("Failed to move torrent."):
        print("qBittorrent logged:", e["message"])' | tail -1
check "partial copies are left in the destination" partial_copies
check "the destination is full" destination_full

# ---- the box ------------------------------------------------------------------------------
omabox down -b "$BOX" >/dev/null 2>&1 || true
up_args=(--net isolated --allow "$PORT" --ro-bind "$WORK")
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive "${up_args[@]}" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Stuck-move walk %s\n\nBuild `%s`. A finished torrent whose move into a full disk failed. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    omabox up "$BOX" "${up_args[@]}" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
mkdir -p "$home/.config/omaseed" && chmod 700 "$home/.config/omaseed"
printf '{"version":1,"defaultId":"stuck","servers":[{"id":"stuck","name":"stuck","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-10-22T00:00:00Z"}]}' "$PORT" \
    >"$home/.config/omaseed/servers.json"
chmod 600 "$home/.config/omaseed/servers.json"
omabox run -b "$BOX" --pass OMASEED_DISPLAY -d --wait -- ./build/omaseed >/dev/null
omabox wait -b "$BOX" window "$APP_ID" >/dev/null
check "the app shows the torrent" wait_for "the torrent in the table" 30 in_tree "$NAME"

# ---- the walk -----------------------------------------------------------------------------
omabox keys -b "$BOX" --window "$APP_ID" --wait ctrl+k -t "$NAME" Return >/dev/null || true
check "The story opens with the stuck move's block" \
    wait_for "the block's headline" 20 in_tree "Finished, but its files never reached $MOUNT"
step "Finished, never arrived" "docs/44 M1: the row's status reads ✕ move failed, both in red; facts' Status row seeding · move failed. The story opens with the red-ruled block: ✕ Finished, but its files never reached $MOUNT; qBittorrent couldn't move them: No space left on device. It's seeding from $INCOMPLETE. Once there is room, a recheck moves it there.; Partial copies may be left in the destination. (the box can't see it); qBittorrent's log · today HH:MM; then show both folders · c recheck · m set location… · copy the line · its lines in the log. No yellow \"move didn't happen\" block under it."
check "unseen, the partial copies may be left: nothing guessed" in_tree "Partial copies may be left in the destination."
check "the block offers c recheck (C28)" in_tree "recheck"

if click_text "show both folders"; then
    check "Show both folders asked for both" wait_for "two open folder lines" 10 app_logged "show both folders: $INCOMPLETE/$NAME"
else
    check "show both folders is in the window's accessibility tree" false
fi

omabox keys -b "$BOX" --window "$APP_ID" --wait m >/dev/null || true
step "Set location…" "Board 43's sheet for the stuck torrent, as for any move: another place to put it (a move to $MOUNT, where qBittorrent thinks it is, would do nothing). esc closes it; nothing is sent."
omabox keys -b "$BOX" --window "$APP_ID" --wait Escape >/dev/null || true

# Room made (the filler and the partial copies go), then the block's `c recheck` (C28, #158):
# once it checks complete, qBittorrent moves it to its save path. The block goes with it.
rm -rf -- "${MOUNT:?}/filler.bin" "${MOUNT:?}/$NAME"
omabox keys -b "$BOX" --window "$APP_ID" --wait c >/dev/null || true
arrived() { [[ "$(torrent_info content_path)" == "$MOUNT"/* ]]; }
check "after c recheck with room made, it moved into the destination" wait_for "the move" 60 arrived
block_gone() { ! in_tree "Finished, but its files never reached"; }
check "the block went with it" wait_for "the block to go" 20 block_gone
step "Arrived" "The row reads seeding again (no move failed), the story has no red block, its save path $MOUNT."

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. enter takes everything down (the box, the qBittorrent, the image)"
    read -r _
else
    echo "done: $OUT/index.md"
fi
if [ "$FAILED" -gt 0 ]; then
    echo "$FAILED check(s) failed" >&2
    exit 1
fi
