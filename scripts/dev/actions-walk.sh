#!/usr/bin/env bash
# omaseed — the risky actions, walked through the app's own UI: renames, categories, tags,
# automatic torrent management, set location through the Move… sheet (board 43: also onto
# another filesystem, a 2 GiB torrent, and onto a folder that already has its name),
# recheck, the action bar on two torrents (DECISIONS C32 D17), what each action came to in the
# strip over the rows (board 45, DECISIONS C18: done, a refusal, two at once), picking rows
# with the mouse's □ (C18 D6), the command palette (docs/30), delete with and
# without files, and two torrents that share one file (#27: the twin goes in through the add
# sheet's "save there"; #28: the delete confirm, the location sheet, a category change and
# turning automatic management on (#65) ask), and board 44's skipped files that still hold space
# (#118): the On disk column and band (also after an `x` skip and tick back in the app, #180),
# free the space (taught by the ? sheet and the palette,
# #160) on a v1-only torrent (with its recheck), on a hybrid and a v2-only torrent where only
# adding it again frees them (DECISIONS C29, C30 F12-F17: said up front by the band, the palette
# and shift+f, straight to the confirm, then added again), on a hybrid with some to free and
# some not (F15); and a big tick that waits with Undo (#119). After each step it checks what qBittorrent says and
# what is on disk, and stops at the first mismatch. OMASEED_ACTIONS_WALK_FROM=disk runs board
# 44's steps alone.
# Run it again after any change to an action, a sheet, a palette or the action bar.
#
#   scripts/dev/actions-walk.sh [OUT_DIR]        # agents: a headless box, a shot per step,
#                                                # OUT_DIR/index.md says what each shot shows
#                                                # and lists every check
#   scripts/dev/actions-walk.sh --interactive    # Diogo: the box is a window on workspace 9;
#                                                # each step says what it will do and waits for
#                                                # enter, then does it and prints its checks
#
# Everything happens in a box with a qBittorrent of its own (scripts/dev/app-box.sh up --qbt):
# its 127.0.0.1 is not this machine's, it reaches only the swarm's tracker and seeder, and
# throwing the box away throws away every download, move and delete. No other server (the
# playground, the strict twin, the real library) is involved. Five swarm torrents download
# for real (about 260 MB, a minute or two); a 2 GiB torrent is made in the box from a sparse
# file, so the move across filesystems (the box's ~ is on disk, its /tmp is a tmpfs) copies
# real bytes.
#
# The keys are the app's own: the jump palette's tab opens the actions menu on a torrent, and
# esc from the menu leaves the keyboard on the table there. The action bar (floating over the
# list while torrents are selected, DECISIONS C32 D17) is mouse only, so its buttons are clicked
# by their accessible names, where the window's tree places them (bar_click).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_ACTIONS_BOX:-actions-walk}"
BOX_HOME=/home/sbx
TORRENTS_DIR="$BOX_HOME/Torrents"   # app-box.sh --qbt's save path
BOX_API=http://127.0.0.1:8090/api/v2  # app-box.sh --qbt's qBittorrent, as the box sees it
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-actions-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

SEEDING="('uploading', 'stalledUP', 'queuedUP', 'forcedUP')"

# ---- helpers ------------------------------------------------------------------------------

api() { "$APP_BOX" api -b "$BOX" "$@"; }

# tq HASH EXPR: a Python expression over `t`, the torrent HASH (None once it is gone), and
# `every`, all of them, as qBittorrent lists them now.
tq() {
    api torrents/info | python3 -c '
import json, sys
every = json.load(sys.stdin)
t = next((x for x in every if x["hash"] == sys.argv[1]), None)
print(eval(sys.argv[2]))' "$1" "$2"
}

# hash_of PREFIX: the hash of the torrent whose name starts with PREFIX.
hash_of() {
    api torrents/info | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1])))' "$1"
}

# in_box CMD...: run CMD in the box (its /tmp and paths are not this machine's).
in_box() { omabox run -b "$BOX" -- "$@" >/dev/null 2>&1; }

# The outcome strip's line runs down for 6 s at the table's bottom (DECISIONS C18 D3): a wait
# for the window to be still would wait it out, and the strip with it. Its band of the screen
# (the action bar's too) is left out of every wait; it is wide and tall enough to hold
# whether omabox reads it in the screen's or the window's coordinates.
STRIP_IGNORE="0,920 1920,110"
keys() { omabox keys -b "$BOX" --window omaseed --wait --ignore "$STRIP_IGNORE" "$@" >/dev/null 2>&1 || true; }
# keys_now KEY...: the same without waiting for the screen to settle, for while something on it
# keeps moving (the hold band's countdown, #119) and the 3 s matter.
keys_now() { omabox keys -b "$BOX" --window omaseed "$@" >/dev/null 2>&1 || true; }
click() { omabox click -b "$BOX" --window omaseed --wait --ignore "$STRIP_IGNORE" "$1" "$2" >/dev/null 2>&1 || true; }

# row_tick NAME: click the □ at the start of the row whose torrent is NAME (C18 D6), where the
# window's tree places the row.
row_tick() {
    local where
    where="$(omabox tree -b "$BOX" --window omaseed --find "$1" 2>/dev/null | python3 -c '
import re, sys
for line in sys.stdin:
    m = re.search(r"list item \"" + re.escape(sys.argv[1]) + r"\"[^@]* @(-?\d+),(-?\d+) (\d+)x(\d+)", line)
    if m:
        x, y, w, h = map(int, m.groups())
        print(x + 8, y + h // 2)
        break' "$1")"
    [ -n "$where" ] || fail "no row \"$1\" on screen"
    # shellcheck disable=SC2086 # two numbers
    omabox click -b "$BOX" --window omaseed --wait --ignore "$STRIP_IGNORE" $where >/dev/null 2>&1 || true
}

# menu_on QUERY: the jump palette's tab: the cursor on the torrent QUERY finds, its actions
# menu open. table_on QUERY: the same, the menu closed again, the keyboard on the table.
menu_on() { keys ctrl+k -t "$1" Tab; }
table_on() { menu_on "$1"; keys Escape; }

# watch_checking HASH OUT: in the background, poll the box's qBittorrent from inside the box
# every 50 ms for up to 15 s: OUT (a file here) gets "polling" once it runs, then "seen" once
# HASH reads checking (or "missed").
# A 2 GiB check in memory lasts about two seconds, too short for polls through omabox run;
# start it before the key, then wait for it.
watch_checking() {
    # shellcheck disable=SC2016 # expanded by the box's sh
    omabox run -b "$BOX" -- sh -c '
echo polling
for _ in $(seq 1 300); do
    curl -s "$1/torrents/info?hashes=$2" | grep -q "\"state\":\"checking" && { echo seen; exit 0; }
    sleep 0.05
done
echo missed' sh "$BOX_API" "$1" >"$2" 2>/dev/null &
}

CHECKS=0
STEP=0
STEP_SLUG=""

fail() {
    printf '  \033[31m✗ %s\033[0m\n' "$1"
    if [ "$INTERACTIVE" = 0 ]; then
        printf '  - ✗ **%s**\n' "$1" >>"$OUT/index.md"
        omabox shot -b "$BOX" -o "$OUT/failed.png" >/dev/null 2>&1 || true
        printf '\n**Stopped here.** ![](failed.png)\n' >>"$OUT/index.md"
        echo "failed: $OUT/index.md"
    fi
    exit 1
}

pass() {
    CHECKS=$((CHECKS + 1))
    printf '  ✓ %s\n' "$1"
    [ "$INTERACTIVE" = 1 ] || printf '  - ✓ %s\n' "$1" >>"$OUT/index.md"
}

# expect "WHAT" HASH EXPR [SECONDS]: wait until EXPR (see tq) is True for HASH.
expect() {
    local what="$1" hash="$2" expr="$3" limit="${4:-20}" got=""
    for _ in $(seq 1 $((limit * 4))); do
        got="$(tq "$hash" "$expr" 2>&1 || true)"
        [ "$got" = True ] && { pass "$what"; return 0; }
        sleep 0.25
    done
    fail "$what (after ${limit}s: $expr is ${got:-nothing})"
}

# expect_disk "WHAT" TEST PATH [SECONDS]: wait until `test TEST PATH` holds in the box; TEST
# `!` waits until nothing is at PATH.
expect_disk() {
    local what="$1" op="$2" path="$3" limit="${4:-20}" test=()
    if [ "$op" = "!" ]; then test=(! -e "$path"); else test=("$op" "$path"); fi
    for _ in $(seq 1 $((limit * 4))); do
        in_box test "${test[@]}" && { pass "$what"; return 0; }
        sleep 0.25
    done
    fail "$what (after ${limit}s: test $op $path is false in the box)"
}

# expect_text "WHAT" TEXT [SECONDS]: wait until omaseed's window shows TEXT (its accessibility
# tree has a node holding it).
expect_text() {
    local what="$1" text="$2" limit="${3:-20}"
    for _ in $(seq 1 "$limit"); do
        omabox tree -b "$BOX" --window omaseed --find "$text" >/dev/null 2>&1 && {
            pass "$what"
            return 0
        }
        sleep 1
    done
    fail "$what (after ${limit}s no \"$text\" in the window)"
}

# expect_no_text "WHAT" TEXT [SECONDS]: wait until omaseed's window no longer shows TEXT.
expect_no_text() {
    local what="$1" text="$2" limit="${3:-20}"
    for _ in $(seq 1 "$limit"); do
        omabox tree -b "$BOX" --window omaseed --find "$text" >/dev/null 2>&1 || {
            pass "$what"
            return 0
        }
        sleep 1
    done
    fail "$what (after ${limit}s \"$text\" is still in the window)"
}

# expect_read "WHAT" PREFIX TEXT [SECONDS]: wait until the window's node whose name holds PREFIX
# reads TEXT on screen. The tree cuts names at 80 characters, so the node's region is shot and
# read with tesseract (whitespace folded, case aside); without tesseract only PREFIX is checked.
OUT_READ="$(mktemp --suffix=.png)"
expect_read() {
    local what="$1" prefix="$2" text="$3" limit="${4:-10}" where="" read=""
    if ! command -v tesseract >/dev/null; then
        expect_text "$what (tesseract missing: only \"$prefix\" checked)" "$prefix" "$limit"
        return 0
    fi
    for _ in $(seq 1 "$limit"); do
        where="$(omabox tree -b "$BOX" --window omaseed --find "$prefix" 2>/dev/null |
            sed -nE 's/.*@(-?[0-9]+),(-?[0-9]+) ([0-9]+)x([0-9]+).*/\1,\2 \3x\4/p' | tail -n1)"
        if [ -n "$where" ]; then
            omabox shot -b "$BOX" --window omaseed -g "$where" --zoom 2 -o "$OUT_READ" >/dev/null 2>&1 || true
            read="$(tesseract "$OUT_READ" - 2>/dev/null | tr -s '[:space:]' ' ')"
            grep -qiF -- "$text" <<<"$read" && { pass "$what"; return 0; }
        fi
        sleep 1
    done
    fail "$what (after ${limit}s the line reads: ${read:-nothing})"
}

