#!/usr/bin/env bash
# omaseed — a local BitTorrent swarm for real-data tests, fully offline.
#
# A seeder qBittorrent (:8083, its own profile) shares generated content through a tracker
# on 127.0.0.1 (scripts/dev/swarm-tracker.py); the playground (:8081) downloads it for real.
# Both clients listen on loopback only and the torrents are private, so nothing leaves the
# machine and the real library on :8080 is never involved. One fixture, the Public Domain
# Sampler, is not private: a magnet can fetch its metadata from the seeder (libtorrent never
# sends a private torrent's). Only the seeder has it; manual-box.sh hands its magnet out. The
# seeder has DHT, PeX and LSD off and listens on loopback, so it stays on this machine too.
#
#   scripts/dev/swarm.sh start            # seeder + tracker (and the playground if it is down)
#   scripts/dev/swarm.sh seed             # generate the fixtures, share them, add them to :8081
#   scripts/dev/swarm.sh status           # the swarm's torrents as the playground sees them
#   scripts/dev/swarm.sh break CASE [NAME]
#   scripts/dev/swarm.sh fix              # undo every break (stalled peers reconnect within
#                                         # a minute: libtorrent backs off before retrying)
#   scripts/dev/swarm.sh rate KIB         # the seeder's upload limit (default 4096 KiB/s)
#   scripts/dev/swarm.sh reset            # remove the swarm's torrents and files from both sides
#   scripts/dev/swarm.sh stop             # the seeder, the tracker and the leecher
#   scripts/dev/swarm.sh leech start [--rate KIB]   # the playground uploads, nonstop
#   scripts/dev/swarm.sh leech status     # one line: the playground's upload, the leecher's torrents
#   scripts/dev/swarm.sh leech stop       # the leecher and its files go
#
# leech: a fourth qBittorrent (:8084, profile $SWARM/leecher, loopback like the seeder) and a
# churn loop (scripts/dev/swarm-leech.py, in the background with a pid file, log leech.log)
# that downloads the fixtures the playground has finished, deletes each one with its files when
# it completes and adds it again after a random pause. Its busy phases (0-3 downloads, random
# per-torrent limits) vary which torrents upload and leave quiet stretches. The tracker tells
# the leecher about the playground only and nobody about the leecher, so every byte it gets is
# the playground's upload; the seeder's rate is not touched. --rate caps the leecher's download
# (default 2048 KiB/s). The loop ends itself when the playground or the seeder goes away.
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
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
SWARM="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}"
SEEDER_PORT=8083
SEEDER_BT_PORT=26997
TRACKER_PORT=26996
TRACKER="http://127.0.0.1:$TRACKER_PORT/announce"
DEAD_TRACKER="http://127.0.0.1:9/announce"
SEEDER="http://127.0.0.1:$SEEDER_PORT/api/v2"
PLAY_PORT="${OMASEED_TEST_PORT:-8081}"
PLAY="http://127.0.0.1:$PLAY_PORT/api/v2"
PLAY_DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
CONTENT="$SWARM/content"
TORRENTS="$SWARM/torrents"
CONTROL="$SWARM/control.json"
TRACKER_PID="$SWARM/tracker.pid"
CATEGORY=swarm
LEECH_PORT=8084
LEECH_BT_PORT=26995
LEECH_PROFILE="$SWARM/leecher"
LEECHER="http://127.0.0.1:$LEECH_PORT/api/v2"
LEECH_PID="$SWARM/leech.pid"
refuse_home "$SEEDER_PORT" "$SEEDER_BT_PORT" "$TRACKER_PORT" "$PLAY" "$LEECH_PORT" "$LEECH_BT_PORT"

seeder_server() {
    OMASEED_TEST_PROFILE="$SWARM" OMASEED_TEST_PORT=$SEEDER_PORT OMASEED_TEST_BT_PORT=$SEEDER_BT_PORT \
        "$repo/scripts/dev/test-server.sh" "$@"
}
play_server() { "$repo/scripts/dev/test-server.sh" "$@"; }
leecher_server() {
    OMASEED_TEST_PROFILE="$LEECH_PROFILE" OMASEED_TEST_PORT=$LEECH_PORT OMASEED_TEST_BT_PORT=$LEECH_BT_PORT \
        "$repo/scripts/dev/test-server.sh" "$@"
}

