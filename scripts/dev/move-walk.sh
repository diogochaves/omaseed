#!/usr/bin/env bash
# omaseed — moving a folder (board 42, DECISIONS C15), walked through the app's own UI on a
# library of a few hundred small torrents: a folder with a nested save path and loose files
# moved to another filesystem, omaseed killed mid-move and started again, the loose files
# carried after the torrents; pause, end and "move the rest…"; a destination qBittorrent
# can't write; a destination that fills up. After each step it checks what qBittorrent says
# and what is on disk, and stops at the first mismatch.
# Run it again after any change to the move sheet, the job, the carry or their words.
#
#   scripts/dev/move-walk.sh [OUT_DIR]        # agents: a headless box, a shot per step,
#                                             # OUT_DIR/index.md says what each shot shows and
#                                             # lists every check
#   scripts/dev/move-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                             # step says what it will do and waits for enter
#   OMASEED_MOVE_WALK_FROM=music …            # only the last two steps (unwritable, full)
#   OMASEED_MOVE_WALK_KEEP=1 …                # a failed run leaves its box up
#
# Everything happens in a box with a qBittorrent of its own (scripts/dev/app-box.sh up --qbt),
# so no other server is involved. The library is made in the box: 160 torrents in
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
table_on() { keys ctrl+k -t "$1" Tab; keys Escape; }

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

# press NAME: click the middle of the node (a button first) whose accessible name is NAME.
press() {
    local where
    # The job's desktop notice (a pause, the end) sits over the band's right end, and a click
    # on it shows the job's torrents: wait for the toast to go (the shell keeps one JSON per
    # toast on screen), then let the window settle.
    for _ in $(seq 1 40); do
        compgen -G "$home/.local/state/omarchy/notifications/*.json" >/dev/null || break
        sleep 0.5
    done
    omabox wait -b "$BOX" --window omaseed --timeout 10s still >/dev/null 2>&1 || true
    where="$(omabox tree -b "$BOX" --window omaseed --find "$1" 2>/dev/null | python3 -c '
import re, sys
name = sys.argv[1]
found = []
for line in sys.stdin:
    m = re.search(r"(\w[\w ]*) \"" + re.escape(name) + r"\"[^@>]*@(-?\d+),(-?\d+) (\d+)x(\d+)", line)
    if m:
        x, y, w, h = map(int, m.groups()[1:])
        found.append((m.group(1).strip() != "button", x + w // 2, y + h // 2))
if found:
    print(*sorted(found)[0][1:])' "$1")"
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

# open_move QUERY DEST N: the cursor on QUERY's torrent, shift+m, DEST typed over the field;
# waits until the sheet offers to move N torrents.
open_move() {
    table_on "$1"
    keys shift+m
    expect_text "the move sheet is open" "New path of the folder" 10
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
        chmod -R u+w "$home/locked" 2>/dev/null || true
        "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
home=""
trap cleanup EXIT

# ---- setup --------------------------------------------------------------------------------

# A read-only folder left by an earlier run would stop app-box.sh from clearing the box.
chmod -R u+w "$HOME/.cache/omabox/$BOX/home/locked" 2>/dev/null || true

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

def make(root, name, size, seed):
    folder = os.path.join(home, "Torrents/library", root, name)
    os.makedirs(folder, exist_ok=True)
    data = hashlib.sha256(seed.encode()).digest() * (size // 32 + 1)
    data = data[:size]
    with open(os.path.join(folder, "data.bin"), "wb") as f:
        f.write(data)
    pieces = b"".join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))
    info = {"name": name, "piece length": PIECE, "pieces": pieces, "private": 1,
            "files": [{"length": size, "path": ["data.bin"]}]}
    out = os.path.join(home, "walk-torrents", root.replace("/", "-"))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".torrent"), "wb") as f:
        f.write(bencode({"info": info, "created by": "move-walk"}))

for i in range(1, games - handheld + 1):
    make("games", "Arcade Set %03d" % i, 8192 + 997 * i, "g%d" % i)
for i in range(1, handheld + 1):
    make("games/handheld", "Handheld Pack %03d" % i, 8192 + 991 * i, "h%d" % i)
for i in range(1, films + 1):
    make("films", "Short Film %03d" % i, 8192 + 983 * i, "f%d" % i)
for i in range(1, music + 1):
    make("music", "Album %02d" % i, 600 * 1024 + i, "m%d" % i)

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
PY

add_root() { # add_root DIR SAVE_PATH: every .torrent in the walk's DIR, 40 a request
    local dir="$1" save="$2" args=() file n=0
    for file in "$home/walk-torrents/$dir"/*.torrent; do
        args+=(-F "torrents=@$BOX_HOME/walk-torrents/$dir/$(basename "$file")")
        n=$((n + 1))
        if [ "$n" = 40 ]; then
            api torrents/add "${args[@]}" -F "savepath=$save" -F skip_checking=true >/dev/null
            args=()
            n=0
        fi
    done
    [ ${#args[@]} = 0 ] || api torrents/add "${args[@]}" -F "savepath=$save" -F skip_checking=true >/dev/null
}
add_root games "$LIB/games"
add_root games-handheld "$LIB/games/handheld"
add_root films "$LIB/films"
add_root music "$LIB/music"

step "The library" "$((GAMES + FILMS + MUSIC)) torrents added as complete: games ($GAMES, $HANDHELD of them in games/handheld), films ($FILMS), music ($MUSIC); six files no torrent owns beside them."
expect_count "games holds $GAMES torrents" count_under "$LIB/games" -eq "$GAMES" 60
expect_count "games/handheld holds $HANDHELD" count_at "$LIB/games/handheld" -eq "$HANDHELD"
expect_count "films holds $FILMS" count_under "$LIB/films" -eq "$FILMS"
expect_count "music holds $MUSIC" count_under "$LIB/music" -eq "$MUSIC"
expect_text "the rail lists the folders" "handheld" 30
shot "The library: the rail's folders group lists library, games with handheld under it, films and music."

# OMASEED_MOVE_WALK_FROM=music skips games and films (for working on the last two steps).
if [ "${OMASEED_MOVE_WALK_FROM:-games}" = games ]; then

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

# ---- music: a folder qBittorrent can't write, then a full one ------------------------------

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

step "A folder picked in the rail" "A click on fast/games in the rail's folders group: the table narrows to it and the folder line above it keeps \"move this folder…\" on its one line, the text cut short instead."
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
expect_text "the folder line offers to move it" "move this folder…" 10
shot "The folder line: one line, its text cut short, \"shift+m move this folder…\" whole at its end."

echo
echo "all $CHECKS checks passed"
if [ "$INTERACTIVE" = 0 ]; then
    printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
    echo "shots and checks: $OUT/index.md"
fi
