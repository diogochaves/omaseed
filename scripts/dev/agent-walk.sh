#!/usr/bin/env bash
# omaseed — the agent walk (board 62, DECISIONS C45, #163): the words an agent asks the running
# omaseed (`omaseed status|errors|log|moves [--json]`), run in a box against the playgrounds and
# checked with jq and their exit codes. omaseed starts on the Transmission playground: status
# lists both servers, log has no log to read (exit 4), errors for the other server is not
# current (exit 4), moves answers for it (exit 0), an unknown server is usage (exit 2), and
# --skill prints the guide AGENTS.md holds. What an agent may do (board 67, C45 V14–V18, C50,
# #291–#297), on three torrents of the Transmission playground made for the walk: the gate (do
# --list; Fixes off and do delete refused, exit 5; --delete-files usage, exit 2; Reads off set
# through Settings › General › Agents, read back from state.json: status 5, skin list and do
# --list still 0); Fixes allow: do recheck HASH8 done, the strip's "· by an agent", the journal
# line, reannounce --dry-run would run; Fixes ask: the ask's window (↵ alone does nothing, tab to
# allow and ↵: allowed, the CLI's "Asking in omaseed's window…"), esc refuses (5), unanswered
# for OMASEED_ASK_TIMEOUT_MS (5); a torrent's files moved away by hand: locate --to a folder
# without them and one with half (not-found, 5, nothing changed on the daemon), then the right
# one (located, 100 % after the recheck, the story's "Located in … · by an agent"); remove on a
# healthy torrent not-removable, on one whose data is gone asked, removed from the daemon, kept
# in the box's ~/.local/share/omaseed/removed/, added back from the strip, another removed and
# added back from the Agents row's sheet; --plan dry runs (a refused line: 5, its text indented;
# both lines would ask) and a real plan, one ask with a torrent unticked (Done: 2 of 3 · 1
# rechecking; the journal's plan line and a line per torrent, #304); help
# agents (the address on show, --for gemini). The skill (C48, #286): `skill` prints the map
# (plain and --json), `skill library` the exit-code table with do's and the missing-files
# path; in the box's home `skill link` links ~/.agents and ~/.claude to omaseed's skill folder and leaves a folder of the box's own at
# ~/.codex/skills/omaseed, `skill status` says so, a second link changes nothing, `skill unlink`
# takes out only its links, and AGENTS.md is the printed map. Then the Status interface switches to the :8081
# playground and a torrent goes into error: errors names its cause and fix with qBittorrent's
# line about it (warning or worse), log has that line naming it, status counts it. Moved from the window (m) to a
# folder it can write, moves has the landed move and what a move back would do, and errors has
# nothing for it; log --since the earlier answer's next has only the lines after. Moves off
# refuses do move; Moves ask: do move tiny-notes.txt --to that folder, allowed in the ask, a j-
# job, on the server and on disk; moves prints its id, the story says by an agent; do move-back
# ID brings it back (server and disk), and again is refused. Without
# --json each word's lines are plain when piped. Last, with omaseed quit, status says it isn't
# running (exit 3).
#
#   scripts/dev/agent-walk.sh [OUT_DIR]        # agents: a headless box; each word's answer in
#                                              # OUT_DIR (NAME.json, NAME.txt, NAME.err), a shot
#                                              # per window step, OUT_DIR/index.md lists the checks
#   scripts/dev/agent-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                              # window step waits for enter
#
# The box is scripts/dev/app-box.sh up --transmission --and-playground --ro-bind PROFILE (both
# playgrounds started if they aren't; the Transmission playground's profile bound in read-only,
# so omaseed sees its disks and .torrent folder). The walk's torrents and their folder there
# (agent-walk/) are deleted at the end, and tiny-notes.txt is put back if it was left moved. The break is scripts/dev/swarm.sh break error (a folder with no write
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
# The swarm's smallest torrent, moved by an agent and back (5b).
TINY="tiny-notes.txt"
MAIN='title:^omaseed$'
ASK='title:^omaseed · agent$'
TEST_SERVER="$repo/scripts/dev/test-server.sh"
TR_PORT="${OMASEED_TRANSMISSION_PORT:-8085}"
TR_RPC="http://127.0.0.1:$TR_PORT/transmission/rpc"
TR_PROFILE="${OMASEED_ENGINES_ROOT:-$HOME/.local/share/omaseed-test}/transmission"
# The acting part's torrents on the Transmission playground live here (bound into the box,
# read-only, so omaseed sees the daemon's disks and its .torrent folder: locate and remove
# need both).
TW="$TR_PROFILE/agent-walk"
TA_NAME="agent-walk-a"
TB_NAME="agent-walk-b"
# Removed and added back from the Agents row's sheet (a torrent added back is new to the
# daemon, 0 %: a verify finds no data missing there, so it can't be gone twice).
TC_NAME="agent-walk-c"
# The ask's two minutes, shortened so the walk sees one go unanswered (app-box.sh passes it).
export OMASEED_ASK_TIMEOUT_MS="${OMASEED_ASK_TIMEOUT_MS:-20000}"
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