tracker_running() { [ -f "$TRACKER_PID" ] && kill -0 "$(cat "$TRACKER_PID")" 2>/dev/null; }
leech_running() { [ -f "$LEECH_PID" ] && kill -0 "$(cat "$LEECH_PID")" 2>/dev/null; }

start_tracker() {
    nohup python3 "$repo/scripts/dev/swarm-tracker.py" $TRACKER_PORT "$CONTROL" \
        >"$SWARM/tracker.log" 2>&1 &
    echo $! >"$TRACKER_PID"
    sleep 0.5
}

# control KEY JSON_VALUE: set one key of the tracker's control file (read on every announce).
control() {
    python3 - "$CONTROL" "$1" "$2" <<'PY'
import json, os, sys
path, key, value = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
try:
    data = json.load(open(path))
except (OSError, ValueError):
    data = {}
if value is None:
    data.pop(key, None)
else:
    data[key] = value
# Each writer its own temporary name: walks running side by side all call `swarm.sh start`,
# and with one shared name a writer's rename found the file already gone.
tmp = f"{path}.{os.getpid()}.tmp"
with open(tmp, "w") as f:
    json.dump(data, f)
os.replace(tmp, path)  # the tracker reads it on every announce: never a half-written file
PY
}

reannounce() {
    curl -fs -X POST "$SEEDER/torrents/reannounce" -d hashes=all >/dev/null || true
    curl -fs -X POST "$PLAY/torrents/reannounce" -d hashes=all >/dev/null || true
}

# need_play: stop here, with one line, when the playground isn't answering (#53).
need_play() {
    curl -fs "$PLAY/app/version" >/dev/null || {
        echo "the playground (127.0.0.1:$PLAY_PORT) isn't answering: start it with scripts/dev/test-server.sh start" >&2
        exit 1
    }
}

# play_json PATH PYTHON [ARG...]: run PYTHON with the playground's JSON answer to PATH on its
# stdin. When the playground doesn't answer, one line on stderr and status 1: piping curl
# straight into python3 handed it an empty string and ended in a traceback (#53).
play_json() {
    local path="$1" body
    shift
    body="$(curl -fs "$PLAY/$path")" || {
        echo "the playground (127.0.0.1:$PLAY_PORT) did not answer ${path%%\?*}" >&2
        return 1
    }
    printf '%s' "$body" | python3 -c "$@"
}

# find NAME [FIELD]: the playground's swarm torrent whose name contains NAME (first match).
find_torrent() {
    play_json "torrents/info?category=$CATEGORY" '
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
    state="$(play_json "torrents/info?hashes=$1" 'import json,sys; print(json.load(sys.stdin)[0]["state"])')"
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
    # (a tier of its own) be tried, not skipped while the first one works. Every peer of the
    # swarm is 127.0.0.1, so each qBittorrent must allow several connections from one IP: with
    # it off, libtorrent keeps one peer per IP (the last port the tracker named) and turns away
    # a second peer from 127.0.0.1 (M15: the manual box never reached the seeder).
    curl -fs -X POST "$PLAY/app/setPreferences" --data-urlencode \
        'json={"max_active_downloads":2,"max_active_uploads":20,"max_active_torrents":20,"announce_to_all_tiers":true,"announce_to_all_trackers":true,"enable_multi_connections_from_same_ip":true}' >/dev/null
    seeder_server start | sed 's/^/seeder:     /'
    curl -fs -X POST "$SEEDER/app/setPreferences" --data-urlencode \
        "json={\"enable_embedded_tracker\":false,\"queueing_enabled\":false,\"enable_multi_connections_from_same_ip\":true,\"up_limit\":$((${OMASEED_SWARM_RATE:-4096} * 1024))}" >/dev/null
    tracker_running || start_tracker
    tracker_running || { echo "tracker did not start; see $SWARM/tracker.log" >&2; exit 1; }
    echo "tracker:    $TRACKER (log $SWARM/tracker.log)"
}

cmd_stop() {
    cmd_leech_stop
    if tracker_running; then kill "$(cat "$TRACKER_PID")"; fi
    rm -f "$TRACKER_PID"
    seeder_server stop | sed 's/^/seeder: /'
    echo "the playground on :$PLAY_PORT is left running"
}

# The fixtures: name, then a Python expression listing (relative path, size in bytes), then
# the torrent format. Files are sparse zeros on the seeder side: free to create, real bytes on
# the downloader side. About 1.5 GB in all.
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
tree("Public Domain Sampler", [
    ("sampler.mkv", 24 * MB),
    ("sampler.srt", 42 * 1024),
    ("README.txt", 812),
])

