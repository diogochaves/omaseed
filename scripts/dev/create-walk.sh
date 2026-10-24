#!/usr/bin/env bash
# omaseed — the create walkthrough (#146, board 49, DECISIONS C27): a torrent made from a folder
# by the box's own qBittorrent, end to end, from folders of invented files in the box's HOME.
#
#   scripts/dev/create-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step;
#                                               # OUT_DIR/index.md says what to look for in each,
#                                               # OUT_DIR/checks.md lists the checks
#   scripts/dev/create-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                               # script still presses the keys, then says what
#                                               # to check and waits for enter
#   OMASEED_BUILD=DIR                           # the build to run (default build/)
#
# The box comes from `scripts/dev/app-box.sh up --qbt` (qbittorrent-nox on the box's
# 127.0.0.1:8090, an empty library, reaching only the swarm's tracker and seeder). Its library
# gets one private torrent of the swarm's, stopped, with an invented announce URL for an invented
# host whose path holds an invented passkey, so the private path has a tracker to offer. Then:
#   - "+ add ▾" and its rows; ctrl+n, the sheet empty;
#   - public: a folder of invented files, a tracker typed in, create and seed: the outcome strip
#     (under 3 s), seeding at 100 % from the folder's parent, the magnet on the clipboard, and the
#     .torrent read back (v1, the automatic piece size, the tracker, not private);
#   - a big sparse folder: the band while it hashes, then its green summary;
#   - private: the host from the library, the announce only as dots, a source tag typed; the
#     .torrent saved in ~/Downloads as "NAME [HOST].torrent" (private, v1, the source, the
#     passkey), not added; the passkey in no text of the window and nowhere in the app's log;
#   - the round trip: the saved file itself (a warning), the site's copy with another source tag
#     (another hash: checked there, 100 %, nothing downloaded), and, made again, the site's copy
#     with only the announce changed (the same hash: seed mode, 100 % at once);
#   - an unreadable folder and a missing one, said before anything is hashed; a cancel while it
#     hashes, nothing left on the server.
# Every file is made up here; nothing leaves the box. Any failed check makes the script exit 1.
# The box goes down at the end, also on ^C.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
TORRENT_PY="$repo/scripts/dev/torrent-file.py"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
BOX="${OMASEED_CREATE_BOX:-create-walk}"
BUILD="${OMASEED_BUILD:-build}"
QBT_PORT=8090 # app-box.sh's qBittorrent in the box
API="http://127.0.0.1:$QBT_PORT/api/v2"
SHARE=/home/sbx/share # the walk's folders, inside the box
DOWNLOADS=/home/sbx/Downloads
# The invented private tracker: a host nobody answers for, a passkey made up for this run.
HOST=tracker-a.example
KEY="walk$(date +%s)c0ffee"
ANNOUNCE="https://$HOST/announce/$KEY"
PUBLIC_TRACKER="udp://open.tracker-c.example:6969/announce"
refuse_home "$QBT_PORT"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-create-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x "$BUILD/omaseed" ] || { echo "build first: cmake --build $BUILD" >&2; exit 1; }

# api PATH [CURL ARGS]: the box's qBittorrent, from inside the box.
api() {
    local path="$1"
    shift
    omabox run -b "$BOX" -- curl -fs "$@" "$API/$path" 2>/dev/null || true
}

# torrent FIELD NAME: a field of the box's newest torrent called NAME, "" when there is none.
torrent() {
    api torrents/info | python3 -c '
import json, sys
rows = sorted((t for t in json.load(sys.stdin) or [] if t["name"] == sys.argv[2]),
              key=lambda t: t["added_on"])
print(rows[-1].get(sys.argv[1], "") if rows else "")' "$1" "$2" 2>/dev/null || true
}

# copies NAME: how many torrents of the box's are called NAME.
copies() {
    api torrents/info | python3 -c '
import json, sys
print(sum(1 for t in json.load(sys.stdin) or [] if t["name"] == sys.argv[1]))' "$1" 2>/dev/null || echo 0
}

# progress NAME: the newest NAME's progress as a whole percent.
progress() {
    local value
    value="$(torrent progress "$1")"
    [ -n "$value" ] && python3 -c 'import sys; print(round(float(sys.argv[1]) * 100))' "$value" || echo ""
}

