#!/usr/bin/env bash
# omaseed — moving a folder (board 42, DECISIONS C15), walked through the app's own UI on a
# library of a few hundred small torrents: a folder with a nested save path and loose files
# moved to another filesystem, omaseed killed mid-move and started again, the loose files
# carried after the torrents; a small folder moved and moved back, the loose files it carried
# going back with it but the one changed since (DECISIONS C32 D19); pause, end and "move the rest…"; three that fail and the
# summary's recheck, which starts only the one that was running (#138) and leaves out the
# hybrid, moved again instead (DECISIONS C21); a destination
# qBittorrent can't write; a destination that fills up. Then a selection moved through the Move… sheet
# (board 43, DECISIONS C16): one torrent's quick move, three from three folders keeping their
# places, one queued behind a folder's move and the folder moved back from its summary, a name
# already at the destination; one big torrent whose file qBittorrent makes at full length before
# it copies (#162), the summary's count of torrents added during it (#164), and what a file
# already at the destination is and whose (#161), an empty one replaced (#176); and a second box whose omaseed is on the playground, whose disks
# it can't see (L13). After each step it checks what qBittorrent says and what is on disk, and
# stops at the first mismatch.
# Run it again after any change to the move sheet, the job, the carry or their words.
#
#   scripts/dev/move-walk.sh [OUT_DIR]        # agents: a headless box, a shot per step,
#                                             # OUT_DIR/index.md says what each shot shows and
#                                             # lists every check
#   scripts/dev/move-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                             # step says what it will do and waits for enter
#   OMASEED_MOVE_WALK_FROM=loose …            # only extras' loose files moved there and back (C32 D19)
#   OMASEED_MOVE_WALK_FROM=selection …        # from the selections on (no folder moves first)
#   OMASEED_MOVE_WALK_FROM=sheet …            # from the places, names and partial pack on
#   OMASEED_MOVE_WALK_FROM=history …          # from move back after the summary (C18) on
#   OMASEED_MOVE_WALK_FROM=big …              # from one big torrent's move and a leftover on
#   OMASEED_MOVE_WALK_FROM=music …            # from the recheck, unwritable and full steps on
#   OMASEED_MOVE_WALK_FROM=remote …           # only the server omaseed can't see
#   OMASEED_MOVE_WALK_KEEP=1 …                # a failed run leaves its box up
#
# Everything happens in a box with a qBittorrent of its own (scripts/dev/app-box.sh up --qbt),
# so no other server is involved, but for the last step: a second box on the playground
# (scripts/dev/test-server.sh start), whose Public Domain Sampler it moves and puts back. The library is made in the box: 160 torrents in
# ~/Torrents/library/games (40 of them in games/handheld), 100 in films and 20 in music, each
# a folder with one small file, added as already complete, plus files no torrent owns beside
# them. The box's /tmp is a tmpfs, so moving there copies real bytes. The full destination is
# a 4 MiB tmpfs that only the box's qBittorrent sees: for the last step it is started again
# inside a user namespace that mounts one at ~/small (no root needed).
#
# The band's and sheet's buttons are found by their accessible names (omabox tree) and
# clicked at their middle; keys are the app's own (the jump palette, shift+m).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_MOVE_BOX:-move-walk}"
BOX_HOME=/home/sbx
LIB="$BOX_HOME/Torrents/library"
BOX_API=http://127.0.0.1:8090/api/v2 # app-box.sh --qbt's qBittorrent, as the box sees it
GAMES=160
HANDHELD=40
FILMS=100
MUSIC=20
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-move-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

# ---- helpers ------------------------------------------------------------------------------

api() { "$APP_BOX" api -b "$BOX" "$@"; }
in_box() { omabox run -b "$BOX" -- "$@" >/dev/null 2>&1; }
keys() { omabox keys -b "$BOX" --window omaseed --wait "$@" >/dev/null 2>&1 || true; }
# The palette is waited for before the name, and the name for a match before tab: a torrent
# just added reaches the app at its next sync, and with no match tab does nothing and esc only
# clears the query, so the next key lands in the palette (a full run typed m there).
table_on() {
    keys ctrl+k
    for _ in $(seq 1 20); do
        find_text "jump to a torrent" && break
        sleep 0.25
    done
    keys -t "$1"
    for _ in $(seq 1 60); do
        omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qE 'label "0 of [0-9]+"' || break
        sleep 0.25
    done
    keys Tab
    keys Escape
}

# count_under PATH: how many torrents qBittorrent saves at PATH or below it.
count_under() {
    api torrents/info | python3 -c '
import json, sys
root = sys.argv[1].rstrip("/")
print(sum(1 for t in json.load(sys.stdin)
          if t["save_path"].rstrip("/") == root or t["save_path"].startswith(root + "/")))' "$1"
}

# count_at PATH: how many torrents save exactly at PATH.
count_at() {
    api torrents/info | python3 -c '
import json, sys
print(sum(1 for t in json.load(sys.stdin) if t["save_path"].rstrip("/") == sys.argv[1].rstrip("/")))' "$1"
}

# bad_states: torrents in error or missing files, by name.
bad_states() {
    api torrents/info | python3 -c '
import json, sys
print(" ".join(t["name"] for t in json.load(sys.stdin) if t["state"] in ("error", "missingFiles")))'
}

CHECKS=0
STEP=0
STEP_SLUG=""

fail() {
    printf '  \033[31m✗ %s\033[0m\n' "$1"
    if [ "$INTERACTIVE" = 0 ]; then
        printf '  - ✗ **%s**\n' "$1" >>"$OUT/index.md"
        omabox shot -b "$BOX" -o "$OUT/failed.png" >/dev/null 2>&1 || true
        omabox tree -b "$BOX" --window omaseed >"$OUT/failed-tree.txt" 2>&1 || true
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

# expect_count "WHAT" FUNC PATH OP N [SECONDS]: wait until `FUNC PATH` OP N (-eq, -ge…).
expect_count() {
    local what="$1" func="$2" path="$3" op="$4" n="$5" limit="${6:-30}" got=""
    for _ in $(seq 1 "$limit"); do
        got="$("$func" "$path" 2>/dev/null || true)"
        [ -n "$got" ] && test "$got" "$op" "$n" && { pass "$what ($got)"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s: $func $path is ${got:-nothing}, wanted $op $n)"
}

# expect_disk "WHAT" TEST PATH [SECONDS]: wait until `test TEST PATH` holds in the box; TEST
# `!` waits until nothing is at PATH.
expect_disk() {
    local what="$1" op="$2" path="$3" limit="${4:-20}" test=()
    if [ "$op" = "!" ]; then test=(! -e "$path"); else test=("$op" "$path"); fi
    for _ in $(seq 1 $((limit * 2))); do
        in_box test "${test[@]}" && { pass "$what"; return 0; }
        sleep 0.5
    done
    fail "$what (after ${limit}s: test $op $path is false in the box)"
}

# find_text TEXT: omaseed's accessibility tree has a node holding TEXT.
find_text() { omabox tree -b "$BOX" --window omaseed --find "$1" >/dev/null 2>&1; }

# expect_text "WHAT" TEXT [SECONDS]: wait until the window shows TEXT.
expect_text() {
    local what="$1" text="$2" limit="${3:-30}"
    for _ in $(seq 1 "$limit"); do
        find_text "$text" && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s no \"$text\" in the window)"
}

# expect_tree "WHAT" STRING [SECONDS]: wait until the window's tree, as text, holds STRING (a
# role with its name, like 'menu item "move…"', or a field's value, like "'~/sel/one'").
expect_tree() {
    local what="$1" text="$2" limit="${3:-10}"
    for _ in $(seq 1 "$limit"); do
        omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qF -- "$text" && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s no $text in the window's tree)"
}

# expect_tree_re "WHAT" ERE [SECONDS]: as expect_tree, for a pattern (grep -E).
expect_tree_re() {
    local what="$1" pattern="$2" limit="${3:-10}"
    for _ in $(seq 1 "$limit"); do
        omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qE -- "$pattern" && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s no /$pattern/ in the tree)"
}

# press NAME [top]: click the middle of the node (a button first) whose accessible name is NAME;
# the leftmost such node, or with `top` the topmost of any role (the band's link, under the top
# bar, when the details pane offers the same action as a button further down).
press() {
    local where
    # The job's desktop notice (a pause, the end) sits over the band's right end, and a click
    # on it shows the job's torrents: wait for the toast to go (the shell keeps one JSON per
    # toast on screen), then let the window settle. Dismissed first: waiting them out took up to
    # 20 s a press, long enough for a folder's move to end before its queue could be looked at.
    in_box omarchy-shell -q notifications dismissAll || true
    for _ in $(seq 1 40); do
        compgen -G "$home/.local/state/omarchy/notifications/*.json" >/dev/null || break
        sleep 0.5
    done
    omabox wait -b "$BOX" --window omaseed --timeout 10s still >/dev/null 2>&1 || true
    where="$(omabox tree -b "$BOX" --window omaseed --find "$1" 2>/dev/null | python3 -c '
import re, sys
name = sys.argv[1]
top = sys.argv[2] == "top"
found = []
for line in sys.stdin:
    m = re.search(r"(\w[\w ]*) \"" + re.escape(name) + r"\"[^@>]*@(-?\d+),(-?\d+) (\d+)x(\d+)", line)
    if m:
        x, y, w, h = map(int, m.groups()[1:])
        # The band has links, not buttons: with top only the place counts.
        found.append((not top and m.group(1).strip() != "button", y if top else x, x + w // 2, y + h // 2))
if found:
    print(*sorted(found)[0][2:])' "$1" "${2:-}")"
    [ -n "$where" ] || fail "no button \"$1\" in the window"
    # shellcheck disable=SC2086 # two numbers
    omabox click -b "$BOX" --window omaseed --wait $where >/dev/null 2>&1 || true
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

# save_of PREFIX: where qBittorrent saves the torrent whose name starts with PREFIX.
save_of() {
    api torrents/info | python3 -c '
import json, sys
print(next(t["save_path"] for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1])).rstrip("/"))' "$1"
}

# expect_at "WHAT" PREFIX PATH [SECONDS]: wait until PREFIX's torrent saves at PATH, moved.
expect_at() {
    local what="$1" prefix="$2" path="$3" limit="${4:-30}" got=""
    for _ in $(seq 1 "$limit"); do
        got="$(api torrents/info | python3 -c '
import json, sys
t = next(t for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1]))
print(t["save_path"].rstrip("/") + ("" if t["state"] != "moving" else " (moving)"))' "$prefix" 2>/dev/null || true)"
        [ "$got" = "$path" ] && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s it saves at ${got:-nothing}, not $path)"
}

