#!/usr/bin/env bash
# omaseed — the shared-menu walkthrough (#73, docs/33 §11: D10, D24, D25). One menu, built once
# in the app, shown by the tray and by the bar widget popup's ⋯: first run puts the widget in
# the bar; with one server the ⋯ has no servers and the server line no ▾ (D19); "private" is
# added; ctrl+. opens the menu and switches to private, ctrl+s opens the servers and a click
# goes back to public; quit from the ⋯. Then "In the bar: both" (D25, read from the two old
# settings), the tray's menu with its servers block, a switch from it, and quit from it. What
# each action must leave is checked in servers.json and the process list, not only in shots.
#
#   scripts/dev/menu-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                             # OUT_DIR/index.md says what to look for in each
#   scripts/dev/menu-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                             # script still does the typing, then waits for enter
#
# The box is a manual box with Omarchy's stock bar (scripts/dev/app-box.sh up --manual
# --stock-bar: the tray lives in it, at the top right of a 1920x1080 screen): omaseed from
# build-box/ installed in it, "public" on the box's 127.0.0.1:8090 and "private" on :8091. The
# logins stay in the box, as in servers-walk.sh.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
APP_ID=io.github.diogochaves.omaseed
BOX="${OMASEED_MENU_BOX:-menu-walk}"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-menu-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

# Where the stock bar puts the tray at 1920x1080: its drawer's "<", and the omaseed icon once
# the drawer is open (the icon takes the place the "<" had).
TRAY_X=1783
TRAY_Y=13

keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

# clip SERVER: SERVER's password onto the box's clipboard, from the login file the box keeps.
clip() {
    # shellcheck disable=SC2016 # expanded by the box's sh
    omabox run -b "$BOX" -- sh -c \
        'sed -n "s/^password  //p" -- "$HOME/.omaseed-box-logins/$1.txt" | tr -d "\n" | wl-copy' \
        sh "$1"
}
unclip() { omabox run -b "$BOX" -- wl-copy --clear >/dev/null 2>&1 || true; }

# default_is NAME: servers.json's default server is NAME.
default_is() {
    python3 -c '
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
names = {s["id"]: s["name"] for s in d["servers"]}
sys.exit(0 if names.get(d.get("defaultId")) == sys.argv[2] else 1)' \
        "$home/.config/omaseed/servers.json" "$1"
}

# status_has EXPR: python EXPR over the box's status.json (`s`) is true.
status_has() {
    python3 -c '
import json, sys
s = json.load(open(sys.argv[1], encoding="utf-8"))
sys.exit(0 if eval(sys.argv[2]) else 1)' "$run/omaseed/status.json" "$1"
}

# tray_rows: the tray's menu as omaseed exports it (com.canonical.dbusmenu), one row a line:
# "ID<tab>TOGGLE<tab>LABEL", TOGGLE 1 for a checked row. What the shell draws comes from this.
tray_rows() {
    local item service path menu
    item="$(omabox run -b "$BOX" -- busctl --user --json=short get-property org.kde.StatusNotifierWatcher \
        /StatusNotifierWatcher org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems |
        python3 -c 'import json, sys; print(json.load(sys.stdin)["data"][0])')"
    service="${item%%/*}"
    path="/${item#*/}"
    menu="$(omabox run -b "$BOX" -- busctl --user --json=short get-property "$service" "$path" \
        org.kde.StatusNotifierItem Menu | python3 -c 'import json, sys; print(json.load(sys.stdin)["data"])')"
    echo "$service $menu" >"$OUT_ROWS.where"
    omabox run -b "$BOX" -- busctl --user --json=short call "$service" "$menu" com.canonical.dbusmenu \
        GetLayout iias 0 -- -1 0 | python3 -c '
import json, sys
def walk(node):
    ident, props, children = node[0], node[1], node[2]
    label = props.get("label", {}).get("data", "")
    toggle = props.get("toggle-state", {}).get("data", 0)
    if props.get("type", {}).get("data") != "separator" and label:
        print("%s\t%s\t%s" % (ident, toggle, label))
    for child in children:
        walk(child["data"])
walk(json.load(sys.stdin)["data"][1])'
}

