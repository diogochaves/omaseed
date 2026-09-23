#!/usr/bin/env bash
# omaseed — a local BitTorrent swarm for real-data tests, fully offline.
#
# A seeder qBittorrent (:8083, its own profile) shares generated content through a tracker
# on 127.0.0.1 (scripts/dev/swarm-tracker.py); the playground (:8081) downloads it for real.
# Both clients listen on loopback only and the torrents are private, so nothing leaves the
# machine and the real library on :8080 is never involved.
#
#   scripts/dev/swarm.sh start            # seeder + tracker (and the playground if it is down)
#   scripts/dev/swarm.sh seed             # generate the fixtures, share them, add them to :8081
#   scripts/dev/swarm.sh status           # the swarm's torrents as the playground sees them
#   scripts/dev/swarm.sh break CASE [NAME]
#   scripts/dev/swarm.sh fix              # undo every break (stalled peers reconnect within
#                                         # a minute: libtorrent backs off before retrying)
#   scripts/dev/swarm.sh rate KIB         # the seeder's upload limit (default 4096 KiB/s)
#   scripts/dev/swarm.sh reset            # remove the swarm's torrents and files from both sides
#   scripts/dev/swarm.sh stop
#
# CASE (NAME is a case-insensitive part of a fixture's name; each case has a default):
#   stall             the seeder stops sharing: downloads go stalledDL
#   tracker [NAME]    the tracker answers with a failure reason (Trackers tab, error badge)
#                     A break aimed at a queued torrent force-starts it, or nothing would show.
#   dead [NAME]       adds a tracker nobody listens on (127.0.0.1:9): "not working"
#   missing [NAME]    deletes a torrent's downloaded files and restarts :8081: missingFiles
#   error             adds a fixture whose save path is read-only: error
#   checking [NAME]   force a recheck: checkingUP / checkingDL
#   crowd             the tracker reports 12 more seeds and 40 more peers than are there
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWARM="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}"
SEEDER_PORT=8083
SEEDER_BT_PORT=26997
TRACKER_PORT=26996
TRACKER="http://127.0.0.1:$TRACKER_PORT/announce"
DEAD_TRACKER="http://127.0.0.1:9/announce"
SEEDER="http://127.0.0.1:$SEEDER_PORT/api/v2"
PLAY="http://127.0.0.1:${OMASEED_TEST_PORT:-8081}/api/v2"
PLAY_DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
CONTENT="$SWARM/content"
TORRENTS="$SWARM/torrents"
CONTROL="$SWARM/control.json"
TRACKER_PID="$SWARM/tracker.pid"
CATEGORY=swarm

seeder_server() {
    OMASEED_TEST_PROFILE="$SWARM" OMASEED_TEST_PORT=$SEEDER_PORT OMASEED_TEST_BT_PORT=$SEEDER_BT_PORT \
        "$repo/scripts/dev/test-server.sh" "$@"
}
play_server() { "$repo/scripts/dev/test-server.sh" "$@"; }

tracker_running() { [ -f "$TRACKER_PID" ] && kill -0 "$(cat "$TRACKER_PID")" 2>/dev/null; }

# control KEY JSON_VALUE: set one key of the tracker's control file (read on every announce).
control() {
    python3 - "$CONTROL" "$1" "$2" <<'PY'
import json, sys
path, key, value = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
try:
    data = json.load(open(path))
except (OSError, ValueError):
    data = {}
if value is None:
    data.pop(key, None)
else:
    data[key] = value
json.dump(data, open(path, "w"))
PY
}

reannounce() {
    curl -fs -X POST "$SEEDER/torrents/reannounce" -d hashes=all >/dev/null || true
    curl -fs -X POST "$PLAY/torrents/reannounce" -d hashes=all >/dev/null || true
}

# find NAME [FIELD]: the playground's swarm torrent whose name contains NAME (first match).
find_torrent() {
    curl -fs "$PLAY/torrents/info?category=$CATEGORY" | python3 -c '
import json, sys
needle, field = sys.argv[1].lower(), sys.argv[2]
for t in sorted(json.load(sys.stdin), key=lambda t: t["name"]):
    if needle in t["name"].lower():
        print(t[field]); break
else:
    sys.exit("no swarm torrent matches \"%s\"" % sys.argv[1])
' "$1" "${2:-hash}"
}

# wake HASH: a queued torrent neither announces nor opens files, so a break aimed at one
# would show nothing; force-start it past the playground's two-download queue (fix undoes it).
wake() {
    local state
    state="$(curl -fs "$PLAY/torrents/info?hashes=$1" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["state"])')"
    case "$state" in
    queued*) curl -fs -X POST "$PLAY/torrents/setForceStart" -d "hashes=$1" -d value=true >/dev/null ;;
    esac
}

