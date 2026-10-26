#!/usr/bin/env bash
# omaseed — the error walkthrough (docs/28, #54): a torrent goes into error while the app
# runs, and the app says why, from qBittorrent's log. Its row shows the short cause, the story
# (DECISIONS C24 H6) opens with the error block, and one notice carries the reason. Stopped, from the API and
# then with `p`, the row reads "stopped" in red (DECISIONS C20, #142); then it is moved to a
# writable folder and `e` (retry) brings it back. Then (docs/32, #55) a move out of a
# folder that can't be written fails: the story's yellow block, the footer's note when the app
# asked for it, and the block gone once the move lands. Last, a torrent whose files are gone
# after a restart shows the missing-files block.
#
#   scripts/dev/error-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                              # OUT_DIR/index.md says what to look for in each
#                                              # and lists the checks
#   scripts/dev/error-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                              # step sets the state up, says what to check, and
#                                              # waits for enter
#
# Breaks come from scripts/dev/swarm.sh (break error: a folder with no write permission;
# break missing: a finished torrent's file deleted and the playground restarted), and
# `swarm.sh fix` undoes them at the end (also on ^C). What the app sent is read from the
# box's session bus (dbus-monitor), what the shell drew from its notification state files.
# The box's network reaches :8081 alone.
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
WRITABLE="$DOWNLOADS/swarm-writable"
MOVED="$DOWNLOADS/swarm-moved"
APP_ID=io.github.diogochaves.omaseed
BOX=error-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-error-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

BROKEN="unwritable-fixture.bin"
MISSING="tiny-notes.txt"

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
    echo "  (waited ${limit}s: \"$name\" is \"$state\", not /$pattern/)" >&2
    return 1
}

hash_of() {
    curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if sys.argv[1] in t["name"]))' "$1"
}

window_shown() { omabox hyprctl -b "$BOX" -j clients | grep -q "\"class\": \"$APP_ID\""; }

# launch: (re)start the app in the box and wait for its window and first sync.
launch() {
    omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
    sleep 0.5
    # Qt's output in the run log (omabox#26), where app_back looks for "server back".
    omabox run -b "$BOX" --pass OMASEED_DISPLAY -d -- env QT_FORCE_STDERR_LOGGING=1 ./build/omaseed >/dev/null
    for _ in $(seq 1 40); do
        window_shown && break
        sleep 0.25
    done
    sleep 2.5
}

# show NAME: put the table's cursor on NAME (the jump palette). The story is the tab a fresh
# app opens on; the digits would pick rail rows from the table.
show() {
    omabox keys -b "$BOX" --window "$APP_ID" ctrl+k -t "$1" Return >/dev/null
    sleep 1.5
}

# errored_notices: the "… stopped with an error" notices sent so far, one JSON line each.
errored_notices() {
    python3 - "$home/bus.log" <<'PY'
import json, re, sys
block = []
def flush():
    if block and block[0].startswith("method call") and "member=Notify" in block[0]:
        strings = [re.match(r'   string "(.*)"$', l).group(1) for l in block[1:]
                   if l.startswith("   string ")]
        if len(strings) >= 4 and strings[2].endswith("stopped with an error"):
            print(json.dumps({"summary": strings[2], "body": strings[3]}, ensure_ascii=False))
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if line and not line.startswith(" "):
        flush(); block.clear()
    block.append(line)
flush()
PY
}

# notice_up [SECONDS]: until the shell has drawn an errored notice.
notice_up() {
    local limit="${1:-30}"
    for _ in $(seq 1 $((limit * 4))); do
        grep -rlqF -- "stopped with an error" "$home/.local/state/omarchy/notifications" 2>/dev/null &&
            return 0
        sleep 0.25
    done
    echo "  (no errored notice drawn in ${limit}s)" >&2
    return 1
}