# Many files in deep folders (the add sheet's tree, M17/M18). tests/fixtures/make_torrents.py's
# nested_library_files() lists the same tree (same seed, same order): keep the two in step.
def nested_library():
    rng = random.Random(29)
    files = []
    for season, eps in ((1, 10), (2, 8)):
        for e in range(1, eps + 1):
            base = f"Season {season:02d}/S{season:02d}E{e:02d} - Episode {e}"
            files.append((f"{base}.mkv", rng.randint(2, 5) * MB + rng.randint(0, 99999)))
            for lang in ("en", "pt-BR", "ja"):
                files.append((f"Season {season:02d}/Subs/S{season:02d}E{e:02d}.{lang}.srt",
                              rng.randint(30, 60) * 1024))
    for day in range(1, 6):
        for c in range(1, rng.randint(3, 6)):
            files.append((f"Extras/Behind the Scenes/Day {day:02d}/clip {c:02d}.mkv",
                          rng.randint(1, 3) * MB))
    for who in ("Director", "Composer", "Lead Actor", "Café Owner (cameo)"):
        files.append((f"Extras/Interviews/{who}.mp4", rng.randint(2, 4) * MB))
    for i in range(1, 13):
        files.append((f"Artwork/Stills/still_{i:03d}.png", rng.randint(200, 900) * 1024))
    for p in ("Poster [A]", "Poster [B]", "ポスター (Japan)"):
        files.append((f"Artwork/Posters/{p}.jpg", rng.randint(300, 1200) * 1024))
    return files + [
        ("Docs/README.md", 4321), ("Docs/Changelog.txt", 2100),
        ("Docs/licenses/LICENSE-CC-BY-4.0.txt", 18_000), ("Docs/licenses/LICENSE-MIT.txt", 1_070),
        ("deep/a/b/c/d/e/f/the deepest file.txt", 512),
        ("Soundtrack/01 - Opening.flac", 3 * MB), ("Soundtrack/02 - Main Theme.flac", 4 * MB),
        ("Soundtrack/03 - Credits (Extended Mix).flac", 5 * MB), ("Soundtrack/cover.jpg", 400 * 1024),
        ("sample.mkv", 2 * MB), ("checksums.sha256", 9000),
    ]

tree("Nested Library Sampler (2026)", nested_library())
PY
}

# create NAME FORMAT [PRIVATE]: have the seeder build NAME's .torrent and start seeding it.
# PRIVATE is true unless given.
create_torrent() {
    local name="$1" format="$2" private="${3:-true}" out="$TORRENTS/$1.torrent" task status
    [ -f "$out" ] && return 0
    task="$(curl -fs -X POST "$SEEDER/torrentcreator/addTask" \
        --data-urlencode "sourcePath=$CONTENT/$name" --data-urlencode "torrentFilePath=$out" \
        -d "private=$private" -d startSeeding=true -d "format=$format" --data-urlencode "trackers=$TRACKER" |
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
    [ "$status" = Finished ] || { echo "creating $name timed out after 5 minutes ($status)" >&2; exit 1; }
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
    need_play
    mkdir -p "$CONTENT" "$TORRENTS" # reset removed them, and the tracker may still be up
    generate_content
    local unicode="Ünïcødé ✓ Spaces & Symbols (100%) [v2] {test} — a deliberately long torrent name to test eliding everywhere"
    create_torrent "tiny-notes.txt" hybrid
    create_torrent "Open Movie Night (2026)" hybrid
    create_torrent "distro-2026.09-x86_64.iso" v1
    create_torrent "Field Recordings Vol. 3 [FLAC]" hybrid
    create_torrent "photo-archive-2019-2025" hybrid
    create_torrent "$unicode" v2
    create_torrent "unwritable-fixture.bin" hybrid
    create_torrent "Public Domain Sampler" hybrid false # the seeder's only: see the top
    create_torrent "Nested Library Sampler (2026)" hybrid # the seeder's only: for the add sheet
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
    local play
    play="$(curl -fs "$PLAY/app/version" 2>/dev/null)" || play=down
    echo "tracker $(tracker_running && echo up || echo down) · seeder $(curl -fs "$SEEDER/app/version" 2>/dev/null || echo 'not answering') · playground $play"
    [ "$play" != down ] || return 0
    play_json "torrents/info?category=$CATEGORY" '
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
    case "$case" in tracker | dead | missing | error | checking) need_play ;; esac
    case "$case" in
    stall)
        curl -fs -X POST "$SEEDER/torrents/stop" -d hashes=all >/dev/null
        echo "the seeder stopped sharing; downloads stall" ;;
    tracker)
        name="${name:-Ünïcødé}"
        v1="$(find_torrent "$name" infohash_v1)"; v2="$(find_torrent "$name" infohash_v2)"
        wake "$(find_torrent "$name")"
        python3 - "$CONTROL" "$v1" "${v2:0:40}" <<'PY'