cmd_start() {
    mkdir -p "$CONTENT" "$TORRENTS"
    [ -f "$CONTROL" ] || echo '{}' >"$CONTROL"
    control exemptPorts "[$SEEDER_BT_PORT]"
    play_server start | sed 's/^/playground: /'
    # Two downloads at a time, so the rest show as queued; but finished swarm torrents must not
    # use up the active slots (5 by default), or a later download sits "stalled" with its peer
    # right there, held back by the queue. Announcing to every tier lets `break dead`'s tracker
    # (a tier of its own) be tried, not skipped while the first one works.
    curl -fs -X POST "$PLAY/app/setPreferences" --data-urlencode \
        'json={"max_active_downloads":2,"max_active_uploads":20,"max_active_torrents":20,"announce_to_all_tiers":true,"announce_to_all_trackers":true}' >/dev/null
    seeder_server start | sed 's/^/seeder:     /'
    curl -fs -X POST "$SEEDER/app/setPreferences" --data-urlencode \
        "json={\"enable_embedded_tracker\":false,\"queueing_enabled\":false,\"up_limit\":$((${OMASEED_SWARM_RATE:-4096} * 1024))}" >/dev/null
    if ! tracker_running; then
        nohup python3 "$repo/scripts/dev/swarm-tracker.py" $TRACKER_PORT "$CONTROL" \
            >"$SWARM/tracker.log" 2>&1 &
        echo $! >"$TRACKER_PID"
        sleep 0.5
    fi
    tracker_running || { echo "tracker did not start; see $SWARM/tracker.log" >&2; exit 1; }
    echo "tracker:    $TRACKER (log $SWARM/tracker.log)"
}

cmd_stop() {
    if tracker_running; then kill "$(cat "$TRACKER_PID")"; fi
    rm -f "$TRACKER_PID"
    seeder_server stop | sed 's/^/seeder: /'
    echo "the playground on :8081 is left running"
}

# The fixtures: name, then a Python expression listing (relative path, size in bytes), then
# the torrent format. Files are sparse zeros on the seeder side: free to create, real bytes on
# the downloader side. About 1.4 GB in all.
generate_content() {
    python3 - "$CONTENT" <<'PY'
import os, random, sys
root = sys.argv[1]
MB = 1024 * 1024
rng = random.Random(2026)  # the same tree every time

def tree(name, files):
    for rel, size in files:
        path = os.path.join(root, name, rel) if rel else os.path.join(root, name)
        if os.path.exists(path) and os.path.getsize(path) == size:
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.truncate(size)

tree("Open Movie Night (2026)", [
    ("Open Movie Night (2026).mkv", 620 * MB),
    ("extras/behind the scenes.mkv", 64 * MB),
    ("extras/trailer.mp4", 18 * MB),
    ("extras/poster.jpg", 2 * MB + 12345),
    ("subs/english.srt", 96 * 1024),
    ("subs/português (Brasil).srt", 98 * 1024),
    ("subs/日本語.srt", 110 * 1024),
    ("sample/sample.mkv", 12 * MB),
    ("README.txt", 1834),
])
tree("distro-2026.09-x86_64.iso", [("", 480 * MB)])
tree("Field Recordings Vol. 3 [FLAC]", [
    (f"{i:02d} - {t}.flac", rng.randint(4, 9) * MB + rng.randint(0, 999999))
    for i, t in enumerate(["Harbour at Dawn", "Rain on Tin", "Market Voices", "Night Trains",
                           "Cicadas", "Wind Through Pines", "Old Clock Shop", "Tide Pools",
                           "Snowfall (Muffled)", "Café Terrace", "Bell Tower", "Thunder, Distant",
                           "Frogs After Rain", "Last Ferry"], 1)
] + [("cover.jpg", 1 * MB + 4321), ("album.cue", 2890)])
tree("photo-archive-2019-2025", [
    (f"{year}/{month:02d}/IMG_{year}{month:02d}{n:03d}.jpg", rng.randint(2, 60) * 1024)
    for year in range(2019, 2026) for month in range(1, 13) for n in range(rng.randint(10, 18))
])
tree("tiny-notes.txt", [("", 4096)])
tree("Ünïcødé ✓ Spaces & Symbols (100%) [v2] {test} — a deliberately long torrent name to test eliding everywhere", [
    ("naïve café — menu.pdf", 2 * MB),
    ("日本語のファイル名.txt", 3000),
    ("emoji 🎬 clip.mp4", 3 * MB),
])
tree("unwritable-fixture.bin", [("", 3 * MB)])
PY
}