# tray_click LABEL: what a click on the tray menu's row LABEL sends (dbusmenu Event "clicked").
tray_click() {
    local id where
    id="$(tray_rows | awk -F '\t' -v l="$1" '$3 == l { print $1; exit }')"
    [ -n "$id" ] || return 1
    read -r -a where <"$OUT_ROWS.where"
    omabox run -b "$BOX" -- busctl --user call "${where[0]}" "${where[1]}" com.canonical.dbusmenu \
        Event isvu "$id" clicked s "" 0 >/dev/null
}

# tray_checked LABEL: the tray menu's row LABEL is checked.
tray_checked() { tray_rows | awk -F '\t' -v l="$1" '$3 == l && $2 == 1 { found = 1 } END { exit !found }'; }
tray_ends_with_quit() { [ "$(tray_rows | tail -1 | cut -f3)" = quit ]; }

running() { omabox run -b "$BOX" -- pgrep -x omaseed >/dev/null 2>&1; }
not_running() { ! running; }

popup() { omabox run -b "$BOX" -- omarchy-shell chaves.omaseed toggle >/dev/null; settle; }
settle() { omabox wait -b "$BOX" --timeout 5s still >/dev/null 2>&1 || true; }

STEP=0
CHECKS=""
FAILED=0
# step "title" "what to look for" [GEOMETRY]: a screenshot (headless) or a pause.
step() {
    STEP=$((STEP + 1))
    local file
    file="$(printf '%02d' "$STEP")-$(echo "$1" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-').png"
    if [ "$INTERACTIVE" = 1 ]; then
        printf '\n\033[1m%02d %s\033[0m\n    %s\n    enter for the next step ' "$STEP" "$1" "$2"
        read -r _
    else
        settle
        if [ -n "${3:-}" ]; then
            omabox shot -b "$BOX" -g "$3" -o "$OUT/$file" >/dev/null 2>&1
        else
            omabox shot -b "$BOX" -o "$OUT/$file" >/dev/null 2>&1
        fi
        printf -- '- **%02d %s**: %s\n  ![](%s)\n' "$STEP" "$1" "$2" "$file" >>"$OUT/index.md"
        echo "$(printf '%02d' "$STEP") $1 -> $OUT/$file"
    fi
}

check() {
    local what="$1"; shift
    local mark="FAIL"
    if "$@"; then mark="ok"; else FAILED=$((FAILED + 1)); fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}

cleanup() {
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

POPUP="1530,0 390x620"
TRAY_MENU="1600,0 320x500"

# ---- the box --------------------------------------------------------------------------------
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --manual --interactive --stock-bar -b "$BOX" | grep -v 'admin / '
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Menu walk %s\n\nBuild `%s`. Omarchy'"'"'s stock bar; two qBittorrents in the box: public (:8090) and private (:8091). Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up --manual --headless --stock-bar -b "$BOX" | grep -v 'admin / '
fi
home="$(omabox path -b "$BOX")/home"
run="$(omabox path -b "$BOX" --run)"
OUT_ROWS="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-menu-walk-tray-XXXX")/tray"

# ---- first run, the widget put in the bar ----------------------------------------------------
"$APP_BOX" restart -b "$BOX" >/dev/null
keys Return -s 800
clip public
keys Return -s 600 Tab ctrl+a -t public Tab Tab space Tab Tab -t admin Tab ctrl+v Tab Return -s 2500
unclip
# continue, "not now" on links, then the bar step's own focus: "add to the bar".
keys Tab Tab Return -s 1500 Tab Return -s 1500 Return -s 4000
keys Return -s 3000
check "the widget is in the bar: its plugin is linked" test -L "$home/.config/omarchy/plugins/chaves.omaseed"
check "status.json lists one server and the menu (v stays 2)" \
    status_has 's["v"] == 2 and len(s["servers"]) == 1 and s["menu"][-1]["id"] == "quit"'
check "one server: no servers rows in the menu (D19)" \
    status_has 'not any(r["id"].startswith("server:") for r in s["menu"])'

popup
keys ctrl+period -s 600
step "One server, the menu" "W1 with one server (T0): ⋯ lit in the head, the server line with no ▾; the menu: Alternative Speed Limits (or Turtle/Burst) with A, Stop All, Start All, a hairline, Add Torrent… with m, omaseed Settings…, a hairline, Quit omaseed. No SERVERS." "$POPUP"
keys ctrl+period -s 400 Escape -s 600