import json, os, sys
path = sys.argv[1]
data = json.load(open(path))
fail = data.setdefault("fail", {})
for h in sys.argv[2:]:
    if h:
        fail[h] = "unregistered torrent (omaseed swarm)"
tmp = f"{path}.{os.getpid()}.tmp"
with open(tmp, "w") as f:
    json.dump(data, f)
os.replace(tmp, path)
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
    need_play
    control fail null && control extraSeeds null && control extraLeechers null
    curl -fs -X POST "$SEEDER/torrents/start" -d hashes=all >/dev/null
    local hashes
    hashes="$(play_json "torrents/info?category=$CATEGORY" '
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
        curl -fs -X POST "$PLAY/torrents/recheck" -d "hashes=$(play_json "torrents/info?category=$CATEGORY&filter=errored" \
            'import json,sys; print("|".join(t["hash"] for t in json.load(sys.stdin)))')" >/dev/null || true
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
    cmd_leech_stop
    # The playground may be down: the seeder's torrents and the files still go.
    hashes="$(play_json "torrents/info?category=$CATEGORY" '
import json, sys
print("|".join(t["hash"] for t in json.load(sys.stdin)))')" || hashes=""
    if [ -n "$hashes" ]; then
        curl -fs -X POST "$PLAY/torrents/delete" -d "hashes=$hashes" -d deleteFiles=true >/dev/null ||
            echo "the playground did not take the delete; its swarm torrents stay" >&2
    fi
    curl -fs -X POST "$SEEDER/torrents/delete" -d hashes=all -d deleteFiles=false >/dev/null || true
    if [ -d "$PLAY_DOWNLOADS/$CATEGORY-readonly" ]; then chmod 755 "$PLAY_DOWNLOADS/$CATEGORY-readonly"; fi
    rm -rf -- "${PLAY_DOWNLOADS:?}/$CATEGORY" "${PLAY_DOWNLOADS:?}/$CATEGORY-readonly" \
        "${TORRENTS:?}" "${CONTENT:?}"
    echo '{}' >"$CONTROL"
    control exemptPorts "[$SEEDER_BT_PORT]"
    echo "swarm torrents and files removed from both sides; run seed again"
}

# human BYTES: 1.4 GiB, 512 KiB, ...
human() {
    python3 -c '
import sys
n = float(sys.argv[1])
for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
    if n < 1024 or unit == "TiB":
        print(("%d %s" if unit == "B" else "%.1f %s") % (n, unit)); break
    n /= 1024' "$1"
}

# A tracker started before swarm-tracker.py last changed runs old code (the leech needs its
# "isolate" key): restart it, and have both sides announce again, since it forgets its peers.
refresh_tracker() {
    local pid age
    tracker_running || return 0
    pid="$(cat "$TRACKER_PID")"
    age="$(ps -o etimes= -p "$pid" | tr -d ' ')"
    [ "$(($(date +%s) - age))" -lt "$(stat -c %Y "$repo/scripts/dev/swarm-tracker.py")" ] || return 0
    kill "$pid"
    for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
    start_tracker
    tracker_running || { echo "tracker did not restart; see $SWARM/tracker.log" >&2; exit 1; }
    reannounce
    echo "tracker:    restarted on newer code"
}

