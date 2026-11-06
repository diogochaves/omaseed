#!/usr/bin/env bash
# omaseed — the visual check: the screens the walks reach, compared with a baseline instead of
# looked at (omabox check, omabox's reference "Visual checks"). A box's own qBittorrent holds
# the swarm's torrents, added stopped, so nothing the app shows depends on a download; the
# times it shows from the real clock (a torrent's "added today", its "created on") and the
# disk's free space are left out where they appear. A Status cell drawn a few px off in one start
# (#74) shows at --approve as "run N of 3 differed" for the library checkpoints.
#
#   omabox check run --approve --base .local/visual --plugin plugin -- scripts/dev/visual-check.sh
#       three runs, each in a fresh `up --clean` box; the baseline goes to .local/visual (per
#       GPU and machine, so it is not committed). Read the baseline.png it prints first.
#   omabox check run --base .local/visual --plugin plugin -- scripts/dev/visual-check.sh
#       after a change: same, or changed with each region and the tree's lines; report.png first.
#
#   OMASEED_SKIN=console omabox check run [--approve] --keep fonts --base .local/visual-console \
#       --plugin plugin -- scripts/dev/visual-check.sh
#       the same screens in the Console skin (#171, DECISIONS C31), against a baseline of their
#       own; `--keep fonts` brings this machine's fonts, so Console's own families draw where
#       they are installed (without them it draws its stand-ins, also a fair check).
#
# OMASEED_BUILD: the build to check (default build/; it must be inside this checkout, which the
# box mounts read-only). The swarm's .torrent files must exist (scripts/dev/swarm.sh start).
# OMASEED_SKIN: the skin the app draws (omarchy when unset), passed to it as is. The script
# itself never asks which skin it is: both skins walk the same steps.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# DECISIONS C24 H4: a new install shows the rows display, and a box is a new install. This
# walk reads the table (its columns, its rows' order), so it pins the table: every scope starts
# on it (App's OMASEED_DISPLAY), passed to each launch here and by app-box.sh.
export OMASEED_DISPLAY="${OMASEED_DISPLAY:-table}"
APP="class:io.github.diogochaves.omaseed"
BUILD="${OMASEED_BUILD:-$repo/build}"
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
QBT_PORT=8090

