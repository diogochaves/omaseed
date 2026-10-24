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
# OMASEED_BUILD: the build to check (default build/; it must be inside this checkout, which the
# box mounts read-only). The swarm's .torrent files must exist (scripts/dev/swarm.sh start).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
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
omabox wait --timeout 20s cmd -- curl -fs -o /dev/null "http://127.0.0.1:$QBT_PORT/api/v2/app/version"
# One at a time and in name order, so the table's order is the same in every run.
# shellcheck disable=SC2016 # expanded by the box's sh
omabox run -- sh -c 'for t in "$HOME"/torrents/*.torrent; do
    curl -fs -F "torrents=@$t" -F stopped=true "http://127.0.0.1:$1/api/v2/torrents/add" >/dev/null
done' sh "$QBT_PORT"
printf '{"version":1,"defaultId":"box","servers":[{"id":"box","name":"box","url":"http://127.0.0.1:%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' \
    "$QBT_PORT" >"$home/.config/omaseed/servers.json"
chmod 700 "$home/.config/omaseed"
chmod 600 "$home/.config/omaseed/servers.json"

# ---- the app ----------------------------------------------------------------------------------
omabox run -d --wait -- "$BUILD/omaseed"
omabox wait --timeout 20s window "$APP"

# Window coordinates of what follows the real clock or the disk: the selected torrent's
# "added today HH:MM", its "Created on" value, the status line's free space (and "synced now"
# after it, which moves with its width).
ADDED="1500,72 396x22"
CREATED="1600,698 296x26"
FREE="750,1006 190x24"
# A sheet over the dimmed window: its own region only (settings and help; the add sheet is smaller).
# Settings is as tall as its head and its tallest pane, so shorter than help, and taller on the
# qBittorrent sections (their head) than on omaseed's: the region is the tallest it can be.
SHEET="568,78 760x875"
ADD_SHEET="648,275 600x481"
# Settings sections that show today's date or the clock, in window coordinates (every --ignore
# with --window is, crop or not): the alt-speed schedule's "Next: off at 20:00", omaseed's backup
# "seed history since <date>", qBittorrent's backup file names "…-<date>.json" (settings, torrents).
declare -A SECTION_IGNORE=(
    [qbt-speed]="792,673 512x38"
    [backup]="798,293 425x52"
    [qbt-backup]="798,262 430x52|798,533 430x52"
)

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
omabox check point library --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key 5 # resting (DECISIONS C24 H2), where these stopped torrents are
omabox check point library-resting --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
# A folder picked in the rail (DECISIONS C24 H5): its name the title, the scope line's facts its
# sentence, "move this folder…" a title-bar tool. The pointer leaves the row, so its path's
# tooltip is not in the shot.
key 0
click_text Torrents
omabox pointer --window "$APP" -- move 900 600 >/dev/null
omabox check point folder --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key 0 j j
omabox check point library-third --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key l 2
omabox check point details-files --window "$APP" --ignore "$ADDED" --ignore "$FREE"
key Escape h g
key Return
omabox check point actions --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
key colon
omabox check point palette --window "$APP" --ignore "$ADDED" --ignore "$CREATED" --ignore "$FREE"
key Escape
key question
omabox check point help --window "$APP" -g "$SHEET"
key Escape
key a
omabox check point add --window "$APP" -g "$ADD_SHEET"
# The first Escape clears the magnet field (which may change nothing), the second closes the sheet.
key Escape Escape
# Three rows selected (DECISIONS C24 H8): the status line is the action bar, its ledger and
# buttons in the line's place; no free space shows there, so only the details' clock is left out.
key g space j space j space
omabox check point selection --window "$APP" --ignore "$ADDED" --ignore "$CREATED"
key Escape

# Settings: every section, j by j, the first (General) open.
sections=(general servers notifications keybindings appearance backup about
    qbt-speed qbt-discovery qbt-connection qbt-proxy qbt-seeding qbt-queue qbt-downloads qbt-webui
    qbt-advanced qbt-backup)
key comma
for i in "${!sections[@]}"; do
    [ "$i" = 0 ] || key j
    ignores=()
    IFS='|' read -ra regions <<<"${SECTION_IGNORE[${sections[$i]}]:-}"
    for r in "${regions[@]}"; do ignores+=(--ignore "$r"); done
    omabox check point "settings-${sections[$i]}" --window "$APP" -g "$SHEET" "${ignores[@]}"
done
key Escape

# The bar widget's popup and its ⋯ menu, over an empty workspace (holes show only over the wallpaper).
omabox hyprctl dispatch 'hl.dsp.focus({workspace = 2})' >/dev/null
omabox run -- omarchy-shell shell summon chaves.omaseed >/dev/null
omabox check point popup
omabox keys --wait ctrl+period >/dev/null
omabox check point popup-menu