one_notice_with_reason() {
    [ "$(errored_notices | wc -l)" = 1 ] &&
        errored_notices | grep -qF "\"body\": \"Can't write to its save folder: Permission denied, opening $BROKEN\""
}
still_one_notice() { [ "$(errored_notices | wc -l)" = 1 ]; }
# app_back SECONDS: until the app has logged "server back" (the playground restarted under it),
# or reads the restarted playground without having lost it: a quick restart falls inside the
# client's own retry, so no sync fails and there is nothing to come back from. Only the
# restarted server knows MISSING's files are missing, so its row saying so is the proof.
app_back() {
    for _ in $(seq 1 $(($1 * 2))); do
        omabox log -b "$BOX" run --grep "server back" 2>/dev/null | grep -q "server back" && return 0
        omabox tree -b "$BOX" --window "$APP_ID" --find "missing" >/dev/null 2>&1 && return 0
        sleep 0.5
    done
    return 1
}
broken_moves_again() { wait_state "$BROKEN" "^(downloading|stalledDL|uploading|stalledUP|forcedUP)" 60; }
# broken_stopped: until the playground's `stopped` filter lists the fixture. Its state stays
# `error` (qBittorrent ranks the error above the stop); the filter is what tells.
broken_stopped() {
    local hash
    hash="$(hash_of "$BROKEN")"
    for _ in $(seq 1 20); do
        curl -fs "$PLAY/torrents/info?filter=stopped&hashes=$hash" | grep -q "\"$hash\"" && return 0
        sleep 0.5
    done
    return 1
}
# broken_running: until the `stopped` filter no longer lists it (started again, still in error).
broken_running() {
    local hash
    hash="$(hash_of "$BROKEN")"
    for _ in $(seq 1 20); do
        curl -fs "$PLAY/torrents/info?filter=stopped&hashes=$hash" | grep -q "\"$hash\"" || return 0
        sleep 0.5
    done
    return 1
}
# failed_moves: how many failed moves of the fixture the playground's log holds.
failed_moves() {
    curl -fs "$PLAY/log/main?normal=false&info=false&warning=true&critical=true" | python3 -c '
import json, sys
print(sum(1 for e in json.load(sys.stdin) if e["message"].startswith("Failed to move torrent.")
          and "\"" + sys.argv[1] + "\"" in e["message"]))' "$BROKEN" 2>/dev/null || echo 0
}
MOVES_BEFORE=0
# moves_failed COUNT SECONDS: until the log holds COUNT more failed moves than MOVES_BEFORE.
moves_failed() {
    local want="$1" limit="$2" have=0
    for _ in $(seq 1 $((limit * 2))); do
        have=$(($(failed_moves) - MOVES_BEFORE))
        [ "$have" -ge "$want" ] && return 0
        sleep 0.5
    done
    echo "  (waited ${limit}s: $have new failed moves of $BROKEN logged, not $want)" >&2
    return 1
}
# move_from_app: `m` on the table's row (the sheet holds the failed move's destination), then
# enter, each once the window has settled.
move_from_app() {
    omabox keys -b "$BOX" --window "$APP_ID" --wait m >/dev/null || true
    omabox keys -b "$BOX" --window "$APP_ID" --wait Return >/dev/null || true
}
# moved_to FOLDER SECONDS: until the fixture's save path is FOLDER.
moved_to() {
    local folder="$1" limit="$2" path=""
    for _ in $(seq 1 $((limit * 2))); do
        path="$(curl -fs "$PLAY/torrents/info?hashes=$(hash_of "$BROKEN")" |
            python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["save_path"].rstrip("/"))' 2>/dev/null || true)"
        [ "$path" = "${folder%/}" ] && return 0
        sleep 0.5
    done
    echo "  (waited ${limit}s: the save path is \"$path\", not $folder)" >&2
    return 1
}

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
    chmod 755 "$WRITABLE" 2>/dev/null || true
    "$SWARM" fix >/dev/null 2>&1 || true
    rm -rf -- "$WRITABLE" "$MOVED"
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "readying the swarm (every break undone)"
"$SWARM" start >/dev/null
"$SWARM" fix >/dev/null
"$SWARM" seed >/dev/null
mkdir -p "$DOWNLOADS"

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Error walk %s\n\nBuild `%s`. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow "$PLAY_PORT" --ro-bind "$DOWNLOADS" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
mkdir -p "$home/.config/omaseed" && chmod 700 "$home/.config/omaseed"
printf '{"version":1,"defaultId":"playground","servers":[{"id":"playground","name":"playground","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' "$PLAY_PORT" \
    >"$home/.config/omaseed/servers.json"
chmod 600 "$home/.config/omaseed/servers.json"
# The desktop entry and icon, so the shell can resolve the notice's icon.
omabox run -b "$BOX" -- scripts/dev/install-desktop.sh >/dev/null 2>&1
# shellcheck disable=SC2016 # the box's $HOME, expanded inside the box
omabox run -b "$BOX" -d -- sh -c \
    'exec dbus-monitor --session "interface=org.freedesktop.Notifications" >"$HOME/bus.log" 2>&1' >/dev/null
launch

# ---- the walk -----------------------------------------------------------------------------
# Broken only now, so the error happens while the app watches.
"$SWARM" break error >/dev/null
wait_state "$BROKEN" "^error" 60 || true
notice_up 30 || true
step "Notice" "Top right: \"$BROKEN stopped with an error\" / \"Can't write to its save folder: Permission denied, opening $BROKEN\", in the shell's toast style."

show "$BROKEN"
step "Error block" "The row's status reads \"! no access\" (not \"error\"). The story opens with the red-ruled block: ✕ Can't write to its save folder; Permission denied, opening $BROKEN; the swarm-readonly folder; qBittorrent's log · today HH:MM; then m move… · e retry · copy the line. The fact reads \"no access\" in red over \"needs you\"; facts' Status row \"error · no access\"."

check "one errored notice, with the reason in its body" one_notice_with_reason

