#!/usr/bin/env bash
# omaseed — the shared-menu walkthrough (#73, docs/33 §11: D10, D24, D25). One menu, built once
# in the app, shown by the tray, by the bar widget popup's ⋯ and by the window's mark (ctrl+.,
# DECISIONS C24 H9): first run puts the widget in the bar; with one server the ⋯ and the
# window's menu have no servers and the server line no ▾ (D19); "private" is added; the
# window's menu switches to private and back, and opens settings… by its key; the widget's
# ctrl+. opens its menu and switches to private, ctrl+s opens the servers and a click goes
# back to public; quit from the ⋯. Then "In the bar: both" (D25, read from the two old
# settings), the tray's menu with its servers block, a switch from it, and quit from it. What
# each action must leave is checked in servers.json and the process list, not only in shots.
#
#   scripts/dev/menu-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                             # OUT_DIR/index.md says what to look for in each
#   scripts/dev/menu-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                             # script still does the typing, then waits for enter
#
# The box is a manual box with Omarchy's stock bar (scripts/dev/app-box.sh up --manual
# --stock-bar: the tray lives in it, found from the bar itself, wherever that Omarchy puts it):
# omaseed from build-box/ installed in it, "public" on the box's 127.0.0.1:8090 and "private" on
# :8091. The logins stay in the box, as in servers-walk.sh.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
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

# ---- the stock bar's tray, found from the bar itself (#139) --------------------------------
# The stock bar publishes no accessibility tree and its tray takes no keys, and where the tray
# sits depends on what else is in the bar (Omarchy 4.5 put an indicator beside it). So the walk
# finds it by what changes when omaseed's icon moves: pinned (the tray's own Pin, as its card
# writes it in shell.json), the icon takes the place the drawer's "<" had and the "<" moves one
# slot over, and nothing else in the bar moves. That place is where the icon shows when the
# drawer opens. The menu is then what changed under the bar when it opened, and its rows the
# lines of text in it, in the order omaseed exports them.

# png CMD ARGS…: what the walk reads from its own shots, with python's standard library only
# (8-bit RGB or RGBA, not interlaced, as omabox writes them).
#   png runs A B          the runs of columns where A and B differ, "START END" a line
#   png box A B           the smallest rectangle holding every pixel that differs, "X Y W H"
#   png bands A X Y W H   the lines of text in that rectangle of A, "TOP BOTTOM" each: rows
#                         unlike its commonest colour, 3 px thin or less left out (rules, marks)
png() {
    python3 -I - "$@" <<'PY'
import collections, struct, sys, zlib


def read(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour not in (2, 6) or interlace:
                sys.exit("%s: not an 8-bit RGB(A) PNG" % path)
            bpp = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw, stride = zlib.decompress(idat), width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(width)])
        prev = line
    return rows


def spans(flags):
    out, start = [], None
    for i, flag in enumerate(flags + [False]):
        if flag and start is None:
            start = i
        elif not flag and start is not None:
            out.append((start, i - 1))
            start = None
    return out


command, args = sys.argv[1], sys.argv[2:]
if command in ("runs", "box"):
    a, b = read(args[0]), read(args[1])
    if len(a) != len(b) or len(a[0]) != len(b[0]):
        sys.exit("the two shots differ in size")
    diff = [[pa != pb for pa, pb in zip(ra, rb)] for ra, rb in zip(a, b)]
    if command == "runs":
        for start, end in spans([any(row[x] for row in diff) for x in range(len(a[0]))]):
            print(start, end)
    else:
        xs = [x for row in diff for x, d in enumerate(row) if d]
        ys = [y for y, row in enumerate(diff) if any(row)]
        if not xs:
            sys.exit(1)
        print(min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)
elif command == "bands":
    rows = read(args[0])
    x, y, w, h = map(int, args[1:5])
    cut = [row[x:x + w] for row in rows[y:y + h]]
    ground = collections.Counter(p for row in cut for p in row).most_common(1)[0][0]
    ink = [sum(sum(abs(c - g) for c, g in zip(p, ground)) > 48 for p in row) >= 2 for row in cut]
    for top, bottom in spans(ink):
        if bottom - top + 1 > 3:
            print(y + top, y + bottom)
PY
}

# bar_rect: "X Y W H" of the stock bar (its layer surface, omarchy-bar).
bar_rect() {
    omabox hyprctl -b "$BOX" layers -j | python3 -I -c '
import json, sys
for monitor in json.load(sys.stdin).values():
    for layers in monitor["levels"].values():
        for layer in layers:
            if layer["namespace"] == "omarchy-bar":
                print(layer["x"], layer["y"], layer["w"], layer["h"])
                sys.exit(0)
sys.exit(1)'
}