# moved_to FOLDER SECONDS [HASH]: until the save path of HASH (the fixture's) is FOLDER.
moved_to() {
    local folder="$1" limit="$2" hash="${3:-$HASH}" path=""
    for _ in $(seq 1 $((limit * 2))); do
        path="$(curl -fs "$PLAY/torrents/info?hashes=$hash" |
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

# ---- the acting part's helpers (board 67, DECISIONS C45 V14–V18, C50) ----------------------
# trpc METHOD [PARAMS]: one call to the Transmission playground's RPC.
trpc() {
    local body session
    body="$(python3 -c 'import json,sys; print(json.dumps({"jsonrpc":"2.0","method":sys.argv[1],"params":json.loads(sys.argv[2]),"id":1}))' "$1" "${2:-{\}}")"
    session="$(curl -s -o /dev/null -D - -X POST "$TR_RPC" | sed -n 's/^X-Transmission-Session-Id: *//Ip' | tr -d '\r')"
    curl -fs -H "X-Transmission-Session-Id: $session" -H 'Content-Type: application/json' \
        --data "$body" "$TR_RPC"
}
# tr_is HASH EXPR: a Python expression over `t`, the torrent HASH as the daemon lists it now
# (None once it is gone), is true.
tr_is() {
    trpc torrent_get '{"fields":["hash_string","download_dir","percent_done","status","error","error_string"]}' |
        python3 -c '
import json, sys
t = next((t for t in json.load(sys.stdin)["result"]["torrents"] if t["hash_string"] == sys.argv[1]), None)
sys.exit(0 if eval(sys.argv[2]) else 1)' "$1" "$2"
}
# tr_until SECONDS HASH EXPR: until tr_is.
tr_until() {
    local limit="$1"
    shift
    for _ in $(seq 1 $((limit * 2))); do
        tr_is "$@" && return 0
        sleep 0.5
    done
    echo "  (waited ${limit}s: the daemon never had $1 with $2)" >&2
    return 1
}
# tr_torrent NAME FILE…: a folder NAME of FILE (SIZE in bytes as FILE:SIZE) of random bytes,
# its .torrent, the data copied to TW/in, added to the daemon started; prints the hash.
tr_torrent() {
    local name="$1" spec hash
    shift
    mkdir -p "$TW/src/$name" "$TW/in"
    for spec in "$@"; do head -c "${spec#*:}" /dev/urandom >"$TW/src/$name/${spec%%:*}"; done
    transmission-create -c "omaseed agent walk" -o "$TW/$name.torrent" "$TW/src/$name" >/dev/null
    cp -r "$TW/src/$name" "$TW/in/"
    hash="$(transmission-show "$TW/$name.torrent" | sed -n 's/^ *Hash v1: *//p')"
    trpc torrent_add "{\"metainfo\":\"$(base64 -w0 "$TW/$name.torrent")\",\"download_dir\":\"$TW/in\"}" >/dev/null
    echo "$hash"
}
# files_gone HASH: what a person finds after a disk went away: the data deleted, a verify
# (Transmission's "Paused torrent as no data was found!"), until omaseed's errors says its
# files are missing (the daemon first, then omaseed's next sync).
files_gone() {
    trpc torrent_verify "{\"ids\":[\"$1\"]}" >/dev/null
    tr_until 20 "$1" 't and t["error"] == 3 and t["status"] == 0' &&
        until_answer 30 "errors-gone-${1:0:8}" \
            "[.torrents[] | select(.hash == \"$1\" and .cause == \"missingFiles\")] | length == 1" errors --json
}

# saved_agent KEY DEFAULT: the box's state.json "agents" section (a key not written yet is its
# default).
saved_agent() {
    jq -r --arg key "$1" --arg default "$2" '.agents[$key] // $default' \
        "$home/.config/omaseed/state.json" 2>/dev/null || echo "$2"
}
main_keys() { omabox keys -b "$BOX" --window "$MAIN" --wait "$@" >/dev/null 2>&1 || true; }
# gate LINE LEVEL: Settings › General, the Agents row's LINE (cursor stops 6–9 after the row,
# as settings-walk.sh walks them), enter until state.json says LEVEL, then closed.
gate() {
    local line="$1" want="$2" down default=off before
    case "$line" in reads) down=6 default=on ;; fixes) down=7 ;; moves) down=8 ;; remove) down=9 ;; esac
    local js=()
    for _ in $(seq 1 "$down"); do js+=(j); done
    main_keys comma 1 h l "${js[@]}"
    for _ in 1 2 3; do
        before="$(saved_agent "$line" "$default")"
        [ "$before" = "$want" ] && break
        main_keys Return
        for _ in $(seq 1 20); do
            [ "$(saved_agent "$line" "$default")" != "$before" ] && break
            sleep 0.1
        done
    done
    main_keys Escape Escape
    [ "$(saved_agent "$line" "$default")" = "$want" ] ||
        { echo "  (state.json's agents.$line is $(saved_agent "$line" "$default"), not $want)" >&2; return 1; }
}