# show FILE KEY: KEY of torrent-file.py's reading of FILE (a path in the box's HOME).
show() {
    python3 -I "$TORRENT_PY" show "$HOME_HOST${1#/home/sbx}" "${@:3}" | python3 -c '
import json, sys
value = json.load(sys.stdin)[sys.argv[1]]
print(json.dumps(value) if isinstance(value, (list, dict)) else value)' "$2"
}

# keys KEY...: into omaseed's window, once the screen has settled after them.
keys() {
    omabox keys -b "$BOX" --window omaseed "$@" >/dev/null
    omabox wait -b "$BOX" --timeout 5s still >/dev/null || true
}

# find_text TEXT: the window shows TEXT (an accessible name starting with it).
find_text() { omabox tree -b "$BOX" --window omaseed --find "$1" >/dev/null 2>&1; }

# wait_text TEXT [SECONDS]: until the window shows TEXT; 0 when it did.
wait_text() {
    for _ in $(seq 1 "${2:-15}"); do
        find_text "$1" && return 0
        sleep 1
    done
    return 1
}

# all_text: every accessible name in the window, one a line.
all_text() {
    omabox tree -b "$BOX" --window omaseed --json 2>/dev/null |
        python3 -c 'import json, sys; [print(n.get("name") or "") for n in json.load(sys.stdin)]'
}

# click_text TEXT: a click on the first element whose accessible name starts with TEXT.
click_text() {
    local at
    at="$(omabox tree -b "$BOX" --window omaseed --json 2>/dev/null | python3 -c '
import json, sys
for node in json.load(sys.stdin):
    if (node.get("name") or "").startswith(sys.argv[1]) and node.get("w", 0) > 0:
        print(node["x"] + node["w"] // 2, node["y"] + node["h"] // 2)
        break' "$1")"
    [ -n "$at" ] || return 1
    # shellcheck disable=SC2086 # "X Y"
    omabox click -b "$BOX" --window omaseed $at >/dev/null
    # The pointer away again: a tooltip would cover the next shot.
    omabox pointer -b "$BOX" -- move 5 5 >/dev/null 2>&1 || true
    omabox wait -b "$BOX" --timeout 5s still >/dev/null || true
}

# app_log: the box's run logs, omaseed's among them.
app_log() { cat "$HOME_HOST"/run-*.log 2>/dev/null || true; }

STEP=0
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
        omabox shot -b "$BOX" --window omaseed -o "$OUT/$file" >/dev/null 2>&1
        printf -- '- **%02d %s**: %s\n  ![](%s)\n' "$STEP" "$1" "$2" "$file" >>"$OUT/index.md"
        echo "$(printf '%02d' "$STEP") $1 -> $OUT/$file"
    fi
}