# tray_id: omaseed's tray item's Id, the name the tray pins it by.
tray_id() {
    local item
    item="$(omabox run -b "$BOX" -- busctl --user --json=short get-property org.kde.StatusNotifierWatcher \
        /StatusNotifierWatcher org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems |
        python3 -c 'import json, sys; print(json.load(sys.stdin)["data"][0])')"
    omabox run -b "$BOX" -- busctl --user --json=short get-property "${item%%/*}" "/${item#*/}" \
        org.kde.StatusNotifierItem Id | python3 -I -c 'import json, sys; print(json.load(sys.stdin)["data"])'
}

# tray_pinned JSON: the tray's pinned ids ("null": the key left out) into the box's shell.json,
# as the tray's Pin card writes them; prints what it held before. The shell reads it at once.
tray_pinned() {
    python3 -I - "$home/.config/omarchy/shell.json" "$1" <<'PY'
import json, os, sys
path, pinned = sys.argv[1], json.loads(sys.argv[2])
config = json.load(open(path, encoding="utf-8"))
for section in config.get("bar", {}).get("layout", {}).values():
    for i, entry in enumerate(section):
        if entry == "omarchy.tray":
            entry = section[i] = {"id": entry}
        if isinstance(entry, dict) and entry.get("id") == "omarchy.tray":
            print(json.dumps(entry.get("pinned")))
            if pinned is None:
                entry.pop("pinned", None)
            else:
                entry["pinned"] = pinned
            tmp = path + ".walk-tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(config, f, indent=2)
            os.replace(tmp, path)
            sys.exit(0)
sys.exit("no omarchy.tray in the bar's layout")
PY
}

# park: the pointer on the bar's far end from the tray, so nothing near the tray is hovered (no
# tooltip over the menu) and the shots see the same thing each time.
park() { omabox pointer -b "$BOX" -- move "$((BAR_X + 2))" "$((BAR_Y + BAR_H / 2))" >/dev/null 2>&1 || true; }
bar_shot() { omabox shot -b "$BOX" --no-pointer -g "$BAR_X,$BAR_Y ${BAR_W}x$BAR_H" -o "$1" >/dev/null 2>&1; }
under_shot() { omabox shot -b "$BOX" --no-pointer -g "$UNDER" -o "$1" >/dev/null 2>&1; }
bar_changed() { omabox wait -b "$BOX" --timeout 5s change -g "$BAR_X,$BAR_Y ${BAR_W}x$BAR_H" >/dev/null 2>&1 || true; }
# bar_until as|unlike SHOT: until the bar looks as SHOT shows it, or not (10 s at most), then
# until it is still. The shell may redraw before a `wait change` could start, so it is polled.
bar_until() {
    local _ runs
    for _ in $(seq 1 20); do
        bar_shot "$OUT_ROWS.bar-now.png"
        runs="$(png runs "$2" "$OUT_ROWS.bar-now.png")"
        if { [ "$1" = as ] && [ -z "$runs" ]; } || { [ "$1" = unlike ] && [ -n "$runs" ]; }; then
            settle
            return 0
        fi
    done
    return 1
}

