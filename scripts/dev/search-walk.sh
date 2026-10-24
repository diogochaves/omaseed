#!/usr/bin/env bash
# omaseed — the search walkthrough (#145, board 48, DECISIONS C26): qBittorrent's search
# plugins, end to end, against a qBittorrent that lives in the box and plugins that never leave
# the machine. scripts/dev/search-plugins.py writes four invented plugins whose rows are the
# offline swarm's own torrents (a magnet for its public one, links to .torrent files the plugin
# serves itself for its private ones), so a result is added and downloads for real.
#
#   scripts/dev/search-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step;
#                                               # OUT_DIR/index.md says what to look for in each,
#                                               # OUT_DIR/checks.md lists the checks
#   scripts/dev/search-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                               # script still presses the keys, then says what
#                                               # to check and waits for enter
#   OMASEED_BUILD=DIR                           # the build to run (default build/)
#
# The box comes from `scripts/dev/app-box.sh up --qbt` (qbittorrent-nox on the box's
# 127.0.0.1:8090, an empty library, reaching only the swarm's tracker and seeder). The steps:
#   - ctrl+f with no plugins: the empty state, its link to Settings › qBittorrent › Search
#     plugins;
#   - plugins a, b and c installed there from files (the server is this computer); a missing
#     file "didn't install" after 30 s; plugin c off and on again;
#   - "sampler": the rows fill in; plugin b fails, its reason from qBittorrent's log; the two
#     copies of the public torrent fold into one row; the plugins' answers; turn plugin b off;
#   - space on the folded row: the result on the right; enter: the add sheet "from search",
#     add, it downloads from the swarm, and the row says added;
#   - a .torrent link: the add sheet fetches it through its plugin (`downloader`), its files
#     listed; add; it arrives;
#   - hide dead; `/` filters the rows; plugin d (slow): "still looking" past 30 s, stop asking;
#   - uninstall plugin a from ⋯.
# Each check reads the box's qBittorrent; any failed check makes the script exit 1. The box goes
# down at the end, also on ^C.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
BOX=search-walk
BUILD="${OMASEED_BUILD:-build}"
QBT_PORT=8090 # app-box.sh's qBittorrent in the box
API="http://127.0.0.1:$QBT_PORT/api/v2"
PLUGINS=/home/sbx/search-plugins # where the plugins are, inside the box
refuse_home "$QBT_PORT"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-search-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x "$BUILD/omaseed" ] || { echo "build first: cmake --build $BUILD" >&2; exit 1; }

# api PATH [CURL ARGS]: the box's qBittorrent, from inside the box.
api() {
    local path="$1"
    shift
    omabox run -b "$BOX" -- curl -fs "$@" "$API/$path" 2>/dev/null || true
}

# plugin_state NAME: "on", "off" or "none" for the box's plugin NAME.
plugin_state() {
    api search/plugins | python3 -c '
import json, sys
for p in json.load(sys.stdin) or []:
    if p["name"] == sys.argv[1]:
        print("on" if p["enabled"] else "off"); break
else:
    print("none")' "$1" 2>/dev/null || echo none
}

# torrent FIELD NAME: a field of the box's torrent called NAME, "" when it isn't there. By name:
# a hybrid added by its v1 magnet is keyed by its v2 hash once its metadata is in.
torrent() {
    api torrents/info | python3 -c '
import json, sys
rows = [t for t in json.load(sys.stdin) or [] if t["name"] == sys.argv[2]]
print(rows[0].get(sys.argv[1], "") if rows else "")' "$1" "$2" 2>/dev/null || true
}