# check "what" ACTUAL EXPECTED: one line in the log (and checks.md), and a failure remembered.
check() {
    local mark=ok
    if [ "$2" != "$3" ]; then
        mark=FAILED
        FAILED=$((FAILED + 1))
    fi
    printf '  check %-6s %s (%s%s)\n' "$mark" "$1" "$2" "$([ "$mark" = ok ] || echo ", wanted $3")"
    if [ -n "$OUT" ]; then
        # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
        printf -- '- [%s] %s: `%s`%s\n' "$([ "$mark" = ok ] && echo x || echo ' ')" "$1" "$2" \
            "$([ "$mark" = ok ] || echo " (wanted \`$3\`)")" >>"$OUT/checks.md"
    fi
}

# shows "what" TEXT [SECONDS]: a check that the window shows TEXT.
shows() {
    check "$1" "$(wait_text "$2" "${3:-15}" && echo shown || echo missing)" shown
}

# wait_for "what" EXPECTED SECONDS COMMAND...: until COMMAND prints EXPECTED, then a check.
wait_for() {
    local what="$1" expected="$2" seconds="$3" got=""
    shift 3
    for _ in $(seq 1 "$((seconds * 2))"); do
        got="$("$@")"
        [ "$got" = "$expected" ] && break
        sleep 0.5
    done
    check "$what" "$got" "$expected"
}

# no_key_shown "where": the passkey is in no accessible text of the window (C27 CR5).
no_key_shown() {
    check "the passkey isn't in the window's text ($1)" \
        "$(all_text | grep -qF -- "$KEY" && echo shown || echo hidden)" hidden
}

# a_dialog: a sheet is up (the add sheet, the create sheet).
a_dialog() {
    omabox tree -b "$BOX" --window omaseed 2>/dev/null | grep -qE '^[[:space:]]*dialog "(Add torrent|Create a torrent)'
}

# close_sheets: esc until no sheet is up (a guarded sheet takes two, a field's text one more).
close_sheets() {
    for _ in 1 2 3 4 5; do
        a_dialog || return 0
        keys Escape
    done
}

# create_now: enter in the folder's field, which runs the sheet's main button (create and seed,
# create .torrent) without aiming at it: a desktop notice may sit over the sheet's foot.
create_now() {
    click_text "folder or file to share" || true
    keys End Return
}

# create FOLDER: ctrl+n, the folder typed in, its check read.
open_create() {
    keys ctrl+n
    keys ctrl+a -t "$1"
    sleep 1
}

# random_file PATH KIB: KIB KiB of random bytes at PATH (in the box's HOME, from here).
random_file() {
    mkdir -p "$(dirname "$HOME_HOST${1#/home/sbx}")"
    head -c "$(($2 * 1024))" /dev/urandom >"$HOME_HOST${1#/home/sbx}"
}

cleanup() {
    [ -n "${HOME_HOST:-}" ] && chmod -R u+rwX "$HOME_HOST/share" 2>/dev/null || true
    [ "$INTERACTIVE" = 1 ] || "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [ "$INTERACTIVE" = 0 ]; then
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Create walk %s\n\nBuild `%s`. Each shot: what should be on screen. Checks: checks.md.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    : >"$OUT/checks.md"
fi
echo "starting the box (omaseed on a qBittorrent of the box's own)"
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --qbt --interactive -b "$BOX" --build "$BUILD" >/dev/null
else
    "$APP_BOX" up --qbt -b "$BOX" --build "$BUILD" >/dev/null
fi
omabox pointer -b "$BOX" -- move 5 5 >/dev/null 2>&1 || true
HOME_HOST="$(omabox path -b "$BOX")/home"

# ---- the folders, all invented ------------------------------------------------------------------
for n in 1 2 3; do random_file "$SHARE/photo-walks/harbour/walk-0$n.jpg" 2048; done
random_file "$SHARE/photo-walks/notes.txt" 1
random_file "$SHARE/photo-walks/.DS_Store" 1
for n in 1 2 3 4; do random_file "$SHARE/field-recordings/0$n take.flac" 1024; done
# Sparse, so the hashing takes seconds while nothing is stored: the band's turn.
for name in big-archive big-archive-2; do
    mkdir -p "$HOME_HOST/share/$name"
    truncate -s 24G "$HOME_HOST/share/$name/disk.img"
done
random_file "$SHARE/locked/secret.txt" 1
chmod 000 "$HOME_HOST/share/locked"

# A private torrent of the swarm's, stopped (it never announces), its tracker the invented one.
PRIVATE_FILE="$(find "$HOME_HOST/Downloads" -name 'tiny-notes.txt.torrent' -print -quit)"
[ -n "$PRIVATE_FILE" ] || { echo "no tiny-notes.txt.torrent in the box: run scripts/dev/swarm.sh seed" >&2; exit 1; }
api torrents/add -F "torrents=@$DOWNLOADS/tiny-notes.txt.torrent" -F stopped=true \
    -F "savepath=/home/sbx/Torrents" >/dev/null
wait_for "the library has its private torrent" 1 15 copies tiny-notes.txt
TINY="$(torrent hash tiny-notes.txt)"
api torrents/addTrackers --data-urlencode "hash=$TINY" --data-urlencode "urls=$ANNOUNCE" >/dev/null
sleep 2

# ---- where it starts (CR1) ------------------------------------------------------------------------
click_text "add" || true
shows "+ add ▾ has create" "create a torrent from a folder…" 5
step "Add menu" "Under '+ add ▾': 'add a torrent or magnet… a', 'create a torrent from a folder… ctrl+n', a rule, 'search… ctrl+f'."
keys Escape
keys ctrl+n
shows "ctrl+n opens the sheet" "Create a torrent" 5
step "Create, empty" "The sheet 'Create a torrent': from (empty, places ▾, browse), for: Anyone picked over A private tracker, trackers (no public tracker in the library), format v1, pieces automatic, more, when done; create and seed off."
close_sheets

# ---- public (CR4, CR10–CR12, CR14) ---------------------------------------------------------------
open_create "$SHARE/photo-walks"
shows "the folder is read first" "4 files" 10
click_text "add a tracker" || true
keys -t "$PUBLIC_TRACKER" Return
step "Create, public" "The folder's line '4 files · 6.00 MB · 1 hidden file left out (.DS_Store) · read here'; the tracker as a chip; pieces 'automatic · 32 KiB, about 193 pieces'; the when-done line: seed mode at $SHARE."
create_now
shows "a quick one is the strip" "photo-walks is made and seeds from $SHARE" 20
wait_for "photo-walks seeds at 100 %" 100 20 progress photo-walks
check "from where it is" "$(torrent save_path photo-walks)" "$SHARE"
check "nothing downloaded" "$(torrent downloaded photo-walks)" 0
check "the magnet is on the clipboard" \
    "$(omabox run -b "$BOX" -- wl-paste -n 2>/dev/null | grep -c '^magnet:?xt=urn:btih:')" 1
step "Seeding" "photo-walks in the library, seeding, 100 %; the outcome strip at the bottom: 'photo-walks is made and seeds from $SHARE', '4 files, 6.00 MB · the magnet is copied'."
api "torrents/export?hash=$(torrent hash photo-walks)" -o "$DOWNLOADS/photo-walks.check.torrent" >/dev/null
check "made v1" "$(show "$DOWNLOADS/photo-walks.check.torrent" format)" v1
check "the automatic piece size (CreatePlan)" "$(show "$DOWNLOADS/photo-walks.check.torrent" piece_length)" 32768
check "the tracker typed in" "$(show "$DOWNLOADS/photo-walks.check.torrent" announce)" "$PUBLIC_TRACKER"
check "not private" "$(show "$DOWNLOADS/photo-walks.check.torrent" private)" 0
check "the hidden file left out" "$(show "$DOWNLOADS/photo-walks.check.torrent" files)" 4

# ---- the band (CR13) ------------------------------------------------------------------------------
open_create "$SHARE/big-archive"
shows "a 24 GiB folder read" "1 file" 10
create_now
shows "past 3 s, the band" "Creating big-archive" 10
sleep 1
step "Hashing in the band" "Under the title, across the content: '◇ Creating big-archive', 'hashing … of 24.0 GB on box · N % · about … left · v1 · 2 MiB pieces · then it seeds from where it is', a progress line, cancel; the rail beside it usable."
shows "then its summary" "big-archive is made and seeds from $SHARE" 90
step "Made, in the band" "The band green: '✓ big-archive is made and seeds from $SHARE', 'v1 · 2 MiB pieces · 1 file, 24.0 GB · hashed in … · the magnet is copied · keep the files there', with copy magnet, save .torrent, show, close."
click_text "close" || true

# ---- private (CR5–CR7, CR15) ----------------------------------------------------------------------
open_create "$SHARE/field-recordings"
shows "the private folder read" "4 files" 10
click_text "A private tracker" || true
# The host button opens the library's private trackers; the invented one is picked from them.
click_text "127.0.0.1 · 1 torrent in your library" || true
keys Down Return # the list: 127.0.0.1, then the invented host
shows "the host picked" "$HOST · 1 torrent in your library" 5
click_text "type one" || true
keys -t TA Return
no_key_shown "the private sheet"
step "Create, private" "For: A private tracker; tracker '$HOST · 1 torrent in your library ▾'; announce 'https://$HOST/announce/••••••••'; the facts: Private flag on, Format v1, Source TA; pieces; save .torrent '~/Downloads/field-recordings [$HOST].torrent'; the round-trip note; create .torrent."
create_now
SAVED="$DOWNLOADS/field-recordings [$HOST].torrent"
shows "saved, the band says" "field-recordings [$HOST].torrent is ready to upload" 20
check "not added" "$(copies field-recordings)" 0
check "saved in Downloads" "$([ -f "$HOME_HOST${SAVED#/home/sbx}" ] && echo yes || echo no)" yes
check "private" "$(show "$SAVED" private)" 1
check "v1" "$(show "$SAVED" format)" v1
check "the source tag typed" "$(show "$SAVED" source)" TA
check "the announce, passkey and all" "$(show "$SAVED" announce --full)" "$ANNOUNCE"
no_key_shown "the private summary"
step "Private, saved" "The band green: '✓ field-recordings [$HOST].torrent is ready to upload', 'saved in ~/Downloads · not added: $HOST doesn't know it yet · …', show the file, copy path, close; the file's chip, '… · drag to the upload form'."
click_text "close" || true

# ---- the round trip (CR8, CR9) --------------------------------------------------------------------
# add_file PATH: the add sheet on a .torrent, its parse read.
add_file() {
    keys a
    keys ctrl+a -t "$1"
    sleep 2
}
add_file "$SAVED"
shows "the made file itself" "This is the .torrent made here for $HOST" 10
step "The made .torrent added by mistake" "The add sheet for field-recordings: a yellow '! This is the .torrent made here for $HOST', '… upload it on $HOST's site, then add the site's copy'; add still offered."
close_sheets
python3 -I "$TORRENT_PY" site "$HOME_HOST${SAVED#/home/sbx}" "$HOME_HOST/Downloads/site-other.torrent" --source TA-site
add_file "$DOWNLOADS/site-other.torrent"
shows "another hash, the same files" "Made here today" 10
step "The site's copy, another hash" "The add sheet: '✓ Made here today HH:MM: the same 4 files, another hash', 'its source tag TA became TA-site · every file matches by name and size in $SHARE/field-recordings · …'; Save to $SHARE; skip hash check off."
keys Return
wait_for "checked there, then 100 %" 100 30 progress field-recordings
check "nothing downloaded" "$(torrent downloaded field-recordings)" 0
check "saved at the folder's parent" "$(torrent save_path field-recordings)" "$SHARE"
api torrents/delete -d "hashes=$(torrent hash field-recordings)" -d deleteFiles=false >/dev/null
# Made again, and the site only changes the announce: the same hash.
open_create "$SHARE/field-recordings"
shows "the private folder read again" "4 files" 10
create_now
shows "saved again" "is ready to upload" 20
click_text "close" || true
SAVED2="$DOWNLOADS/field-recordings [$HOST] (2).torrent"
python3 -I "$TORRENT_PY" site "$HOME_HOST${SAVED2#/home/sbx}" "$HOME_HOST/Downloads/site-same.torrent" \
    --announce "https://$HOST/announce/${KEY}site"
add_file "$DOWNLOADS/site-same.torrent"
shows "the same hash" "Made here today" 10
step "The site's copy, the same hash" "The add sheet: '✓ Made here today HH:MM: the same torrent', 'same info hash; the site only changed the announce · it seeds from $SHARE/field-recordings at once, nothing downloads'; Save to $SHARE; skip hash check on."
keys Return
wait_for "seed mode: 100 % at once" 100 20 progress field-recordings
check "nothing downloaded" "$(torrent downloaded field-recordings)" 0

# ---- errors (CR17) and a cancel (CR13) ------------------------------------------------------------
open_create "$SHARE/locked"
shows "an unreadable folder is said first" "qBittorrent can't read $SHARE/locked" 10
step "An unreadable folder" "Under from, in red: 'qBittorrent can't read $SHARE/locked'; create and seed off."
keys ctrl+a -t "$SHARE/nowhere"
shows "a missing one too" "$SHARE/nowhere isn't there on box" 10
close_sheets
open_create "$SHARE/big-archive-2"
shows "the second big folder read" "1 file" 10
create_now
shows "its band" "Creating big-archive-2" 10
click_text "cancel" || true
shows "a cancel is a dim strip" "big-archive-2 wasn't made: cancelled at" 10
step "Cancelled" "No band; the strip at the bottom, dim: '× big-archive-2 wasn't made: cancelled at N %', 'nothing saved, nothing added, the folder is as it was', create again…"
check "no task left on the server" "$(api torrentcreator/status)" "[]"
check "and no torrent" "$(copies big-archive-2)" 0

# ---- the passkey (CR5) ----------------------------------------------------------------------------
check "the passkey isn't in the app's log" "$(app_log | grep -cF -- "$KEY" || true)" 0

echo "the box's library at the end:"
api torrents/info | python3 -c 'import json, sys; [print("  ", t["name"], t["state"]) for t in json.load(sys.stdin)]' || true
echo
if [ "$FAILED" -gt 0 ]; then
    echo "create walk: $FAILED check(s) FAILED${OUT:+; see $OUT/checks.md}" >&2
    exit 1
fi
echo "create walk: every check passed${OUT:+; shots and notes in $OUT/index.md}"