# tray_find: TRAY_X / TRAY_Y, the middle of omaseed's place in the tray (the drawer's "<" while
# it is closed, omaseed's icon while it is open), and UNDER, the part of the screen under the
# bar where its menu opens. The place is the last run of columns that changes when the icon is
# pinned for a moment: the tray grows from the bar's end inwards, so whatever else moves (the
# "<", or what sits before the tray) is further in.
tray_find() {
    local pinned run
    read -r BAR_X BAR_Y BAR_W BAR_H < <(bar_rect) || return 1
    park
    settle
    bar_shot "$OUT_ROWS.bar-drawer.png"
    pinned="$(tray_pinned "[\"$(tray_id)\"]")" || return 1
    bar_until unlike "$OUT_ROWS.bar-drawer.png"
    bar_shot "$OUT_ROWS.bar-pinned.png"
    tray_pinned "$pinned" >/dev/null
    bar_until as "$OUT_ROWS.bar-drawer.png" || echo "  (the bar did not come back as it was after the pin was undone)" >&2
    TRAY_RUNS="$(png runs "$OUT_ROWS.bar-drawer.png" "$OUT_ROWS.bar-pinned.png" | tr '\n' ' ')"
    run="$(png runs "$OUT_ROWS.bar-drawer.png" "$OUT_ROWS.bar-pinned.png" | tail -1)"
    [ -n "$run" ] || return 1
    TRAY_X=$((BAR_X + (${run% *} + ${run#* }) / 2))
    TRAY_Y=$((BAR_Y + BAR_H / 2))
    local left=$((TRAY_X - 400)) right=$((TRAY_X + 400))
    [ "$left" -ge "$BAR_X" ] || left=$BAR_X
    [ "$right" -le $((BAR_X + BAR_W)) ] || right=$((BAR_X + BAR_W))
    UNDER="$left,$((BAR_Y + BAR_H)) $((right - left))x600"
    TRAY_MENU="$left,$BAR_Y $((right - left))x$((BAR_H + 600))"
}

# tray_menu_open: the drawer opened by hovering omaseed's place, the icon right-clicked, the
# pointer parked; TRAY_CARD is the menu's rectangle in $OUT_ROWS.under-open.png.
tray_menu_open() {
    park
    settle
    under_shot "$OUT_ROWS.under-closed.png"
    omabox pointer -b "$BOX" -- move "$TRAY_X" "$TRAY_Y" >/dev/null 2>&1
    bar_changed
    settle
    omabox click -b "$BOX" "$TRAY_X" "$TRAY_Y" right >/dev/null
    omabox wait -b "$BOX" --timeout 3s change >/dev/null 2>&1 || true
    park
    settle
    under_shot "$OUT_ROWS.under-open.png"
    TRAY_CARD="$(png box "$OUT_ROWS.under-closed.png" "$OUT_ROWS.under-open.png")"
}

# tray_menu_row LABEL: TRAY_ROW_X / TRAY_ROW_Y, the middle of the open menu's row LABEL: the
# menu's lines of text (inside its border) are its rows, in the order tray_rows lists them.
tray_menu_row() {
    local cx cy cw ch index count top bottom
    [ -n "${TRAY_CARD:-}" ] || return 1
    read -r cx cy cw ch <<<"$TRAY_CARD"
    png bands "$OUT_ROWS.under-open.png" $((cx + 8)) $((cy + 6)) $((cw - 16)) $((ch - 12)) >"$OUT_ROWS.bands"
    tray_rows | cut -f3 >"$OUT_ROWS.labels"
    count="$(wc -l <"$OUT_ROWS.bands")"
    TRAY_LINES="$count lines of text for $(wc -l <"$OUT_ROWS.labels") rows"
    [ "$count" = "$(wc -l <"$OUT_ROWS.labels")" ] || return 1
    index="$(grep -nxF -- "$1" "$OUT_ROWS.labels" | head -1 | cut -d: -f1)"
    [ -n "$index" ] || return 1
    read -r top bottom < <(sed -n "${index}p" "$OUT_ROWS.bands")
    TRAY_ROW_X=$((${UNDER%%,*} + cx + cw / 2))
    TRAY_ROW_Y=$((BAR_Y + BAR_H + (top + bottom) / 2))
}

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
    [ -z "${OUT_ROWS:-}" ] || rm -rf "$(dirname "$OUT_ROWS")"
}
trap cleanup EXIT

POPUP="1530,0 390x620"

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
keys Return -s 800ms
clip public
# space on the cookie chip puts the caret in Username (#201 F1).
keys Return -s 600ms Tab ctrl+a -t public Tab Tab space -t admin Tab ctrl+v Tab Return -s 2500ms
unclip
# continue, "not now" on links, then the bar step's own focus: "add to the bar".
keys Tab Tab Return -s 1500ms Tab Return -s 1500ms Return -s 4000ms
keys Return -s 3000ms
check "the widget is in the bar: its plugin is linked" test -L "$home/.config/omarchy/plugins/chaves.omaseed"
check "status.json lists one server and the menu (v stays 2)" \
    status_has 's["v"] == 2 and len(s["servers"]) == 1 and s["menu"][-1]["id"] == "quit"'
check "one server: no servers rows in the menu (D19)" \
    status_has 'not any(r["id"].startswith("server:") for r in s["menu"])'

popup
keys ctrl+period -s 600ms
step "One server, the menu" "W1 with one server (T0): ⋯ lit in the head, the server line with no ▾; the menu: Alternative Speed Limits (or Turtle/Burst) with A, Stop All, Start All, a hairline, Add Torrent… with m, omaseed Settings…, a hairline, Quit omaseed. No SERVERS." "$POPUP"
keys ctrl+period -s 400ms Escape -s 600ms

# ---- the window: the mark's menu, C13's third place (DECISIONS C24 H9) ---------------------
keys --window "$APP_ID" ctrl+period -s 600ms
step "One server, the window's menu" "Under the rail's mark: omaseed, its version, public · qBittorrent …; turtle (or the alternative limits' word) with A, stop all, start all, a rule, add torrent… a, settings… comma, a rule, THIS WINDOW ONLY: seed console, log, commands, keys and help, hide window, each with its key, a rule, quit omaseed ctrl+q. No servers (D19)."
keys Escape -s 400ms

# ---- the second server ---------------------------------------------------------------------
clip private
keys --window "$APP_ID" comma -s 600ms 2 -s 300ms a -s 800ms -t private Tab -t http://127.0.0.1:8091 Tab Tab space \
    -t admin Tab ctrl+v Tab Tab Tab Tab -s 800ms Tab Return -s 2500ms Return -s 1200ms
unclip
keys Escape -s 1500ms
check "two servers in status.json, two server rows in the menu" \
    status_has 'len(s["servers"]) == 2 and sum(r["kind"] == "radio" for r in s["menu"]) == 2'

# ---- the window's menu with two servers: a switch, back, and settings… ----------------------
keys --window "$APP_ID" ctrl+period -s 600ms
step "Two servers, the window's menu" "As the widget's: SERVERS, ● public ✓ (0 torrents) and ● private (its torrents, or nothing before its first sync), a rule, then the rows of the one-server shot. The cursor on public."
keys j -s 300ms Return -s 3000ms
check "the window's menu switched to private" default_is private
keys --window "$APP_ID" ctrl+period -s 600ms Return -s 3000ms
check "the window's menu switched back to public" default_is public
keys --window "$APP_ID" ctrl+period -s 600ms comma -s 1200ms
step "Settings, from the window's menu" "settings… picked by its key (,): Settings open over the window, on the pane it was last on (Servers, from adding private)."
keys Escape -s 800ms

# ---- the widget: ⋯ and ▾ -------------------------------------------------------------------
popup
keys ctrl+period -s 600ms
step "Two servers, the menu" "W1: SERVERS, ● public (0 torrents) and ○ private, a hairline, then the rows of the last shot. The server line ends in ▾. The cursor on public." "$POPUP"
keys j -s 300ms Return -s 3000ms
check "the ⋯ menu switched to private" default_is private
step "Switched from the widget" "The popup stayed open and refilled: PRIVATE · QBITTORRENT in the head, the menu closed, Add lists the Downloads files with \"already on private\"." "$POPUP"
keys ctrl+s -s 600ms
step "The server line's ▾" "W2: just the servers, ○ public 0 torrents, ● private 5 torrents (the cursor on it); the ▾ turned ▴ in the accent." "$POPUP"
keys k -s 300ms Return -s 3000ms
check "the servers part switched back to public" default_is public
keys ctrl+period -s 600ms j j j j j j j -s 400ms
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
keys --window "$APP_ID" comma -s 700ms 5 -s 700ms
step "In the bar: both" "D25 (W3): Settings › Appearance, \"In the bar\" with both selected (widget in the bar, tray icon always), its line naming what both carry; then Widget with customize."
keys Escape -s 800ms

# ---- the tray ------------------------------------------------------------------------------
# Found from the bar, its menu's rows from the menu: no coordinates of one Omarchy's bar (#139).
if tray_find; then
    echo "  omaseed's place in the tray: $TRAY_X,$TRAY_Y (bar ${BAR_W}x$BAR_H; changed when pinned: $TRAY_RUNS)"
else
    echo "  omaseed's place in the tray was not found (changed when pinned: ${TRAY_RUNS:-nothing})" >&2
fi
if [ -n "${TRAY_X:-}" ]; then
    tray_menu_open
    step "The tray's menu" "D10 (T1): show omaseed, the figures, servers with ✓ public · 0 torrents and private · 5 torrents, then the alternative limits, stop all, start all, add torrent…, settings…, and quit last after a separator." "$TRAY_MENU"
    # private's row by its name: its figure is whatever the box's private holds.
    PRIVATE_ROW="$(tray_rows | cut -f3 | grep -m1 '^private · ' || true)"
    if [ -n "$PRIVATE_ROW" ] && tray_menu_row "$PRIVATE_ROW"; then
        omabox click -b "$BOX" "$TRAY_ROW_X" "$TRAY_ROW_Y" >/dev/null
        settle
    else
        echo "  the menu's row \"${PRIVATE_ROW:-private · …}\" was not found: ${TRAY_LINES:-no menu under the bar}" >&2
    fi
fi
check "the tray switched to private" default_is private
keys Escape -s 400ms
if [ -n "${TRAY_X:-}" ]; then
    tray_menu_open
    step "The tray on private" "✓ private · 5 torrents now; public · 0 torrents (last seen)." "$TRAY_MENU"
    keys Escape -s 400ms
fi
check "the tray's exported menu has private checked" tray_checked "${PRIVATE_ROW:-private}"
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