# asking NAME ARGS…: the word in the background, as an agent's call waits for the ask;
# collect NAME then sets CODE.
ASKING=""
asking() {
    local name="$1"
    shift
    (
        word "$name" "$@"
        echo "$CODE" >"$ANSWERS/$name.code"
    ) &
    ASKING=$!
}
collect() {
    wait "$ASKING" 2>/dev/null || true
    CODE="$(cat "$ANSWERS/$1.code" 2>/dev/null || echo 99)"
}
ask_open() { omabox wait -b "$BOX" --timeout 20s window "$ASK" >/dev/null 2>&1; }
ask_keys() { omabox keys -b "$BOX" --window "$ASK" --wait "$@" >/dev/null 2>&1 || true; }
ask_shown() { omabox windows -b "$BOX" 2>/dev/null | grep -q '"omaseed · agent"'; }
# ask_gone SECONDS: until the ask's window is gone.
ask_gone() {
    for _ in $(seq 1 $(($1 * 2))); do
        ask_shown || return 0
        sleep 0.5
    done
    echo "  (the ask's window still up after $1s)" >&2
    return 1
}
# ask_focused TEXT: the ask's button whose name starts with TEXT has the keyboard.
ask_focused() {
    omabox tree -b "$BOX" --window "$ASK" 2>/dev/null | grep -q "button \"$1[^\"]*\" \[focused" ||
        { echo "  (no focused button \"$1…\" in the ask)" >&2; return 1; }
}
# allow_ask BUTTON: V17's only way to allow: tab to the button (refuse is first, BUTTON next),
# ↵ on it. Never fails the script: the answer's check says what came of it. The ↵ goes without
# --wait: the strip it brings counts down for 6 s, so the screen wouldn't settle before it went.
allow_ask() {
    ask_keys Tab Tab
    if ask_focused "$1"; then
        omabox keys -b "$BOX" --window "$ASK" Return >/dev/null 2>&1 || true
    fi
}
# said NAME TEXT: the word's stderr (NAME.err) has TEXT.
said() { grep -qF "$2" "$ANSWERS/$1.err" || { echo "  ($1.err: $(head -c 300 "$ANSWERS/$1.err"))" >&2; return 1; }; }
# cursor_on NAME: the jump palette puts the cursor on NAME (its story in the details pane).
cursor_on() { omabox keys -b "$BOX" --window "$MAIN" ctrl+k -t "$1" Return >/dev/null; sleep 1.5; }
# logged TEXT: omaseed's own output in the box has TEXT (the journal line, C45 V18).
logged() { omabox log -b "$BOX" run 2>/dev/null | grep -qF "$1" || { echo "  (no \"$1\" in omaseed's log)" >&2; return 1; }; }
# main_has TEXT [SECONDS [SHOT]]: until the main window's tree has TEXT (the ask's window has
# the same class, so the main one is picked by its title); SHOT, a shot the moment it does.
main_has() {
    local shot=()
    [ -z "${3:-}" ] || shot=(--shot "$3")
    omabox wait -b "$BOX" --timeout "${2:-10}s" tree "$1" --window "$MAIN" "${shot[@]}" >/dev/null 2>&1 ||
        { echo "  (waited ${2:-10}s: no \"$1\" in the main window)" >&2; return 1; }
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
    # The swarm's torrent an agent moved goes home before its folder is deleted.
    if [ -n "${TINY_HASH:-}" ] && [ -n "${TINY_HOME:-}" ] && [ -e "$WRITABLE/$TINY" ]; then
        curl -fs -X POST "$PLAY/torrents/setLocation" -d "hashes=$TINY_HASH" \
            --data-urlencode "location=$TINY_HOME" >/dev/null 2>&1 || true
        for _ in $(seq 1 40); do
            [ -e "$WRITABLE/$TINY" ] || break
            sleep 0.5
        done
    fi
    rm -rf -- "$WRITABLE"
    local hash
    for hash in ${TA:-} ${TB:-} ${TC:-}; do
        trpc torrent_remove "{\"ids\":[\"$hash\"],\"delete_local_data\":true}" >/dev/null 2>&1 || true
    done
    rm -rf -- "${TW:?}"
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

echo "readying the Transmission playground's two walk torrents"
"$TEST_SERVER" status --transmission >/dev/null 2>&1 || "$TEST_SERVER" start --transmission >/dev/null
rm -rf -- "${TW:?}"
TA="$(tr_torrent "$TA_NAME" a.bin:307200 b.txt:64)"
TB="$(tr_torrent "$TB_NAME" c.bin:204800)"
TC="$(tr_torrent "$TC_NAME" d.bin:102400)"

echo "starting the box"
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up -b "$BOX" --interactive --transmission --and-playground --ro-bind "$TR_PROFILE" >/dev/null
else
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Agent walk %s\n\nBuild `%s`. Each answer is beside this file (NAME.json, NAME.txt, NAME.err).\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up -b "$BOX" --transmission --and-playground --ro-bind "$TR_PROFILE" >/dev/null
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
    grep -q '^  5  not done (do): not-allowed' "$ANSWERS/skill-library.txt" &&
    grep -q '^  6  failed' "$ANSWERS/skill-library.txt" &&
    grep -q '^## Missing files, sorted out$' "$ANSWERS/skill-library.txt"; }
