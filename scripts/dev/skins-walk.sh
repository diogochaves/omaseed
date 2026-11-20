#!/usr/bin/env bash
# omaseed — the skins walk (board 55, DECISIONS C37, #221, #227): a person's own skin, made,
# checked, drawn and given back, with the words an agent uses (`omaseed skin list|clone|use`,
# `omaseed check`), run in a box and checked with jq and their exit codes. skin list has the two
# built-ins and none of yours; skin clone console warm writes a skin.json with $schema,
# cloned_from and every seam's face, and a second clone of the same name is refused with the file
# untouched; check warm draws it with nothing dropped and leaves a picture; a token out of range
# is dropped with its why, a broken file is drawn as Omarchy's (exit 1). With the app open, skin
# use warm draws it live (a log line), and a save while it is drawn repaints it (the line again).
# Settings › Appearance: the Skin row's second line (yours · warm · open folder), its yellow line
# for a dropped token and for a broken file. skin use omarchy is the way back. With omaseed quit,
# skin use warm saves the choice in state.json, every other key kept. Last, warm.v2 (a dotted
# name) is cloned, checked, used, and drawn when omaseed starts again.
#
#   scripts/dev/skins-walk.sh [OUT_DIR]        # agents: a headless box; each word's answer in
#                                              # OUT_DIR (NAME.json, NAME.txt, NAME.err), a shot
#                                              # per window step, OUT_DIR/index.md lists the checks
#   scripts/dev/skins-walk.sh --interactive    # Diogo: the box is a window on workspace 9; each
#                                              # window step waits for enter
#
# The box is scripts/dev/app-box.sh up (the :8081 playground, started if it isn't up). Every
# skin is invented and lives in the box's own ~/.config/omaseed/skins/; nothing outside the box
# is written but OUT_DIR.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a box is a new install; the table is what the other walks pin.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
# The walk picks skins itself: a pinned one would win over every choice it makes.
unset OMASEED_SKIN
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PLAY_PORT="${OMASEED_TEST_PORT:-8081}"
refuse_home "$PLAY_PORT"
APP_ID=io.github.diogochaves.omaseed
BOX=skins-walk
BOX_HOME=/home/sbx
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-skins-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac
# The answers go somewhere in both modes; interactive keeps them in a temporary folder.
ANSWERS="${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-skins-walk-XXXX")}"
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
        omabox shot -b "$BOX" --window "$APP_ID" -o "$OUT/$file" >/dev/null 2>&1 || true
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

keys() {
    omabox keys -b "$BOX" --window "$APP_ID" "$@" >/dev/null
    omabox wait -b "$BOX" --timeout 5s still >/dev/null || true
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

# The box's files, as this side sees them.
home=""
skin_file() { echo "$home/.config/omaseed/skins/$1/skin.json"; }
state_file() { echo "$home/.config/omaseed/state.json"; }

# put_skin NAME CONTENT: write the box's skins/NAME/skin.json by rename, as an editor saves.
put_skin() {
    # shellcheck disable=SC2016 # expanded by the box's shell, from its arguments
    omabox run -b "$BOX" -- sh -c 'f="$HOME/.config/omaseed/skins/$1/skin.json" &&
        printf "%s\n" "$2" >"$f.next" && mv "$f.next" "$f"' sh "$1" "$2"
}
# edit_skin NAME JQ: the skin's file through a jq filter, saved by rename.
edit_skin() { put_skin "$1" "$(jq "$2" "$(skin_file "$1")")"; }

# skin_lines NAME: how many times the app logged drawing NAME ("skin: NAME (choice …").
APP_LOG=""
skin_lines() { grep -acF "skin: $1 (choice " "$APP_LOG" 2>/dev/null || true; }
last_skin_line() { grep -aF "skin: " "$APP_LOG" 2>/dev/null | grep -a "(choice " | tail -n 1 || true; }
# until_lines NAME N: until the app has logged drawing NAME N times (10 s).
until_lines() {
    for _ in $(seq 1 40); do
        [ "$(skin_lines "$1")" -ge "$2" ] && return 0
        sleep 0.25
    done
    echo "  (waited 10s: $(skin_lines "$1") lines for $1, not $2; last: $(last_skin_line))" >&2
    return 1
}
# logged TEXT: the app's last skin line has TEXT (10 s).
logged() {
    for _ in $(seq 1 40); do
        case "$(last_skin_line)" in *"$1"*) return 0 ;; esac
        sleep 0.25
    done
    echo "  (waited 10s: the last skin line is \"$(last_skin_line)\", not \"$1\")" >&2
    return 1
}

