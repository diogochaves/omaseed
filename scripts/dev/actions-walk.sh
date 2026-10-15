#!/usr/bin/env bash
# omaseed — the risky actions, walked through the app's own UI: renames, categories, tags,
# automatic torrent management, set location (also onto another filesystem, a 2 GiB torrent),
# recheck, the selection bar on two torrents, the command palette (docs/30), delete with and
# without files, and two torrents that share one file (#27: the twin goes in through the add
# sheet's "save there"; #28: the delete confirm, the location sheet and a category change
# ask). After each step it checks what qBittorrent says and what is on disk, and stops at the
# first mismatch.
# Run it again after any change to an action, a sheet, a palette or the selection bar.
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
# esc from the menu leaves the keyboard on the table there. The selection bar is mouse only,
# so it is clicked: its buttons' places are measured on a 1854x1056 window (see BAR_*), moved
# to the window's real size before the first click (bar_fit), and the checks after each click
# catch a button that moved.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
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

# The selection bar's buttons, in omaseed's window coordinates, with two torrents selected,
# the rail and the details pane open (docs/17 M9.3's order), for a BAR_W x BAR_H window (the
# box's 1920x1080 screen with the bar Omarchy puts on the left). The bar is centred over the
# table and sits a fixed height above the window's bottom: bar_fit moves the buttons by half
# the width's change and all of the height's, so another bar layout (a top bar) still lands.
# Measured again after #24 T7 put the selection's ledger (size, given, ratio) at the bar's start.
BAR_W=1854
BAR_H=1056
BAR_Y=975
BAR_START=775
BAR_STOP=808
BAR_CATEGORY=940
BAR_TAGS=974
BAR_DELETE=1175

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