# DECISIONS C20 (#107, #142): an errored torrent that is stopped reads "stopped". qBittorrent
# still says `error`; only its `stopped` filter tells, and the app reads that filter, so a stop
# sent from elsewhere (here the API, as qBittorrent's Web UI would) shows the same.
curl -fs -X POST "$PLAY/torrents/stop" -d "hashes=$(hash_of "$BROKEN")" >/dev/null
check "a stop sent from the API: the server's stopped filter lists it" broken_stopped
# The app reads the filter after its next poll (1.5 s); the screen is the only place it
# shows, and `p` below is the check: it starts only a torrent the app knows is stopped.
sleep 4
step "Stopped elsewhere" "Stopped through the API, not omaseed. The row's status reads \"! stopped\", the word and the glyph in the error colour, whole at the column's default width; facts' Status row \"error · stopped · no access\", the block unchanged (m move… · e retry)."
check "the stopped errored torrent is still in error" wait_state "$BROKEN" "^error" 5

# `p` on a stopped errored torrent starts it (the retry); on one that runs, it stops it. Had
# the app missed the stop sent from the API, this `p` would have stopped it again.
omabox keys -b "$BOX" --window "$APP_ID" p >/dev/null
check "p on the row stopped elsewhere started it: the app knew it was stopped" broken_running
sleep 4 # the app reads the filter again once the start is answered, and at its next poll
# Started, it is in error again only once it writes a piece, which takes a peer: stopped
# before that, it would be plainly stopped, with no error left for `e` to try again.
check "started, it fails to write again" wait_state "$BROKEN" "^error" 60
omabox keys -b "$BOX" --window "$APP_ID" p >/dev/null
check "p on it running stopped it" broken_stopped
sleep 4
step "Stopped from omaseed" "After p, p: stopped again, from omaseed this time; the row reads \"! stopped\" in the error colour."

# The fix: somewhere it can write (as the location sheet would), then `e` on its row: retry.
mkdir -p "$WRITABLE"
curl -fs -X POST "$PLAY/torrents/setLocation" -d "hashes=$(hash_of "$BROKEN")" \
    --data-urlencode "location=$WRITABLE" >/dev/null
wait_state "$BROKEN" "^error" 10 || true
omabox keys -b "$BOX" --window "$APP_ID" e >/dev/null
check "e on the errored row retries it, and it downloads from the new folder" broken_moves_again
sleep 3
step "Tried again" "$BROKEN downloads or seeds again from swarm-writable: no block in the story, the status cell back to a plain state."
check "trying again sent no second notice" still_one_notice

# docs/32 (#55): a move that answers "OK" and fails. Once the fixture is whole, its folder
# goes read-only and it is moved (through the API: nobody watched that one), then again from
# the app with `m`, whose sheet holds the destination; then the folder is fixed and `m` moves it.
wait_state "$BROKEN" "^(uploading|stalledUP|forcedUP)" 120 || true
mkdir -p "$MOVED"
MOVES_BEFORE=$(failed_moves)
chmod 555 "$WRITABLE"
curl -fs -X POST "$PLAY/torrents/setLocation" -d "hashes=$(hash_of "$BROKEN")" \
    --data-urlencode "location=$MOVED" >/dev/null
moves_failed 1 20 || true
show "$BROKEN"
# Nobody watched this move: the app finds it at its next look at the log, every 15 s.
sleep 16
step "Move failed" "The story opens with a yellow-ruled block: ! The move to downloads/swarm-moved didn't happen; Permission denied, moving $BROKEN; Its files are still in …/swarm-writable; qBittorrent's log · today HH:MM; m set location · o open folder · x dismiss. The row still reads seeding (the table is unchanged)."
check "the move failed, and qBittorrent said so in its log only" moves_failed 1 1
move_from_app
moves_failed 2 20 || true
sleep 1.5
step "Move failed again, from the app" "The footer's note, in yellow: \"$BROKEN didn't move: Permission denied\". The block now ends \"· 2 times\"."
check "m, then enter, asked for the same destination again" moves_failed 2 1
chmod 755 "$WRITABLE"
move_from_app
check "with the folder writable, the move landed" moved_to "$MOVED" 30
sleep 2
step "Moved" "$BROKEN is in swarm-moved: no block in the story, its save path ends in swarm-moved."

"$SWARM" fix >/dev/null
rm -rf -- "$WRITABLE" "$MOVED"
"$SWARM" break missing >/dev/null
wait_state "$MISSING" "^missingFiles" 60 || true
check "the app reconnects to the restarted playground by itself" app_back 40
sleep 2 # its first poll after that reads the new log
show "$MISSING"
step "Missing files" "$MISSING: the row reads \"! missing\"; the story opens with the yellow-ruled block: ! Its files aren't where qBittorrent left them; Moved, deleted or changed since qBittorrent last ran (mismatching file size); the swarm folder; qBittorrent's log · today HH:MM; m set location · c recheck · copy the line."
check "missing files at a server's start sends no notice" still_one_notice

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