check "skill library: exit 0, the exit-code table, do's codes, the missing-files path" skill_library
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

# ---- 1c. the gate (board 67 §3, DECISIONS C50 GT3, GT8, #291) -------------------------------
echo "1c. the gate on the Transmission playground"
check "the daemon has the walk's three torrents, complete" \
    tr_until 30 "$TC" 't and t["percent_done"] == 1 and t["status"] == 6'
DAEMON_COUNT="$(trpc torrent_get '{"fields":["id"]}' | jq '.result.torrents | length')"
check "status: omaseed has the daemon's $DAEMON_COUNT torrents" until_answer 30 status-tr \
    ".questions.all == $DAEMON_COUNT" status --json
word do-list "do" --list --json
check "do --list: Reads on, Fixes, Moves and Remove off, the never words" answered 0 do-list.json \
    '.command == "do" and .list == true
     and ([.lines[] | {(.kind): .level}] | add) == {reads: "on", fixes: "off", moves: "off", remove: "off"}
     and (.never | index("delete") != null)'
word do-off "do" recheck "$TA" --json
check "do recheck, Fixes off: exit 5, not-allowed naming Fixes" answered 5 do-off.json \
    '.error.code == "not-allowed" and (.error.message | contains("Fixes is off"))'
word do-never "do" delete "$TA" --json
check "do delete: exit 5, never" answered 5 do-never.json '.error.code == "never"'
word do-files "do" remove "$TA" --delete-files --json
check "do remove --delete-files: exit 2, usage from the CLI" answered 2 do-files.json '.error.code == "usage"'
check "Reads off through the Agents row: state.json says off" gate reads off
word status-off status --json
check "status, Reads off: exit 5, not-allowed" answered 5 status-off.json '.error.code == "not-allowed"'
word do-readsoff "do" recheck "$TA" --json
check "do recheck, Reads off: exit 5, not-allowed (Reads)" answered 5 do-readsoff.json \
    '.error.code == "not-allowed" and (.error.message | contains("Reads is off"))'