keys() { omabox keys -b "$BOX" --window omaseed --wait "$@" >/dev/null 2>&1 || true; }
# bar_fit: move BAR_* from the BAR_W x BAR_H window they were measured on to omaseed's own.
bar_fit() {
    local w h dx dy
    read -r w h < <(omabox windows -b "$BOX" --json | python3 -c '
import json, sys
print(*next(x["size"] for x in json.load(sys.stdin) if "omaseed" in x["class"]))')
    dx=$(((w - BAR_W) / 2))
    dy=$((h - BAR_H))
    BAR_Y=$((BAR_Y + dy))
    BAR_START=$((BAR_START + dx))
    BAR_STOP=$((BAR_STOP + dx))
    BAR_CATEGORY=$((BAR_CATEGORY + dx))
    BAR_TAGS=$((BAR_TAGS + dx))
    BAR_DELETE=$((BAR_DELETE + dx))
    BAR_W=$w
    BAR_H=$h
}
click() { omabox click -b "$BOX" --window omaseed --wait "$1" "$2" >/dev/null 2>&1 || true; }

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

step "Set location, same disk" "Nested Library Sampler, the menu's set location (m), ~/Moved, enter. 113 files in 26 folders move."
menu_on "Nested Library"
keys m
keys ctrl+a -t "$BOX_HOME/Moved"
shot "The set location sheet: ~/Moved, \"123 MB moves with it\"." sheet
keys Return
expect "the save path is ~/Moved" "$NESTED" "t['save_path'] == '$BOX_HOME/Moved'" 30
expect_disk "the files are in ~/Moved" -f "$BOX_HOME/Moved/Nested Library Sampler (2026)/Docs/README.md" 30
expect_disk "…and gone from ~/Torrents" ! "$TORRENTS_DIR/Nested Library Sampler (2026)"
expect "it still seeds, complete" "$NESTED" "t['state'] in $SEEDING and t['progress'] == 1" 30
shot "Nested Library Sampler's details: save path ~/Moved."

step "Set location, another filesystem" "$BIG (2 GiB), m, /tmp/elsewhere (a tmpfs in the box): qBittorrent copies the bytes, then deletes the old file."
menu_on "$BIG"
keys m
keys ctrl+a -t /tmp/elsewhere Return
shot "Straight after enter. A tmpfs takes 2 GiB in about a second, so the row seldom still reads moving."
expect "the save path is /tmp/elsewhere" "$BIGH" "t['save_path'] == '/tmp/elsewhere' and t['state'] != 'moving'" 120
expect_size "the 2 GiB file is in /tmp/elsewhere, whole" "/tmp/elsewhere/$BIG" "$BIG_SIZE"
expect_disk "…and gone from ~/Torrents" ! "$TORRENTS_DIR/$BIG"
expect "it still seeds, complete" "$BIGH" "t['state'] in $SEEDING and t['progress'] == 1" 30
shot "$BIG seeding from /tmp/elsewhere." after

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

step "Select two, stop them from the bar" "space on Field Recordings and Nested Library Sampler; a jump + tab onto a third shows the menu says \"2 selected\" (esc); then the selection bar's stop button."
table_on "Field Recordings"
keys space
table_on "Nested Library"
keys space
shot "Two rows selected; the selection bar reads \"2 selected\"." bar
# Q24: a jump + tab onto a third torrent opens the menu there, but its rows act on the two.
menu_on "tiny notes"
shot "The actions menu on tiny notes (renamed).txt, headed by a dim \"2 selected\" line above stop." menu
keys Escape
bar_fit
click "$BAR_STOP" "$BAR_Y"
expect "Field Recordings is stopped" "$FIELD" 't["state"] == "stoppedUP"'
expect "Nested Library Sampler is stopped" "$NESTED" 't["state"] == "stoppedUP"'
expect "no other torrent is" "$FIELD" 'all(not x["state"].startswith("stopped") for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'
shot "Both rows read stopped; the others still seed."

step "Category from the bar" "The bar's category button, \"walk\", enter. Field Recordings is managed, so it moves to ~/Torrents/walk; Nested Library is not (set location turned management off), so it stays in ~/Moved."
click "$BAR_CATEGORY" "$BAR_Y"
keys -t walk
shot "The category palette for 2 torrents, walk under the cursor." palette
keys Return
expect "both are in walk" "$FIELD" 'all(x["category"] == "walk" for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))'
expect "no other torrent is" "$FIELD" 'all(x["category"] == "" for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'
expect_disk "Field Recordings moved to ~/Torrents/walk" -f "$TORRENTS_DIR/walk/Field Recordings Vol. 3 [FLAC]/album.cue" 30
expect "Nested Library Sampler stayed in ~/Moved" "$NESTED" "t['save_path'] == '$BOX_HOME/Moved' and not t['auto_tmm']"
expect_disk "…on disk too" -f "$BOX_HOME/Moved/Nested Library Sampler (2026)/Docs/README.md"

step "Tag from the bar" "The bar's tags button, \"bulk\", enter on create \"bulk\"."
click "$BAR_TAGS" "$BAR_Y"
keys -t bulk
keys Return
expect "both are tagged bulk" "$FIELD" 'all(x["tags"] == "bulk" for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))'
expect "no other torrent is" "$FIELD" 'all(x["tags"] == "" for x in every if x["hash"] not in ("'"$FIELD"'", "'"$NESTED"'"))'

step "Start them from the bar" "The bar's start button."
click "$BAR_START" "$BAR_Y"
expect "both seed again" "$FIELD" 'all(x["state"] in '"$SEEDING"' for x in every if x["hash"] in ("'"$FIELD"'", "'"$NESTED"'"))' 30
shot "Both rows seeding, still selected, walk and bulk on them."
keys Escape # clears the selection

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
click "$BAR_DELETE" "$BAR_Y"
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
shot "Add torrent, the twin in the field: a blue line, \"This file is already on disk. The same file, same size, as $BIG, seeding from 127.0.0.1\", with save there; alt+s save there in the hints." match
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
shot "Set location: /tmp/pair; the yellow line names $BIG; move both picked; the button reads move 2." sheet
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
expect_disk "the file is gone from /tmp/shelf" ! "/tmp/shelf/$BIG" 30
expect_disk "Public Domain Sampler's files are still there" -f "$TORRENTS_DIR/Sampler (renamed)/sampler.mkv"

echo
echo "all $CHECKS checks passed"
if [ "$INTERACTIVE" = 0 ]; then
    printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
    echo "shots and checks: $OUT/index.md"
fi