# expect_size "WHAT" PATH BYTES: the file PATH in the box is BYTES long.
expect_size() {
    local size
    size="$(omabox run -b "$BOX" -- stat -c %s "$2" 2>/dev/null || true)"
    if [ "$size" = "$3" ]; then pass "$1"; else fail "$1 ($2 is ${size:-missing}, not $3 bytes)"; fi
}

# expect_file "WHAT" HASH NAME: wait until the torrent HASH lists the file NAME.
expect_file() {
    local what="$1" hash="$2" name="$3" limit="${4:-20}"
    for _ in $(seq 1 $((limit * 4))); do
        api "torrents/files?hash=$hash" | python3 -c '
import json, sys
sys.exit(0 if sys.argv[1] in [f["name"] for f in json.load(sys.stdin)] else 1)' "$name" && {
            pass "$what"
            return 0
        }
        sleep 0.25
    done
    fail "$what (after ${limit}s the torrent does not list $name)"
}

# step "TITLE" "WHAT IT DOES": interactive, say it and wait for enter; headless, a heading.
step() {
    STEP=$((STEP + 1))
    STEP_SLUG="$(printf '%02d' "$STEP")-$(echo "$1" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')"
    printf '\n\033[1m%02d %s\033[0m\n    %s\n' "$STEP" "$1" "$2"
    if [ "$INTERACTIVE" = 1 ]; then
        printf '    enter to do it '
        read -r _
    else
        printf '\n### %02d %s\n\n%s\n\n' "$STEP" "$1" "$2" >>"$OUT/index.md"
    fi
}

# shot "WHAT TO SEE" [SUFFIX]: headless, the app's window into OUT and index.md.
shot() {
    [ "$INTERACTIVE" = 0 ] || return 0
    local file="$STEP_SLUG${2:+-$2}.png"
    omabox shot -b "$BOX" --window omaseed -o "$OUT/$file" >/dev/null 2>&1
    printf '  ![%s](%s)\n  *%s*\n' "$file" "$file" "$1" >>"$OUT/index.md"
}

