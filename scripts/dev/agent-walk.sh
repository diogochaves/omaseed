#!/usr/bin/env bash
# omaseed — the agent walk (board 62, DECISIONS C45, #163): the words an agent asks the running
# omaseed (`omaseed status|errors|log|moves [--json]`), run in a box against the playgrounds and
# checked with jq and their exit codes. omaseed starts on the Transmission playground: status
# lists both servers, log has no log to read (exit 4), errors for the other server is not
# current (exit 4), moves answers for it (exit 0), an unknown server is usage (exit 2), and
# --skill prints the guide AGENTS.md holds. The skill (C48, #286): `skill` prints the map
# (plain and --json), `skill library` the exit-code table; in the box's home `skill link` links
# ~/.agents and ~/.claude to omaseed's skill folder and leaves a folder of the box's own at
# ~/.codex/skills/omaseed, `skill status` says so, a second link changes nothing, `skill unlink`
# takes out only its links, and AGENTS.md is the printed map. Then the Status interface switches to the :8081
# playground and a torrent goes into error: errors names its cause and fix with qBittorrent's
# line about it (warning or worse), log has that line naming it, status counts it. Moved from the window (m) to a
# folder it can write, moves has the landed move and what a move back would do, and errors has
# nothing for it; log --since the earlier answer's next has only the lines after. Without
# --json each word's lines are plain when piped. Last, with omaseed quit, status says it isn't
# running (exit 3).
#
#   scripts/dev/agent-walk.sh [OUT_DIR]        # agents: a headless box; each word's answer in
#                                              # OUT_DIR (NAME.json, NAME.txt, NAME.err), a shot
#                                              # per window step, OUT_DIR/index.md lists the checks
#   scripts/dev/agent-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                              # window step waits for enter
#
# The box is scripts/dev/app-box.sh up --transmission --and-playground (both playgrounds started
# if they aren't). The break is scripts/dev/swarm.sh break error (a folder with no write
# permission), undone by `swarm.sh fix` at the end (also on ^C). The playground's queue is off
# while it runs (put back as it was) and its swarm downloads are held, as in error-walk.sh.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a box is a new install, which shows the rows display; the jump palette and
# `m` below work on either, and the table is what the other walks pin.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
APP_BOX="$repo/scripts/dev/app-box.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PLAY_PORT="${OMASEED_TEST_PORT:-8081}"
PLAY="http://127.0.0.1:$PLAY_PORT/api/v2"
refuse_home "$PLAY"
DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
WRITABLE="$DOWNLOADS/agent-writable"
APP_ID=io.github.diogochaves.omaseed
BOX=agent-walk
BROKEN="unwritable-fixture.bin"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-agent-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac
# The answers go somewhere in both modes; interactive keeps them in a temporary folder.
ANSWERS="${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-agent-walk-XXXX")}"
mkdir -p "$ANSWERS"

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

# word NAME ARGS…: `omaseed ARGS…` in the box; stdout in ANSWERS/NAME.json (or .txt without
# --json), stderr in NAME.err, the exit code in CODE. Never fails the script.
CODE=0
word() {
    local name="$1" ext=txt arg
    shift
    for arg in "$@"; do [ "$arg" = --json ] && ext=json; done
    CODE=0
    omabox run -b "$BOX" -- ./build/omaseed "$@" >"$ANSWERS/$name.$ext" 2>"$ANSWERS/$name.err" ||
        CODE=$?
}

# is FILE JQ: the jq expression is true of the answer in ANSWERS/FILE.
is() { jq -e "$2" "$ANSWERS/$1" >/dev/null 2>&1; }

hash_of() {
    curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if sys.argv[1] in t["name"]))' "$1"
}

# until_answer SECONDS NAME JQ ARGS…: ask the word again until its answer makes JQ true.
until_answer() {
    local limit="$1" name="$2" expr="$3"
    shift 3
    for _ in $(seq 1 "$limit"); do
        word "$name" "$@"
        is "$name.json" "$expr" && return 0
        sleep 1
    done
    echo "  (waited ${limit}s: omaseed $* never answered $expr)" >&2
    return 1
}