# create NAME FORMAT: have the seeder build NAME's .torrent and start seeding it.
create_torrent() {
    local name="$1" format="$2" out="$TORRENTS/$1.torrent" task status
    [ -f "$out" ] && return 0
    task="$(curl -fs -X POST "$SEEDER/torrentcreator/addTask" \
        --data-urlencode "sourcePath=$CONTENT/$name" --data-urlencode "torrentFilePath=$out" \
        -d private=true -d startSeeding=true -d "format=$format" --data-urlencode "trackers=$TRACKER" |
        python3 -c 'import json,sys; print(json.load(sys.stdin)["taskID"])')"
    for _ in $(seq 1 600); do
        status="$(curl -fs "$SEEDER/torrentcreator/status?taskID=$task" |
            python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["status"])')"
        case "$status" in
        Finished) break ;;
        Failed) echo "creating $name failed" >&2; exit 1 ;;
        esac
        sleep 0.5
    done
    curl -fs -X POST "$SEEDER/torrentcreator/deleteTask" -d "taskID=$task" >/dev/null || true
    echo "created  $name ($format)"
}

# add NAME [SAVE_PATH]: hand NAME's .torrent to the playground, unless it is there already.
add_torrent() {
    local name="$1" save="${2:-$PLAY_DOWNLOADS/$CATEGORY}"
    if find_torrent "$name" name >/dev/null 2>&1; then return 0; fi
    curl -fs -X POST "$PLAY/torrents/add" -F "torrents=@$TORRENTS/$name.torrent" \
        -F "category=$CATEGORY" -F "savepath=$save" >/dev/null
    echo "added    $name"
}

cmd_seed() {
    tracker_running || cmd_start
    generate_content
    local unicode="Ünïcødé ✓ Spaces & Symbols (100%) [v2] {test} — a deliberately long torrent name to test eliding everywhere"
    create_torrent "tiny-notes.txt" hybrid
    create_torrent "Open Movie Night (2026)" hybrid
    create_torrent "distro-2026.09-x86_64.iso" v1
    create_torrent "Field Recordings Vol. 3 [FLAC]" hybrid
    create_torrent "photo-archive-2019-2025" hybrid
    create_torrent "$unicode" v2
    create_torrent "unwritable-fixture.bin" hybrid
    # The small one first, so something is seeding within seconds; the playground runs two
    # downloads at a time, so the rest queue behind them.
    add_torrent "tiny-notes.txt"
    add_torrent "$unicode"
    add_torrent "Open Movie Night (2026)"
    add_torrent "distro-2026.09-x86_64.iso"
    add_torrent "Field Recordings Vol. 3 [FLAC]"
    add_torrent "photo-archive-2019-2025"
    reannounce
    cmd_status
}

