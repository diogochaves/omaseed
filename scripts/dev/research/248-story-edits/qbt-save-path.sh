#!/usr/bin/env bash
# #248 (board 58, DECISIONS C43 Y6/Y7): when does qBittorrent's save_path change during a move,
# and does a set location keep automatic management on?
#
#   scripts/dev/research/248-story-edits/qbt-save-path.sh -b BOX [--mib N] [--throttle]
#
# In a box with its own qBittorrent (scripts/dev/app-box.sh up --qbt -b BOX, made first), after
# research/162-preallocated-move/watch-move.sh: a single-file torrent of N MiB (default 1024)
# made in the box's ~/Torrents/r248 and added as complete. Then, each printed every 0.1 s (a
# line only when a reading changes) as `save_path, download_path, state, auto_tmm`:
#   1. torrents/setLocation to /tmp/r248 (the box's tmpfs, another filesystem: a copy);
#   2. category r248cat (savePath ~/Torrents/r248cat, back across), setCategory, then
#      setAutoManagement on;
#   3. torrents/setLocation to /tmp/r248loc (across again) on the auto-managed torrent:
#      auto_tmm after it.
# The torrent, its files and the category are removed at the end. --throttle stops and continues
# the box's qBittorrent (SIGSTOP 0.8 s, SIGCONT 0.02 s) during the moves, to make them last.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX=""
MIB=1024
THROTTLE=0
while [ $# -gt 0 ]; do
    case "$1" in
    -b) BOX="$2"; shift 2 ;;
    --mib) MIB="$2"; shift 2 ;;
    --throttle) THROTTLE=1; shift ;;
    -h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$BOX" ] || { echo "-b BOX: a box made with app-box.sh up --qbt" >&2; exit 2; }

api() { "$APP_BOX" api -b "$BOX" "$@"; }
home="$(omabox path -b "$BOX")/home"
NAME="r248-feature.bin"

HASH="$(python3 - "$home" "$MIB" "$NAME" <<'PY'
import hashlib, os, sys
home, mib, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
PIECE = 1 << 20
block = os.urandom(PIECE)
folder = os.path.join(home, "Torrents/r248")
os.makedirs(folder, exist_ok=True)
with open(os.path.join(folder, name), "wb") as f:
    for _ in range(mib):
        f.write(block)
piece = hashlib.sha1(block).digest()
def benc(x):
    if isinstance(x, int): return b"i%de" % x
    if isinstance(x, bytes): return b"%d:%s" % (len(x), x)
    if isinstance(x, str): return benc(x.encode())
    return b"d" + b"".join(benc(k) + benc(x[k]) for k in sorted(x)) + b"e"
info = {"name": name, "length": mib * PIECE, "piece length": PIECE, "pieces": piece * mib, "private": 1}
with open(os.path.join(home, "r248.torrent"), "wb") as f:
    f.write(benc({"created by": "r248", "info": info}))
print(hashlib.sha1(benc(info)).hexdigest())
PY
)"
api torrents/add -F torrents=@/home/sbx/r248.torrent -F savepath=/home/sbx/Torrents/r248 \
    -F skip_checking=true -F seedMode=true -F autoTMM=false >/dev/null
for _ in $(seq 1 30); do
    api "torrents/info?hashes=$HASH" | grep -q '"progress":1' &&
        ! api "torrents/info?hashes=$HASH" | grep -q '"state":"checking' && break
    sleep 0.5
done
echo "qBittorrent $(api app/version) (WebUI API $(api app/webapiVersion)), $MIB MiB"
pid="$(omabox run -b "$BOX" -- pgrep -x qbittorrent-nox)"

reading() {
    api "torrents/info?hashes=$HASH" | python3 -c '
import json, sys
t = json.load(sys.stdin)[0]
print("save_path %s  download_path %s  state %s  auto_tmm %s" % (
    t["save_path"], t["download_path"] or chr(34) * 2, t["state"], str(t["auto_tmm"]).lower()))'
}

throttle_on() {
    [ "$THROTTLE" = 1 ] || return 0
    omabox run -b "$BOX" -d -- sh -c "while kill -STOP $pid; do sleep 0.8; kill -CONT $pid; sleep 0.02; done" >/dev/null 2>&1
}
throttle_off() {
    [ "$THROTTLE" = 1 ] || return 0
    omabox run -b "$BOX" -- pkill -f "while kill -STOP" || true
    omabox run -b "$BOX" -- kill -CONT "$pid" || true
}

# Readings until the torrent sits in $1 and isn't moving, for three readings in a row (60 s cap).
watch_until() {
    local target="$1" start last="" line t still=0
    start=$(date +%s%N)
    while :; do
        line="$(reading)"
        t=$((($(date +%s%N) - start) / 100000000))
        if [ "$line" != "$last" ]; then
            printf '   %4d.%ds  %s\n' $((t / 10)) $((t % 10)) "$line"
            last="$line"
        fi
        case "$line" in
        "save_path $target "*) case "$line" in *"state moving"*) still=0 ;; *) still=$((still + 1)) ;; esac ;;
        *) still=0 ;;
        esac
        [ "$still" -ge 3 ] && break
        [ "$t" -ge 600 ] && { echo "   not settled within 60 s"; break; }
        sleep 0.1
    done
}

cleanup() {
    throttle_off
    api torrents/delete -d "hashes=$HASH" -d deleteFiles=true >/dev/null || true
    api torrents/removeCategories -d categories=r248cat >/dev/null || true
    rm -f "$home/r248.torrent"
}
trap cleanup EXIT

echo
echo "1. setLocation /tmp/r248"
echo "   before: $(reading)"
throttle_on
api torrents/setLocation -d "hashes=$HASH" --data-urlencode location=/tmp/r248 >/dev/null
watch_until /tmp/r248
throttle_off

echo
echo "2. category r248cat (savePath /home/sbx/Torrents/r248cat), setCategory, then setAutoManagement on"
api torrents/createCategory -d category=r248cat --data-urlencode savePath=/home/sbx/Torrents/r248cat >/dev/null
api torrents/setCategory -d "hashes=$HASH" -d category=r248cat >/dev/null
echo "   after setCategory: $(reading)"
throttle_on
api torrents/setAutoManagement -d "hashes=$HASH" -d enable=true >/dev/null
watch_until /home/sbx/Torrents/r248cat
throttle_off

echo
echo "3. setLocation /tmp/r248loc on the auto-managed torrent"
throttle_on
api torrents/setLocation -d "hashes=$HASH" --data-urlencode location=/tmp/r248loc >/dev/null
watch_until /tmp/r248loc
throttle_off