cleanup() {
    local status=$?
    rm -f "$OUT_READ"
    if [ "$INTERACTIVE" = 1 ]; then
        echo
        echo "The box stays up for a look around; throw it away with: scripts/dev/app-box.sh down -b $BOX"
    else
        "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
trap cleanup EXIT

# ---- setup --------------------------------------------------------------------------------

if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --qbt --interactive -b "$BOX" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Actions walk %s\n\nBuild `%s`, a box with its own qBittorrent (`app-box.sh up --qbt`). Each step: what was done, every check on the server and on disk, and shots.\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up --qbt -b "$BOX" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"

# ---- board 44: skipped files and the disk (DECISIONS C17, #118–#120) ----------------------
# Two torrents of four 8 MiB files, one v1-only and one hybrid, made and seeded by the box's
# own qBittorrent, so every skipped file holds its data and its downloaded pieces; and a v2-only
# one whose files are no piece multiple, as C29's trap needs (#182). The ntfs3
# side (a tick that allocates in full, on a loop image) needs a mount the box can write to,
# which udisks makes only on this machine: tests/core/tst_skipped_ntfs3_live.cpp and, for a
# tick that doesn't fit and so waits (DECISIONS C28, #159), tests/app/tst_app_tickroom_live.cpp
# run it there against the playground.

# click_text TEXT: a click in the middle of the app's accessible node whose name starts with
# TEXT, from the window's own tree.
click_text() {
    local at
    at="$(omabox tree -b "$BOX" --window omaseed --json 2>/dev/null | python3 -c '
import json, sys
for node in json.load(sys.stdin):
    if (node.get("name") or "").startswith(sys.argv[1]) and node.get("w", 0) > 0:
        print(node["x"] + node["w"] // 2, node["y"] + node["h"] // 2)
        break' "$1")"
    [ -n "$at" ] || fail "no \"$1\" to click in the window"
    # shellcheck disable=SC2086 # "X Y"
    click $at
}

# bar_click NAME: the action bar's button named NAME exactly ("stop", "category", "delete…"),
# the lowest such node in the window: the bar floats at the list's foot (C32 D17); a row of
# the list it opens upward over the bar ("walk") is lower than the rail's row of that name.
bar_click() {
    local at
    at="$(omabox tree -b "$BOX" --window omaseed --json 2>/dev/null | python3 -c '
import json, sys
nodes = [n for n in json.load(sys.stdin) if (n.get("name") or "") == sys.argv[1] and n.get("w", 0) > 0]
if nodes:
    n = max(nodes, key=lambda n: n["y"])
    print(n["x"] + n["w"] // 2, n["y"] + n["h"] // 2)' "$1")"
    [ -n "$at" ] || fail "no \"$1\" button on the action bar"
    # shellcheck disable=SC2086 # "X Y"
    click $at
}

# make_disk_torrent NAME FORMAT [SIZES]: four files of random bytes in ~/Torrents/NAME, a.bin to
# d.bin (8 MiB each, or the four byte counts SIZES lists), and a torrent of them (FORMAT v1, v2
# or hybrid) that the box's qBittorrent makes and seeds from there.
make_disk_torrent() {
    local name="$1" format="$2" task i sizes files=(a b c d)
    read -r -a sizes <<<"${3:-8388608 8388608 8388608 8388608}"
    mkdir -p "$home/Torrents/$name"
    for i in 0 1 2 3; do
        head -c "${sizes[$i]}" /dev/urandom >"$home/Torrents/$name/${files[$i]}.bin"
    done
    task="$(api torrentcreator/addTask -d "sourcePath=$TORRENTS_DIR/$name" -d "format=$format" \
        -d startSeeding=true | python3 -c 'import json, sys; print(json.load(sys.stdin)["taskID"])')"
    for _ in $(seq 1 120); do
        api torrents/info | python3 -c '
import json, sys
sys.exit(0 if any(t["name"] == sys.argv[1] and t["progress"] == 1 for t in json.load(sys.stdin)) else 1)' "$name" && break
        sleep 0.5
    done
    api -X POST torrentcreator/deleteTask -d "taskID=$task" >/dev/null 2>&1 || true
}

# make_mixed_torrent NAME: a hybrid of four 8 MiB files whose c.bin is skipped after it was
# all there (it holds downloaded pieces) and whose d.bin was missing when the torrent was added,
# so qBittorrent knows no piece of it, then is skipped and written here: it holds space and no
# piece (DECISIONS C30 F15's "some to free, some not"). Made with startSeeding off, so nothing
# is ever rechecked after a delete (#115).
make_mixed_torrent() {
    local name="$1" i task file="$BOX_HOME/$1.torrent"
    mkdir -p "$home/Torrents/$name"
    for i in a b c d; do
        head -c 8388608 /dev/urandom >"$home/Torrents/$name/$i.bin"
    done
    task="$(api torrentcreator/addTask -d "sourcePath=$TORRENTS_DIR/$name" -d format=hybrid \
        -d startSeeding=false --data-urlencode "torrentFilePath=$file" |
        python3 -c 'import json, sys; print(json.load(sys.stdin)["taskID"])')"
    for _ in $(seq 1 120); do
        [ -s "$home/$name.torrent" ] && break
        sleep 0.5
    done
    api -X POST torrentcreator/deleteTask -d "taskID=$task" >/dev/null 2>&1 || true
    rm -f -- "$home/Torrents/$name/d.bin"
    api torrents/add -F "torrents=@$file" -F "savepath=$TORRENTS_DIR" -F stopped=true >/dev/null
    for _ in $(seq 1 120); do
        api torrents/info | python3 -c '
import json, sys
sys.exit(0 if any(t["name"] == sys.argv[1] and t["state"] in ("stoppedDL", "stoppedUP", "pausedDL", "pausedUP")
                  and 0.7 < t["progress"] < 0.8 for t in json.load(sys.stdin)) else 1)' "$name" && break
        sleep 0.5
    done
    local hash
    hash="$(hash_of "$name")"
    api torrents/filePrio -d "hash=$hash" -d "id=2|3" -d priority=0 >/dev/null
    head -c 8388608 /dev/urandom >"$home/Torrents/$name/d.bin"
    api torrents/start -d "hashes=$hash" >/dev/null
    expect "b44-mix seeds its two wanted files" "$hash" "t['state'] in $SEEDING and t['progress'] == 1" 60
}

# priorities HASH: the torrent's file priorities, in file order ("1 1 0 0").
priorities() {
    api "torrents/files?hash=$1" | python3 -c '
import json, sys
print(" ".join(str(f["priority"]) for f in json.load(sys.stdin)))'
}

# expect_priorities "WHAT" HASH "P P P P" [SECONDS]: wait until the torrent's file priorities
# are those.
expect_priorities() {
    local got="" limit="${4:-10}"
    for _ in $(seq 1 $((limit * 4))); do
        got="$(priorities "$2")"
        [ "$got" = "$3" ] && { pass "$1: $3"; return 0; }
        sleep 0.25
    done
    fail "$1 (after ${limit}s the priorities are $got, not $3)"
}

disk_walk() {
    make_disk_torrent b44-v1 v1
    make_disk_torrent b44-hybrid hybrid
    make_disk_torrent b44-skip v1
    # C29: no file a piece multiple, so every file's last piece is short (the clip in #182).
    make_disk_torrent b44-v2 v2 "8388608 3158073 2097929 500003"
    D_V1="$(hash_of b44-v1)"
    D_HY="$(hash_of b44-hybrid)"
    D_SK="$(hash_of b44-skip)"
    D_V2="$(hash_of b44-v2)"
    [ "$(tq "$D_HY" 'bool(t["infohash_v1"]) and bool(t["infohash_v2"])')" = True ] ||
        { echo "b44-hybrid is not hybrid" >&2; exit 1; }
    [ "$(tq "$D_V1" 'not t["infohash_v2"]')" = True ] || { echo "b44-v1 is not v1-only" >&2; exit 1; }
    [ "$(tq "$D_V2" 'not t["infohash_v1"] and bool(t["infohash_v2"])')" = True ] ||
        { echo "b44-v2 is not v2-only" >&2; exit 1; }
    for h in "$D_V1" "$D_HY"; do
        api torrents/filePrio -d "hash=$h" -d "id=2|3" -d priority=0 >/dev/null
    done
    # b44-v2: one file skipped once it is all there, its pieces downloaded.
    api torrents/filePrio -d "hash=$D_V2" -d id=2 -d priority=0 >/dev/null
    api torrents/createCategory -d category=b44 >/dev/null 2>&1 || true
    api torrents/setCategory -d "hashes=$D_HY" -d category=b44 >/dev/null
    api torrents/addTags -d "hashes=$D_HY" -d tags=b44-tag >/dev/null
    keys Escape

    # #180: the band follows a skip and a tick made with `x` in the app, not only skips that
    # were on the server before the tab opened.
    step "Skip in the app, then tick back" "b44-skip, every file wanted, its Files tab: j onto a.bin, x skips it. Its 8 MiB stay on disk, a quarter of the torrent, so the band comes once qBittorrent has the skip. x again ticks it back: the whole torrent, so it waits 3 s (#119), and once it is sent the band goes."
    table_on "b44-skip"
    keys l 2
    expect_no_text "no band while every file is wanted" "skipped file holds" 3
    keys j
    keys x
    expect_priorities "qBittorrent has a.bin skipped" "$D_SK" "0 1 1 1"
    expect_text "the band says 1 skipped file holds space" "1 skipped file holds" 10
    shot "a.bin reads skip, On disk 8.00 MB; the yellow band: 1 skipped file holds 8.00 MB, with Free the space…."
    keys_now x
    expect_text "the tick back waits" "Wanting 1 more file" 3
    expect_priorities "sent once the hold ran out: every file wanted" "$D_SK" "1 1 1 1" 10
    expect_no_text "the band is gone" "skipped file holds" 10
    shot "Every file normal again: no band, and no On disk column (nothing skipped holds space)." ticked

    step "Skipped files that hold space" "b44-v1, its Files tab: c.bin and d.bin are skipped but still hold their 8 MiB each. The On disk column shows it, and the yellow band says what they hold (16.0 MB, half the torrent: past 5 %)."
    table_on "b44-v1"
    keys l 2
    expect_text "the band says 2 skipped files hold space" "2 skipped files hold"
    expect_text "the On disk column is there" "On disk"
    shot "The Files tab: On disk after Size, \"—\" on the wanted files, 8.4 MB on the skipped; the yellow band with Free the space… and Why?."

    step "Free the space is taught" "#160: the ? sheet has F, free the space, or add it again (C30 F13), among the acts; the : palette, from the Files tab, offers free the space… with its key F."
    keys -t "?"
    expect_text "the ? sheet teaches F" "In the files tab: free the space, or add it again" 10
    shot "The ? sheet: F, free the space, or add it again, after O o r." help
    keys Escape
    keys -t ":free"
    expect_text "the palette offers free the space…" "free the space…" 10
    shot "The command palette from the Files tab, \"free\" typed: free the space… with its key F." palette

    step "Free the space, v1-only" "From the palette, enter (shift+f does the same: the hybrid's step presses it): the sheet deletes c.bin and d.bin; they hold downloaded pieces, so on this v1-only torrent it says it rechecks after the start. enter."
    keys Return
    expect_text "the sheet says it rechecks after the start" "Start it again, then recheck it"
    shot "Free 16.0 MB: stop, delete 2 skipped files (some holding downloaded pieces), start and recheck; both files marked delete." sheet
    keys Return
    expect_disk "c.bin is gone" ! "$TORRENTS_DIR/b44-v1/c.bin" 30
    expect_disk "d.bin is gone" ! "$TORRENTS_DIR/b44-v1/d.bin"
    expect_disk "a.bin, wanted, is still there" -f "$TORRENTS_DIR/b44-v1/a.bin"
    expect_priorities "the selection is as it was" "$D_V1" "1 1 0 0"
    expect "rechecked, it seeds the wanted files, complete" "$D_V1" "t['state'] in $SEEDING and t['progress'] == 1" 60
    expect_text "the footer says what was freed" "freed"
    expect_no_text "the band is gone" "skipped files hold"
    shot "The band is gone; the footer: freed 16.0 MB, 2 skipped files deleted, rechecking."

    # DECISIONS C30 F12-F17: on a hybrid whose space-holding skipped files all have downloaded
    # pieces, only adding it again frees them, and the band, the palette and shift+f say so up
    # front; the size says what is still on disk.
    step "Only adding it again frees them, hybrid" "b44-hybrid, its Files tab: c.bin and d.bin hold downloaded pieces, and a recheck on a hybrid hits libtorrent's bug (#115). The band keeps its title, says only adding it again frees them, and offers Add it again… F; Why? says hybrid. The files head: 2 skipped, 16.0 MB still on disk."
    table_on "b44-hybrid"
    keys l 2
    expect_text "the band shows on the hybrid" "2 skipped files hold"
    expect_read "the band says only adding it again frees them (F12)" "They were written while wanted, and pieces of them" "only adding it again frees them"
    expect_text "its action is Add it again (F12)" "Add it again…"
    expect_read "the files head says what is still on disk (F17)" "hide skipped files" "MB still on disk"
    click_text "Why?"
    expect_text "Why? says hybrid" "This torrent is hybrid (v1 and v2)"
    shot "The band: 2 skipped files hold 16.0 MB, only adding it again frees them, Add it again… F, Why? open on hybrid; the files head 2 skipped, 16.0 MB still on disk." band
    click_text "Why?"

    step "The size in facts" "5: facts, its Size row: 32.0 MB · 2 skipped, 16.0 MB still on disk (F17)."
    keys 5
    expect_read "facts' size says what is still on disk (F17)" "Size" "still on disk"
    shot "Facts: Size 32.0 MB · 2 skipped, 16.0 MB still on disk; Done, Ratio, then Status." facts
    keys 2

    step "The palette and the ? sheet say add it again" "The : palette from the Files tab offers add it again to free 16.0 MB… with F (F13); the ? sheet's F reads free the space, or add it again."
    keys -t ":add it again"
    expect_text "the palette row says add it again (F13)" "add it again to free 16.0 MB…"
    shot "The palette: add it again to free 16.0 MB…, F." palette
    keys Escape Escape
    keys -t "?"
    expect_text "the ? sheet's F (F13)" "In the files tab: free the space, or add it again"
    keys Escape

    step "shift+f goes straight to the confirm" "shift+f: no sheet, the add-again confirm at once (F13): Add b44-hybrid again?, 2 skipped files are deleted (16.0 MB), what it loses, and one dim line: Nothing else frees them: this torrent is hybrid, and pieces of them were downloaded. why? opens the longer words. l, enter: with no sheet's plan in hand it plans first (F16), then removes it keeping its files, deletes c.bin and d.bin, adds it back with the same selection, category and tag, and starts it."
    keys shift+f
    expect_text "the confirm asks first" "Add b44-hybrid again?"
    expect_read "the confirm names the size" "It is removed from qBittorrent keeping its files, 2 skipped" "files are deleted (16"
    expect_text "and says why in one line" "Nothing else frees them: this torrent is hybrid"
    click_text "why?"
    expect_read "why? opens the longer words" "Deleting a file that holds downloaded pieces" "on a hybrid hits a libtorrent bug"
    shot "Add b44-hybrid again? 2 skipped files are deleted (16.0 MB); Nothing else frees them: this torrent is hybrid, …; why? open." dialog
    keys l Return
    expect_disk "c.bin is gone" ! "$TORRENTS_DIR/b44-hybrid/c.bin" 30
    expect_disk "d.bin is gone" ! "$TORRENTS_DIR/b44-hybrid/d.bin"
    expect "it is back, seeding, not in error" "$D_HY" "t is not None and t['state'] in $SEEDING and t['progress'] == 1" 60
    expect "its category and tag came back" "$D_HY" "t['category'] == 'b44' and 'b44-tag' in t['tags']"
    expect_priorities "the same files are wanted" "$D_HY" "1 1 0 0"
    expect_no_text "the band is gone" "skipped files hold" 30
    shot "b44-hybrid is back; the footer: added again."

    # F15: a hybrid with one skipped file that holds pieces and one that holds space but no
    # piece (written here after qBittorrent checked it missing): some can go.
    step "Some to free, some not" "b44-mix, a hybrid: c.bin skipped after it was all there (pieces), d.bin skipped and missing when added, then written here (space, no piece). The band keeps Free the space…; shift+f: the sheet frees 8.00 MB (d.bin) and offers re-add to free all 16.0 MB… beside it (F15). enter: d.bin goes, no recheck, c.bin stays."
    make_mixed_torrent b44-mix
    D_MX="$(hash_of b44-mix)"
    table_on "b44-mix"
    keys l 2
    expect_text "the band shows on the mixed hybrid" "2 skipped files hold"
    expect_text "its action is still Free the space (F15)" "Free the space…"
    keys shift+f
    expect_text "the sheet frees what it can" "Free 8.00 MB"
    expect_text "and offers the re-add of all of it beside (F15)" "re-add to free all 16.0 MB…"
    expect_text "c.bin is marked as staying" "has downloaded pieces · stays"
    shot "Free 8.00 MB: d.bin delete, c.bin has downloaded pieces · stays; buttons cancel, show in folder, re-add to free all 16.0 MB…, free 8.00 MB." sheet
    keys Return
    expect_disk "d.bin is gone" ! "$TORRENTS_DIR/b44-mix/d.bin" 30
    expect_disk "c.bin, with pieces, stays" -f "$TORRENTS_DIR/b44-mix/c.bin"
    expect "it seeds, not rechecked, not in error" "$D_MX" "t['state'] in $SEEDING and t['progress'] == 1" 30
    expect_read "now only adding it again frees the one left (F12)" "It was written while wanted, and pieces of it" "only adding it again frees it" 30
    shot "After the free: the band for c.bin alone, 1 skipped file holds 8.00 MB, only adding it again frees it." mixed

    # DECISIONS C29 (#182): deleting c.bin and rechecking would leave b44-v2 short of 100 % for
    # good, so only adding it again frees it, said up front in v2's words (C30 F12-F13).
    step "Only adding it again frees it, v2-only" "b44-v2, a v2-only torrent, its Files tab: c.bin (2 MiB, no piece multiple) was skipped after it was all there. The band: only adding it again frees it, Add it again… F; Why? says v2 (short of 100 % for good). shift+f: straight to the confirm, in v2's words. esc: nothing changed, still 100 %."
    expect_priorities "c.bin is skipped" "$D_V2" "1 1 0 1"
    table_on "b44-v2"
    keys l 2
    expect_text "the band shows on the v2-only torrent" "1 skipped file holds"
    expect_read "the band says only adding it again frees it (F12)" "It was written while wanted, and pieces of it" "only adding it again frees it"
    click_text "Why?"
    expect_read "Why? says v2, short of 100 % for good" "This torrent is v2" "short of 100"
    shot "The band on b44-v2: 1 skipped file holds, only adding it again frees it, Why? open on v2." band
    click_text "Why?"
    keys shift+f
    expect_text "the confirm asks first" "Add b44-v2 again?"
    expect_text "in v2's words (F13)" "Nothing else frees it: this torrent is v2"
    expect_text "and names the one file that goes" "1 skipped file is deleted"
    shot "Add b44-v2 again? 1 skipped file is deleted (…); Nothing else frees it: this torrent is v2, and pieces of it were downloaded." dialog
    keys Escape
    expect_no_text "the confirm is closed" "Add b44-v2 again?" 5
    expect_disk "c.bin is still there" -f "$TORRENTS_DIR/b44-v2/c.bin"
    expect "nothing rechecked: it seeds, 100 %" "$D_V2" "t['state'] in $SEEDING and t['progress'] == 1"

    step "Add it again, v2-only" "The band's Add it again…: the same confirm; l, enter: it plans first (F16), removes it keeping its files, deletes c.bin, adds it back from its own .torrent with the same selection, and starts it. Checked from scratch with c.bin gone, it reads 100 % (C29: adding it again is sound on v2)."
    click_text "Add it again…"
    expect_text "the confirm asks first" "Add b44-v2 again?"
    keys l Return
    expect_disk "c.bin is gone" ! "$TORRENTS_DIR/b44-v2/c.bin" 30
    expect_disk "d.bin, wanted, is still there" -f "$TORRENTS_DIR/b44-v2/d.bin"
    expect "it is back, seeding, 100 %" "$D_V2" "t is not None and t['state'] in $SEEDING and t['progress'] == 1" 60
    expect "still v2-only" "$D_V2" "not t['infohash_v1'] and bool(t['infohash_v2'])"
    expect_priorities "the same files are wanted" "$D_V2" "1 1 0 1"
    expect_no_text "the band is gone" "skipped file holds" 30
    shot "b44-v2 is back at 100 %, c.bin gone; the footer: added again." back

    step "A big tick waits, and undo sends nothing" "b44-v1 (c.bin and d.bin skipped, gone from disk): ctrl+a, + raises every file, the whole torrent, so the change waits 3 s with Undo and Send now (#119). u: the rows go back, nothing reaches qBittorrent, and no file is created."
    table_on "b44-v1"
    keys l 2
    keys ctrl+a
    keys_now -t "+"
    expect_text "the band says what waits" "Wanting 2 more files" 3
    shot "Wanting 2 more files · 16.0 MB, Undo u, Send now ⏎, its line running down; the rows already read high." hold
    keys_now u
    expect_no_text "the band is gone" "Wanting 2 more files" 5
    sleep 4
    expect_priorities "qBittorrent never heard of it" "$D_V1" "1 1 0 0"
    expect_disk "c.bin was never created" ! "$TORRENTS_DIR/b44-v1/c.bin"

    step "Send now" "The same tick again, then enter: it goes at once."
    keys_now -t "+"
    expect_text "the band is back" "Wanting 2 more files" 3
    keys_now Return
    expect_priorities "qBittorrent has every file at high" "$D_V1" "6 6 6 6"

    step "Leaving sends what waits" "Back to 1 1 0 0 through the API; the same tick, then the jump palette onto b44-hybrid before the 3 s are up: the change goes as the torrent is left."
    api torrents/filePrio -d "hash=$D_V1" -d "id=0|1" -d priority=1 >/dev/null
    api torrents/filePrio -d "hash=$D_V1" -d "id=2|3" -d priority=0 >/dev/null
    expect_priorities "back to 1 1 0 0" "$D_V1" "1 1 0 0"
    sleep 3
    keys_now -t "+"
    keys_now ctrl+k -t "b44-hybrid" Tab
    keys_now Escape
    expect_priorities "sent on leaving" "$D_V1" "6 6 6 6" 5
    keys Escape
}

# finish: the walk's last lines.
finish() {
    echo
    echo "all $CHECKS checks passed"
    if [ "$INTERACTIVE" = 0 ]; then
        printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
        echo "shots and checks: $OUT/index.md"
    fi
}

# OMASEED_ACTIONS_WALK_FROM=disk: board 44's steps alone, on a box with nothing else in it.
if [ "${OMASEED_ACTIONS_WALK_FROM:-}" = disk ]; then
    disk_walk
    finish
    exit 0
fi
echo "box $BOX is up; adding five swarm torrents and making a 2 GiB one"

# The swarm's .torrent files are in the box's ~/Downloads (app-box.sh put them there).
add_args=()
for name in "tiny-notes.txt" "Ünïcødé" "Public Domain Sampler" "Field Recordings" "Nested Library"; do
    for file in "$home"/Downloads/*.torrent; do
        [[ "$(basename "$file")" == "$name"* ]] && add_args+=(-F "torrents=@$BOX_HOME/Downloads/$(basename "$file")")
    done
done
[ ${#add_args[@]} = 10 ] || { echo "the box's ~/Downloads lacks some swarm .torrent files" >&2; exit 1; }
api torrents/add "${add_args[@]}" >/dev/null

# The 2 GiB torrent: a sparse file of zeros where qBittorrent saves, a .torrent for it (every
# 4 MiB piece hashes the same), added so qBittorrent checks it and finds it complete.
BIG=walk-2GiB.bin
BIG_SIZE=$((2 * 1024 * 1024 * 1024))
mkdir -p "$home/Torrents"
truncate -s "$BIG_SIZE" "$home/Torrents/$BIG"
# Its twin (#28): the same file with another `source` in the info dict, as a second tracker
# would issue it: another infohash, one copy on disk. TWIN is that infohash.
TWIN="$(python3 - "$home/Downloads/$BIG.torrent" "$BIG" "$BIG_SIZE" "$home/Downloads/$BIG.twin.torrent" <<'PY'
import hashlib, sys
path, name, size, twin = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
piece = 4 * 1024 * 1024
def b(v):
    if isinstance(v, int): return b"i%de" % v
    if isinstance(v, str): v = v.encode()
    if isinstance(v, bytes): return b"%d:%s" % (len(v), v)
    if isinstance(v, dict): return b"d" + b"".join(b(k) + b(v[k]) for k in sorted(v)) + b"e"
    raise TypeError(v)
info = {"name": name, "length": size, "piece length": piece,
        "pieces": hashlib.sha1(bytes(piece)).digest() * (size // piece)}
open(path, "wb").write(b({"created by": "omaseed actions-walk", "info": info}))
info["source"] = "walk-twin"
open(twin, "wb").write(b({"created by": "omaseed actions-walk", "info": info,
                          "announce": "http://127.0.0.1:26996/announce"}))
print(hashlib.sha1(b(info)).hexdigest())
PY
)"
api torrents/add -F "torrents=@$BOX_HOME/Downloads/$BIG.torrent" -F "savepath=$TORRENTS_DIR" >/dev/null

echo "waiting for the downloads and the check (up to 5 min)"
for _ in $(seq 1 300); do
    [ "$(api torrents/info | python3 -c '
import json, sys
every = json.load(sys.stdin)
print(len(every) == 6 and all(t["progress"] == 1 for t in every))')" = True ] && break
    sleep 1
done
TINY="$(hash_of tiny-notes)"
UNI="$(hash_of Ünïcødé)"
PDS="$(hash_of "Public Domain")"
FIELD="$(hash_of "Field Recordings")"
NESTED="$(hash_of "Nested Library")"
BIGH="$(hash_of "$BIG")"
for h in "$TINY" "$UNI" "$PDS" "$FIELD" "$NESTED" "$BIGH"; do
    [ "$(tq "$h" 't["progress"] == 1')" = True ] || { echo "not every torrent finished: $(tq "$h" 't["name"], t["state"], t["progress"]')" >&2; exit 1; }
done
UNI_PATH="$(tq "$UNI" 't["content_path"]')"
keys Escape
sleep 1

# ---- the walk -----------------------------------------------------------------------------

step "Rename a torrent" "tiny-notes.txt, r in the table, a new name, enter. qBittorrent renames the torrent only: the file on disk keeps its name."
table_on "tiny-notes"
keys r
shot "The inline editor on tiny-notes.txt, the whole name selected." editor
keys -t "tiny notes (renamed).txt" Return
expect "the torrent is called \"tiny notes (renamed).txt\"" "$TINY" 't["name"] == "tiny notes (renamed).txt"'
expect_disk "the file on disk is still tiny-notes.txt" -f "$TORRENTS_DIR/tiny-notes.txt"
shot "The row reads \"tiny notes (renamed).txt\"."

step "Rename a file" "Public Domain Sampler's Files tab, README.txt, r, \"LEES MIJ\", enter (the extension stays)."
table_on "Public Domain"
keys l 2
keys j r
shot "The Files tab: README.txt in the inline editor, \"README\" selected, \".txt\" not." editor
keys -t "LEES MIJ" Return
expect_file "qBittorrent lists Public Domain Sampler/LEES MIJ.txt" "$PDS" "Public Domain Sampler/LEES MIJ.txt"
expect_disk "LEES MIJ.txt is on disk" -f "$TORRENTS_DIR/Public Domain Sampler/LEES MIJ.txt"
expect_disk "README.txt is gone from disk" ! "$TORRENTS_DIR/Public Domain Sampler/README.txt"
shot "The Files tab lists LEES MIJ.txt."

step "Rename a folder" "The same tab, the torrent's top folder, r, \"Sampler (renamed)\", enter: the folder on disk follows and the torrent keeps seeding."
keys k r
shot "The folder in the inline editor, its whole name selected and readable from its first letter." editor
keys -t "Sampler (renamed)" Return
expect_file "qBittorrent lists Sampler (renamed)/sampler.mkv" "$PDS" "Sampler (renamed)/sampler.mkv"
expect_disk "the folder is renamed on disk" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"
expect_disk "the old folder is gone" ! "$TORRENTS_DIR/Public Domain Sampler"
expect "it still seeds, complete" "$PDS" "t['state'] in $SEEDING and t['progress'] == 1"
shot "The Files tab's top folder reads \"Sampler (renamed)/\"."

step "Rename many files" "Field Recordings' Files tab, ctrl+a, r: the rename sheet on all 16 files. ctrl+e (regex), find \"(\\d+) - \", replace \"\$1. \", enter: the 14 tracks become \"NN. Title.flac\", album.cue and cover.jpg stay. Then the same sheet puts the names back."
ALBUM="Field Recordings Vol. 3 [FLAC]"
table_on "Field Recordings"
keys l 2
keys ctrl+a r
keys -t '(\d+) - '
keys ctrl+e Tab
# shellcheck disable=SC2016 # a regex group for the app, typed as is
keys -t '$1. '
shot "The rename sheet: regex on, 14 of 16 change, \"01. Harbour at Dawn.flac\" with \"01. \" in accent, the button reads \"rename 14\"." sheet
keys Return
expect_file "qBittorrent lists $ALBUM/01. Harbour at Dawn.flac" "$FIELD" "$ALBUM/01. Harbour at Dawn.flac"
expect_file "…and 14. Last Ferry.flac" "$FIELD" "$ALBUM/14. Last Ferry.flac"
expect_file "album.cue keeps its name" "$FIELD" "$ALBUM/album.cue"
expect_disk "01. Harbour at Dawn.flac is on disk" -f "$TORRENTS_DIR/$ALBUM/01. Harbour at Dawn.flac"
expect_disk "01 - Harbour at Dawn.flac is gone from disk" ! "$TORRENTS_DIR/$ALBUM/01 - Harbour at Dawn.flac"
shot "The Files tab lists 01. Harbour at Dawn.flac to 14. Last Ferry.flac; the footer said \"renamed 14 files\"."
# The sheet keeps its fields and regex: select all again and swap the pattern back.
keys ctrl+a r
keys -t '(\d+)\. '
keys Tab ctrl+a
# shellcheck disable=SC2016 # a regex group for the app, typed as is
keys -t '$1 - ' Return
expect_file "the names are back: 01 - Harbour at Dawn.flac" "$FIELD" "$ALBUM/01 - Harbour at Dawn.flac"
expect_disk "…on disk too" -f "$TORRENTS_DIR/$ALBUM/14 - Last Ferry.flac"

step "Create and set a category" "Field Recordings, the menu's category (C), \"walk\", enter on create \"walk\". Automatic management is off, so nothing moves."
menu_on "Field Recordings"
keys C
keys -t walk
shot "The category palette: create \"walk\"." palette
keys Return
api torrents/categories | python3 -c 'import json, sys; sys.exit(0 if "walk" in json.load(sys.stdin) else 1)' ||
    fail "the server has no category walk"
pass "torrents/categories lists walk"
expect "Field Recordings is in walk" "$FIELD" 't["category"] == "walk"'
expect "…and still in ~/Torrents (automatic management off)" "$FIELD" "t['save_path'] == '$TORRENTS_DIR' and not t['auto_tmm']"
expect_disk "its folder has not moved" -d "$TORRENTS_DIR/Field Recordings Vol. 3 [FLAC]"
shot "Field Recordings carries \"walk\"; the rail lists walk with 1."

step "Automatic management on" "Field Recordings, the menu's automatic management (a). qBittorrent moves the files to the category's folder, ~/Torrents/walk."
menu_on "Field Recordings"
keys a
expect "automatic management is on" "$FIELD" 't["auto_tmm"]'
expect "the save path is ~/Torrents/walk" "$FIELD" "t['save_path'] == '$TORRENTS_DIR/walk'" 30
expect_disk "the folder is in ~/Torrents/walk" -f "$TORRENTS_DIR/walk/Field Recordings Vol. 3 [FLAC]/album.cue" 30
expect_disk "…and gone from ~/Torrents" ! "$TORRENTS_DIR/Field Recordings Vol. 3 [FLAC]"
expect "it still seeds, complete" "$FIELD" "t['state'] in $SEEDING and t['progress'] == 1" 30
shot "Field Recordings' details: save path ~/Torrents/walk, auto manage on."

step "Category none, still managed" "Field Recordings, C (the cursor opens on walk), up to none, enter. Managed torrents follow their category, so the files go back to ~/Torrents."
menu_on "Field Recordings"
keys C
keys Up
shot "The category palette, the cursor on none." palette
keys Return
expect "Field Recordings has no category" "$FIELD" 't["category"] == ""'
expect "the save path is ~/Torrents again" "$FIELD" "t['save_path'] == '$TORRENTS_DIR' and t['auto_tmm']" 30
expect_disk "the folder is back in ~/Torrents" -f "$TORRENTS_DIR/Field Recordings Vol. 3 [FLAC]/album.cue" 30
expect_disk "…and gone from ~/Torrents/walk" ! "$TORRENTS_DIR/walk/Field Recordings Vol. 3 [FLAC]"
shot "Field Recordings without a category."

step "Add a tag" "Sampler (renamed) is Public Domain Sampler: t, \"walk-tag\", enter on create \"walk-tag\"."
menu_on "Public Domain"
keys t
keys -t walk-tag
shot "The tag palette: create \"walk-tag\"." palette
keys Return
expect "Public Domain Sampler is tagged walk-tag" "$PDS" 't["tags"] == "walk-tag"'
expect "no other torrent is" "$PDS" 'all(x["tags"] == "" for x in every if x["hash"] != t["hash"])'
shot "Public Domain Sampler carries walk-tag; the rail lists it."

step "Remove the tag" "The same torrent: t, space on walk-tag (checked), enter."
menu_on "Public Domain"
keys t
keys space
shot "The tag palette, walk-tag unchecked." palette
keys Return
expect "Public Domain Sampler has no tags" "$PDS" 't["tags"] == ""'
api torrents/tags | python3 -c 'import json, sys; sys.exit(0 if "walk-tag" in json.load(sys.stdin) else 1)' ||
    fail "the tag walk-tag itself was deleted (the app never deletes tags)"
pass "the tag itself stays on the server"

step "Set location, same disk" "Nested Library Sampler, the menu's move… (m), ~/Moved, enter. 113 files in 26 folders move; it lands within 3 s, so no band, and the strip over the rows says so, its row's status \"✓ moved · Moved\" (C18 D1)."
menu_on "Nested Library"
keys m
expect_text "the Move… sheet is open" "new path on" 10
keys ctrl+a -t "$BOX_HOME/Moved"
expect_text "one green line" "access are fine" 20
shot "The Move… sheet: ~/Moved, one green line (L3)." sheet
keys Return
expect "the save path is ~/Moved" "$NESTED" "t['save_path'] == '$BOX_HOME/Moved'" 30
expect_text "the strip says it moved" "Nested Library Sampler (2026) moved to $BOX_HOME/Moved" 20
shot "The green strip at the rows' bottom middle: ✓ Nested Library Sampler (2026) moved to ~/Moved and ×, no show: it is the cursor's row, in view (C30 F8); the row reads ✓ moved · Moved." strip
expect_disk "the files are in ~/Moved" -f "$BOX_HOME/Moved/Nested Library Sampler (2026)/Docs/README.md" 30
expect_disk "…and gone from ~/Torrents" ! "$TORRENTS_DIR/Nested Library Sampler (2026)"
expect "it still seeds, complete" "$NESTED" "t['state'] in $SEEDING and t['progress'] == 1" 30
shot "Nested Library Sampler's details: save path ~/Moved."

step "Set location, another filesystem" "$BIG (2 GiB), m, /tmp/elsewhere (a tmpfs in the box): qBittorrent copies the bytes, then deletes the old file."
menu_on "$BIG"
keys m
keys ctrl+a -t /tmp/elsewhere
expect_text "the sheet reads the room on /tmp" "Room and access are fine on /tmp" 20
keys Return
shot "Straight after enter. A tmpfs takes 2 GiB in about a second, so the row seldom still reads moving, and the band (after 3 s) seldom shows."
expect_text "the strip says it moved, or the band does" "$BIG moved to /tmp/elsewhere" 120
expect "the save path is /tmp/elsewhere" "$BIGH" "t['save_path'] == '/tmp/elsewhere' and t['state'] != 'moving'" 120
expect_size "the 2 GiB file is in /tmp/elsewhere, whole" "/tmp/elsewhere/$BIG" "$BIG_SIZE"
expect_disk "…and gone from ~/Torrents" ! "$TORRENTS_DIR/$BIG"
expect "it still seeds, complete" "$BIGH" "t['state'] in $SEEDING and t['progress'] == 1" 30
shot "$BIG seeding from /tmp/elsewhere." after

step "Set location onto a folder with its name" "The box's ~/clash has a folder called Field Recordings Vol. 3 [FLAC] already (a file in it). Field Recordings, m, ~/clash: qBittorrent would keep that folder's files and call the move done (board 43 §1), so the sheet leaves it out and has nothing to move. esc; nothing moved, the folder there is untouched."
mkdir -p "$home/clash/Field Recordings Vol. 3 [FLAC]"
echo planted >"$home/clash/Field Recordings Vol. 3 [FLAC]/planted.txt"
menu_on "Field Recordings"
keys m
keys ctrl+a -t "$BOX_HOME/clash"
expect_text "the sheet leaves it out" "1 left out" 20
expect_text "…and has nothing to move" "Nothing to move" 10
shot "The Move… sheet: 1 left out, a folder with its name is already at ~/clash; nothing to move." sheet
keys Return
keys Escape
expect "Field Recordings has not moved" "$FIELD" "t['save_path'] == '$TORRENTS_DIR' and t['state'] != 'moving'" 5
expect_disk "the folder at ~/clash is untouched" -f "$BOX_HOME/clash/Field Recordings Vol. 3 [FLAC]/planted.txt"
expect_disk "…and holds nothing of the torrent's" ! "$BOX_HOME/clash/Field Recordings Vol. 3 [FLAC]/album.cue"

step "Recheck" "$BIG, the menu's recheck (c): qBittorrent hashes the moved 2 GiB again and finds it whole."
menu_on "$BIG"
seen="$(mktemp)"
watch_checking "$BIGH" "$seen"
# The key goes only once the watcher polls: starting it in the box can take longer than the
# whole 2 GiB check under load.
for _ in $(seq 1 100); do grep -q polling "$seen" && break; sleep 0.1; done
keys c
shot "Straight after c. Hashing 2 GiB in memory takes about two seconds, so the row may already be back to seeding or queued; the checks below saw qBittorrent check it."
wait
if [ "$(tail -n 1 "$seen")" = seen ]; then pass "qBittorrent checks it"; else fail "qBittorrent never read checking after c"; fi
rm -f "$seen"
expect "…and finds it whole" "$BIGH" "t['state'] in $SEEDING and t['progress'] == 1" 120
shot "$BIG seeding again after the check." after

step "Select two, stop them from the bar" "space on Field Recordings and Nested Library Sampler; the action bar floats over the list, the status line under it still the server's; a jump + tab onto a third shows the menu says \"2 selected\" (esc); then the action bar's stop button."
table_on "Field Recordings"
keys space
table_on "Nested Library"
keys space
expect_text "the status line stays the server's (D17.3)" "synced" 10
shot "Two rows selected; the action bar floats over the list, bottom middle, \"2 selected\" over the ledger, stop · recheck | move… · category › · tags › | limits… · more › | delete… | esc; the status line under it unchanged (C32 D17)." bar
# Q24: a jump + tab onto a third torrent opens the menu there, but its rows act on the two.
menu_on "tiny notes"
shot "The actions menu on tiny notes (renamed).txt, headed by a dim \"2 selected\" line above stop." menu
keys Escape
bar_click stop
expect "Field Recordings is stopped" "$FIELD" 't["state"] == "stoppedUP"'
expect "Nested Library Sampler is stopped" "$NESTED" 't["state"] == "stoppedUP"'
expect "no other torrent is" "$FIELD" 'all(not x["state"].startswith("stopped") for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'
expect_text "the strip counts them: 2 stopped" "2 stopped" 10
shot "Both rows read stopped; the others still seed. The strip, \"✓ 2 stopped\", floats just above the action bar, with no show: both are in view and selected (C32 D17, C30 F8)."

step "Category from the bar" "The bar's category ›: its list opens upward over the bar; a click on walk. Field Recordings is managed, so it moves to ~/Torrents/walk; Nested Library is not (set location turned management off), so it stays in ~/Moved."
bar_click category
expect_text "the bar's category list" "category for 2" 10
shot "The category list for 2 over the bar, category › lit: walk, swarm…, new category…, no category (C32 D17.1)." palette
bar_click walk
expect "both are in walk" "$FIELD" 'all(x["category"] == "walk" for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))'
expect "no other torrent is" "$FIELD" 'all(x["category"] == "" for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'
expect_disk "Field Recordings moved to ~/Torrents/walk" -f "$TORRENTS_DIR/walk/Field Recordings Vol. 3 [FLAC]/album.cue" 30
expect "Nested Library Sampler stayed in ~/Moved" "$NESTED" "t['save_path'] == '$BOX_HOME/Moved' and not t['auto_tmm']"
expect_disk "…on disk too" -f "$BOX_HOME/Moved/Nested Library Sampler (2026)/Docs/README.md"

step "Tag from the bar" "The bar's tags ›, new tag… in its list: the tag palette, \"bulk\", enter on create \"bulk\"."
bar_click tags
expect_text "the bar's tags list" "tags for 2" 10
click_text "new tag"
keys -t bulk
keys Return
expect "both are tagged bulk" "$FIELD" 'all(x["tags"] == "bulk" for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))'
expect "no other torrent is" "$FIELD" 'all(x["tags"] == "" for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'

step "Start them from the bar" "The bar's start button."
bar_click start
expect "both seed again" "$FIELD" 'all(x["state"] in '"$SEEDING"' for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))' 30
shot "Both rows seeding, still selected, walk and bulk on them."
keys Escape # closes the strip
keys Escape # clears the selection

step "Pick rows with the mouse" "A plain click on Field Recordings' row: one selected, and the bar's quiet hint \"ctrl+click add · shift+click range\" (C18 D6, the first of 3 times). The □ at Nested Library Sampler's row start adds it, the first staying; a second click on its □ takes it out. Then the ? sheet has a \"select with the mouse\" block."
keys Escape
# A plain click on the row, past its □:
where="$(omabox tree -b "$BOX" --window omaseed --find "Field Recordings Vol. 3 [FLAC]" 2>/dev/null | python3 -c '
import re, sys
for line in sys.stdin:
    m = re.search(r"list item \"[^\"]*\"[^@]* @(-?\d+),(-?\d+) (\d+)x(\d+)", line)
    if m:
        x, y, w, h = map(int, m.groups())
        print(x + 200, y + h // 2)
        break')"
# shellcheck disable=SC2086 # two numbers
omabox click -b "$BOX" --window omaseed --wait --ignore "$STRIP_IGNORE" $where >/dev/null 2>&1 || true
expect_text "one selected" "1 selected" 10
expect_text "the bar hints the mouse" "ctrl+click add · shift+click range" 10
row_tick "Nested Library Sampler (2026)"
expect_text "the □ added the second, the first stays" "2 selected" 10
shot "Two rows selected, each with ■ at its start; the bar reads 2 selected and its dim hint sits before esc." mouse
row_tick "Nested Library Sampler (2026)"
expect_text "a second click on its □ took it out" "1 selected" 10
keys Escape
keys -t "?"
shot "The ? sheet: SELECT WITH THE MOUSE after the select keys (click □, ctrl+click, shift+click, middle click, right click, below the fold)." help
keys -t "middle"
expect_text "the ? sheet finds the mouse's middle click" "Add or remove the row, as ctrl+click" 10
keys Escape Escape

step "A refusal says so in the strip" "Queueing switched off on the server; Public Domain Sampler, [ (up the queue): qBittorrent refuses (409). The strip is red and stays: \"Sampler (renamed) didn't move in the queue\", why, details, ×; esc closes it (C18 D1, D3). Queueing back on."
api app/setPreferences --data-urlencode 'json={"queueing_enabled": false}' >/dev/null
table_on "Public Domain"
keys -t "["
expect_text "the strip says it was refused" "didn't move in the queue" 15
sleep 7
find_text_aw() { omabox tree -b "$BOX" --window omaseed --find "$1" >/dev/null 2>&1; }
find_text_aw "didn't move in the queue" || fail "the refusal went by itself (D3: it stays until closed)"
pass "it is still there after 7 s"
shot "The red strip: ✕ … didn't move in the queue, refused: Torrent queueing must be enabled, details, ×; the row reads ✕ didn't move in the queue." refused
keys Escape
if find_text_aw "didn't move in the queue"; then fail "esc left the strip"; else pass "esc closed it"; fi
api app/setPreferences --data-urlencode 'json={"queueing_enabled": true}' >/dev/null

# docs/30 §7: the command palette runs a command as its key would, menu-only ones included.
step "Force start from the command palette" "Field Recordings, p (stopped); then : \"force\", enter: force start, on the server. Then : \"seq\", enter: sequential download, the actions menu's s, which has no window key."
table_on "Field Recordings"
keys p
expect "Field Recordings is stopped" "$FIELD" 't["state"] == "stoppedUP"'
keys -t ":force"
shot "The command palette, \"force\" typed, force start under the cursor with its key P." palette
keys Return
expect "Field Recordings is force-started" "$FIELD" 't["force_start"] and t["state"] == "forcedUP"' 30
expect "no other torrent is" "$FIELD" 'not any(x["force_start"] for x in every if x["hash"] != "'"$FIELD"'")'
keys -t ":seq"
shot "\"seq\": sequential download on, via enter s, now off on Field Recordings." seq
keys Return
expect "sequential download is on" "$FIELD" 't["seq_dl"]'
keys -t ":seq"
keys Return
expect "…and off again" "$FIELD" 'not t["seq_dl"]'
keys p
expect "p stops it" "$FIELD" 't["state"] == "stoppedUP"'
# p goes by the row's state as the app last synced it, and the row's "✓ stopped" mark (C18 D1)
# shows before that sync: let one poll (1.5 s) bring the stop in first.
sleep 2
keys p
expect "it seeds again, not forced" "$FIELD" "t['state'] in $SEEDING and not t['force_start']" 30

step "Delete, keep the files" "tiny notes (renamed).txt, d in the table: the toggle is off (nothing remembered yet), the cursor on cancel; l, enter."
table_on "tiny notes"
keys d
shot "Delete this torrent? tiny notes (renamed).txt, 4 KB; \"also delete files\" off." dialog
keys l Return
expect "the torrent is gone" "$TINY" 't is None'
expect_disk "its file stays on disk" -f "$TORRENTS_DIR/tiny-notes.txt"

step "Delete with files" "The unicode torrent, d, tab tab to the toggle (the dialog opens on cancel, tab runs toggle → cancel → delete and wraps), space (on), l, enter. Its folder goes; nothing else does."
table_on "Spaces"
keys d
keys Tab Tab space
shot "The toggle on; the buttons' cursor on delete (tab passed it on the way)." dialog
keys l Return
expect "the torrent is gone" "$UNI" 't is None'
expect_disk "its folder is gone from disk" ! "$UNI_PATH" 30
expect_disk "tiny-notes.txt (another torrent's, deleted without files) is still there" -f "$TORRENTS_DIR/tiny-notes.txt"
expect_disk "Sampler (renamed) is still there" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"
expect_disk "the 2 GiB file is still there" -f "/tmp/elsewhere/$BIG"
if grep -q '"deleteFiles"' "$home/.config/omaseed/state.json" 2>/dev/null &&
    python3 -c 'import json, sys; sys.exit(0 if json.load(open(sys.argv[1]))["deleteFiles"]["value"] is True else 1)' "$home/.config/omaseed/state.json"; then
    pass "state.json remembers \"also delete files\" on"
else
    fail "state.json does not remember \"also delete files\" on"
fi

step "The toggle remembers" "Public Domain Sampler, d: the toggle opens on (remembered). esc: nothing is deleted."
table_on "Public Domain"
keys d
shot "Delete this torrent? Sampler, the toggle on." dialog
keys Escape
sleep 1
expect "Public Domain Sampler is still there" "$PDS" 't is not None'
expect_disk "…and its files" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"

step "Delete two with files, from the bar" "space on Field Recordings and Nested Library Sampler, the bar's delete button: Delete 2 torrents?, the toggle on as remembered; l, enter."
table_on "Field Recordings"
keys space
table_on "Nested Library"
keys space
bar_click "delete…"
shot "Delete 2 torrents? both names, 226 MB; the toggle on." dialog
keys l Return
expect "Field Recordings is gone" "$FIELD" 't is None'
expect "Nested Library Sampler is gone" "$NESTED" 't is None'
expect_disk "Field Recordings' folder is gone" ! "$TORRENTS_DIR/walk/Field Recordings Vol. 3 [FLAC]" 30
expect_disk "Nested Library Sampler's folder is gone" ! "$BOX_HOME/Moved/Nested Library Sampler (2026)" 30
expect "two torrents are left: Public Domain Sampler and $BIG" "$PDS" 'sorted(x["hash"] for x in every) == sorted(["'"$PDS"'", "'"$BIGH"'"])'
expect_disk "Public Domain Sampler's files are still there" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"
expect_disk "…and the 2 GiB file" -f "/tmp/elsewhere/$BIG"
shot "Two torrents left: Public Domain Sampler and $BIG."

# ---- #28: two torrents, one file -----------------------------------------------------------

step "Add a twin, saved where the file is" "The 2 GiB file's twin: the same file under another infohash (another source, as a second tracker issues it). The add sheet finds the file already on disk (#27) and alt+s saves it there, in /tmp/elsewhere: qBittorrent checks it and seeds it at once, nothing downloaded. It is renamed \"walk-2GiB twin\" so the jump palette tells the two apart."
keys a
keys -t "$BOX_HOME/Downloads/$BIG.twin.torrent"
sleep 3 # the parse, then the library's file lists
shot "Add torrent, the twin in the field: a blue line, \"This file is already on disk. The same file, same size, as $BIG, seeding from 127.0.0.1\", with save there (alt+s in the ? sheet, C24 H10)." match
keys alt+s
shot "After alt+s: \"Saving beside $BIG\" with undo; Save to /tmp/elsewhere, \"from the files match\"; the line under it green, \"needs nothing new\"." saved
keys Return
expect "the twin is complete without downloading" "$TWIN" "t is not None and t['progress'] == 1 and t['downloaded'] == 0" 120
api torrents/rename -d "hash=$TWIN" --data-urlencode "name=walk-2GiB twin" >/dev/null
expect "it is called walk-2GiB twin" "$TWIN" 't["name"] == "walk-2GiB twin"'
expect "both point at the same file" "$TWIN" "t['content_path'] == '/tmp/elsewhere/$BIG' and next(x for x in every if x['hash'] == '$BIGH')['content_path'] == t['content_path']"

step "Delete one of the pair, keep the file" "$BIG, D (files on): the confirm names the twin and picks \"keep the files\"; the button reads \"delete, keep files\". l, enter."
table_on "$BIG"
keys shift+d
shot "Delete this torrent? The yellow line: its files are also seeded by walk-2GiB twin (127.0.0.1, seeding); keep the files picked; delete, keep files." dialog
keys l Return
expect "$BIG is gone" "$BIGH" 't is None'
expect_size "the file is still there, whole" "/tmp/elsewhere/$BIG" "$BIG_SIZE"
expect "the twin still seeds it, complete" "$TWIN" "t['state'] in $SEEDING and t['progress'] == 1" 30

step "Move both" "$BIG is added again beside the twin. The twin, m, /tmp/pair: the sheet names $BIG and picks \"move both\"; enter moves the two together."
api torrents/add -F "torrents=@$BOX_HOME/Downloads/$BIG.torrent" -F "savepath=/tmp/elsewhere" >/dev/null
expect "$BIG is back, complete" "$BIGH" "t is not None and t['progress'] == 1" 120
table_on "twin"
keys m
keys ctrl+a -t /tmp/pair
shot "Move…: /tmp/pair; the yellow line names $BIG; move both picked; the button reads move 2." sheet
keys Return
expect "the twin is in /tmp/pair" "$TWIN" "t['save_path'] == '/tmp/pair' and t['state'] != 'moving'" 120
expect "$BIG is in /tmp/pair too" "$BIGH" "t['save_path'] == '/tmp/pair' and t['state'] != 'moving'" 120
expect_size "the file is in /tmp/pair, whole" "/tmp/pair/$BIG" "$BIG_SIZE"
expect_disk "…and gone from /tmp/elsewhere" ! "/tmp/elsewhere/$BIG"
expect "both still seed it, complete" "$TWIN" "all(x['state'] in $SEEDING and x['progress'] == 1 for x in every if x['hash'] in ('$TWIN', '$BIGH'))" 30

step "Move both by category" "Two categories with folders of their own: pair (/tmp/pair) and shelf (/tmp/shelf). The twin goes into pair with automatic management on, where it already is, so nothing moves; $BIG stays manual. The twin, C, \"shelf\", enter: \"Move to shelf?\" names $BIG and picks \"move both\"; enter gives $BIG the category and automatic management too, and the two move together."
api torrents/createCategory -d category=pair --data-urlencode savePath=/tmp/pair >/dev/null
api torrents/createCategory -d category=shelf --data-urlencode savePath=/tmp/shelf >/dev/null
api torrents/setCategory -d "hashes=$TWIN" -d category=pair >/dev/null
api torrents/setAutoManagement -d "hashes=$TWIN" -d enable=true >/dev/null
expect "the twin is in pair, managed, still in /tmp/pair" "$TWIN" "t['category'] == 'pair' and t['auto_tmm'] and t['save_path'] == '/tmp/pair'"
expect "$BIG has no category and is not managed" "$BIGH" "t['category'] == '' and not t['auto_tmm']"
sleep 2 # the app's next poll brings in what the API just changed
menu_on "twin"
keys C
keys -t shelf
shot "The category palette for 1 torrent, shelf under the cursor, /tmp/shelf beside it." palette
keys Return
shot "Move to shelf? walk-2GiB twin · automatic management moves its files; the yellow line names $BIG; move both picked; the button reads move 2." dialog
keys Return
expect "both are in shelf" "$TWIN" "all(x['category'] == 'shelf' for x in every if x['hash'] in ('$TWIN', '$BIGH'))"
expect "$BIG is managed now" "$BIGH" "t['auto_tmm']"
expect "the twin is in /tmp/shelf" "$TWIN" "t['save_path'] == '/tmp/shelf' and t['state'] != 'moving'" 120
expect "$BIG is in /tmp/shelf too" "$BIGH" "t['save_path'] == '/tmp/shelf' and t['state'] != 'moving'" 120
expect_size "the file is in /tmp/shelf, whole" "/tmp/shelf/$BIG" "$BIG_SIZE"
expect_disk "…and gone from /tmp/pair" ! "/tmp/pair/$BIG"
expect "both still seed it, complete" "$TWIN" "all(x['state'] in $SEEDING and x['progress'] == 1 for x in every if x['hash'] in ('$TWIN', '$BIGH'))" 30
shot "Both rows in shelf, seeding."

step "Move both by automatic management" "Both taken off automatic management, still in /tmp/shelf; the twin put in pair (its folder /tmp/pair), which moves nothing while it is manual. The twin, the menu's automatic management (a): \"Turn on automatic management?\" names $BIG and picks \"move both\"; enter gives $BIG the twin's category, pair, and management too, and the two move to /tmp/pair together (#65)."
api torrents/setAutoManagement -d "hashes=$TWIN|$BIGH" -d enable=false >/dev/null
api torrents/setCategory -d "hashes=$TWIN" -d category=pair >/dev/null
expect "the twin is in pair, manual, still in /tmp/shelf" "$TWIN" "t['category'] == 'pair' and not t['auto_tmm'] and t['save_path'] == '/tmp/shelf'"
expect "$BIG is in shelf, manual, in /tmp/shelf" "$BIGH" "t['category'] == 'shelf' and not t['auto_tmm'] and t['save_path'] == '/tmp/shelf'"
# What the API just changed reaches the app on its next poll (1.5 s): before it, the app still
# sees the twin in shelf, where management would move nothing, and asks nothing.
sleep 2
menu_on "twin"
keys a
sleep 1
shot "Turn on automatic management? walk-2GiB twin · automatic management moves its files; the yellow line names $BIG; move both picked (the other torrent gets the same category and moves with it); the button reads move 2." dialog
expect "nothing changed before the answer" "$TWIN" "not t['auto_tmm'] and t['save_path'] == '/tmp/shelf'"
keys Return
expect "both are in pair" "$TWIN" "all(x['category'] == 'pair' for x in every if x['hash'] in ('$TWIN', '$BIGH'))"
expect "both are managed" "$TWIN" "all(x['auto_tmm'] for x in every if x['hash'] in ('$TWIN', '$BIGH'))"
expect "the twin is in /tmp/pair" "$TWIN" "t['save_path'] == '/tmp/pair' and t['state'] != 'moving'" 120
expect "$BIG is in /tmp/pair too" "$BIGH" "t['save_path'] == '/tmp/pair' and t['state'] != 'moving'" 120
expect_size "the file is in /tmp/pair, whole" "/tmp/pair/$BIG" "$BIG_SIZE"
expect_disk "…and gone from /tmp/shelf" ! "/tmp/shelf/$BIG"
expect "both still seed it, complete" "$TWIN" "all(x['state'] in $SEEDING and x['progress'] == 1 for x in every if x['hash'] in ('$TWIN', '$BIGH'))" 30
shot "Both rows in pair, seeding."

step "A selection: one shares, one does not" "space on the twin and Public Domain Sampler, D: \"1 of them shares files with 1 torrent you're not deleting\"; esc."
table_on "twin"
keys space
table_on "Public Domain"
keys space
keys shift+d
shot "Delete 2 torrents? The yellow line lists the twin, with 1; keep their files picked; the button reads delete 2." dialog
keys Escape
expect "nothing was deleted" "$TWIN" "all(any(x['hash'] == h for x in every) for h in ('$TWIN', '$BIGH', '$PDS'))"
keys Escape # clears the selection

step "Both selected" "space on the twin and $BIG, D: no question, a dim line says they share one copy and the size counts 2 GiB once; esc."
table_on "twin"
keys space
table_on "$BIG"
keys space
keys shift+d
shot "Delete 2 torrents? 2.15 GB on disk (once); the dim line: these 2 share one copy of their files." dialog
keys Escape
keys Escape # clears the selection

step "Delete both with the file" "The twin, D, j (delete the files and that torrent too), l, enter: both go, and the file with them."
table_on "twin"
keys shift+d
keys j
shot "The second row picked; the button reads delete 2 with files." dialog
keys l Return
expect "the twin is gone" "$TWIN" 't is None'
expect "$BIG is gone" "$BIGH" 't is None'
expect_disk "the file is gone from /tmp/pair" ! "/tmp/pair/$BIG" 30
expect_disk "Public Domain Sampler's files are still there" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"

disk_walk
finish
