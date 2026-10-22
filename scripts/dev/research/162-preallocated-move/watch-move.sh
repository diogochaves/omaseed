#!/usr/bin/env bash
# #162: how big is a torrent's file at its new place while qBittorrent moves it?
#
#   scripts/dev/research/162-preallocated-move/watch-move.sh [-b BOX] [--mib N] [--throttle]
#
# In a box with its own qBittorrent (scripts/dev/app-box.sh up --qbt -b BOX, made first): a
# single-file torrent of N MiB (default 2048) made in the box's ~/Torrents/r162, added as
# complete, then moved to /tmp/r162 (the box's tmpfs, another filesystem, so qBittorrent
# copies). The destination file's size is printed every 0.1 s until the move ends, then the
# torrent and its files are deleted. --throttle stops and continues the box's qBittorrent
# (SIGSTOP 0.8 s, SIGCONT 0.02 s) during the move, as move-walk.sh does to make the move last.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX=""
MIB=2048
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
NAME="r162-feature.bin"

python3 - "$home" "$MIB" "$NAME" <<'PY'
import hashlib, os, sys
home, mib, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
PIECE = 1 << 20
block = os.urandom(PIECE)
folder = os.path.join(home, "Torrents/r162")
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
with open(os.path.join(home, "r162.torrent"), "wb") as f:
    f.write(benc({"created by": "r162", "info": info}))
print(hashlib.sha1(benc(info)).hexdigest())
PY
HASH="$(python3 -c '
import hashlib, sys
d = open(sys.argv[1], "rb").read()
i = d.index(b"4:info") + 6
print(hashlib.sha1(d[i:-1]).hexdigest())' "$home/r162.torrent")"
api torrents/add -F torrents=@/home/sbx/r162.torrent -F savepath=/home/sbx/Torrents/r162 \
    -F skip_checking=true -F seedMode=true >/dev/null
for _ in $(seq 1 30); do
    api "torrents/info?hashes=$HASH" | grep -q '"progress":1' && break
    sleep 0.5
done
echo "qBittorrent $(api app/version), $MIB MiB, hash $HASH"
pid="$(omabox run -b "$BOX" -- pgrep -x qbittorrent-nox)"
api torrents/setLocation -d "hashes=$HASH" --data-urlencode location=/tmp/r162 >/dev/null
if [ "$THROTTLE" = 1 ]; then
    omabox run -b "$BOX" -d -- sh -c "while kill -STOP $pid; do sleep 0.8; kill -CONT $pid; sleep 0.02; done" >/dev/null
fi
start=$(date +%s%N)
while :; do
    size="$(omabox run -b "$BOX" -- stat -c %s "/tmp/r162/$NAME" 2>/dev/null || echo -)"
    blocks="$(omabox run -b "$BOX" -- stat -c %b "/tmp/r162/$NAME" 2>/dev/null || echo -)"
    src="$(omabox run -b "$BOX" -- stat -c %s "/home/sbx/Torrents/r162/$NAME" 2>/dev/null || echo gone)"
    t=$((($(date +%s%N) - start) / 100000000))
    printf '%4d.%ds  dest size %s  dest 512-byte blocks %s  source %s\n' \
        $((t / 10)) $((t % 10)) "$size" "$blocks" "$src"
    [ "$src" = gone ] && break
    sleep 0.1
done
if [ "$THROTTLE" = 1 ]; then
    omabox run -b "$BOX" -- pkill -f "while kill -STOP" || true
    omabox run -b "$BOX" -- kill -CONT "$pid" || true
fi
api torrents/delete -d "hashes=$HASH" -d deleteFiles=true >/dev/null
rm -f "$home/r162.torrent"