# done_or_not NAME: "complete" once the box's torrent NAME has every byte.
done_or_not() {
    [ "$(torrent amount_left "$1")" = 0 ] && echo "complete" || echo "not yet"
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

# install SOURCE: typed into Settings › qBittorrent › Search plugins' field, enter.
install() {
    click_text "a search plugin to install" || true
    keys ctrl+a -t "$1" Return
}

cleanup() {
    "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [ "$INTERACTIVE" = 0 ]; then
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Search walk %s\n\nBuild `%s`. Each shot: what should be on screen. Checks: checks.md.\n\n' \
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
# The invented plugins, in the box's HOME; plugin d answers after 40 s.
python3 -I "$repo/scripts/dev/search-plugins.py" make "$(omabox path -b "$BOX")/home/search-plugins" --slow 40 >/dev/null

# ---- no plugins (SR13) ------------------------------------------------------------------------
keys ctrl+f
step "Search with no plugins" "The rail's search row lit (after log); the title Search, its sentence 'no search plugins'; the empty state: 'No search plugins', what a plugin is without naming any, 'install a plugin…', and the place in Settings. No bar."
shows "the empty state" "No search plugins"
click_text "install a plugin" || true
shows "the plugins pane" "a search plugin to install"
step "The plugins pane, empty" "Settings › qBittorrent › Search (lit in the nav): the scope line (box's plugins), 'install a plugin' with a field that takes a URL or a .py file here and browse…, 0 installed, check for updates, and 'No plugins yet…' naming none."

# ---- install, a failed install, a switch (SR10) ----------------------------------------------
install "$PLUGINS/omaseed_plugin_a.py"
wait_for "plugin a installed" on 15 plugin_state omaseed_plugin_a
shows "installed: plugin a" "installed: plugin a"
step "Plugin a installed" "The line under the field: a green dot and 'installed: plugin a'; the list: 1 installed, plugin a with its switch on, its version and its types (movies · music · software)."
install "$PLUGINS/omaseed_plugin_b.py"
wait_for "plugin b installed" on 15 plugin_state omaseed_plugin_b
install "$PLUGINS/omaseed_plugin_c.py"
wait_for "plugin c installed" on 15 plugin_state omaseed_plugin_c
install "$PLUGINS/no-such-plugin.py"
shows "a missing file didn't install" "no-such-plugin.py didn't install" 40
step "A plugin that didn't install" "The line in yellow: '/home/sbx/search-plugins/no-such-plugin.py didn't install. After 30 s box lists no new plugin, and qBittorrent gives no reason…'; the three plugins listed."
click_text "use plugin c" || true
wait_for "plugin c switched off" off 10 plugin_state omaseed_plugin_c
step "Plugin c off" "plugin c's switch off, its name dim; the others on."
click_text "use plugin c" || true
wait_for "plugin c switched on again" on 10 plugin_state omaseed_plugin_c
keys Escape Escape Escape
check "settings closed" "$(find_text "a search plugin to install" && echo open || echo closed)" closed

# ---- a search, filling in (SR1–SR5) ------------------------------------------------------------
keys ctrl+f -t "sampler" Return
step "Asking" "The field holds 'sampler'; the plugins' line: what has come so far and a pill per plugin (a dot pulsing while asked); rows appear as each plugin answers."
jobs="$(api search/status | python3 -c 'import json, sys; print(sum(1 for j in json.load(sys.stdin) if j["pattern"] == "sampler"))')"
check "one job per plugin" "$jobs" 3
shows "plugin b's failure is named" "1 of 3 plugins didn't answer" 20
step "The results" "Three rows, most seeds first: Nested Library Sampler (2026) (⋯, a .torrent link), Public Domain Sampler ('plugin a and plugin c · 2 copies folded, 2 trackers'), sampler commentary track (0, no seeds, dim). The line: '3 torrents from 4 results · most seeds, then newest', plugin a 2, plugin b failed (yellow), plugin c 2, '1 of 3 plugins didn't answer ›'. The title's sentence: 'sampler' in quotes · 3 torrents from 2 of 3 plugins."
shows "the folded row" "Public Domain Sampler"
click_text "1 of 3 plugins didn't answer" || true
shows "the failure's reason, from the log" "qBittorrent's log" 5
step "The plugins' answers" "A menu: '3 plugins on box, this search'; plugin a 2 results in N s; plugin b failed after N s, \"qBittorrent's log, HH:MM\" and its reason in quotes (RuntimeError: the invented site answered nothing), the log line, search it again, turn it off… (red); plugin c 2 results."
click_text "turn it off" || true
wait_for "turn it off: plugin b off" off 10 plugin_state omaseed_plugin_b

# ---- a result, and add… (SR3, SR9) ---------------------------------------------------------------
keys ctrl+f Down j space
shows "the result on the right" "2 copies of one torrent" 5
step "The result on the right" "The folded row has the cursor; on the right: its name, 12 seeds (green), 'Two copies…', copies (plugin a names it, plugin c folded in with 1 new tracker), the trackers in its magnet (one 'from plugin c'), its facts as plugin a says, and add… · copy magnet · open its page."
keys Return
shows "the add sheet from search" "Add torrent" 5
# Wait for the file list before adding: qBittorrent 5.3.0 crashes when a magnet is added while its
# metadata lookup is still running (the add sheet does that on an early add).
shows "the magnet's files, asked of the swarm" "3 files" 30
step "The add sheet from a result" "Today's add sheet: the head's line 'Public Domain Sampler · 24.1 MB · from search · plugin a and plugin c · a magnet with 2 trackers'; the magnet in the field; the file list being asked for, then listed."
keys Return
wait_for "the folded magnet was added" "Public Domain Sampler" 20 torrent name "Public Domain Sampler"
wait_for "it downloads from the swarm" complete 90 done_or_not "Public Domain Sampler"
shows "the row says added" "added" 10
step "Added" "Back on the results, the row's glyph ↓, 'added' beside it, its line 'added HH:MM · …' and trackers… where add… was; the library counts 1."

# ---- a link, through its plugin (SR9, downloader) ---------------------------------------------
keys Escape k Return
shows "the link's files, fetched through plugin a" "113 files" 20
step "A link, fetched through its plugin" "The add sheet on 'http://plugin-a.invalid/get/1.torrent' with 'from search · plugin a · a link to a .torrent'; the box's qBittorrent fetched it through plugin a: the Nested Library Sampler's 113 files listed."
keys Return
nested="$(api torrents/info | python3 -c 'import json, sys; print(sum(1 for t in json.load(sys.stdin) if t["name"].startswith("Nested Library Sampler")))')"
for _ in $(seq 1 20); do
    [ "$nested" = 1 ] && break
    sleep 1
    nested="$(api torrents/info | python3 -c 'import json, sys; print(sum(1 for t in json.load(sys.stdin) if t["name"].startswith("Nested Library Sampler")))')"
done
check "the link was added through its plugin" "$nested" 1

# ---- hide dead, `/` (SR4, SR14) -----------------------------------------------------------------
click_text "hide dead" || true
shows "hide dead says what it hid" "1 hidden" 5
step "Hide dead" "The 0-seed row gone, a line under the rows: '1 hidden: no seeds when the plugin looked · rows whose seeds aren't reported stay'; the chip '✓ hide dead'."
click_text "✓ hide dead" || true
keys ctrl+f Down slash -t "nested"
check "/ narrows the rows" "$(find_text "Public Domain Sampler" && echo shown || echo gone)" gone
step "/ filters the results" "The title bar's filter holds 'nested' and counts '1 of 3'; only the Nested Library Sampler row is left; nothing was asked again."
keys Escape

# ---- a slow plugin (SR2) ------------------------------------------------------------------------
api search/installPlugin --data-urlencode "sources=$PLUGINS/omaseed_plugin_d.py" >/dev/null
wait_for "plugin d installed" on 15 plugin_state omaseed_plugin_d
keys ctrl+f -t "sampler" Return
# Done without plugin d once 30 s have passed: the line names who didn't answer.
shows "slow: done without it past 30 s" "1 of 3 plugins didn't answer" 45
step "A slow plugin" "Past 30 s: the search is done without plugin d; its pill 'plugin d still looking' in yellow, pulsing; '1 of 3 plugins didn't answer ›'."
click_text "1 of 3 plugins didn't answer" || true
shows "stop asking is offered" "stop asking" 5
step "Slow, in the answers" "plugin d: 'no answer in 30 s', still asked until qBittorrent's 3 min limit, stop asking and turn it off…; the footer says a slow plugin isn't stopped."
click_text "stop asking" || true
running="$(api search/status | python3 -c 'import json, sys; print(sum(1 for j in json.load(sys.stdin) if j["status"] == "Running"))')"
check "stop asking stopped its job" "$running" 0

# ---- uninstall (SR10) ---------------------------------------------------------------------------
keys Escape ctrl+shift+comma h 9 j
shows "back in the plugins pane" "a search plugin to install" 5
click_text "⋯" || true
click_text "uninstall" || true
wait_for "plugin a uninstalled" none 10 plugin_state omaseed_plugin_a
step "Plugin a uninstalled" "The list without plugin a: plugin b (off, its line 'turned off · it failed in the last search, HH:MM'), plugin c and plugin d."
keys Escape Escape Escape

echo "the box's library at the end:"
api torrents/info | python3 -c 'import json, sys; [print("  ", t["name"], t["state"]) for t in json.load(sys.stdin)]' || true
echo
if [ "$FAILED" -gt 0 ]; then
    echo "search walk: $FAILED check(s) FAILED${OUT:+; see $OUT/checks.md}" >&2
    exit 1
fi
echo "search walk: every check passed${OUT:+; shots and notes in $OUT/index.md}"