# in_tree TEXT [SECONDS]: until the window's tree has TEXT.
in_tree() {
    local limit="${2:-10}"
    for _ in $(seq 1 $((limit * 2))); do
        omabox tree -b "$BOX" --window "$APP_ID" --find "$1" >/dev/null 2>&1 && return 0
        sleep 0.5
    done
    echo "  (waited ${limit}s: no \"$1\" in the window)" >&2
    return 1
}

# moved_to FOLDER SECONDS: until the fixture's save path is FOLDER.
moved_to() {
    local folder="$1" limit="$2" path=""
    for _ in $(seq 1 $((limit * 2))); do
        path="$(curl -fs "$PLAY/torrents/info?hashes=$HASH" |
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

# check "what" CONDITION…: a pass/fail line.
check() {
    local what="$1"; shift
    local mark="FAIL"
    if "$@"; then mark="ok"; else FAILED=$((FAILED + 1)); fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}
exits() { [ "$CODE" = "$1" ] || { echo "  (exit $CODE, not $1)" >&2; return 1; }; }
# answered CODE FILE JQ: the last word exited CODE and its answer makes JQ true.
answered() {
    exits "$1" || return 1
    is "$2" "$3" || { echo "  ($2: $(head -c 400 "$ANSWERS/$2"))" >&2; return 1; }
}
# plain NAME: the word's lines (NAME.txt) are there, exit 0, and hold no ESC byte (piped: no colour).
plain() {
    exits 0 && [ -s "$ANSWERS/$1.txt" ] && ! grep -q $'\e' "$ANSWERS/$1.txt"
}

QUEUEING=""
cleanup() {
    [ -z "$OUT" ] || "$repo/scripts/dev/look.sh" "$OUT" || true  # copies to Read (#251)
    if [ -n "$OUT" ]; then
        omabox log -b "$BOX" run >"$OUT/app.log" 2>/dev/null || true
    fi
    "$SWARM" fix >/dev/null 2>&1 || true
    [ -z "$QUEUEING" ] || curl -fs -X POST "$PLAY/app/setPreferences" \
        --data-urlencode "json={\"queueing_enabled\": $QUEUEING}" >/dev/null 2>&1 || true
    rm -rf -- "$WRITABLE"
    [ "$INTERACTIVE" = 1 ] || "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# hold_downloads: stop the swarm torrents still downloading on the playground, but the fixture
# (error-walk.sh's reason, #190: a seeder busy with the swarm sends the fixture nothing).
hold_downloads() {
    local hashes
    hashes="$(curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
print("|".join(t["hash"] for t in json.load(sys.stdin)
               if t["progress"] < 1 and not t["name"].startswith("unwritable-fixture")))')"
    [ -z "$hashes" ] || curl -fs -X POST "$PLAY/torrents/stop" -d "hashes=$hashes" >/dev/null
}

echo "readying the swarm (every break undone, its downloads held)"
"$SWARM" start >/dev/null
"$SWARM" fix >/dev/null
"$SWARM" seed >/dev/null
QUEUEING="$(curl -fs "$PLAY/app/preferences" |
    python3 -c 'import json, sys; print(str(json.load(sys.stdin)["queueing_enabled"]).lower())')"
curl -fs -X POST "$PLAY/app/setPreferences" --data-urlencode 'json={"queueing_enabled": false}' >/dev/null
hold_downloads

echo "starting the box"
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up -b "$BOX" --interactive --transmission --and-playground >/dev/null
else
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Agent walk %s\n\nBuild `%s`. Each answer is beside this file (NAME.json, NAME.txt, NAME.err).\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up -b "$BOX" --transmission --and-playground >/dev/null
fi
home="$(omabox path -b "$BOX")/home"

# ---- 1. on Transmission ------------------------------------------------------------------
echo "1. omaseed on the Transmission playground"
word status1 status --json
check "status: on transmission, both servers listed" answered 0 status1.json \
    '.v == 1 and .command == "status" and (.omaseed | type) == "string" and (.at | test("Z$"))
     and .server.id == "transmission" and .server.engine == "transmission"
     and ([.servers[].id] | index("transmission") != null and index("playground") != null)'
word log-transmission log --json
check "log on Transmission: exit 4, no-log" answered 4 log-transmission.json '.error.code == "no-log"'
word errors-other errors --server playground --json
check "errors --server playground: exit 4, not-current" answered 4 errors-other.json \
    '.error.code == "not-current" and (.omaseed | type) == "string"'
word moves-other moves --server playground --json
check "moves --server playground: exit 0, that server's, no move back known" answered 0 moves-other.json \
    '.server.id == "playground" and .server.reachable == false and (.running | type) == "array"
     and ([.landed[].back] | all(. == null))'
word errors-nobody errors --server nobody --json
check "errors --server nobody: exit 2, unknown-server" answered 2 errors-nobody.json '.error.code == "unknown-server"'
word do-reserved "do" --json
check "do (reserved): exit 2, usage, made by the CLI (no omaseed key)" answered 2 do-reserved.json \
    '.error.code == "usage" and (has("omaseed") | not)'
word skill --skill
check "--skill: exit 0, the guide" plain skill
agents_header() {
    [ "$(head -n 1 "$home/.config/omaseed/AGENTS.md")" = \
        "Written by omaseed on every start. Edits here are overwritten." ] &&
        [ "$(cat "$home/.config/omaseed/CLAUDE.md")" = "@AGENTS.md" ]
}
check "the box's .config/omaseed/AGENTS.md written with its header, CLAUDE.md points at it" agents_header

# ---- 1b. the skill (DECISIONS C48, #286) -------------------------------------------------
# Every link and unlink runs in the box (omabox run), whose HOME is the box's: never this home.
echo "1b. the skill: printed, linked, unlinked in the box's home"
word skill-map skill
skill_map() { plain skill-map && grep -q '^Pages: omaseed skill library, skins, files\.$' "$ANSWERS/skill-map.txt"; }
check "skill: exit 0, the map, plain, ending in the pages" skill_map
word skill-map-json skill --json
check "skill --json: page map, its text" answered 0 skill-map-json.json \
    '.v == 1 and .command == "skill" and .page == "map" and (.text | contains("omaseed skill library"))
     and (has("omaseed") | not)'
word skill-library skill library
skill_library() { plain skill-library && grep -q '^## Exit codes$' "$ANSWERS/skill-library.txt" &&
    grep -q '^  5  not-written' "$ANSWERS/skill-library.txt"; }
check "skill library: exit 0, the exit-code table" skill_library
word skill-status0 skill status --json
check "skill status --json: nothing linked yet" answered 0 skill-status0.json \
    '.command == "skill status" and ([.folders[] | select(.state == "linked")] | length == 0)'
# A folder of the box's own where Codex looks: a link must leave it, and unlink keep it.
# shellcheck disable=SC2016 # $HOME expands in the box, to the box's home
omabox run -b "$BOX" -- sh -c 'mkdir -p "$HOME/.codex/skills/omaseed" &&
    printf -- "---\nname: \"omaseed\"\n---\nmine\n" >"$HOME/.codex/skills/omaseed/SKILL.md"' >/dev/null
word skill-link skill link --json
check "skill link --json: exit 0, the two always linked, Codex's folder left as yours" answered 0 skill-link.json \
    '([.folders[] | select(.state == "linked") | .agent] | sort) == ["Claude Code", "the shared ~/.agents"]
     and ([.folders[] | select(.agent == "Codex")][0] | .state == "yours" and .copy == true)
     and (.changed | length == 2)'
# ours_link REL: the box's ~/REL is a link to an omaseed skill folder (it holds the SKILL.md).
ours_link() {
    [ -L "$home/$1" ] && [[ "$(readlink "$home/$1")" =~ /(omaseed|data)/agents/skill$ ]] &&
        [ -f "$(readlink "$home/$1")/SKILL.md" ]
}
check "the box's ~/.agents/skills/omaseed links to omaseed's skill" ours_link .agents/skills/omaseed
check "the box's ~/.claude/skills/omaseed links to omaseed's skill" ours_link .claude/skills/omaseed
word skill-status1 skill status
skill_status() { plain skill-status1 && grep -q '^linked ' "$ANSWERS/skill-status1.txt" &&
    grep -q '^yours  *Codex' "$ANSWERS/skill-status1.txt"; }
check "skill status: linked, Codex's own copy said as yours" skill_status
word skill-link2 skill link --json
check "skill link again: exit 0, nothing changed, Codex's folder still left" answered 0 skill-link2.json \
    '(.changed | length == 0) and ([.folders[] | select(.agent == "Codex")][0].state == "yours")'
word skill-unlink skill unlink --json
unlinked() {
    answered 0 skill-unlink.json '.changed | length == 2' &&
        [ ! -e "$home/.agents/skills/omaseed" ] && [ ! -L "$home/.agents/skills/omaseed" ] &&
        [ ! -L "$home/.claude/skills/omaseed" ] && [ -d "$home/.claude/skills" ] &&
        [ "$(tail -n 1 "$home/.codex/skills/omaseed/SKILL.md")" = mine ]
}
check "skill unlink: only ours gone, the folders kept, Codex's folder untouched" unlinked
agents_is_map() {
    local file="$home/.config/omaseed/AGENTS.md"
    [ "$(head -n 1 "$file")" = "Written by omaseed on every start. Edits here are overwritten." ] &&
        grep -q 'omaseed skill library' "$file" && ! grep -q 'ref: ' "$file"
}
check "AGENTS.md is the printed map: its header, omaseed skill library, no ref:" agents_is_map

# ---- 2. onto the playground, and a torrent in error ---------------------------------------
echo "2. switched to the playground; $BROKEN breaks"
switched() {
    omabox run -b "$BOX" -- busctl --user call io.github.diogochaves.omaseed /io/github/diogochaves/omaseed \
        io.github.diogochaves.omaseed.Status SwitchServer s playground | grep -q true
}
check "Status.SwitchServer playground" switched
check "status: on the playground, reachable" until_answer 60 status2 \
    '.server.id == "playground" and .server.reachable == true and .server.engine == "qbittorrent"' status --json
check "errors answers past first-sync" until_answer 60 errors0 '.error == null' errors --json
"$SWARM" break error >/dev/null
HASH="$(hash_of "$BROKEN")"

# ---- 3. errors, log, status ----------------------------------------------------------------
echo "3. what needs you"
check "errors: $BROKEN needs you, no access, fix move --to, qBittorrent's line about it" until_answer 90 errors1 \
    "[.torrents[] | select(.name == \"$BROKEN\" and .hash == \"$HASH\" and .cause == \"noAccess\"
      and .kind == \"error\" and .fix.do == \"move\" and .fix.needs == [\"--to\"]
      and ([.log[] | select(.level == \"warning\" or .level == \"critical\")] | length) >= 1)]
     | length == 1" errors --json
word log1 log --lines 3 --json
check "log --lines 3: a line naming $BROKEN, problems-only (fromStart null, oldest set)" answered 0 log1.json \
    "(.lines | length) <= 3 and .level == \"warning\" and .fromStart == null and .oldest != null
     and ([.lines[] | select(.torrent == \"$HASH\")] | length) >= 1 and .restarted == false"
NEXT="$(jq -r '.next' "$ANSWERS/log1.json" 2>/dev/null || echo 0)"
word status3 status --json
check "status: needsYou ≥ 1" answered 0 status3.json '.questions.needsYou >= 1'
word errors-text errors
check "errors without --json: plain lines naming $BROKEN" plain errors-text
check "…its lines say no access" grep -q "$BROKEN" "$ANSWERS/errors-text.txt"
omabox keys -b "$BOX" --window "$APP_ID" ctrl+k -t "$BROKEN" Return >/dev/null
sleep 1.5
step "In error" "$BROKEN on the cursor: the row reads \"! no access\", the story's red block, m move… · e retry: what errors.txt says."

# ---- 4. moved from the window --------------------------------------------------------------
echo "4. m moves it somewhere it can write"
mkdir -p "$WRITABLE"
omabox keys -b "$BOX" --window "$APP_ID" --wait m >/dev/null || true
check "m: the Move… sheet for it" in_tree "Move $BROKEN" 10
omabox keys -b "$BOX" --window "$APP_ID" ctrl+a -t "$WRITABLE" >/dev/null
# The playground's disks are out of the box's sight: the sheet says so once it has looked.
in_tree "can't see" 20 || true
step "Move sheet" "The Move… sheet on $BROKEN, $WRITABLE typed, the remote line (omaseed can't see the server's disks)."
omabox keys -b "$BOX" --window "$APP_ID" Return >/dev/null
check "the move landed: its save path is $WRITABLE" moved_to "$WRITABLE" 60
# Moved, it may still be in error until it is started again: e is the window's retry.
if ! until_answer 10 errors2 "[.torrents[] | select(.hash == \"$HASH\")] | length == 0" errors --json \
    2>/dev/null; then
    omabox keys -b "$BOX" --window "$APP_ID" --wait e >/dev/null || true
fi
check "errors: nothing for $BROKEN" until_answer 60 errors2 \
    "[.torrents[] | select(.hash == \"$HASH\")] | length == 0" errors --json
check "moves: one landed to $WRITABLE, it goes back" until_answer 30 moves1 \
    "(.landed | length) >= 1 and .landed[0].to == \"$WRITABLE\" and .landed[0].back.goBack == 1
     and ([.landed[0].torrents[].hash] | index(\"$HASH\") != null) and .kept >= 1" moves --json
step "Moved" "$BROKEN in agent-writable: no block in the story; what moves.json says landed."

# ---- 5. log --since -----------------------------------------------------------------------
echo "5. log --since $NEXT"
# The move and the retry log at qBittorrent's normal level, so every level is asked for.
word log2 log --since "$NEXT" --level normal --json
check "log --since NEXT --level normal: the lines from NEXT on, and some" answered 0 log2.json \
    "(.lines | length) >= 1 and ([.lines[].id] | all(. >= $NEXT)) and .restarted == false
     and .next > $NEXT and .fromStart != null"
word log3 log --since 999999999 --json
check "log --since past the newest: restarted, from the first line" answered 0 log3.json '.restarted == true'

# ---- 6. the lines, piped ------------------------------------------------------------------
echo "6. every word without --json"
for w in status errors log moves help; do
    word "$w-text" "$w"
    check "$w: plain lines when piped" plain "$w-text"
done
check "status' head names the playground and qBittorrent" \
    grep -q "^playground · qBittorrent" "$ANSWERS/status-text.txt"

# ---- 7. omaseed quit ----------------------------------------------------------------------
echo "7. omaseed quit"
omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
for _ in $(seq 1 20); do
    omabox run -b "$BOX" -- pgrep -x omaseed >/dev/null 2>&1 || break
    sleep 0.5
done
word status-gone status
not_running_words() { exits 3 && grep -q "omaseed isn't running" "$ANSWERS/status-gone.err"; }
check "status with omaseed quit: exit 3, the not-running words on stderr" not_running_words
word status-gone-json status --json
check "status --json with omaseed quit: exit 3, not-running, no omaseed key" answered 3 status-gone-json.json \
    '.error.code == "not-running" and (has("omaseed") | not)'

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The answers are in $ANSWERS. The box stays up; close it with: $APP_BOX down -b $BOX"
else
    echo "done: $OUT/index.md"
fi
if [ "$FAILED" -gt 0 ]; then
    echo "$FAILED check(s) failed" >&2
    exit 1
fi