# ---- the second server ---------------------------------------------------------------------
clip private
keys --window "$APP_ID" comma -s 600 2 -s 300 a -s 800 -t private Tab -t http://127.0.0.1:8091 Tab Tab space Tab Tab \
    -t admin Tab ctrl+v Tab Tab Tab Tab -s 800 Tab Return -s 2500 Return -s 1200
unclip
keys Escape -s 1500
check "two servers in status.json, two server rows in the menu" \
    status_has 'len(s["servers"]) == 2 and sum(r["kind"] == "radio" for r in s["menu"]) == 2'

# ---- the widget: ⋯ and ▾ -------------------------------------------------------------------
popup
keys ctrl+period -s 600
step "Two servers, the menu" "W1: SERVERS, ● public (0 torrents) and ○ private, a hairline, then the rows of the last shot. The server line ends in ▾. The cursor on public." "$POPUP"
keys j -s 300 Return -s 3000
check "the ⋯ menu switched to private" default_is private
step "Switched from the widget" "The popup stayed open and refilled: PRIVATE · QBITTORRENT in the head, the menu closed, Add lists the Downloads files with \"already on private\"." "$POPUP"
keys ctrl+s -s 600
step "The server line's ▾" "W2: just the servers, ○ public 0 torrents, ● private 4 torrents (the cursor on it); the ▾ turned ▴ in the accent." "$POPUP"
keys k -s 300 Return -s 3000
check "the servers part switched back to public" default_is public
keys ctrl+period -s 600 j j j j j j j -s 400
step "Quit, from the widget" "The cursor on Quit omaseed, the menu's last row after a hairline (§11 change 2)." "$POPUP"
keys Return
omabox wait -b "$BOX" --timeout 10s window "$APP_ID" --gone >/dev/null 2>&1 || true
check "quit from the widget: omaseed is gone" not_running

# ---- D25: "In the bar: both", from the old settings ----------------------------------------
# The two old settings, as an earlier omaseed wrote them: the widget in the bar and the tray
# icon "always" read as "both".
python3 - "$home/.config/omaseed/state.json" <<'PY'
import json, sys
path = sys.argv[1]
state = json.load(open(path, encoding="utf-8"))
state.setdefault("appearance", {})["trayIconMode"] = "always"
json.dump(state, open(path, "w", encoding="utf-8"))
PY
"$APP_BOX" restart -b "$BOX" >/dev/null
keys --window "$APP_ID" comma -s 700 5 -s 700
step "In the bar: both" "D25 (W3): Settings › Appearance, \"In the bar\" with both selected (widget in the bar, tray icon always), its line naming what both carry; then Widget with customize."
keys Escape -s 800

# ---- the tray ------------------------------------------------------------------------------
omabox click -b "$BOX" "$TRAY_X" "$TRAY_Y" >/dev/null # the drawer: the icon is in it
settle
omabox click -b "$BOX" "$TRAY_X" "$TRAY_Y" right >/dev/null
omabox wait -b "$BOX" --timeout 3s change >/dev/null 2>&1 || true
step "The tray's menu" "D10 (T1): show omaseed, the figures, servers with ✓ public · 0 torrents and private · 4 torrents, then the alternative limits, stop all, start all, add torrent…, settings…, and quit last after a separator." "$TRAY_MENU"
omabox click -b "$BOX" 1727 216 >/dev/null
settle
check "the tray switched to private" default_is private
keys Escape -s 400
omabox click -b "$BOX" "$TRAY_X" "$TRAY_Y" right >/dev/null
omabox wait -b "$BOX" --timeout 3s change >/dev/null 2>&1 || true
step "The tray on private" "✓ private · 4 torrents now; public · 0 torrents (last seen). (The stock bar sometimes answers this second right click with its drawer's Pin/Hide card instead; the checks below read the exported menu either way.)" "$TRAY_MENU"
keys Escape -s 400
check "the tray's exported menu has private checked" tray_checked "private · 4 torrents"
check "the tray's exported menu ends with quit" tray_ends_with_quit
tray_click quit
omabox wait -b "$BOX" --timeout 10s window "$APP_ID" --gone >/dev/null 2>&1 || true
check "quit from the tray: omaseed is gone" not_running

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
if [ "$FAILED" -gt 0 ]; then
    echo "$FAILED check(s) failed" >&2
    exit 1
fi