cmd_status() {
    echo "tracker $(tracker_running && echo up || echo down) · seeder $(curl -fs "$SEEDER/app/version" 2>/dev/null || echo 'not answering')"
    curl -fs "$PLAY/torrents/info?category=$CATEGORY" | python3 -c '
import json, sys
rows = sorted(json.load(sys.stdin), key=lambda t: t["name"])
for t in rows:
    name = t["name"] if len(t["name"]) <= 44 else t["name"][:43] + "…"
    print("  %-44s %-13s %5.1f%%  ↓ %6d KiB/s" % (name, t["state"], t["progress"] * 100, t["dlspeed"] // 1024))
print(f"  {len(rows)} swarm torrents on the playground")
'
}

cmd_rate() {
    curl -fs -X POST "$SEEDER/app/setPreferences" --data-urlencode "json={\"up_limit\":$(($1 * 1024))}" >/dev/null
    echo "seeder upload limit: $1 KiB/s"
}

cmd_break() {
    local case="${1:-}" name="${2:-}" hash v1 v2
    case "$case" in
    stall)
        curl -fs -X POST "$SEEDER/torrents/stop" -d hashes=all >/dev/null
        echo "the seeder stopped sharing; downloads stall" ;;
    tracker)
        name="${name:-Ünïcødé}"
        v1="$(find_torrent "$name" infohash_v1)"; v2="$(find_torrent "$name" infohash_v2)"
        wake "$(find_torrent "$name")"
        python3 - "$CONTROL" "$v1" "${v2:0:40}" <<'PY'
import json, sys
path = sys.argv[1]
data = json.load(open(path))
fail = data.setdefault("fail", {})
for h in sys.argv[2:]:
    if h:
        fail[h] = "unregistered torrent (omaseed swarm)"
json.dump(data, open(path, "w"))
PY
        reannounce
        echo "the tracker now fails $(find_torrent "$name" name)" ;;
    dead)
        name="${name:-distro}"
        hash="$(find_torrent "$name")"
        curl -fs -X POST "$PLAY/torrents/addTrackers" -d "hash=$hash" --data-urlencode "urls=$DEAD_TRACKER" >/dev/null
        wake "$hash"
        reannounce
        echo "added $DEAD_TRACKER to $(find_torrent "$name" name)" ;;
    missing)
        name="${name:-tiny-notes}"
        local content
        content="$(find_torrent "$name" content_path)"
        case "$content" in "$PLAY_DOWNLOADS"/*) ;; *) echo "refusing to delete $content" >&2; exit 1 ;; esac
        rm -rf -- "$content"
        play_server stop >/dev/null
        play_server start >/dev/null
        echo "deleted $content and restarted the playground" ;;
    error)
        local ro="$PLAY_DOWNLOADS/$CATEGORY-readonly"
        mkdir -p "$ro" && chmod 555 "$ro"
        add_torrent "unwritable-fixture.bin" "$ro"
        # Force-started outright: it is still loading here, too early for wake to see it queued.
        curl -fs -X POST "$PLAY/torrents/setForceStart" -d "hashes=$(find_torrent unwritable-fixture)" -d value=true >/dev/null
        echo "unwritable-fixture.bin saves into read-only $ro" ;;
    checking)
        name="${name:-Open Movie Night}"
        curl -fs -X POST "$PLAY/torrents/recheck" -d "hashes=$(find_torrent "$name")" >/dev/null
        echo "rechecking $(find_torrent "$name" name)" ;;
    crowd)
        control extraSeeds 12 && control extraLeechers 40
        reannounce
        echo "the tracker reports 12 extra seeds and 40 extra peers" ;;
    *)
        sed -n '/^# CASE/,/^set -e/p' "$0" | sed '$d'; exit 2 ;;
    esac
}

cmd_fix() {
    control fail null && control extraSeeds null && control extraLeechers null
    curl -fs -X POST "$SEEDER/torrents/start" -d hashes=all >/dev/null
    local hashes
    hashes="$(curl -fs "$PLAY/torrents/info?category=$CATEGORY" | python3 -c '
import json, sys
print("|".join(t["hash"] for t in json.load(sys.stdin)))')"
    if [ -n "$hashes" ]; then
        for h in ${hashes//|/ }; do
            curl -fs -X POST "$PLAY/torrents/removeTrackers" -d "hash=$h" --data-urlencode "urls=$DEAD_TRACKER" >/dev/null 2>&1 || true
        done
        # The unwritable fixture goes; missing files come back by downloading again.
        local unwritable
        unwritable="$(find_torrent unwritable-fixture hash 2>/dev/null || true)"
        [ -n "$unwritable" ] && curl -fs -X POST "$PLAY/torrents/delete" -d "hashes=$unwritable" -d deleteFiles=false >/dev/null
        curl -fs -X POST "$PLAY/torrents/recheck" -d "hashes=$(curl -fs "$PLAY/torrents/info?category=$CATEGORY&filter=errored" |
            python3 -c 'import json,sys; print("|".join(t["hash"] for t in json.load(sys.stdin)))')" >/dev/null || true
        curl -fs -X POST "$PLAY/torrents/setForceStart" -d "hashes=$hashes" -d value=false >/dev/null
        curl -fs -X POST "$PLAY/torrents/start" -d "hashes=$hashes" >/dev/null
    fi
    [ -d "$PLAY_DOWNLOADS/$CATEGORY-readonly" ] && chmod 755 "$PLAY_DOWNLOADS/$CATEGORY-readonly" &&
        rm -rf -- "$PLAY_DOWNLOADS/$CATEGORY-readonly"
    reannounce
    echo "every break undone"
}

cmd_reset() {
    local hashes
    hashes="$(curl -fs "$PLAY/torrents/info?category=$CATEGORY" | python3 -c '
import json, sys
print("|".join(t["hash"] for t in json.load(sys.stdin)))')"
    [ -n "$hashes" ] && curl -fs -X POST "$PLAY/torrents/delete" -d "hashes=$hashes" -d deleteFiles=true >/dev/null
    curl -fs -X POST "$SEEDER/torrents/delete" -d hashes=all -d deleteFiles=false >/dev/null || true
    [ -d "$PLAY_DOWNLOADS/$CATEGORY-readonly" ] && chmod 755 "$PLAY_DOWNLOADS/$CATEGORY-readonly"
    rm -rf -- "$PLAY_DOWNLOADS/$CATEGORY" "$PLAY_DOWNLOADS/$CATEGORY-readonly" "$TORRENTS" "$CONTENT"
    echo '{}' >"$CONTROL"
    control exemptPorts "[$SEEDER_BT_PORT]"
    echo "swarm torrents and files removed from both sides; run seed again"
}

case "${1:-}" in
start) cmd_start ;;
stop) cmd_stop ;;
seed) cmd_seed ;;
status) cmd_status ;;
break) shift; cmd_break "$@" ;;
fix) cmd_fix ;;
rate) cmd_rate "${2:?rate KIB}" ;;
reset) cmd_reset ;;
*) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 2 ;;
esac