# expect_checked "WHAT" NAME [SECONDS]: wait until the radio button NAME reads checked.
expect_checked() {
    local what="$1" name="$2" limit="${3:-10}"
    for _ in $(seq 1 "$limit"); do
        omabox tree -b "$BOX" --window omaseed --find "$name" 2>/dev/null |
            grep -qF "radio button \"$name\" [checked]" && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s \"$name\" is not a checked radio button)"
}

# hold_strip TEXT: the pointer rests on the outcome strip's words TEXT, which holds a done
# strip's 6 s (DECISIONS C18) while the walk looks at it.
hold_strip() {
    local at
    at="$(omabox tree -b "$BOX" --window omaseed --json 2>/dev/null | python3 -c '
import json, sys
for node in json.load(sys.stdin):
    if sys.argv[1] in (node.get("name") or "") and node.get("w", 0) > 0:
        print(node["x"] + node["w"] // 2, node["y"] + node["h"] // 2)
        break' "$1")"
    [ -n "$at" ] || fail "no \"$1\" to rest the pointer on"
    # shellcheck disable=SC2086 # "X Y"
    omabox pointer -b "$BOX" --window omaseed -- move $at >/dev/null 2>&1 || true
}

# no_tree "WHAT" STRING: the window's tree, as text, doesn't hold STRING.
no_tree() {
    if omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qF -- "$2"; then
        fail "$1 ($2 is in the window's tree)"
    else
        pass "$1"
    fi
}

# no_text "WHAT" TEXT: the window doesn't show TEXT.
no_text() {
    if find_text "$2"; then fail "$1 (\"$2\" is in the window)"; else pass "$1"; fi
}

# open_move QUERY DEST N: the cursor on QUERY's torrent, shift+m, DEST typed over the field;
# waits until the sheet offers to move N torrents.
open_move() {
    table_on "$1"
    keys shift+m
    # Once more if it didn't open: right after the box's qBittorrent restarted, a full run lost
    # the key while the app read the library again.
    for _ in $(seq 1 20); do
        find_text "new path of the folder" && break
        sleep 0.5
    done
    find_text "new path of the folder" || { table_on "$1"; keys shift+m; }
    expect_text "the move sheet is open" "new path of the folder" 10
    keys -t "$2"
    expect_text "the sheet offers \"move $3 torrents\"" "move $3 torrents" 30
}

# box_qbt_restart [unshare]: stop the box's qBittorrent (it saves on the way out) and start it
# again; "unshare" starts it in a user namespace with a 4 MiB tmpfs at ~/small.
box_qbt_restart() {
    in_box pkill -x qbittorrent-nox || true
    for _ in $(seq 1 60); do
        in_box pgrep -x qbittorrent-nox || break
        sleep 0.5
    done
    if [ "${1:-}" = unshare ]; then
        # shellcheck disable=SC2016 # expanded by the box's sh
        omabox run -b "$BOX" -d -- unshare -rm sh -c '
mkdir -p "$HOME/small"
mount -t tmpfs -o size=4m omaseed-small "$HOME/small"
exec qbittorrent-nox' >/dev/null 2>&1
    else
        omabox run -b "$BOX" -d -- qbittorrent-nox >/dev/null 2>&1
    fi
    for _ in $(seq 1 60); do
        [ "$(omabox run -b "$BOX" -- curl -s -o /dev/null -w '%{http_code}' "$BOX_API/app/version" 2>/dev/null)" = 200 ] && return 0
        sleep 0.5
    done
    fail "the box's qBittorrent did not come back"
}

cleanup() {
    local status=$?
    if [ "$INTERACTIVE" = 1 ]; then
        echo
        echo "The box stays up for a look around; throw it away with: scripts/dev/app-box.sh down -b $BOX"
    elif [ "$status" != 0 ] && [ -n "${OMASEED_MOVE_WALK_KEEP:-}" ]; then
        echo "kept the box for a look: scripts/dev/app-box.sh down -b $BOX"
    else
        chmod -R u+w "$home/locked" "$home/rk" 2>/dev/null || true
        "$APP_BOX" down -b "$MAIN_BOX" >/dev/null 2>&1 || true
        [ -z "$REMOTE_BOX" ] || "$APP_BOX" down -b "$REMOTE_BOX" >/dev/null 2>&1 || true
    fi
    put_back_remote
    exit "$status"
}
home=""
MAIN_BOX="$BOX"
REMOTE_BOX=""
REMOTE_HASH=""
REMOTE_FROM=""
REMOTE_DEST=""
# put_back_remote: the playground's torrent the last step moved goes back, and the folder it
# made goes (only that one: under the playground's own downloads, named remote-walk).
put_back_remote() {
    [ -n "$REMOTE_HASH" ] || return 0
    curl -s -o /dev/null "$PLAYGROUND/torrents/setLocation" -d "hashes=$REMOTE_HASH" \
        --data-urlencode "location=$REMOTE_FROM" || true
    for _ in $(seq 1 30); do
        [ "$(curl -s "$PLAYGROUND/torrents/info?hashes=$REMOTE_HASH" | python3 -c '
import json, sys
print(json.load(sys.stdin)[0]["save_path"].rstrip("/"))' 2>/dev/null)" = "$REMOTE_FROM" ] && break
        sleep 1
    done
    case "$REMOTE_DEST" in
    "$HOME"/.local/share/omaseed-test/*/remote-walk) rm -rf "$REMOTE_DEST" ;;
    esac
    REMOTE_HASH=""
}
PLAYGROUND=http://127.0.0.1:8081/api/v2
trap cleanup EXIT

# ---- setup --------------------------------------------------------------------------------

# A read-only folder left by an earlier run would stop app-box.sh from clearing the box.
chmod -R u+w "$HOME/.cache/omabox/$BOX/home/locked" "$HOME/.cache/omabox/$BOX/home/rk" 2>/dev/null || true

if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --qbt --interactive -b "$BOX" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Move walk %s\n\nBuild `%s`, a box with its own qBittorrent (`app-box.sh up --qbt`). Each step: what was done, every check on the server and on disk, and shots.\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up --qbt -b "$BOX" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
# The desktop's do-not-disturb: "Download complete" toasts come every few seconds while the
# library downloads, and under the title bar (DECISIONS C24 H5) the move band's links sit where
# a toast covers them, so a press there hit the toast. Nothing here checks a toast.
in_box omarchy-shell -q notifications setDnd on >/dev/null || true
echo "box $BOX is up; making $((GAMES + FILMS + MUSIC)) torrents and their files"

# The library: each torrent a folder with one file, its .torrent beside the walk's own files.
python3 - "$home" "$BOX_HOME" "$GAMES" "$HANDHELD" "$FILMS" "$MUSIC" <<'PY'
import hashlib, os, sys

home, box_home, games, handheld, films, music = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
PIECE = 16384

def bencode(x):
    if isinstance(x, int):
        return b"i%de" % x
    if isinstance(x, bytes):
        return b"%d:%s" % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode())
    if isinstance(x, list):
        return b"l" + b"".join(map(bencode, x)) + b"e"
    return b"d" + b"".join(bencode(k) + bencode(x[k]) for k in sorted(x)) + b"e"

def make(root, name, size, seed, hybrid=False):
    folder = os.path.join(home, "Torrents/library", root, name)
    os.makedirs(folder, exist_ok=True)
    data = hashlib.sha256(seed.encode()).digest() * (size // 32 + 1)
    data = data[:size]
    with open(os.path.join(folder, "data.bin"), "wb") as f:
        f.write(data)
    pieces = b"".join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))
    info = {"name": name, "piece length": PIECE, "pieces": pieces, "private": 1,
            "files": [{"length": size, "path": ["data.bin"]}]}
    top = {"created by": "move-walk"}
    if hybrid:
        # BEP 52's v2 half beside the v1 one, as libtorrent's create_torrent writes it: a file
        # of one 16 KiB block has the block's SHA-256 as its root and no piece layer.
        assert size <= PIECE
        info["meta version"] = 2
        info["file tree"] = {"data.bin": {"": {"length": size, "pieces root": hashlib.sha256(data).digest()}}}
        top["piece layers"] = {}
    out = os.path.join(home, "walk-torrents", root.replace("/", "-"))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".torrent"), "wb") as f:
        f.write(bencode(dict(top, info=info)))

for i in range(1, games - handheld + 1):
    make("games", "Arcade Set %03d" % i, 8192 + 997 * i, "g%d" % i)
for i in range(1, handheld + 1):
    make("games/handheld", "Handheld Pack %03d" % i, 8192 + 991 * i, "h%d" % i)
for i in range(1, films + 1):
    make("films", "Short Film %03d" % i, 8192 + 983 * i, "f%d" % i)
for i in range(1, music + 1):
    make("music", "Album %02d" % i, 600 * 1024 + i, "m%d" % i)
# Move back and the loose files the move carried (DECISIONS C32 D19).
for i in range(1, 4):
    make("extras", "Extras Pack %02d" % i, 8192 + 977 * i, "x%d" % i)
# The recheck after a move (#138): one that moves (the smallest, sent alone first) and three
# below it that can't, the last a hybrid (v1+v2) that the recheck leaves out (DECISIONS C21).
make("recheck", "Recheck One", 8192, "r1")
make("recheck/locked", "Recheck Two", 9000, "r2")
make("recheck/locked", "Recheck Three", 9500, "r3")
make("recheck/locked", "Recheck Four", 9700, "r4", hybrid=True)

def loose(path, size, old=True):
    full = os.path.join(home, "Torrents/library", path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(os.urandom(size))
    if old:
        t = os.path.getmtime(full) - 2 * 86400
        os.utime(full, (t, t))

loose("games/manual.pdf", 300 * 1024)
loose("games/notes/readme.txt", 2048)
loose("games/handheld/saves/slot1.sav", 64 * 1024)
loose("games/Thumbs.db", 4096)
loose("games/capture-in-progress.mkv", 100 * 1024, old=False)
loose("films/poster.jpg", 120 * 1024)
loose("extras/manual.pdf", 18 * 1024)
loose("extras/notes/readme.txt", 2048)
PY

# Web API 2.16 (qBittorrent 5.3) reads the skip-check option as seedMode and ignores
# skip_checking: both are sent, or every torrent is hash-checked on add.
add_root() { # add_root DIR SAVE_PATH: every .torrent in the walk's DIR, 40 a request
    local dir="$1" save="$2" args=() file n=0
    for file in "$home/walk-torrents/$dir"/*.torrent; do
        args+=(-F "torrents=@$BOX_HOME/walk-torrents/$dir/$(basename "$file")")
        n=$((n + 1))
        if [ "$n" = 40 ]; then
            api torrents/add "${args[@]}" -F "savepath=$save" -F skip_checking=true -F seedMode=true >/dev/null
            args=()
            n=0
        fi
    done
    [ ${#args[@]} = 0 ] || api torrents/add "${args[@]}" -F "savepath=$save" -F skip_checking=true -F seedMode=true >/dev/null
}
add_root games "$LIB/games"
add_root games-handheld "$LIB/games/handheld"
add_root films "$LIB/films"
add_root music "$LIB/music"
add_root extras "$LIB/extras"
add_root recheck "$LIB/recheck"
add_root recheck-locked "$LIB/recheck/locked"

step "The library" "$((GAMES + FILMS + MUSIC)) torrents added as complete: games ($GAMES, $HANDHELD of them in games/handheld), films ($FILMS), music ($MUSIC), and four in recheck (one a hybrid); six files no torrent owns beside them; and extras (3), with two of its own."
expect_count "games holds $GAMES torrents" count_under "$LIB/games" -eq "$GAMES" 60
expect_count "games/handheld holds $HANDHELD" count_at "$LIB/games/handheld" -eq "$HANDHELD"
expect_count "films holds $FILMS" count_under "$LIB/films" -eq "$FILMS"
expect_count "music holds $MUSIC" count_under "$LIB/music" -eq "$MUSIC"
expect_text "the rail lists the folders" "handheld" 30
shot "The library: the rail's folders group lists library, games with handheld under it, films and music."

# OMASEED_MOVE_WALK_FROM skips what comes before: selection, sheet, history, big, music or
# remote; loose walks only extras' round trip.
FROM="${OMASEED_MOVE_WALK_FROM:-games}"
if [ "$FROM" = games ]; then

# ---- games: another filesystem, a kill mid-move, the loose files ---------------------------

step "Move games: the sheet" "The cursor on Arcade Set 001, shift+m, /tmp/fast/games typed over the field. The sheet keeps the structure (two path rows), says the room on /tmp, and lists the loose files as a dry run: 3 carried, 1 recent, 1 that never counts."
open_move "Arcade Set 001" /tmp/fast/games "$GAMES"
expect_text "the dry run counts the 3 loose files" "3 files" 20
expect_text "the recent one stays" "changed in the last 15 min" 5
shot "The move sheet: two path rows, room, writable, order, the loose files' dry run, the carry tick." sheet

step "Move games, killed mid-move" "enter. Once 20 have landed on /tmp, omaseed is killed (SIGKILL, no goodbye). qBittorrent finishes the 3 at most it was given; nothing more is sent while omaseed is gone."
keys Return
expect_count "the first 20 land on /tmp/fast/games" count_under /tmp/fast/games -ge 20 120
shot "The running band: n of $GAMES, bytes, %, time left, the torrent moving now; the rail's mark on games." band
in_box pkill -KILL -x omaseed || true
gone=0
for _ in $(seq 1 20); do
    in_box pgrep -x omaseed || { gone=1; break; }
    sleep 0.5
done
if [ "$gone" = 1 ]; then pass "omaseed is gone"; else fail "omaseed is still running"; fi
sleep 4
before="$(count_under /tmp/fast/games)"
sleep 6
after="$(count_under /tmp/fast/games)"
if [ "$before" = "$after" ]; then pass "nothing lands while omaseed is gone ($after)"; else fail "torrents kept landing without omaseed ($before → $after)"; fi

step "Start omaseed again" "app-box.sh restart: the job comes back by itself and goes on (M13), every torrent lands, then the loose files are carried: copied, checked, removed here."
"$APP_BOX" restart -b "$BOX" >/dev/null
expect_text "the band says the move goes on" "Moving games" 30
shot "The band again, after the restart." resumed
expect_count "all $GAMES games torrents are on /tmp/fast/games" count_under /tmp/fast/games -eq "$GAMES" 400
expect_count "the $HANDHELD handheld ones kept their place below games" count_at /tmp/fast/games/handheld -eq "$HANDHELD"
expect_text "the end reads \"games moved, loose files too\"" "games moved, loose files too" 60
expect_disk "manual.pdf is at the new place" -f /tmp/fast/games/manual.pdf
expect_disk "notes/readme.txt too" -f /tmp/fast/games/notes/readme.txt
expect_disk "handheld/saves/slot1.sav too" -f /tmp/fast/games/handheld/saves/slot1.sav
expect_disk "manual.pdf left the old place" ! "$LIB/games/manual.pdf"
expect_disk "the emptied saves folder is gone" ! "$LIB/games/handheld/saves"
expect_disk "the recent file stayed" -f "$LIB/games/capture-in-progress.mkv"
expect_disk "Thumbs.db stayed" -f "$LIB/games/Thumbs.db"
expect_disk "a torrent's file is at the new place" -f "/tmp/fast/games/Arcade Set 001/data.bin"
expect_disk "no temporary copy is left" ! /tmp/fast/games/.manual.pdf.omaseed-carry
if [ -z "$(bad_states)" ]; then pass "no torrent in error or missing files"; else fail "in error: $(bad_states)"; fi
shot "The green summary: torrents and files at /tmp/fast/games, what stayed and why, the empty folder removed; show what stayed." summary
# #173: the record of $GAMES torrents unfolds inside the band, a third of the window at most,
# and scrolls there; the table moves down under it and nothing draws over it.
band_height() {
    omabox tree -b "$BOX" --window omaseed --find "games moved" 2>/dev/null |
        sed -nE 's/^alert "[^"]*"[^@>]*@-?[0-9]+,-?[0-9]+ [0-9]+x([0-9]+)$/\1/p' | head -n1
}
folded="$(band_height)"
press details top
expect_tree "the summary's details unfold, scrollable" 'started ' 10
unfolded="$(band_height)"
# The box's screen is 1080 px tall: a third for the block, the band's own lines above it.
if [ -n "$unfolded" ] && [ -n "$folded" ] && [ "$unfolded" -gt "$folded" ] && [ "$unfolded" -le 540 ]; then
    pass "unfolded, the band is $unfolded px tall (folded $folded; at most half the screen)"
else
    fail "the unfolded band is ${unfolded:-?} px tall (folded ${folded:-?}; wanted more, and at most 540)"
fi
expect_text "the table is still there under it" "Progress" 5
shot "The summary's details unfolded: $GAMES torrents' lines in a block a third of the window tall, scrolling inside it, the table moved down under the band (#173)." details
press details top
no_text "folded again" "started "
press "show what stayed"
expect_text "the record opens: Loose files of games" "Loose files of games" 10
shot "What stayed: the recent capture, with its reason." stayed
keys Escape

# ---- films: pause, end, move the rest ------------------------------------------------------

step "Move films, paused" "The cursor on Short Film 001, shift+m, /tmp/fast/films, enter; pause after 10 have landed. The ones with qBittorrent finish; nothing more is sent."
open_move "Short Film 001" /tmp/fast/films "$FILMS"
keys Return
expect_count "10 films land" count_under /tmp/fast/films -ge 10 120
press pause
expect_text "the band says paused" "paused" 30
sleep 3
before="$(count_under /tmp/fast/films)"
sleep 6
after="$(count_under /tmp/fast/films)"
if [ "$before" = "$after" ]; then pass "paused: nothing more lands ($after)"; else fail "it kept moving while paused ($before → $after)"; fi
shot "The paused band: resume, end the move." paused

step "End, then move the rest" "end the move: every torrent whole on one side or the other. move the rest… opens the sheet with /tmp/fast/films again and only the rest; enter moves them, and poster.jpg follows."
press "end the move"
expect_text "the band says it ended early" "ended early" 30
shot "The move ended early: n moved, the rest still in films; move the rest…" ended
rest=$((FILMS - after))
press "move the rest…"
expect_text "the sheet offers the $rest left" "move $rest torrents" 30
shot "The sheet again: the same new path, only the $rest left." rest
keys Return
expect_count "all $FILMS films are on /tmp/fast/films" count_under /tmp/fast/films -eq "$FILMS" 300
expect_text "the end reads \"films moved, loose files too\"" "films moved, loose files too" 60
expect_disk "poster.jpg followed" -f /tmp/fast/films/poster.jpg
expect_count "nothing is left in films" count_under "$LIB/films" -eq 0 5

fi

# ---- extras: move back carries back what the move carried (DECISIONS C32 D19) --------------

if [ "$FROM" = games ] || [ "$FROM" = loose ]; then

step "Move extras, loose files too" "The cursor on Extras Pack 01, shift+m, /tmp/fast/extras, enter: its 3 torrents land on /tmp, then manual.pdf and notes/readme.txt are carried (another filesystem: copied, checked, removed here)."
open_move "Extras Pack 01" /tmp/fast/extras 3
expect_text "the dry run counts its 2 loose files" "2 files" 20
keys Return
expect_count "the 3 extras land on /tmp/fast/extras" count_at /tmp/fast/extras -eq 3 120
expect_text "the end reads \"extras moved, loose files too\"" "extras moved, loose files too" 60
expect_disk "manual.pdf is at the new place" -f /tmp/fast/extras/manual.pdf
expect_disk "notes/readme.txt too" -f /tmp/fast/extras/notes/readme.txt
expect_disk "manual.pdf left the old place" ! "$LIB/extras/manual.pdf"

step "Since the move" "At /tmp/fast/extras, notes/readme.txt is edited and new-scan.png is put there (neither by omaseed)."
in_box sh -c 'printf "edited after the move\n" >>/tmp/fast/extras/notes/readme.txt'
in_box sh -c 'printf "a scan\n" >/tmp/fast/extras/new-scan.png'
expect_disk "new-scan.png is there" -f /tmp/fast/extras/new-scan.png

step "Move extras back, with what it carried" "The summary's move back…: the sheet carries back the 2 loose files the move carried, under its tick: manual.pdf back, notes/readme.txt changed after the move, so it stays; new-scan.png was put there since, the move didn't carry it, so it stays. The foot says the list is the one the move kept; the button reads move 3 back · 1 file. It moves: the torrents land in extras, then manual.pdf follows."
press "move back…" top
expect_text "the sheet offers to carry back the 2" "Carry back the 2 loose files the move carried" 30
expect_text "the changed one stays, and says when" "after the move" 10
expect_text "the new one stays: the move didn't carry it" "the move didn't carry it" 20
expect_text "the foot names the list" "the list is the one the move kept" 5
expect_text "the button counts the file" "move 3 back · 1 file" 10
shot "Move back: the tick, manual.pdf back, notes/readme.txt changed after the move and new-scan.png put there since, both staying; move 3 back · 1 file." sheet
press "move 3 back · 1 file"
expect_count "the 3 are back in extras" count_at "$LIB/extras" -eq 3 120
expect_text "the end reads \"3 torrents moved back, loose files too\"" "moved back, loose files too" 60
expect_disk "manual.pdf is back" -f "$LIB/extras/manual.pdf"
expect_disk "and gone from /tmp/fast/extras" ! /tmp/fast/extras/manual.pdf
expect_disk "notes/readme.txt, changed since, stayed" -f /tmp/fast/extras/notes/readme.txt
expect_disk "it was not carried back" ! "$LIB/extras/notes/readme.txt"
expect_disk "new-scan.png stayed" -f /tmp/fast/extras/new-scan.png
expect_disk "and never went to extras" ! "$LIB/extras/new-scan.png"
expect_disk "no temporary copy is left" ! "$LIB/extras/.manual.pdf.omaseed-carry"
# The band's line is cut short in the tree: "show what stayed" names it whole.
expect_text "the summary says the move back carried files" "1 file back at $LIB/extras" 10
if in_box grep -aq "edited after the move" /tmp/fast/extras/notes/readme.txt; then pass "the edit is untouched"; else fail "notes/readme.txt at /tmp/fast/extras lost its edit"; fi
shot "The end of the move back: 3 torrents and 1 file back at extras; notes/readme.txt stayed at /tmp/fast/extras, changed after the move." summary
press "show what stayed"
expect_text "what stayed opens" "Loose files of extras" 10
# Each row is one line in the tree: path, size, what happened and why (#201 F24).
expect_tree_re "the record's row: notes/readme.txt stayed, changed after the move" \
    'extras/notes/readme\.txt, [^:]+: stays: changed at [^"]*after the move'
expect_tree_re "the record's row: manual.pdf was carried back" 'extras/manual\.pdf, [^:]+: carried'
shot "What stayed: notes/readme.txt, changed after the move." stayed
keys Escape
press close

if [ "$FROM" = loose ]; then
    echo
    echo "all $CHECKS checks passed"
    [ "$INTERACTIVE" = 1 ] || printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
    exit 0
fi
fi

# ---- a selection (board 43, C16) ------------------------------------------------------------

if [ "$FROM" = games ] || [ "$FROM" = selection ]; then
SEL="$BOX_HOME/sel"

step "One torrent, a quick move" "The cursor on Handheld Pack 040, m: the Move… sheet on that torrent. ~/sel/one typed over the field: one green line (L3). enter: it lands within 3 s, so no band; the strip over the rows says it moved, and the row's status reads ✓ moved · one (C18 D1). No show: it is the cursor's row, in view (C30 F8). With the pointer holding the strip, j takes the cursor off it and show comes, saying why (F10, F11); show puts the cursor back and goes. esc closes the strip."
table_on "Handheld Pack 040"
keys m
expect_text "the Move… sheet is open" "Move Handheld Pack 040" 10
keys ctrl+a -t "$SEL/one"
expect_text "one green line" "access are fine" 20
shot "The Move… sheet for one torrent: the field, one green line, move." sheet
keys Return
expect_at "Handheld Pack 040 saves at ~/sel/one" "Handheld Pack 040" "$SEL/one"
expect_disk "its file is there" -f "$SEL/one/Handheld Pack 040/data.bin"
expect_text "the strip says it moved" "Handheld Pack 040 moved to $SEL/one" 20
no_text "no band for it" "Moving Handheld Pack 040"
hold_strip "Handheld Pack 040 moved to $SEL/one"
no_tree "no show for the cursor's own row" 'button "show"'
shot "The strip at the rows' bottom middle: ✓ Handheld Pack 040 moved to ~/sel/one and ×, no show, its green line held under the pointer; the row's status reads ✓ moved · one in green." strip
keys j
expect_tree "j: show comes" 'button "show"' 5
expect_text "…and says why" "it is not selected" 5
shot "The cursor one row down: the strip offers show, a dim line under its words, \"it is not selected\" (C30 F8, F10)." show
press show
expect_text "show put the cursor back on it" "Handheld Pack 040" 5
for _ in $(seq 1 10); do omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qF 'button "show"' || break; sleep 0.5; done
no_tree "…and show went (F11)" 'button "show"'
keys Escape
no_text "esc closed the strip" "Handheld Pack 040 moved to $SEL/one"

step "A quick move qBittorrent can't do" "The box's ~/locked/quick exists, read-only. Handheld Pack 039, m, that folder, enter: qBittorrent answers OK and logs that it can't write there. The strip turns red and stays: Handheld Pack 039 didn't move, why, details and move…; the row reads ✕ didn't move (C18 D1, D3). details opens the band above the strip; esc closes the band, esc again the strip."
mkdir -p "$home/locked/quick"
chmod 0555 "$home/locked/quick"
table_on "Handheld Pack 039"
keys m
expect_text "the Move… sheet is open" "Move Handheld Pack 039" 10
keys ctrl+a -t "$BOX_HOME/locked/quick"
expect_text "the sheet has checked it" "on $BOX_HOME" 20
keys Return
expect_text "the strip says it didn't move" "Handheld Pack 039 didn't move" 60
sleep 7
find_text "Handheld Pack 039 didn't move" || fail "the failure went by itself (D3: it stays until closed)"
pass "it is still there after 7 s"
shot "The red strip: ✕ Handheld Pack 039 didn't move, why, details, move…, ×; the row reads ✕ didn't move in red." strip
press details
shot "The band above the strip with the failure's words; the strip under it." band
keys Escape
keys Escape
no_text "esc, esc: the band and the strip are gone" "Handheld Pack 039 didn't move"
chmod 0755 "$home/locked/quick"

step "Three from three folders, keeping their places" "space on Arcade Set 002 (games), Handheld Pack 002 (games/handheld) and Short Film 002 (films); m, ~/sel/keep. The sheet offers into one folder or keeping their places below the folder they share; keep their places: each folder's row and where it goes. move 3 torrents: each lands in the same place below ~/sel/keep."
read -r common < <(api torrents/info | python3 -c '
import json, os, sys
paths = [t["save_path"].rstrip("/") for t in json.load(sys.stdin)
         if t["name"] in ("Arcade Set 002", "Handheld Pack 002", "Short Film 002")]
print(os.path.commonpath(paths))')
rel_of() { local at; at="$(save_of "$1")"; echo "${at#"$common"}"; }
arcade_rel="$(rel_of "Arcade Set 002")"
handheld_rel="$(rel_of "Handheld Pack 002")"
film_rel="$(rel_of "Short Film 002")"
table_on "Arcade Set 002"
keys space
table_on "Handheld Pack 002"
keys space
table_on "Short Film 002"
keys space
keys m
expect_text "the sheet is for the three" "Move 3 torrents" 10
keys ctrl+a -t "$SEL/keep"
expect_text "it offers to keep their places below $common" "keep their places below $common" 20
press "keep their places below $common"
expect_checked "keep their places is picked" "keep their places below $common"
expect_text "a row for handheld" "to $SEL/keep$handheld_rel" 10
shot "Keep their places: three rows, each folder and where it goes; room; move 3 torrents." sheet
press "move 3 torrents"
# C16 L6, C18 D1: a move that lands within 3 s says so in the strip ("3 moved to …"); a slower
# one (a busy machine) gets the band and its summary ("3 torrents moved to …"). Either is right.
# Looked for first: the note lasts 6 s, and the server checks below take seconds.
expect_text "the strip or the band says 3 moved" "moved to $SEL/keep" 30
shot "With the three still selected, the action bar floats over the list, and the strip (3 moved to ~/sel/keep, or the band's summary when it ran past 3 s) just above it, with no show: they are in view and selected (C32 D17, C30 F8)." strip
expect_at "Arcade Set 002 is at ~/sel/keep$arcade_rel" "Arcade Set 002" "$SEL/keep$arcade_rel"
expect_at "Handheld Pack 002 at ~/sel/keep$handheld_rel" "Handheld Pack 002" "$SEL/keep$handheld_rel"
expect_at "Short Film 002 at ~/sel/keep$film_rel" "Short Film 002" "$SEL/keep$film_rel"
table_on "Short Film 002"
keys Escape

films="$(save_of "Short Film 010")"
nfilms="$(count_at "$films")"
step "A move queued behind a folder's" "The films folder ($nfilms torrents), shift+m, ~/sel/films, enter: the band. While it runs, Arcade Set 003, m, ~/sel/after: the button reads move after films (Q4); enter, and the band names what's next. When films ends its summary stays, and Arcade Set 003 moves under it (#105)."
open_move "Short Film 010" "$SEL/films" "$nfilms"
keys Return
expect_text "the band shows the folder's move (moving, or renaming on one drive)" "films to $SEL/films" 30
table_on "Arcade Set 003"
keys m
keys ctrl+a -t "$SEL/after"
expect_text "the sheet says it waits" "move after films" 20
shot "The Move… sheet during the folder's move: move after films." sheet
press "move after films"
# The band's line ends "next: Arcade Set 003 to …", past the length omabox's tree keeps of a
# name, so the shot shows it and the details below check it.
shot "The band: films moving, next: Arcade Set 003 to ~/sel/after." band
press details
expect_text "details list it, waiting (J3)" "waiting: Arcade Set 003" 10
shot "The band's details: waiting: Arcade Set 003, with cancel." details
# Its strip, or its band's summary when it ran past 3 s (C16 L6, C18 D1). Looked for first,
# with the details still open: the note lasts 6 s, while closing them waits out the films'
# desktop notice (press) and the server checks below take seconds on a busy machine.
expect_text "it says Arcade Set 003 moved" "Arcade Set 003 moved to $SEL/after" 400
press details
expect_count "all films land on ~/sel/films" count_at "$SEL/films" -eq "$nfilms" 400
expect_at "Arcade Set 003 moved after them" "Arcade Set 003" "$SEL/after" 60
expect_text "the films' summary is still there" "films moved" 5
shot "The films summary, still there under the top bar after the queued move landed under it (C18 D4)." summary

step "Move back" "The films summary's move back…: the sheet sends each of the $nfilms back to $films, and ticks carrying back poster.jpg, the loose file the folder's move carried (C32 D19); move $nfilms back · 1 file. Arcade Set 003, moved after it, stays."
# The band's, not the details pane's: that one is the cursor torrent's own last move (C18 D7).
press "move back…" top
expect_text "the sheet moves the films back, and the loose file the move carried" \
    "move $nfilms back · 1 file" 30
shot "Move back: each to where it was, poster.jpg ticked to go back with them." sheet
press "move $nfilms back · 1 file"
expect_count "the films are back at $films" count_at "$films" -eq "$nfilms" 400
expect_count "none left on ~/sel/films" count_at "$SEL/films" -eq 0 5
expect_disk "poster.jpg is back at $films" -f "$films/poster.jpg" 60
expect_disk "and gone from ~/sel/films" ! "$SEL/films/poster.jpg"
expect_at "Arcade Set 003 stays at ~/sel/after" "Arcade Set 003" "$SEL/after" 1
expect_text "the band says they moved back" "moved back" 60
shot "The end of the move back." back
press close

step "A name already there" "The box's ~/sel/clash already has Arcade Set 004/data.bin, another file under the torrent's file's name. space on Arcade Set 004 and Arcade Set 005, m, ~/sel/clash: 1 left out (qBittorrent would keep that folder's file and call the move done, board 43 §1); move 1 torrent moves the other."
mkdir -p "$home/sel/clash/Arcade Set 004"
echo planted >"$home/sel/clash/Arcade Set 004/data.bin"
clash_from="$(save_of "Arcade Set 004")"
table_on "Arcade Set 004"
keys space
table_on "Arcade Set 005"
keys space
keys m
keys ctrl+a -t "$SEL/clash"
expect_text "one is left out" "1 left out" 20
shot "The Move… sheet: 1 left out, the red line; move 1 torrent." sheet
press "move 1 torrent"
expect_at "Arcade Set 005 is at ~/sel/clash" "Arcade Set 005" "$SEL/clash"
expect_at "Arcade Set 004 stayed" "Arcade Set 004" "$clash_from" 1
if [ "$(cat "$home/sel/clash/Arcade Set 004/data.bin")" = planted ]; then pass "the file there is untouched"; else fail "the planted data.bin changed"; fi
table_on "Arcade Set 005"
keys Escape
fi

# ---- the sheet's places, the names, a partial pack (board 43 L2, L4; C16) -------------------

if [ "$FROM" = games ] || [ "$FROM" = selection ] || [ "$FROM" = sheet ]; then
SEL="$BOX_HOME/sel"

step "Places under the field" "The cursor on Short Film 020, m: the Move… sheet. ↓ opens the places under the field (recent destinations, the library's folders, the categories' folders); ↓ again moves to the next one; enter fills the field with it and moves nothing; ↓ opens it again and esc closes only the list; the next enter moves the torrent to the place filled in."
places_from="$(save_of "Short Film 020")"
table_on "Short Film 020"
keys m
expect_text "the Move… sheet is open" "Move Short Film 020" 10
keys Down
expect_text "↓ opens the places" "fill in" 10
# The rows the list shows, and which one is current ([selected]), from the window's tree.
place_rows() {
    omabox tree -b "$BOX" --window omaseed 2>/dev/null | python3 -c '
import re, sys
inside = False
for line in sys.stdin:
    if "dialog \"Move Short Film 020\"" in line:
        inside = True
    elif inside and re.match(r"  \S", line):
        break
    m = re.search(r"list item \"([^\"]+)\"( \[selected\])?", line) if inside else None
    if m:
        print(("* " if m.group(2) else "  ") + m.group(1))'
}
first="$(place_rows | sed -n 's/^\* //p')"
rows="$(place_rows | wc -l)"
if [ "$rows" -ge 2 ]; then pass "the list shows $rows places"; else fail "the list shows $rows places, expected at least 2"; fi
shot "The places under the field: recent, library, categories; the hints pick, fill in, close, browse…." places
keys Down
second="$(place_rows | sed -n 's/^\* //p')"
if [ -n "$second" ] && [ "$second" != "$first" ]; then pass "↓ moves to the next place ($second)"; else fail "↓ didn't move (\"$first\" then \"$second\")"; fi
# Past the torrent's own folder, so the move below proves the fill.
for _ in 1 2 3 4; do
    [ "$second" != "$places_from" ] && break
    keys Down
    second="$(place_rows | sed -n 's/^\* //p')"
done
[ "$second" != "$places_from" ] || fail "every place picked is Short Film 020's own folder"
keys Return
no_text "enter closes the list" "fill in"
expect_text "the sheet stays open" "Move Short Film 020" 3
expect_at "enter moved nothing" "Short Film 020" "$places_from" 3
keys Down
expect_text "↓ opens the list again" "fill in" 10
keys Escape
no_text "esc closes the list" "fill in"
expect_text "and only the list" "Move Short Film 020" 3
expect_at "still nothing moved" "Short Film 020" "$places_from" 3
# The field reads as a combo box "new path" (C18 D5), whose text the tree doesn't print: the
# fill is proven by where the next enter sends the torrent.
keys Return
expect_at "the next enter moves it to the place filled in" "Short Film 020" "$second" 30

step "The names" "The : palette finds move… when set location is typed; the actions menu (enter) and the action bar (space) say move…; the actions menu has no move folder… (C19 T3: that is the rail folder row's)."
keys colon
keys -t "set location"
expect_tree "the palette finds move… for \"set location\"" 'list item "move…"' 10
shot "The command palette: set location typed, move… found." palette
# The esc ladder: the first clears what was typed, the second closes the palette.
keys Escape
keys Escape
no_text "the palette is closed" "set location"
table_on "Short Film 021"
keys Return
expect_tree "the actions menu says move…" 'menu item "move…"' 10
if omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qF 'menu item "move folder…"'; then
    fail "the actions menu still offers move folder…"
fi
pass "the actions menu offers no move folder…"
shot "The actions menu: move…, no move folder…." actions
keys Escape
table_on "Short Film 021"
keys space
expect_tree "the action bar says move…" 'button "move…"' 10
shot "The action bar, floating over the list: move…." bar
keys space
keys Escape

step "A partial pack to another drive" "Big Collection 1: six files, the first two wanted and on disk, the other four never written, 100 % of what it wants. In its Files tab, x on the half-ticked Disc 1 unticks it and x again brings back the two (C19 T1). m, /tmp/fast/partial, enter: only the two wanted files go; it stays at 100 % with no missing files, and the four others don't appear anywhere."
python3 -I - "$home" <<'PY'
import hashlib, os, sys
home, PIECE = sys.argv[1], 16384
def bencode(x):
    if isinstance(x, int):
        return b"i%de" % x
    if isinstance(x, bytes):
        return b"%d:%s" % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode())
    if isinstance(x, list):
        return b"l" + b"".join(map(bencode, x)) + b"e"
    return b"d" + b"".join(bencode(k) + bencode(x[k]) for k in sorted(x)) + b"e"
# Every file a whole number of pieces, so the two wanted ones verify alone (as move-library.sh).
files = [("Disc 1/part %d.bin" % n,
          (hashlib.sha256(b"p1-%d" % n).digest() * (PIECE * (2 + n) // 32 + 1))[:PIECE * (2 + n)])
         for n in range(6)]
folder = os.path.join(home, "Torrents/library/packs/Big Collection 1")
for path, data in files[:2]:
    os.makedirs(os.path.dirname(os.path.join(folder, path)), exist_ok=True)
    with open(os.path.join(folder, path), "wb") as f:
        f.write(data)
stream = b"".join(d for _, d in files)
info = {"name": "Big Collection 1", "piece length": PIECE, "private": 1,
        "pieces": b"".join(hashlib.sha1(stream[i:i + PIECE]).digest() for i in range(0, len(stream), PIECE)),
        "files": [{"length": len(d), "path": p.split("/")} for p, d in files]}
os.makedirs(os.path.join(home, "walk-torrents/packs"), exist_ok=True)
with open(os.path.join(home, "walk-torrents/packs/Big Collection 1.torrent"), "wb") as f:
    f.write(bencode({"info": info, "created by": "move-walk"}))
PY
api torrents/add -F "torrents=@$BOX_HOME/walk-torrents/packs/Big Collection 1.torrent" \
    -F "savepath=$LIB/packs" -F stopped=true >/dev/null
pack=""
for _ in $(seq 1 20); do
    pack="$(api torrents/info | python3 -c '
import json, sys
print(next((t["hash"] for t in json.load(sys.stdin) if t["name"] == "Big Collection 1"), ""))')"
    [ -n "$pack" ] && break
    sleep 0.5
done
[ -n "$pack" ] || fail "Big Collection 1 wasn't added"
api -X POST torrents/filePrio -d "hash=$pack" -d "id=2|3|4|5" -d priority=0 >/dev/null
# Checked while stopped, then started: a start sent during a check is lost (move-library.sh).
api -X POST torrents/recheck -d "hashes=$pack" >/dev/null
pack_state() {
    api "torrents/info?hashes=$pack" | python3 -c '
import json, sys
t = json.load(sys.stdin)[0]
print(t["state"], t["progress"], t["save_path"].rstrip("/"))'
}
for _ in $(seq 1 60); do
    read -r st prog _ < <(pack_state)
    case "$st" in checking*) ;; *) [ "$prog" = 1 ] && break ;; esac
    sleep 0.5
done
if [ "$prog" = 1 ]; then pass "the pack is at 100 % of its two files"; else fail "the pack reads $prog ($st) after its check"; fi
api -X POST torrents/start -d "hashes=$pack" >/dev/null
# DECISIONS C19 T1 (#134): Disc 1's tick is half; x on it (the same as a click on its tick)
# unticks all six, and x again brings back parts 0 and 1 only, never the whole folder.
pack_prios() {
    api "torrents/files?hash=$pack" | python3 -c '
import json, sys
print("".join(str(f["priority"]) for f in sorted(json.load(sys.stdin), key=lambda f: f["index"])))'
}
expect_prios() {
    local what="$1" want="$2" got=""
    for _ in $(seq 1 20); do
        got="$(pack_prios 2>/dev/null || true)"
        [ "$got" = "$want" ] && { pass "$what ($got)"; return 0; }
        sleep 0.5
    done
    fail "$what (the priorities read ${got:-nothing}, wanted $want)"
}
expect_prios "parts 0 and 1 wanted, 2–5 skipped" 110000
table_on "Big Collection 1"
keys l 2
keys j x
expect_prios "x on the half-ticked Disc 1 unticks it" 000000
shot "The Files tab: Disc 1 unticked by one x on its half tick." files
keys x
expect_prios "x again brings back parts 0 and 1 only" 110000
# The details back on the story and hidden, as the steps after this one expect them.
keys 1 i
keys Escape
table_on "Big Collection 1"
keys m
expect_text "the Move… sheet is open" "Move Big Collection 1" 10
keys ctrl+a -t /tmp/fast/partial
expect_text "it checks the room on /tmp" "Room" 20
shot "The Move… sheet for the partial pack: what it holds on disk, room on /tmp." sheet
keys Return
expect_at "Big Collection 1 saves at /tmp/fast/partial" "Big Collection 1" /tmp/fast/partial 60
expect_disk "part 0 is there" -f "/tmp/fast/partial/Big Collection 1/Disc 1/part 0.bin"
expect_disk "part 1 is there" -f "/tmp/fast/partial/Big Collection 1/Disc 1/part 1.bin"
for n in 2 3 4 5; do
    if in_box test -e "/tmp/fast/partial/Big Collection 1/Disc 1/part $n.bin"; then
        fail "part $n, never wanted, appeared at the destination"
    fi
done
pass "parts 2–5 are nowhere at the destination"
expect_disk "the source's part 0 is gone" ! "$LIB/packs/Big Collection 1/Disc 1/part 0.bin"
read -r st prog _ < <(pack_state)
if [ "$prog" = 1 ] && [ "$st" != missingFiles ] && [ "$st" != error ]; then
    pass "still 100 %, $st, no missing files"
else
    fail "after the move it reads $prog ($st)"
fi
table_on "Big Collection 1"
keys Escape
fi

# ---- move back after the summary closes (board 45 D7–D8, DECISIONS C18, #133) ----------------

if [ "$FROM" = games ] || [ "$FROM" = selection ] || [ "$FROM" = sheet ] || [ "$FROM" = history ]; then
SEL="$BOX_HOME/sel"
# Each film's own folder: on a full run the places step has left one of them elsewhere.
declare -A hist_from
for n in 020 021 022 023; do
    hist_from[$n]="$(save_of "Short Film $n")"
done

step "Four films moved, then three of them again" "space on Short Film 020–023, m, ~/sel/hist, move 4 torrents: a quick move, so no summary is left to close; move back now lives in the history. Then 020–022 again, to ~/sel/hist2. The log (~) has a line for each move, with move back…."
for n in 020 021 022 023; do
    table_on "Short Film $n"
    keys space
done
keys m
expect_text "the sheet is for the four" "Move 4 torrents" 10
keys ctrl+a -t "$SEL/hist"
expect_text "one green line" "access are fine" 20
press "move 4 torrents"
for n in 020 021 022 023; do
    expect_at "Short Film $n is at ~/sel/hist" "Short Film $n" "$SEL/hist"
done
table_on "Short Film 023"
keys Escape
for n in 020 021 022; do
    table_on "Short Film $n"
    keys space
done
keys m
expect_text "the sheet is for the three" "Move 3 torrents" 10
keys ctrl+a -t "$SEL/hist2"
expect_text "one green line" "access are fine" 20
press "move 3 torrents"
for n in 020 021 022; do
    expect_at "Short Film $n is at ~/sel/hist2" "Short Film $n" "$SEL/hist2"
done
table_on "Short Film 022"
keys Escape
keys asciitilde
expect_text "the log has the first move's line" "omaseed moved 4 torrents to $SEL/hist" 20
expect_text "and the second's" "omaseed moved 3 torrents to $SEL/hist2" 5
shot "The log: a line for each move, with move back… at its end." log
keys asciitilde

step "The first move back, from a torrent's details" "Short Film 023's story shows its last move (B1). move back…: the sheet for the four; 3 were moved again since, by the move to ~/sel/hist2, with move that one back first; the button reads move 1 back · 3 stay. move that one back first opens the second move's sheet: move 3 back, and they land on ~/sel/hist. Short Film 023's move back… again: move 4 back, whole again; they all land where they were."
table_on "Short Film 023"
find_text "Moved today" || keys i
expect_text "the story shows its last move" "Moved today" 20
expect_text "from where it was" "from ${hist_from[023]} to $SEL/hist" 5
shot "Short Film 023's details: its last move, move back…, this one only, show the move." details
press "move back…"
expect_text "the three moved since are named" "3 were moved again since, by the move to $SEL/hist2" 30
expect_text "the button counts what stays" "move 1 back · 3 stay" 5
shot "The first move's way back: 1 goes, 3 stay, named with the move that took them; move that one back first." sheet
press "move that one back first"
expect_text "the second move's sheet" "move 3 back" 30
press "move 3 back"
for n in 020 021 022; do
    expect_at "Short Film $n is back at ~/sel/hist" "Short Film $n" "$SEL/hist"
done
table_on "Short Film 023"
expect_text "023's last move is still offered" "Moved today" 20
press "move back…"
expect_text "the first move is whole again" "move 4 back" 30
no_text "nothing stays now" "were moved again since"
shot "The first move's way back, whole again: move 4 back." whole
press "move 4 back"
for n in 020 021 022 023; do
    expect_at "Short Film $n is back where it was" "Short Film $n" "${hist_from[$n]}"
done
expect_count "none left on ~/sel/hist" count_at "$SEL/hist" -eq 0 5
table_on "Short Film 023"
keys Escape
fi

# ---- one big torrent, its neighbours, a leftover (#162, #164, #161) -------------------------

if [ "$FROM" = games ] || [ "$FROM" = selection ] || [ "$FROM" = sheet ] || [ "$FROM" = history ] ||
    [ "$FROM" = big ]; then
BIG="$LIB/big"
FULL=$((2048 * 1024 * 1024))

# throttle on|off: stop and continue the box's qBittorrent (SIGSTOP 0.8 s, SIGCONT 0.02 s),
# so a 2 GiB copy to the tmpfs lasts about 15 s instead of a fraction of one. Its file at the
# destination is at full length from the start meanwhile, as on any drive
# (scripts/dev/research/162-preallocated-move).
throttle() {
    if [ "$1" = on ]; then
        local pid
        pid="$(omabox run -b "$BOX" -- pgrep -x qbittorrent-nox)"
        omabox run -b "$BOX" -d -- sh -c \
            "while kill -STOP $pid; do sleep 0.8; kill -CONT $pid; sleep 0.02; done" >/dev/null 2>&1
    else
        in_box pkill -f "while kill -STOP" || true
        in_box pkill -CONT -x qbittorrent-nox || true
    fi
}

# hash_at "WHAT" HASH PATH [SECONDS]: wait until HASH saves at PATH, moved.
hash_at() {
    local what="$1" hash="$2" path="$3" limit="${4:-30}" got=""
    for _ in $(seq 1 "$limit"); do
        got="$(api "torrents/info?hashes=$hash" | python3 -c '
import json, sys
t = json.load(sys.stdin)[0]
print(t["save_path"].rstrip("/") + ("" if t["state"] != "moving" else " (moving)"))' 2>/dev/null || true)"
        [ "$got" = "$path" ] && { pass "$what"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s it saves at ${got:-nothing}, not $path)"
}

# expect_read "WHAT" PREFIX TEXT [SECONDS]: wait until the window's node whose name holds PREFIX
# reads TEXT on screen. The tree cuts names at 80 characters, so the node's region is shot and
# read with tesseract (whitespace folded, case aside); without tesseract only PREFIX is checked.
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
OUT_READ="$(mktemp --suffix=.png)"

# The big one (2 GiB, one file), three small neighbours in its save path, one more to add
# while it moves, and a twin saved below them whose one file has Big Neighbour 3's name. Each
# torrent's info hash comes back as SEED=HASH, into H.
declare -A H=()
while IFS='=' read -r seed hash; do H[$seed]=$hash; done < <(python3 - "$home" <<'PY'
import hashlib, os, sys
home = sys.argv[1]
lib = os.path.join(home, "Torrents/library/big")
def bencode(x):
    if isinstance(x, int):
        return b"i%de" % x
    if isinstance(x, bytes):
        return b"%d:%s" % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode())
    return b"d" + b"".join(bencode(k) + bencode(x[k]) for k in sorted(x)) + b"e"
def single(name, folder, blocks, piece, seed):
    os.makedirs(folder, exist_ok=True)
    block = hashlib.sha256(seed.encode()).digest() * (piece // 32)
    with open(os.path.join(folder, name), "wb") as f:
        for _ in range(blocks):
            f.write(block)
    info = {"name": name, "length": blocks * piece, "piece length": piece,
            "pieces": hashlib.sha1(block).digest() * blocks, "private": 1}
    out = os.path.join(home, "walk-torrents/big")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, seed + ".torrent"), "wb") as f:
        f.write(bencode({"created by": "move-walk", "info": info}))
    print("%s=%s" % (seed, hashlib.sha1(bencode(info)).hexdigest()))
single("Big Feature.bin", lib, 2048, 1 << 20, "big")
for i in (1, 2, 3):
    single("Big Neighbour %d.bin" % i, lib, i, 16384, "nb%d" % i)
single("Big Late.bin", lib, 1, 16384, "late")
single("Big Neighbour 3.bin", os.path.join(lib, "twin"), 5, 16384, "twin")
PY
)
add_big() { # add_big SEED SAVE_PATH
    api torrents/add -F "torrents=@$BOX_HOME/walk-torrents/big/$1.torrent" -F "savepath=$2" \
        -F skip_checking=true -F seedMode=true >/dev/null
}
for seed in big nb1 nb2 nb3; do add_big "$seed" "$BIG"; done
add_big twin "$BIG/twin"
expect_count "the big folder holds its 4" count_at "$BIG" -eq 4 60
expect_count "the twin is below it" count_at "$BIG/twin" -eq 1 30
# Its own name in the table; its file keeps Big Neighbour 3's.
api torrents/rename -d "hash=${H[twin]}" --data-urlencode "name=Twin of Neighbour 3" >/dev/null
for _ in $(seq 1 30); do
    [ "$(save_of "Twin of Neighbour 3" 2>/dev/null)" = "$BIG/twin" ] && break
    sleep 1
done
if [ "$(save_of "Twin of Neighbour 3" 2>/dev/null)" = "$BIG/twin" ]; then pass "the twin has its name"; else fail "the twin was not renamed"; fi

step "One big torrent's move" "Big Feature.bin (2 GiB, one file) shares its save path with three neighbours. The cursor on it, m, /tmp/fast/big, enter; the box's qBittorrent is slowed so the copy lasts. Its file at /tmp/fast/big is at full length from the first second, so the band says how long it has been moving and that qBittorrent doesn't report a single move's progress, never 100 % measured at the destination (#162). Big Late.bin is added to the same folder meanwhile. The summary counts that one, not the three that were there before (#164)."
table_on "Big Feature.bin"
keys m
expect_text "the Move… sheet is open" "Move Big Feature.bin" 10
keys ctrl+a -t /tmp/fast/big
expect_text "one green line" "access are fine" 20
throttle on
keys Return
expect_text "the band shows the move" "Moving Big Feature.bin" 30
size="$(omabox run -b "$BOX" -- stat -c %s "/tmp/fast/big/Big Feature.bin" 2>/dev/null || true)"
if [ "$size" = "$FULL" ]; then
    pass "its file at /tmp/fast/big is at full length while it moves ($size)"
else
    fail "its file at /tmp/fast/big is ${size:-missing}, not $FULL: this qBittorrent no longer makes it at full length first (scripts/dev/research/162-preallocated-move)"
fi
expect_text "the band says how long it has moved" "moving for" 15
expect_text "the band says qBittorrent tells no progress" "doesn't report a single move's progress" 5
no_text "no figure measured at the destination" "measured at"
shot "The band of one big torrent: its size, moving for …, qBittorrent doesn't report a single move's progress; no bar stuck at 100 %." band
add_big late "$BIG"
hash_at "Big Late.bin is added to the big folder" "${H[late]}" "$BIG" 60
hash_at "Big Feature.bin lands at /tmp/fast/big" "${H[big]}" /tmp/fast/big 180
throttle off
expect_disk "its file is there" -f "/tmp/fast/big/Big Feature.bin"
# The tree cuts long names short: the folder is in the shot.
expect_text "the summary counts the one added" "1 added during the move is still at" 30
no_text "not the neighbours that were there before" "4 added during the move"
shot "The summary: 1 of 1, and 1 added during the move is still at the big folder (the three neighbours were there before)." summary
press close

step "A leftover at the destination" "An empty Big Neighbour 1.bin is at /tmp/fast/big, as an earlier failed copy leaves one. Big Neighbour 1.bin, m, /tmp/fast/big: 1 left out, and the line says it is an empty file no torrent owns (#161). esc. Then Big Neighbour 3.bin moves there, and Twin of Neighbour 3 (saved below, its file has the same name) can't: the line says the file there is Big Neighbour 3.bin's."
in_box sh -c ': >"/tmp/fast/big/Big Neighbour 1.bin"'
table_on "Big Neighbour 1.bin"
keys m
expect_text "the Move… sheet is open" "Move Big Neighbour 1.bin" 10
keys ctrl+a -t /tmp/fast/big
expect_text "one is left out" "1 left out" 20
expect_read "it is an empty file no torrent owns" "1 left out" "(an empty file no torrent owns)" 10
shot "The Move… sheet: 1 left out, (an empty file no torrent owns)." empty
# Typing opened the places list: esc closes it, esc again the sheet.
keys Escape
keys Escape
no_text "the sheet is closed" "Move Big Neighbour 1.bin"
hash_at "Big Neighbour 1.bin stayed" "${H[nb1]}" "$BIG" 1
if [ "$(omabox run -b "$BOX" -- stat -c %s "/tmp/fast/big/Big Neighbour 1.bin")" = 0 ]; then pass "the empty file is untouched"; else fail "the empty file changed"; fi

table_on "Big Neighbour 3.bin"
keys m
expect_text "the Move… sheet is open" "Move Big Neighbour 3.bin" 10
keys ctrl+a -t /tmp/fast/big
expect_text "one green line" "access are fine" 20
keys Return
hash_at "Big Neighbour 3.bin lands at /tmp/fast/big" "${H[nb3]}" /tmp/fast/big 30
keys Escape
table_on "Twin of Neighbour 3"
keys m
expect_text "the Move… sheet is open" "Move Twin of Neighbour 3" 10
keys ctrl+a -t /tmp/fast/big
expect_text "the twin is left out" "1 left out" 20
expect_read "the file there is Big Neighbour 3.bin's" "1 left out" "that Big Neighbour 3.bin owns)" 10
shot "The Move… sheet for the twin: 1 left out, (a file of 80 KB that Big Neighbour 3.bin owns)." owned
keys Escape
keys Escape
no_text "the sheet is closed" "Move Twin of Neighbour 3"
hash_at "the twin stayed below" "${H[twin]}" "$BIG/twin" 1

step "Replace the leftover" "The same sheet again: the line's replace it (DECISIONS C28, #176) deletes the empty file, then moves Big Neighbour 1.bin there; its whole 16 KB lands, and the log's line for the move says it deleted the empty file no torrent owned."
table_on "Big Neighbour 1.bin"
keys m
expect_text "the Move… sheet is open" "Move Big Neighbour 1.bin" 10
keys ctrl+a -t /tmp/fast/big
expect_text "one is left out" "1 left out" 20
expect_tree "the line offers replace it" 'link "replace it"' 10
# Typing opened the places list: esc closes it, so the line's link is in reach.
keys Escape
shot "The Move… sheet: 1 left out, (an empty file no torrent owns), replace it at the line's end and its note under it." replace
press "replace it"
hash_at "Big Neighbour 1.bin lands at /tmp/fast/big" "${H[nb1]}" /tmp/fast/big 30
no_text "the sheet is closed" "Move Big Neighbour 1.bin"
for _ in $(seq 1 30); do
    [ "$(omabox run -b "$BOX" -- stat -c %s "/tmp/fast/big/Big Neighbour 1.bin")" = 16384 ] && break
    sleep 1
done
if [ "$(omabox run -b "$BOX" -- stat -c %s "/tmp/fast/big/Big Neighbour 1.bin")" = 16384 ]; then pass "its whole file is there, not the empty one"; else fail "/tmp/fast/big/Big Neighbour 1.bin is not its 16384 bytes"; fi
keys Escape
keys asciitilde
# qBittorrent's own lines bury it: the log's filter finds it.
keys slash -t "after deleting" Return
# The line itself is in the tree only while it has the cursor; its "move back…" link always
# is, named after it and cut at 80 characters: its start is enough, the shot has the rest.
expect_text "the log's line says what was deleted" "move back: omaseed moved \"Big Neighbour 1.bin\" to /tmp/fast/big after deleting" 20
shot "The log, filtered: omaseed moved \"Big Neighbour 1.bin\" to /tmp/fast/big after deleting the empty file no torrent owned at /tmp/fast/big/Big Neighbour 1.bin." log
keys Escape
keys asciitilde

rm -f "$OUT_READ"
# Gone again, files and all, so the rail's folders are as the later steps expect (a click on
# fast/games needs it on screen) and the tmpfs has its 2 GiB back.
api torrents/delete -d "hashes=$(IFS='|'; echo "${H[*]}")" -d deleteFiles=true >/dev/null
in_box rm -rf /tmp/fast/big "$BIG"
expect_count "the big step's torrents are gone" count_under "$BIG" -eq 0 30
expect_count "and none is left at /tmp/fast/big" count_at /tmp/fast/big -eq 0 5
fi

# ---- music: a folder qBittorrent can't write, then a full one ------------------------------

if [ "$FROM" != remote ]; then

# state_of PREFIX: qBittorrent's state for the torrent whose name starts with PREFIX.
state_of() {
    api torrents/info | python3 -c '
import json, sys
print(next(t["state"] for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1])))' "$1"
}
# expect_state "WHAT" PREFIX REGEX [SECONDS]: wait until PREFIX's state matches REGEX.
expect_state() {
    local what="$1" prefix="$2" want="$3" limit="${4:-30}" got=""
    for _ in $(seq 1 "$limit"); do
        got="$(state_of "$prefix" 2>/dev/null || true)"
        [[ "$got" =~ ^($want)$ ]] && { pass "$what ($got)"; return 0; }
        sleep 1
    done
    fail "$what (after ${limit}s its state is ${got:-nothing}, wanted $want)"
}
RUNNING="uploading|stalledUP|queuedUP|forcedUP"

step "Recheck after a move: only what was running starts, never a hybrid" "The recheck folder: Recheck One, and below it in locked Recheck Two (seeding), Recheck Three (stopped first) and Recheck Four (a hybrid, v1+v2). shift+m, ~/rk, where ~/rk/locked exists read-only: One, the smallest, moves; the other three can't. The summary's recheck the 3…: Two's row says starts if complete, Three's stays stopped (DECISIONS C20, #138); Four is listed apart, not rechecked, with why, and offers move again and move back (C21, #144). ~/rk/locked made writable, move again: Four lands whole in ~/rk/locked. recheck 2: both come out complete; Two starts, Three stays stopped, Four is never rechecked."
# A walk started here (OMASEED_MOVE_WALK_FROM=music) comes straight after the adds, while
# qBittorrent may still be checking them (one at a time, a few minutes for the 284): a torrent
# stopped mid-check would stay incomplete.
checked=""
for _ in $(seq 1 600); do
    checked="$(api torrents/info | python3 -c '
import json, sys
rk = [t for t in json.load(sys.stdin) if t["name"].startswith("Recheck ")]
print(len(rk) == 4 and all(t["progress"] == 1 and not t["state"].startswith("checking") for t in rk))' 2>/dev/null || true)"
    [ "$checked" = True ] && break
    sleep 1
done
if [ "$checked" = True ]; then
    pass "the four Recheck torrents are complete"
else
    fail "the Recheck torrents are not complete after their adds"
fi
three="$(api torrents/info | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if t["name"] == "Recheck Three"))')"
api -X POST torrents/stop -d "hashes=$three" >/dev/null
expect_state "Recheck Three is stopped before the move" "Recheck Three" "stoppedUP" 20
expect_state "Recheck Two is seeding" "Recheck Two" "$RUNNING" 20
if [ "$(api torrents/info | python3 -c '
import json, sys
t = next(t for t in json.load(sys.stdin) if t["name"] == "Recheck Four")
print(bool(t.get("infohash_v1")) and bool(t.get("infohash_v2")))')" = True ]; then
    pass "qBittorrent gives Recheck Four both a v1 and a v2 hash: a hybrid"
else
    fail "Recheck Four is not a hybrid in qBittorrent"
fi
mkdir -p "$home/rk/locked"
chmod 0555 "$home/rk/locked"
open_move "Recheck One" "$BOX_HOME/rk" 4
keys Return
expect_at "Recheck One, sent first, moved" "Recheck One" "$BOX_HOME/rk" 60
expect_text "the summary offers to recheck the three that failed" "recheck the 3…" 120
shot "The summary: 1 moved · 3 failed, with recheck the 3… and retry the 3." summary
press "recheck the 3…" top
expect_text "Recheck Two's row: it starts if complete" "Recheck Two, 9 KB, starts if complete" 10
expect_text "Recheck Three's row: it stays stopped" "Recheck Three, 9 KB, stays stopped" 5
expect_text "Recheck Four's row: not rechecked, and why" "Recheck Four, 9 KB, not rechecked: Its files may be split" 5
expect_text "the sheet rechecks two" "Recheck 2 torrents" 5
expect_tree "Recheck Four offers move again" 'button "move Recheck Four again"' 5
expect_tree "Recheck Four offers move back" 'button "move Recheck Four back"' 5
shot "The recheck sheet: Recheck Two, starts if complete; Recheck Three, stays stopped; apart, Recheck Four, not rechecked, why, move again and move back." sheet
chmod 0755 "$home/rk/locked"
press "move Recheck Four again"
expect_at "Recheck Four, moved again, saves in ~/rk/locked" "Recheck Four" "$BOX_HOME/rk/locked" 60
four_size="$(stat -c %s "$home/rk/locked/Recheck Four/data.bin" 2>/dev/null || echo 0)"
if [ "$four_size" = 9700 ] && [ ! -e "$home/Torrents/library/recheck/locked/Recheck Four" ]; then
    pass "Recheck Four is whole in ~/rk/locked, nothing left behind"
else
    fail "Recheck Four's data.bin in ~/rk/locked is $four_size bytes, or its folder is still in recheck/locked"
fi
expect_text "its row says it was sent" "Recheck Four, 9 KB, moving again" 10
shot "The recheck sheet after move again: Recheck Four, moving again; recheck 2 still offered." again
press "recheck 2"
# Recheck Four is never checked: watched while the two are rechecked.
four_checked=""
watch_four() {
    local got
    got="$(state_of "Recheck Four" 2>/dev/null || true)"
    case "$got" in checking* | error | missingFiles) four_checked="$got" ;; esac
}
for _ in $(seq 1 90); do
    watch_four
    [[ "$(state_of "Recheck Two" 2>/dev/null || true)" =~ ^($RUNNING)$ ]] && break
    sleep 1
done
expect_state "Recheck Two, rechecked complete, runs again" "Recheck Two" "$RUNNING" 5
# Three is rechecked after Two (smallest first), and the app waits a few polls on each.
for _ in $(seq 1 20); do
    watch_four
    got="$(state_of "Recheck Three" 2>/dev/null || true)"
    [[ "$got" =~ ^($RUNNING)$ ]] && fail "Recheck Three started after its recheck ($got)"
    sleep 1
done
pass "Recheck Three stayed stopped through its recheck ($(state_of "Recheck Three"))"
[ -z "$four_checked" ] || fail "Recheck Four was $four_checked during the recheck"
expect_state "Recheck Four was never rechecked, and seeds" "Recheck Four" "$RUNNING" 5
if [ "$(api torrents/info | python3 -c '
import json, sys
print(all(t["progress"] == 1 for t in json.load(sys.stdin) if t["name"].startswith("Recheck ")))')" = True ]; then
    pass "all four complete"
else
    fail "a Recheck torrent is not complete after its recheck"
fi
shot "After the recheck: Recheck Two seeding, Recheck Three stopped, Recheck Four seeding in ~/rk/locked." after
# Move again took the summary's place, as retry does: a band it left is closed.
if omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qF 'link "close"'; then press close top; fi

step "A folder qBittorrent can't write" "The box's ~/locked/music exists, read-only. The sheet warns (yellow, not a block); enter sends the smallest alone; qBittorrent answers 200 and logs \"Permission denied\"; the job stops there: \"qBittorrent can't write to …\"."
mkdir -p "$home/locked/music"
chmod 0555 "$home/locked/music" "$home/locked"
open_move "Album 01" "$BOX_HOME/locked/music" "$MUSIC"
expect_text "the sheet says it isn't writable" "Not writable" 10
shot "The sheet's yellow line: not writable here." sheet
keys Return
expect_text "the band says qBittorrent can't write there" "can't write to $BOX_HOME/locked/music" 60
expect_count "no music torrent left" count_under "$LIB/music" -eq "$MUSIC" 5
shot "The red band: qBittorrent can't write to ~/locked/music; change the folder…, end the move." refused
press "end the move"
expect_text "the move ended" "ended early" 30

step "A destination that fills up" "The box's qBittorrent starts again with a 4 MiB tmpfs at ~/small that only it sees, so omaseed's preflight finds room and qBittorrent runs out: the job pauses itself, \"… is full\", every torrent whole on one side."
box_qbt_restart unshare
expect_text "omaseed is back on the box's qBittorrent" "Album 01" 60
open_move "Album 01" "$BOX_HOME/small/music" "$MUSIC"
keys Return
expect_text "the band says the destination is full" "$BOX_HOME/small/music is full" 120
moved="$(count_under "$BOX_HOME/small/music")"
left="$(count_under "$LIB/music")"
if [ "$moved" -ge 1 ] && [ $((moved + left)) = "$MUSIC" ]; then
    pass "$moved on ~/small, $left still in music: all $MUSIC whole on one side"
else
    fail "after the full disk: $moved on ~/small, $left in music (of $MUSIC)"
fi
if [ -z "$(bad_states)" ]; then pass "no torrent in error or missing files"; else fail "in error: $(bad_states)"; fi
shot "The paused band: the drive is full, the move is paused." full

# The rail's fast/games exists only once the games step moved it there.
if [ "$FROM" = games ]; then
step "A folder picked in the rail" "A click on fast/games in the rail's folders group: the table narrows to it, the title bar's title is games and its sentence the folder's facts (N torrents · bytes on disk · its path, cut short at the end when the bar is short), and \"move this folder…\" is a title-bar tool (C24 H5). A right-click on the row opens its menu, \"move this folder…\" (C19 T3); esc closes it."
where="$(omabox tree -b "$BOX" --window omaseed --find "fast/games" 2>/dev/null | python3 -c '
import re, sys
for line in sys.stdin:
    m = re.search(r"list item \"[^\"]*fast/games\"[^@>]*@(-?\d+),(-?\d+) (\d+)x(\d+)", line)
    if m:
        x, y, w, h = map(int, m.groups())
        print(x + w // 2, y + h // 2)
        break')"
[ -n "$where" ] || fail "no fast/games row in the rail"
# shellcheck disable=SC2086 # two numbers
omabox click -b "$BOX" --window omaseed --wait $where >/dev/null 2>&1 || true
expect_text "the title bar offers to move it" "move this folder…" 10
shot "The title bar: games, its sentence on one line, the move this folder… tool whole before the filter."
# shellcheck disable=SC2086 # two numbers
omabox click -b "$BOX" --window omaseed --wait $where right >/dev/null 2>&1 || true
expect_tree "a right-click on the folder row opens its menu" 'menu item "move this folder…"' 10
shot "The folder row's menu: move this folder…." menu
keys Escape
fi
fi

# ---- a server whose disks omaseed can't see (board 43 L13, #102) ---------------------------

step "A server omaseed can't see" "A second box, its omaseed on the playground (:8081, a qBittorrent on this machine whose folders the box can't see: no path map, nothing local to check). The playground's downloads/remote-walk already holds a folder called photo-archive-2019-2025. Public Domain Sampler and photo-archive-2019-2025 selected, m, that folder: the sheet says what was checked on the server and what can't be, and leaves photo-archive out from the server's own listing. move 1 torrent: Public Domain Sampler moves, the other stays, the folder there is untouched. Then the playground is put back."
[ "$(curl -s "$PLAYGROUND/app/version")" != "" ] || fail "no playground on :8081: scripts/dev/test-server.sh start"
read -r REMOTE_HASH REMOTE_FROM < <(curl -s "$PLAYGROUND/torrents/info" | python3 -c '
import json, sys
t = next(t for t in json.load(sys.stdin) if t["name"] == "Public Domain Sampler")
print(t["hash"], t["save_path"].rstrip("/"))')
photo_from="$(curl -s "$PLAYGROUND/torrents/info" | python3 -c '
import json, sys
print(next(t["save_path"] for t in json.load(sys.stdin) if t["name"] == "photo-archive-2019-2025").rstrip("/"))')"
REMOTE_DEST="$REMOTE_FROM/remote-walk"
mkdir -p "$REMOTE_DEST/photo-archive-2019-2025"
echo planted >"$REMOTE_DEST/photo-archive-2019-2025/planted.txt"
REMOTE_BOX="$MAIN_BOX-remote"
"$APP_BOX" up -b "$REMOTE_BOX" >/dev/null
BOX="$REMOTE_BOX"
expect_text "omaseed is on the playground" "Public Domain Sampler" 60
table_on "Public Domain Sampler"
keys space
table_on "photo-archive"
keys space
keys m
expect_text "the sheet is for the two" "Move 2 torrents" 10
keys ctrl+a -t "$REMOTE_DEST"
expect_text "the remote line says what was checked where" "can't see" 20
expect_text "the server's listing leaves photo-archive out" "1 left out" 30
no_text "nothing claims room read here" "read on this machine"
no_text "nothing claims a local write check" "is writable by the owner"
shot "The Move… sheet on a server omaseed can't see: the remote line, room and writing can't tell, 1 left out from the server's listing." sheet
press "move 1 torrent"
# The note stays 6 s: read it first.
expect_text "the strip says it moved" "Public Domain Sampler moved to" 20
shot "The strip on the playground: Public Domain Sampler moved to …/remote-walk." note
expect_at "Public Domain Sampler is in remote-walk" "Public Domain Sampler" "$REMOTE_DEST"
expect_at "photo-archive-2019-2025 stayed" "photo-archive-2019-2025" "$photo_from" 1
if [ -d "$REMOTE_DEST/Public Domain Sampler" ]; then pass "its files are there, on the playground's disk"; else fail "no $REMOTE_DEST/Public Domain Sampler"; fi
if [ "$(ls -A "$REMOTE_DEST/photo-archive-2019-2025")" = planted.txt ]; then pass "the planted folder holds only its own file"; else fail "the planted folder changed: $(ls -A "$REMOTE_DEST/photo-archive-2019-2025")"; fi
put_back_remote
if [ "$(save_of "Public Domain Sampler")" = "$REMOTE_FROM" ] && [ ! -e "$REMOTE_DEST" ]; then pass "the playground is as it was"; else fail "the playground was not put back"; fi

echo
echo "all $CHECKS checks passed"
if [ "$INTERACTIVE" = 0 ]; then
    printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
    echo "shots and checks: $OUT/index.md"
fi