word skin-list-off skin list
check "skin list, Reads off: exit 0 (it reads no library)" plain skin-list-off
word do-list-off "do" --list --json
check "do --list, Reads off: exit 0, reads off" answered 0 do-list-off.json \
    '[.lines[] | select(.kind == "reads")][0].level == "off"'
step "Reads off" "Settings closed again; the Agents row's last line says 'recheck · not allowed: Reads is off' (open Settings › General to see it)."
check "Reads on again" gate reads on

# ---- 1d. a fix allowed in Settings (C45 V14, V18) --------------------------------------------
echo "1d. Fixes allow: recheck, reannounce --dry-run"
check "Fixes allow through the Agents row" gate fixes allow
word do-recheck "do" recheck "${TA:0:8}" --json
check "do recheck HASH8: exit 0, allowed in Settings (not asked), $TA_NAME done" answered 0 do-recheck.json \
    ".action == \"recheck\" and .asked == false and .answer == \"not-asked\"
     and ([.done[].hash] == [\"$TA\"]) and (.failed | length) == 0"
check "the strip: \"$TA_NAME sent to recheck\" · by an agent" main_has "sent to recheck. by an agent" 10
step "Recheck by an agent" "The outcome strip: '$TA_NAME sent to recheck · by an agent' (the suffix dim, never elided)."
check "the journal line names the act and how" logged "agent: recheck on $TA_NAME, allowed in Settings"
word do-dry "do" reannounce "$TB" --dry-run --json
check "do reannounce --dry-run: exit 0, would run, nothing done" answered 0 do-dry.json \
    ".dryRun == true and .would == \"run\" and ([.torrents[].hash] == [\"$TB\"])"

# ---- 1e. the ask (C45 V17): allowed, refused, not answered -----------------------------------
echo "1e. Fixes ask: allow, refuse, no answer (${OMASEED_ASK_TIMEOUT_MS} ms)"
check "Fixes ask through the Agents row" gate fixes ask
asking ask-allow "do" recheck "$TA" --json
check "do recheck: omaseed's ask window opens" ask_open
step "The ask" "A window of its own, 'omaseed · agent': 'An agent asks to recheck a torrent', $TA_NAME with its folder, who asked (omaseed do, started from omabox's shell), the foot 'tab · ↵ on a button · esc refuses', refuse and allow, neither with the cursor."
ask_keys Return
sleep 1
check "↵ alone does nothing: the ask stays open" ask_shown
allow_ask allow
collect ask-allow
check "tab to allow, ↵: exit 0, asked, allowed, done" answered 0 ask-allow.json \
    ".asked == true and .answer == \"allowed\" and ([.done[].hash] == [\"$TA\"])"
check "the CLI said \"Asking in omaseed's window…\" on stderr" said ask-allow "Asking in omaseed's window…"
check "the window closed" ask_gone 10
check "the journal line says allowed in the window" logged "agent: recheck on $TA_NAME, allowed in the window"
asking ask-refuse "do" reannounce "$TA" --json
check "do reannounce: the ask opens" ask_open
ask_keys Escape
collect ask-refuse
check "esc: exit 5, refused" answered 5 ask-refuse.json '.asked == true and .answer == "refused" and .error.code == "refused"'
check "the window closed" ask_gone 10
asking ask-none "do" recheck "$TB" --json
check "do recheck: the ask opens" ask_open
collect ask-none
check "no answer in OMASEED_ASK_TIMEOUT_MS: exit 5, no-answer" answered 5 ask-none.json \
    '.asked == true and .answer == "no-answer" and .error.code == "no-answer"'
check "the window went with it" ask_gone 10

# ---- 1f. locate (C50 GT2, #294) ---------------------------------------------------------------
echo "1f. $TA_NAME's files moved by hand; locate"
check "Fixes allow again" gate fixes allow
mkdir -p "$TW/found" "$TW/wrong" "$TW/partial/$TA_NAME"
check "$TA_NAME rechecked whole before its files go" tr_until 30 "$TA" 't and t["percent_done"] == 1'
mv "$TW/in/$TA_NAME" "$TW/found/"
cp "$TW/found/$TA_NAME/b.txt" "$TW/partial/$TA_NAME/"
check "a verify: $TA_NAME's data not found" files_gone "$TA"
check "errors: $TA_NAME's files missing, locate offered" until_answer 30 errors-locate \
    "[.torrents[] | select(.hash == \"$TA\" and .cause == \"missingFiles\" and .kind == \"missing-files\" and .fix.do == \"locate\")] | length == 1" errors --json
word locate-wrong "do" locate "$TA" --to "$TW/wrong" --json
check "locate --to a folder without them: exit 5, not-found, found 0 of 2" answered 5 locate-wrong.json \
    '.error.code == "not-found" and .failed[0].found == 0 and .failed[0].files == 2 and (.done | length) == 0'
word locate-partial "do" locate "$TA" --to "$TW/partial" --json
check "locate --to a folder with one of two: exit 5, not-found, found 1 of 2, nothing done" answered 5 locate-partial.json \
    '.error.code == "not-found" and .failed[0].found == 1 and (.done | length) == 0'
check "…and the daemon's folder unchanged" tr_is "$TA" "t[\"download_dir\"] == \"$TW/in\""
word locate "do" locate "$TA" --to "$TW/found" --json
check "locate --to the right folder: exit 0, found 2 of 2, rechecking" answered 0 locate.json \
    '.done[0].found == 2 and .done[0].files == 2 and .done[0].rechecking == true'
check "on the daemon: its folder is found/, 100 % after the recheck, no error" \
    tr_until 30 "$TA" "t[\"download_dir\"] == \"$TW/found\" and t[\"percent_done\"] == 1 and t[\"error\"] == 0"
check "on disk: the files still in found/, nothing back in in/" \
    test -f "$TW/found/$TA_NAME/a.bin" -a ! -e "$TW/in/$TA_NAME"
cursor_on "$TA_NAME"
check "the story: Located in … · by an agent" main_has "Located in" 10
step "Located" "$TA_NAME on the cursor; its story's last line 'Located in …/agent-walk/found · nothing moved · by an agent'."

# ---- 1g. remove and add back (C50 GT1, GT6, #295) ---------------------------------------------
echo "1g. remove: refused on a healthy torrent, then $TB_NAME's files gone"
check "Remove ask through the Agents row" gate remove ask
word remove-healthy "do" remove "$TA" --json
check "do remove on a torrent with its files: exit 5, not-removable" answered 5 remove-healthy.json \
    '.error.code == "not-removable"'
rm -rf "${TW:?}/in/$TB_NAME"
check "$TB_NAME's data deleted and verified: not found" files_gone "$TB"
asking remove "do" remove "$TB" --json
check "do remove: the ask opens" ask_open
step "The remove ask" "'An agent asks to remove a torrent': $TB_NAME 'was in …/agent-walk/in · SIZE · files gone', none of its files on disk (checked), kept 30 days to add back; refuse and 'allow the remove'."
allow_ask "allow the remove"
# A done strip goes after 6 s (C18 D3), its add back with it: the strip (and its shot) first,
# the rest after.
STRIP_SHOT=""
[ "$INTERACTIVE" = 1 ] || STRIP_SHOT="$OUT/removed-strip.png"
check "the strip: removed · kept 30 days · by an agent · add back" main_has "add back" 5 "$STRIP_SHOT"
[ -z "$STRIP_SHOT" ] ||
    printf -- '- **Removed by an agent**: the strip, "1 removed · kept 30 days · by an agent · add back".\n  ![](removed-strip.png)\n' >>"$OUT/index.md"
kept() { compgen -G "$home/.local/share/omaseed/removed/*-${1:0:8}*/*" >/dev/null; }
check "kept under the box's ~/.local/share/omaseed/removed/" kept "$TB"
omabox click -b "$BOX" --window "$MAIN" --find "add back" >/dev/null 2>&1 || true
collect remove
check "allowed: exit 0, removed, kept until a day" answered 0 remove.json \
    ".answer == \"allowed\" and .done[0].hash == \"$TB\" and (.done[0].kept | test(\"^[0-9]{4}-[0-9]{2}-[0-9]{2}$\"))"
check "the CLI said removes always ask" said remove "removes always ask."
check "the journal line: removed, kept" logged "removed: $TB_NAME kept for 30 days"
check "add back from the strip: $TB_NAME on the daemon again, stopped" tr_until 30 "$TB" 't is not None and t["status"] == 0'
check "…the journal says so" logged "removed: $TB_NAME added back to transmission, stopped"
check "…and its keep let go" bash -c "! compgen -G '$home/.local/share/omaseed/removed/*-${TB:0:8}*/*' >/dev/null"
# Another, added back from the Agents row's sheet this time.
rm -rf "${TW:?}/in/$TC_NAME"
check "$TC_NAME's data deleted and verified: not found" files_gone "$TC"
asking remove2 "do" remove "$TC" --json
check "do remove $TC_NAME: the ask opens" ask_open
allow_ask "allow the remove"
collect remove2
check "removed: exit 0" answered 0 remove2.json ".done[0].hash == \"$TC\""
check "on the daemon: gone" tr_until 20 "$TC" 't is None'
check "kept" kept "$TC"
main_keys comma 1
check "the Agents row: add back… (1 kept)" main_has "add back… (1 kept)" 10
omabox click -b "$BOX" --window "$MAIN" --find "add back… (" >/dev/null 2>&1 || true
check "the Add back sheet lists it" main_has "add $TC_NAME back" 10
step "Add back" "Over Settings: 'Add back', '1 torrent an agent removed · kept 30 days', $TC_NAME with where it was, its button 'add back' with the cursor."
main_keys Return
check "added back from the sheet: on the daemon, stopped" tr_until 30 "$TC" 't is not None and t["status"] == 0'
main_keys Escape Escape

# ---- 1h. a plan (C50 GT4, GT5, #296) ---------------------------------------------------------
echo "1h. --plan: a refused line in a dry run, then one ask with a line unticked"
printf '{"lines": [{"action": "recheck", "torrents": ["%s"]}, {"action": "remove", "torrents": ["%s"]}]}\n' \
    "$TA" "$TA" >"$TW/plan-refused.json"
word plan-dry-refused "do" --plan "$TW/plan-refused.json" --dry-run --json
check "dry run, a remove of a healthy torrent: exit 5, the line refused, nothing would run" answered 5 plan-dry-refused.json \
    '.error.code == "not-removable" and .lines[0].would == "run" and .lines[1].would == "refused" and (.lines[1].torrents | length) == 1 and .lines[1].torrents[0].name == "'"$TA_NAME"'"'
word plan-dry-refused-text "do" --plan "$TW/plan-refused.json" --dry-run
indented() { exits 5 && ! grep -q '^omaseed do removes' "$ANSWERS/plan-dry-refused-text.txt"; }
check "…its text: the refusal's second line under its indent" indented
check "Fixes ask" gate fixes ask
printf '{"lines": [{"action": "recheck", "torrents": ["%s", "%s"]}, {"action": "reannounce", "torrents": ["%s"]}]}\n' \
    "$TA" "$TB" "$TA" >"$TW/plan.json"
word plan-dry "do" --plan "$TW/plan.json" --dry-run --json
check "dry run: exit 0, both lines would ask" answered 0 plan-dry.json \
    '.dryRun == true and ([.lines[].would] == ["ask", "ask"])'
asking plan "do" --plan "$TW/plan.json" --json
check "the plan: one ask opens" ask_open
step "The plan's ask" "'An agent asks to run a plan', 540 wide: Recheck 2 ($TA_NAME, $TB_NAME) and Reannounce 1, each ticked; the foot 'space ticks · tab · ↵ on a button · esc refuses'; refuse, allow these."
ask_keys Tab space
check "space unticks the first row" bash -c "omabox tree -b '$BOX' --window '$ASK' | grep -q 'check box \"$TA_NAME\" \[focused unchecked\]'"
ask_keys Tab Tab Tab Tab
if ask_focused "allow these"; then omabox keys -b "$BOX" --window "$ASK" Return >/dev/null 2>&1 || true; fi
collect plan
check "allowed: exit 0, Done: 2 of 3, $TA_NAME's recheck unticked" answered 0 plan.json \
    ".plan == true and .answer == \"allowed\" and .outcome == \"Done: 2 of 3 · 1 rechecking · by an agent\"
     and ([.lines[0].unticked[].hash] == [\"$TA\"]) and ([.lines[0].done[].hash] == [\"$TB\"])
     and (.lines[1].done | length) == 1"
check "the CLI counted what asks" said plan "Asking in omaseed's window… 2 rechecks, 1 reannounce ask."
check "the journal: one line for the plan (#304)" logged "agent: plan on 2 torrents (1 recheck, 1 reannounce), allowed in the window"
check "…and a line per torrent, by name" logged "agent: recheck on $TB_NAME, in the plan"
check "Fixes off again" gate fixes off
check "Remove off again" gate remove off

# ---- 1i. help agents (C50 GT7, #297) ---------------------------------------------------------
echo "1i. help agents"
word help-agents help agents --json
check "help agents --json: the address on show, Claude Code's rules" answered 0 help-agents.json \
    ".server == \"127.0.0.1:$TR_PORT\" and .agent == \"claude\" and (.text | contains(\"Bash(*127.0.0.1:$TR_PORT*)\"))"
word help-agents-gemini help agents --for gemini
gemini_rules() { plain help-agents-gemini && grep -q 'commandRegex' "$ANSWERS/help-agents-gemini.txt" &&
    grep -qF "$TR_PORT" "$ANSWERS/help-agents-gemini.txt"; }
check "help agents --for gemini: the policy file's rules with the address" gemini_rules

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

# ---- 5b. a move and its move back through do (C45 V14, V15, #293) -----------------------------
echo "5b. do move $TINY to $WRITABLE, then do move-back by its id"
TINY_HASH="$(hash_of "$TINY")"
TINY_HOME="$(curl -fs "$PLAY/torrents/info?hashes=$TINY_HASH" | jq -r '.[0].save_path | rtrimstr("/")')"
word move-off "do" move "$TINY_HASH" --to "$WRITABLE" --json
check "do move, Moves off: exit 5, not-allowed naming Moves" answered 5 move-off.json \
    '.error.code == "not-allowed" and (.error.message | contains("Moves is off"))'
check "Moves ask through the Agents row" gate moves ask
asking move "do" move "${TINY_HASH:0:8}" --to "$WRITABLE" --json
check "do move: the ask opens" ask_open
step "The move ask" "'An agent asks to move a torrent': $TINY by name, 'to …/agent-writable · SIZE · …', the move's own checks note; refuse and 'allow the move'."
allow_ask "allow the move"
collect move
check "allowed: exit 0, a job j-…, $TINY moving" answered 0 move.json \
    ".answer == \"allowed\" and (.job | test(\"^j-\")) and .to == \"$WRITABLE\"
     and ([.done[].hash] == [\"$TINY_HASH\"])"
check "the CLI said moves always ask" said move "moves always ask."
check "on the server: its save path is $WRITABLE" moved_to "$WRITABLE" 60 "$TINY_HASH"
check "on disk: $TINY in $WRITABLE, gone from $TINY_HOME" \
    test -f "$WRITABLE/$TINY" -a ! -e "$TINY_HOME/$TINY"
check "moves: landed to $WRITABLE, by an agent" until_answer 30 moves-agent \
    "(.landed[0].to == \"$WRITABLE\") and ([.landed[0].torrents[].hash] | index(\"$TINY_HASH\") != null)" moves --json
word moves-text moves
MOVE_ID="$(sed -n 's/.*id \([0-9a-z]\{8\}\) · .*/\1/p' "$ANSWERS/moves-text.txt" | head -n 1)"
id_printed() {
    [ -n "$MOVE_ID" ] && jq -e --arg id "$MOVE_ID" '.landed[0].id | startswith($id)' "$ANSWERS/moves-agent.json" >/dev/null
}
check "moves' lines print the landed move's id ($MOVE_ID), the newest first" id_printed
cursor_on "$TINY"
check "the story: the move's line says by an agent" main_has "by an agent" 10
step "Moved by an agent" "$TINY on the cursor; the story's move line ends '· by an agent'."
asking move-back "do" move-back "$MOVE_ID" --json
check "do move-back ID: the ask opens" ask_open
allow_ask "allow the move back"
collect move-back
check "allowed: exit 0, a job, $TINY going back" answered 0 move-back.json \
    ".answer == \"allowed\" and (.job | test(\"^j-\")) and ([.done[].hash] == [\"$TINY_HASH\"])"
check "on the server: back in $TINY_HOME" moved_to "$TINY_HOME" 60 "$TINY_HASH"
check "on disk: $TINY back in $TINY_HOME, gone from $WRITABLE" \
    test -f "$TINY_HOME/$TINY" -a ! -e "$WRITABLE/$TINY"
check "the same move back again, once omaseed sees it home: refused (a dry run)" until_answer 30 \
    move-back-again '.error.code == "refused"' "do" move-back "$MOVE_ID" --dry-run --json
check "…exit 5" exits 5
check "Moves off again" gate moves off

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