die() { echo "visual-check.sh: $*" >&2; exit 1; }
[ -n "${OMABOX:-}" ] || die "run it through omabox check run (see the top of this file)"
[ -x "$BUILD/omaseed" ] || die "no omaseed in $BUILD: build it, or set OMASEED_BUILD"
case "$(realpath "$BUILD")/" in "$repo"/*) ;; *) die "$BUILD is outside $repo: the box would not have it" ;; esac
compgen -G "$TORRENTS/*.torrent" >/dev/null || die "no .torrent files in $TORRENTS: scripts/dev/swarm.sh start"

# ---- the box's qBittorrent and omaseed's servers.json -------------------------------------------
home="$(omabox path)/home"
mkdir -p "$home/.config/qBittorrent" "$home/Torrents" "$home/torrents" "$home/.config/omaseed"
cat >"$home/.config/qBittorrent/qBittorrent.conf" <<CONF
[LegalNotice]
Accepted=true

[BitTorrent]
Session\\DefaultSavePath=/home/sbx/Torrents
Session\\Port=26400

[Preferences]
WebUI\\Enabled=true
WebUI\\Address=127.0.0.1
WebUI\\Port=$QBT_PORT
WebUI\\LocalHostAuth=false
CONF
cp "$TORRENTS"/*.torrent "$home/torrents/"
omabox run -d -- qbittorrent-nox >/dev/null
omabox wait --timeout 20s until -- curl -fs -o /dev/null "http://127.0.0.1:$QBT_PORT/api/v2/app/version"
# One at a time and in name order, so the table's order is the same in every run.
# shellcheck disable=SC2016 # expanded by the box's sh
omabox run -- sh -c 'for t in "$HOME"/torrents/*.torrent; do
    curl -fs -F "torrents=@$t" -F stopped=true "http://127.0.0.1:$1/api/v2/torrents/add" >/dev/null
done' sh "$QBT_PORT"
# #145: the invented search plugins (scripts/dev/search-plugins.py), installed from the box's HOME.
python3 -I "$repo/scripts/dev/search-plugins.py" make "$home/search-plugins" >/dev/null
omabox run -- curl -fs -o /dev/null "http://127.0.0.1:$QBT_PORT/api/v2/search/installPlugin" \
    --data-urlencode "sources=/home/sbx/search-plugins/omaseed_plugin_a.py|/home/sbx/search-plugins/omaseed_plugin_b.py|/home/sbx/search-plugins/omaseed_plugin_c.py"
omabox wait --timeout 20s until -- sh -c "curl -fs http://127.0.0.1:$QBT_PORT/api/v2/search/plugins | grep -q omaseed_plugin_c"
# #146: a folder of the box's to create a torrent from, the same bytes every run (sparse).
mkdir -p "$home/share/photo-walks/harbour"
for n in 1 2 3; do truncate -s 2M "$home/share/photo-walks/harbour/walk-0$n.jpg"; done
printf 'notes\n' >"$home/share/photo-walks/notes.txt"
printf '{"version":1,"defaultId":"box","servers":[{"id":"box","name":"box","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' \
    "$QBT_PORT" >"$home/.config/omaseed/servers.json"
chmod 700 "$home/.config/omaseed"
chmod 600 "$home/.config/omaseed/servers.json"

# ---- the app ----------------------------------------------------------------------------------
# A pinned skin (#171) wins over Settings › Appearance's choice for the run.
pass=(--pass OMASEED_DISPLAY)
[ -n "${OMASEED_SKIN:-}" ] && pass+=(--pass OMASEED_SKIN)
omabox run "${pass[@]}" -d --wait -- "$BUILD/omaseed"
omabox wait --timeout 20s window "$APP"

# Window coordinates of what follows the real clock or the disk: the selected torrent's
# "added today HH:MM", the time of the first line of its story's "what happened" (DECISIONS
# C24 H6: the details open on the story, where General's "Created on" was), the status line's
# free space (and "synced now" after it, which moves with its width).
ADDED="1500,72 396x22"
CREATED="1500,252 140x26"
FREE="750,1006 190x24"
# A sheet over the dimmed window: its own region only (settings and help; the add sheet is smaller).
# Settings is as tall as its head and its tallest pane, so shorter than help, and taller on the
# qBittorrent sections (their head) than on omaseed's: the region is the tallest it can be.
SHEET="568,78 760x875"
ADD_SHEET="648,294 600x443"
# Settings sections that show today's date or the clock, in window coordinates (every --ignore
# with --window is, crop or not): the alt-speed schedule's "Next: off at 20:00", omaseed's backup
# "seed history since <date>", qBittorrent's backup file names "…-<date>.json" (settings, torrents).
declare -A SECTION_IGNORE=(
    [qbt-speed]="792,673 512x38"
    [backup]="798,293 425x52"
    [qbt-backup]="798,262 430x52|798,533 430x52"
)

# moving_point ID [ARGS]: a checkpoint of the whole window that also leaves out, wherever the
# skin lays them out, the elements whose words follow the clock or the disk: any "HH:MM" ("added
# today HH:MM", a story's events, an error block's log line) and the status line's free space.
# The regions above are Omarchy's places, and Console's sizes move them. Each element's box is
# the accessibility tree's, 4 px larger.
moving_point() {
    local id="$1" moving
    shift
    mapfile -t moving < <(omabox tree --window "$APP" --json 2>/dev/null | python3 -c '
import json, re, sys
words = re.compile(r"\b\d{1,2}:\d{2}\b|[0-9] ?[KMGT]?B free")
for n in json.load(sys.stdin):
    if n.get("w", 0) > 0 and words.search(n.get("name") or ""):
        print("--ignore")
        print("%d,%d %dx%d" % (max(n["x"] - 4, 0), max(n["y"] - 4, 0), n["w"] + 8, n["h"] + 8))')
    omabox check point "$id" --window "$APP" "${moving[@]}" "$@"
}

# sheet_region NAME REGION: REGION grown to hold the sheet (the dialog) named NAME, as the
# tree has it: a skin may draw a sheet taller than Omarchy's (Console's add sheet is).
sheet_region() {
    omabox tree --window "$APP" --json 2>/dev/null | python3 -c '
import json, re, sys
x, y, w, h = map(int, re.split(r"[, x]", sys.argv[2]))
for n in json.load(sys.stdin):
    if n.get("role") == "dialog" and n.get("name") == sys.argv[1] and n.get("w", 0) > 0:
        x2, y2 = max(x + w, n["x"] + n["w"]), max(y + h, n["y"] + n["h"])
        x, y = min(x, n["x"]), min(y, n["y"])
        w, h = x2 - x, y2 - y
        break
print("%d,%d %dx%d" % (x, y, w, h))' "$1" "$2"
}

# key KEY...: into the app, returning once the window has settled; what the wait saw goes to the
# run's log, so a 124 there says what it waited on.
key() { omabox keys --window "$APP" --wait "$@"; }
# click_text NAME: a click on the middle of the first node of the app's accessibility tree whose
# name starts with NAME (a rail row), waiting for the window to settle.
click_text() {
    local at
    at="$(omabox tree --window "$APP" --json 2>/dev/null | python3 -c '
import json, sys
for node in json.load(sys.stdin):
    if (node.get("name") or "").startswith(sys.argv[1]) and node.get("w", 0) > 0:
        print(node["x"] + node["w"] // 2, node["y"] + node["h"] // 2)
        break' "$1")"
    [ -n "$at" ] || die "no \"$1\" to click in the window"
    # shellcheck disable=SC2086 # "X Y"
    omabox click --window "$APP" --wait $at
}

omabox check point bar -g "0,0 1920x30"
moving_point library --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key 5 # resting (DECISIONS C24 H2), where these stopped torrents are
moving_point library-resting --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
# A folder picked in the rail (DECISIONS C24 H5): its name the title, the scope line's facts its
# sentence, "move this folder…" a title-bar tool. The pointer leaves the row, so its path's
# tooltip is not in the shot.
key 0
click_text Torrents
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point folder --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key 0 j j
moving_point library-third --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key l 2
moving_point details-files --ignore "$ADDED" --ignore "$FREE"
key Escape h g
key Return
moving_point actions --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
key colon
moving_point palette --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
# The omaseed menu under the mark (DECISIONS C24 H9): C13's rows, then this window's, quit last.
key ctrl+period
moving_point app-menu --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
# Alt held alone (C24 H10): the frame's keys in badges, over the rail, the title bar's tools.
key down:alt
moving_point alt-keys --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
omabox keys --window "$APP" up:alt >/dev/null
key question
omabox check point help --window "$APP" -g "$(sheet_region Shortcuts "$SHEET")"
key Escape
key a
omabox check point add --window "$APP" -g "$(sheet_region "Add torrent" "$ADD_SHEET")"
# The first Escape clears the magnet field (which may change nothing), the second closes the sheet.
key Escape Escape
# Create a torrent (#146, board 49): ctrl+n, a folder of the box's typed in and read here.
key ctrl+n
omabox keys --window "$APP" --wait -t "/home/sbx/share/photo-walks" >/dev/null
for _ in $(seq 1 10); do
    omabox tree --window "$APP" --find "4 files" >/dev/null 2>&1 && break
    sleep 1
done
omabox wait --timeout 10s still >/dev/null || true
moving_point create --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
# The sheet is guarded: the first Escape puts the keyboard on cancel, the second closes it.
key Escape Escape
# Three rows selected (DECISIONS C32 D17): the action bar floats over the list, its count and
# ledger, its buttons and C18 D6's hint before esc (a fresh box: the first plain click), the
# status line under it the server's; only the details' clock is left out. A click puts the
# keyboard on the table, wherever the sheets left it; the pointer then leaves the row.
click_text distro
key j space j space
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point selection --ignore "$ADDED" --ignore "$CREATED"
# Alt held over the action bar: each button's key over its corner, no button moved.
key down:alt
moving_point selection-alt-keys --ignore "$ADDED" --ignore "$CREATED"
omabox keys --window "$APP" up:alt >/dev/null
key Escape

# The library's other displays (DECISIONS C24 H3–H4), picked from the palette: the rows grouped
# under the five questions (every torrent here is stopped: resting), then space (board 51 D12 a),
# then the table again for what follows.
display() {
    key colon
    omabox keys --window "$APP" --wait -t "show as $1" >/dev/null
    key Return
    omabox pointer --window "$APP" -- move 900 600 >/dev/null
}
display rows
moving_point rows --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
display space
moving_point space --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
display table

# Search (#145, DECISIONS C26): "sampler" asked of plugins a, b and c; b fails, c answers after 4 s.
# The shot waits for the line that says who didn't answer, then for the "new" marks to fade.
key ctrl+f
omabox keys --window "$APP" --wait -t "sampler" >/dev/null
key Return
for _ in $(seq 1 20); do
    omabox tree --window "$APP" --find "1 of 3 plugins didn't answer" >/dev/null 2>&1 && break
    sleep 1
done
# "new" and "+1 copy" fade 4 s after plugin c's answer.
for _ in $(seq 1 15); do
    omabox tree --window "$APP" --json 2>/dev/null | python3 -c '
import json, sys
sys.exit(1 if any((n.get("name") or "") in ("new", "+1 copy") for n in json.load(sys.stdin)) else 0)' && break
    sleep 1
done
omabox wait --timeout 10s still >/dev/null || true
moving_point search --ignore "$FREE"
# esc clears the words, esc again goes back to the library (SR14).
key Escape Escape

# Settings: every section, j by j, the first (General) open.
sections=(general servers notifications keybindings appearance backup about
    qbt-speed qbt-discovery qbt-connection qbt-proxy qbt-seeding qbt-queue qbt-downloads qbt-webui
    qbt-advanced qbt-search qbt-backup)
key comma
for i in "${!sections[@]}"; do
    [ "$i" = 0 ] || key j
    ignores=()
    IFS='|' read -ra regions <<<"${SECTION_IGNORE[${sections[$i]}]:-}"
    for r in "${regions[@]}"; do ignores+=(--ignore "$r"); done
    omabox check point "settings-${sections[$i]}" --window "$APP" -g "$(sheet_region settings "$SHEET")" "${ignores[@]}"
done
key Escape

# The bar widget's popup and its ⋯ menu, over an empty workspace (holes show only over the wallpaper).
omabox hyprctl dispatch 'hl.dsp.focus({workspace = 2})' >/dev/null
omabox run -- omarchy-shell shell summon chaves.omaseed >/dev/null
omabox check point popup
omabox keys --wait ctrl+period >/dev/null
omabox check point popup-menu

# ---- the states the walk above never reaches (#171) ---------------------------------------------
# Last, so no checkpoint above changes: a torrent that needs you (its fix on the row, its error
# block in the story), a partly downloaded one (the pieces), the trackers tab, picked rows apart
# from the cursor, a refused action's outcome strip, and two servers in the rail. Board 46 draws
# each in Console; both skins take them, each into its own baseline.
omabox keys --wait Escape >/dev/null || true # the popup's menu
omabox keys --wait Escape >/dev/null || true # the popup
omabox hyprctl dispatch 'hl.dsp.focus({workspace = 1})' >/dev/null

# qbt PATH [CURL ARGS]: the box's qBittorrent, from inside the box.
qbt() { local path="$1"; shift; omabox run -- curl -fs "http://127.0.0.1:$QBT_PORT/api/v2/$path" "$@"; }
# hash_of NAME: the hash of the first torrent whose name starts with NAME.
hash_of() {
    qbt torrents/info | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1])))' "$1"
}
FIELD="$(hash_of "Field Recordings")"
BROKEN="$(hash_of "unwritable-fixture")"
# Every other file of the album, in place: the swarm's content is zeros (swarm.sh), and a hybrid
# torrent's files start on a piece, so a recheck finds those files' pieces and only theirs.
qbt "torrents/files?hash=$FIELD" | python3 -c '
import json, os, sys
for i, f in enumerate(json.load(sys.stdin)):
    if i % 2 == 0:
        path = os.path.join(sys.argv[1], f["name"])
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as out:
            out.truncate(f["size"])' "$home/Torrents"
# The fixture's file, there but unreadable: its recheck fails with "Permission denied", an error.
truncate -s "$(qbt "torrents/files?hash=$BROKEN" | python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["size"])')" \
    "$home/Torrents/unwritable-fixture.bin"
chmod 000 "$home/Torrents/unwritable-fixture.bin"
qbt torrents/recheck -d "hashes=$FIELD|$BROKEN" >/dev/null
# shellcheck disable=SC2016 # expanded by the box's sh
omabox wait --timeout 60s until -- sh -c 'curl -fs "http://127.0.0.1:$1/api/v2/torrents/info?hashes=$2|$3" | python3 -c "
import json, sys
s = {t[\"hash\"]: t for t in json.load(sys.stdin)}
sys.exit(0 if s[sys.argv[1]][\"state\"] == \"error\" and s[sys.argv[2]][\"progress\"] > 0
         and not s[sys.argv[2]][\"state\"].startswith(\"checking\") else 1)" "$3" "$2"' sh "$QBT_PORT" "$FIELD" "$BROKEN" >/dev/null

# The table on the torrent that needs you: its status, and the story's error block. The click
# picks the row; esc leaves it the cursor's alone. The details' tab is still files
# (details-files, above): l 1 is the story. The keys that may change nothing on screen (0 when
# the library is in view, a focus move) go without a wait; a click puts the keyboard back.
omabox keys --window "$APP" 0 >/dev/null
click_text unwritable
key Escape l 1
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point needs-you --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
# The rows: its fix on the row, under the needs-you head; then two rows picked, the cursor on a
# third.
display rows
moving_point rows-needs-you --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
click_text unwritable
key Escape j space j space j
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point rows-picked --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
display table
# The album, half there: the story's pieces, then the trackers tab.
click_text "Field Recordings"
key Escape l 1
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point details-pieces --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key 4
moving_point details-trackers --ignore "$ADDED" --ignore "$FREE"
click_text "Field Recordings"
key Escape
omabox pointer --window "$APP" -- move 900 600 >/dev/null
# A refused action stays in its strip (DECISIONS C18 D3): up the queue with queueing off (409).
qbt app/setPreferences --data-urlencode 'json={"queueing_enabled":false}' >/dev/null
key bracketleft
for _ in $(seq 1 15); do
    omabox tree --window "$APP" --find "didn't move in the queue" >/dev/null 2>&1 && break
    sleep 1
done
moving_point outcome --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
qbt app/setPreferences --data-urlencode 'json={"queueing_enabled":true}' >/dev/null

# Two servers, for the rail's servers head: the app again, with a second name for the same
# qBittorrent, so both read connected.
omabox run -- "$BUILD/omaseed" --quit >/dev/null 2>&1 || true
omabox wait --timeout 15s window "$APP" --gone >/dev/null
printf '{"version":1,"defaultId":"box","servers":[%s,%s]}' \
    "$(printf '{"id":"box","name":"box","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}' "$QBT_PORT")" \
    "$(printf '{"id":"spare","name":"spare","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":false,"addedAt":"2026-09-24T00:00:00Z"}' "$QBT_PORT")" \
    >"$home/.config/omaseed/servers.json"
omabox run "${pass[@]}" -d --wait -- "$BUILD/omaseed"
omabox wait --timeout 20s window "$APP"
omabox pointer --window "$APP" -- move 900 600 >/dev/null
moving_point servers-two --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