cmd_leech_start() {
    local rate="${OMASEED_LEECH_RATE:-2048}" play_bt
    while [ $# -gt 0 ]; do
        case "$1" in
        --rate) rate="${2:?--rate KIB}"; shift ;;
        *) echo "leech start: unexpected $1" >&2; exit 2 ;;
        esac
        shift
    done
    [[ "$rate" =~ ^[0-9]+$ ]] && [ "$rate" -gt 0 ] || { echo "leech start: --rate takes KiB/s, more than 0" >&2; exit 2; }
    tracker_running || cmd_start
    curl -fs "$SEEDER/app/version" >/dev/null || { echo "the seeder on :$SEEDER_PORT does not answer: swarm.sh start" >&2; exit 1; }
    need_play
    if ! play_json "torrents/info?category=$CATEGORY&filter=completed" \
        'import json,sys; sys.exit(0 if json.load(sys.stdin) else 1)'; then
        echo "the playground has no finished swarm torrent to share: swarm.sh seed, and let it download" >&2
        exit 1
    fi
    refresh_tracker
    # The leecher meets only the playground's port; the seeder never hears of it.
    play_bt="$(play_json app/preferences 'import json,sys; print(json.load(sys.stdin)["listen_port"])')"
    control isolate "{\"$LEECH_BT_PORT\":[$play_bt]}"
    leecher_server start | sed 's/^/leecher:    /'
    # No queue (the loop decides what runs), several peers from 127.0.0.1, the download cap.
    curl -fs -X POST "$LEECHER/app/setPreferences" --data-urlencode \
        "json={\"queueing_enabled\":false,\"enable_multi_connections_from_same_ip\":true,\"announce_to_all_tiers\":true,\"max_ratio_enabled\":false,\"dl_limit\":$((rate * 1024))}" >/dev/null
    if leech_running; then
        echo "churn loop: already running (pid $(cat "$LEECH_PID")); download cap now $rate KiB/s"
        return 0
    fi
    rm -f "$LEECH_PID"
    setsid nohup python3 "$repo/scripts/dev/swarm-leech.py" "$LEECH_PID" "$TORRENTS" "$CATEGORY" \
        "$LEECHER" "$PLAY" "$SEEDER" >>"$SWARM/leech.log" 2>&1 </dev/null &
    for _ in $(seq 1 20); do leech_running && break; sleep 0.25; done
    leech_running || { echo "the churn loop did not start; see $SWARM/leech.log" >&2; exit 1; }
    echo "churn loop: pid $(cat "$LEECH_PID"), download cap $rate KiB/s (log $SWARM/leech.log)"
}

cmd_leech_stop() {
    local pid
    # Nothing to say when there was no leech (swarm.sh stop and reset call this every time).
    [ -e "$LEECH_PID" ] || [ -e "$LEECH_PROFILE" ] || { control_has_isolate || return 0; }
    if leech_running; then
        pid="$(cat "$LEECH_PID")"
        kill "$pid"
        for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || break; sleep 0.25; done
    fi
    rm -f "$LEECH_PID"
    if [ -f "$LEECH_PROFILE/qbt.pid" ]; then leecher_server stop >/dev/null; fi
    rm -rf -- "${LEECH_PROFILE:?}"
    if [ -f "$CONTROL" ]; then control isolate null; fi
    echo "leech: stopped, its profile and downloads removed"
}

control_has_isolate() { [ -f "$CONTROL" ] && grep -q '"isolate"' "$CONTROL"; }

cmd_leech_status() {
    local up mine
    mine="$(curl -fs "$LEECHER/torrents/info" 2>/dev/null)" || mine=""
    up="$(play_json transfer/info 'import json,sys; t=json.load(sys.stdin); print(t["up_info_data"], t["up_info_speed"])' 2>/dev/null)" ||
        up=""
    {
        if leech_running; then echo "leech running (pid $(cat "$LEECH_PID"))"; else echo "leech not running"; fi
        if [ -n "$up" ]; then
            echo "playground ↑ $(human "${up% *}") this session, $(human "${up#* }")/s now"
        else
            echo "playground not answering"
        fi
        [ -z "$mine" ] || printf '%s' "$mine" | python3 -c '
import json, sys
rows = [t for t in json.load(sys.stdin) if not t["state"].startswith("stopped")]
names = ", ".join("%s %d%%" % (t["name"] if len(t["name"]) <= 24 else t["name"][:23] + "…", t["progress"] * 100)
                  for t in sorted(rows, key=lambda t: t["name"]))
print("leecher: %d active%s" % (len(rows), " (" + names + ")" if rows else ""))'
    } | paste -sd '|' | sed 's/|/ · /g'
}

cmd_leech() {
    case "${1:-}" in
    start) shift; cmd_leech_start "$@" ;;
    stop) cmd_leech_stop ;;
    status) cmd_leech_status ;;
    *) echo "usage: swarm.sh leech start [--rate KIB] | stop | status" >&2; exit 2 ;;
    esac
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
leech) shift; cmd_leech "$@" ;;
*) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 2 ;;
esac