# quit_app: omaseed quit in the box, waited for.
quit_app() {
    omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
    for _ in $(seq 1 20); do
        omabox run -b "$BOX" -- pgrep -x omaseed >/dev/null 2>&1 || return 0
        sleep 0.5
    done
}

cleanup() {
    [ -z "$OUT" ] || "$repo/scripts/dev/look.sh" "$OUT" || true  # copies to Read (#251)
    [ -z "$OUT" ] || [ -z "$APP_LOG" ] || cp "$APP_LOG" "$OUT/app.log" 2>/dev/null || true
    [ "$INTERACTIVE" = 1 ] || "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$repo/scripts/dev/test-server.sh" status >/dev/null 2>&1 || "$repo/scripts/dev/test-server.sh" start >/dev/null

echo "starting the box"
if [ "$INTERACTIVE" = 1 ]; then
    up="$("$APP_BOX" up -b "$BOX" --interactive)"
else
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Skins walk %s\n\nBuild `%s`. Each answer is beside this file (NAME.json, NAME.txt, NAME.err).\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    up="$("$APP_BOX" up -b "$BOX")"
fi
# The app's own output, where it says which skin it draws (app-box.sh's "(log: L)").
APP_LOG="$(sed -n 's/.*(log: \(.*\))$/\1/p' <<<"$up" | tail -n 1)"
home="$(omabox path -b "$BOX")/home"
omabox pointer -b "$BOX" -- move 5 5 >/dev/null 2>&1 || true

# ---- 1. skin list, skin clone --------------------------------------------------------------
echo "1. the list, and a clone"
word list0 skin list --json
check "skin list: omarchy and console built in, none yours, the choice omarchy" answered 0 list0.json \
    '.command == "skin list" and .choice == "omarchy" and .pinned == null
     and [.skins[].name] == ["omarchy", "console"] and ([.skins[] | .builtIn] | all)'
word clone1 skin clone console warm --json
check "skin clone console warm: exit 0, its path" answered 0 clone1.json \
    ".skin == \"warm\" and .from == \"console\" and .path == \"$BOX_HOME/.config/omaseed/skins/warm/skin.json\""
cloned_right() {
    jq -e '.["$schema"] == "../../skin.schema.json" and .cloned_from == {"skin": "console", "version": 1}
           and (.faces | keys) == ["Lamp", "Meter", "Plate", "SelectionMark"]
           and (has("skin") | not)' "$(skin_file warm)" >/dev/null
}
check "warm/skin.json: \$schema, cloned_from console, a face for every seam, no skin key" cloned_right
schema_ours() {
    [ "$(jq -r '.["$comment"]' "$home/.config/omaseed/skin.schema.json")" = \
        "Written by omaseed on every start. Edits here are overwritten." ]
}
check "the clone's \$schema is the skin.schema.json omaseed wrote beside AGENTS.md" schema_ours
SUM="$(sha256sum <"$(skin_file warm)")"
word clone2 skin clone console warm --json
check "skin clone console warm again: exit 2, skin-exists" answered 2 clone2.json '.error.code == "skin-exists"'
check "…and warm's bytes unchanged" test "$(sha256sum <"$(skin_file warm)")" = "$SUM"
word clone-case skin clone omarchy WARM --json
check "skin clone omarchy WARM: exit 2, skin-exists (names ignore case)" answered 2 clone-case.json \
    '.error.code == "skin-exists"'
word clone-builtin skin clone omarchy Console --json
check "skin clone omarchy Console: exit 2, bad-name" answered 2 clone-builtin.json '.error.code == "bad-name"'
word list1 skin list
check "skin list without --json: warm's line, from its folder" grep -q "^  warm .*skins/warm" "$ANSWERS/list1.txt"

# ---- 2. check --------------------------------------------------------------------------------
echo "2. check warm"
word check1 check warm --json
check "check warm: exit 0, drawn, nothing dropped, a picture" answered 0 check1.json \
    ".skin == \"warm\" and .drawn == true and .dropped == [] and .lifted != null
     and (.mode == \"dark\" or .mode == \"light\") and (.baseSize | type) == \"number\"
     and .picture == \"$BOX_HOME/.cache/omaseed/check/warm.png\""
check "…the picture is there" test -s "$home/.cache/omaseed/check/warm.png"
cp "$home/.cache/omaseed/check/warm.png" "$ANSWERS/check-warm.png" 2>/dev/null || true
edit_skin warm '.grounds.hover.alpha = 1.4'
word check2 check warm --json
check "grounds.hover.alpha 1.4: exit 0, dropped with its why" answered 0 check2.json \
    '.drawn == true and ([.dropped[] | select(.token == "grounds.hover" and (.why | test("alpha")))] | length) == 1'
word check2-text check warm
check "…its lines say dropped grounds.hover" grep -q "dropped grounds.hover" "$ANSWERS/check2-text.txt"
GOOD="$(jq '.grounds.hover.alpha = 0.45' "$(skin_file warm)")"
put_skin warm '{"version": 1, "grounds": '
word check3 check warm --json
check "a broken skin.json: exit 1, drawn as Omarchy's, why" answered 1 check3.json \
    '.drawn == false and (.why | test("parse"))'
put_skin warm "$GOOD"
word check4 check warm --json
check "fixed: exit 0 again" answered 0 check4.json '.drawn == true and .dropped == []'
word check-none check nobody --json
check "check nobody: exit 2, unknown-skin" answered 2 check-none.json '.error.code == "unknown-skin"'

# ---- 3. skin use, live -----------------------------------------------------------------------
echo "3. skin use warm, the app open"
word use1 skin use warm --json
check "skin use warm: exit 0, live, drawn" answered 0 use1.json \
    '.live == true and .skin.name == "warm" and .skin.drawn == true'
check "the app logged drawing warm" logged "skin: warm (choice warm"
LINES="$(skin_lines warm)"
edit_skin warm '.description = "Warmer and rounder."'
check "a save to warm while drawn: drawn again (a repaint)" until_lines warm $((LINES + 1))
word use-desktop skin use desktop --json
check "skin use desktop: exit 2, bad-name (Follow the desktop is the Skin row's)" answered 2 use-desktop.json \
    '.error.code == "bad-name"'

# ---- 4. Settings › Appearance ------------------------------------------------------------
echo "4. the Skin row"
# omaseed's own settings (,), Appearance (5), the keyboard on its first row: the Skin row.
keys comma 5 l
check "the Skin row's second line: yours, warm, open folder" in_tree "open folder" 10
word list-desc skin list --json
check "…its description, as skin list has it (the row's caption: the shot)" answered 0 list-desc.json \
    '[.skins[] | select(.name == "warm") | .description] == ["Warmer and rounder."]'
step "Yours" "Appearance › Skin: the three chips, then 'yours' · warm (lit) · open folder; the description 'Warmer and rounder.'; no yellow line."
LINES="$(skin_lines warm)"
edit_skin warm '.grounds.hover.alpha = 1.4'
until_lines warm $((LINES + 1)) || true
check "a dropped token: the yellow line names it" in_tree "warm: 1 token left at Omarchy's" 10
check "the app's line counts it" logged "dropped 1"
step "A token dropped" "The yellow line: 'warm: 1 token left at Omarchy's: grounds.hover.alpha 1.4 isn't between 0 and 1, nor shell.'"
put_skin warm '{"version": 1, "grounds": '
check "a broken file: the row says warm isn't drawn" in_tree "warm isn't drawn: its skin.json doesn't parse." 10
check "the app's line says why" logged "not drawn: unparseable"
step "Not drawn" "The yellow line: 'warm isn't drawn: its skin.json doesn't parse.'; Omarchy's grounds behind it; warm still the chip lit."
put_skin warm "$(jq '.description = "Warmer and rounder."' <<<"$GOOD")"
check "the good save draws warm again" logged "skin: warm (choice warm, fonts the skin's)"
keys Escape Escape

# ---- 5. the way back ---------------------------------------------------------------------
echo "5. skin use omarchy"
word use-back skin use omarchy --json
check "skin use omarchy: exit 0, live" answered 0 use-back.json '.live == true and .skin.name == "omarchy"'
check "the app logged drawing omarchy" logged "skin: omarchy (choice omarchy"
check "state.json: omarchy" test "$(jq -r '.appearance.skin' "$(state_file)")" = omarchy
step "Back" "Omarchy's skin: the window as a new install draws it."

# ---- 6. none running ---------------------------------------------------------------------
echo "6. omaseed quit, skin use warm"
quit_app
jq -S 'del(.appearance.skin)' "$(state_file)" >"$ANSWERS/state-before.json"
word use-saved skin use warm --json
check "skin use warm with omaseed quit: exit 0, not live" answered 0 use-saved.json \
    '.live == false and .skin.name == "warm"'
check "state.json: appearance.skin is warm" test "$(jq -r '.appearance.skin' "$(state_file)")" = warm
jq -S 'del(.appearance.skin)' "$(state_file)" >"$ANSWERS/state-after.json"
check "…every other key kept" cmp -s "$ANSWERS/state-before.json" "$ANSWERS/state-after.json"
word use-saved-text skin use warm
check "…its line: saved, drawn at the next start" grep -q "warm is saved" "$ANSWERS/use-saved-text.txt"

# ---- 7. a dotted name --------------------------------------------------------------------
echo "7. warm.v2"
word clone-v2 skin clone warm warm.v2 --json
check "skin clone warm warm.v2: exit 0" answered 0 clone-v2.json '.skin == "warm.v2" and .from == "warm"'
word check-v2 check warm.v2 --json
check "check warm.v2: exit 0, drawn, its picture" answered 0 check-v2.json \
    ".drawn == true and .picture == \"$BOX_HOME/.cache/omaseed/check/warm.v2.png\""
word use-v2 skin use warm.v2 --json
check "skin use warm.v2 (none running): exit 0, saved" answered 0 use-v2.json '.live == false'
[ -z "$OUT" ] || cp "$APP_LOG" "$OUT/app-before-restart.log" 2>/dev/null || true
up="$("$APP_BOX" restart -b "$BOX")"
APP_LOG="$(sed -n 's/.*(log: \(.*\))$/\1/p' <<<"$up" | tail -n 1)"
check "omaseed started again draws warm.v2" logged "skin: warm.v2 (choice warm.v2"
word list2 skin list --json
check "skin list: warm and warm.v2 after the built-ins, warm.v2 the choice" answered 0 list2.json \
    '[.skins[].name] == ["omarchy", "console", "warm", "warm.v2"] and .choice == "warm.v2"
     and ([.skins[] | select(.name == "warm.v2") | .clonedFrom.skin] == ["warm"])'
step "warm.v2" "omaseed started on warm.v2: the console-like grounds of the clone."

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
