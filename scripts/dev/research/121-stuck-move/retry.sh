#!/usr/bin/env bash
# #121 research: once a finished torrent's move out of its incomplete folder failed (a full
# destination), what makes qBittorrent try it again? Stages the stuck move as
# scripts/dev/stuck-walk.sh does (a small FAT image through udisksctl, a throwaway
# qBittorrent, a torrent already complete in the incomplete folder), frees the destination,
# then tries, in order, and prints where the content is after each:
#   1. torrents/setLocation to the same save path (the one it never reached);
#   2. a recheck;
#   3. torrents/setDownloadPath to "" (no incomplete folder any more);
#   4. torrents/setLocation to another folder, then back to the destination.
# The image holds a filler, so the 12 MiB torrent doesn't fit at first; "freeing" removes the
# filler and the partial copies, so it would fit afterwards.
#
#   scripts/dev/research/121-stuck-move/retry.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$repo"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
PORT="${OMASEED_STUCK_PORT:-8098}"
BT_PORT="${OMASEED_STUCK_BT_PORT:-26986}"
API="http://127.0.0.1:$PORT/api/v2"
refuse_home "$API" "$BT_PORT"
WORK="${OMASEED_STUCK_DIR:-$HOME/.local/share/omaseed-stuck-research}"
PROFILE="$WORK/qbt"
INCOMPLETE="$WORK/incomplete"
IMAGE="$WORK/full.img"
NAME="Stuck Move Sampler"

server() {
    OMASEED_TEST_PROFILE="$PROFILE" OMASEED_TEST_PORT="$PORT" OMASEED_TEST_BT_PORT="$BT_PORT" \
        scripts/dev/test-server.sh "$@"
}

LOOP=""
MOUNT=""
cleanup() {
    server stop >/dev/null 2>&1 || true
    if [ -n "$LOOP" ]; then
        udisksctl unmount --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
        udisksctl loop-delete --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$WORK"
}
trap cleanup EXIT

torrent_info() {
    curl -fs "$API/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"] == sys.argv[1]:
        print(t.get(sys.argv[2], "")); break' "$NAME" "$1" 2>/dev/null || true
}
where() { echo "$1: state $(torrent_info state), save $(torrent_info save_path), content $(torrent_info content_path)"; }
settle() { sleep "${1:-6}"; }

# The full destination, and the torrent complete in the incomplete folder.
rm -rf -- "$WORK"
mkdir -p "$INCOMPLETE/$NAME" "$WORK/elsewhere"
truncate -s 24M "$IMAGE"
mkfs.vfat -n OMASEEDRSCH "$IMAGE" >/dev/null
LOOP="$(udisksctl loop-setup --no-user-interaction -f "$IMAGE" | sed -n 's/.* as \(\/dev\/loop[0-9]*\)\.$/\1/p')"
for _ in $(seq 1 20); do
    MOUNT="$(lsblk -no MOUNTPOINT "$LOOP" 2>/dev/null | head -1)"
    [ -n "$MOUNT" ] && break
    udisksctl mount --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true
    sleep 0.25
done
head -c $((14 * 1048576)) /dev/zero >"$MOUNT/filler.bin"
for n in $(seq -w 1 12); do
    head -c 1048576 /dev/urandom >"$INCOMPLETE/$NAME/part-$n.bin"
done
python3 - "$INCOMPLETE/$NAME" "$WORK/stuck.torrent" <<'PY'
import hashlib, os, sys
root, out = sys.argv[1], sys.argv[2]
piece = 262144
files = sorted(os.listdir(root))
data = b"".join(open(os.path.join(root, f), "rb").read() for f in files)
pieces = b"".join(hashlib.sha1(data[i:i + piece]).digest() for i in range(0, len(data), piece))
def enc(v):
    if isinstance(v, int): return b"i%de" % v
    if isinstance(v, str): v = v.encode()
    if isinstance(v, bytes): return b"%d:%s" % (len(v), v)
    if isinstance(v, list): return b"l" + b"".join(enc(x) for x in v) + b"e"
    return b"d" + b"".join(enc(k) + enc(v[k]) for k in sorted(v)) + b"e"
info = {"name": os.path.basename(root), "piece length": piece, "pieces": pieces, "private": 1,
        "files": [{"length": os.path.getsize(os.path.join(root, f)), "path": [f]} for f in files]}
open(out, "wb").write(enc({"announce": "http://127.0.0.1:9/announce", "info": info}))
PY
server start >/dev/null
echo "qBittorrent $(curl -fs "$API/app/version"), destination $MOUNT"
curl -fs "$API/torrents/add" -F "torrents=@$WORK/stuck.torrent" -F "savepath=$MOUNT" \
    -F "useDownloadPath=true" -F "downloadPath=$INCOMPLETE" -F "autoTMM=false" >/dev/null
settle 10
HASH="$(torrent_info hash)"
where "stuck"
rm -rf -- "${MOUNT:?}/$NAME" "$MOUNT/filler.bin"
echo "destination freed: $(df -h --output=avail "$MOUNT" | tail -1 | tr -d ' ') free"

curl -fs "$API/torrents/setLocation" -d "hashes=$HASH" --data-urlencode "location=$MOUNT" >/dev/null
settle
where "1. setLocation to the same save path"

curl -fs "$API/torrents/recheck" -d "hashes=$HASH" >/dev/null
settle
where "2. recheck"

curl -fs "$API/torrents/setDownloadPath" -d "id=$HASH" --data-urlencode "path=" >/dev/null || echo "(setDownloadPath refused)"
settle
where "3. setDownloadPath to nothing"

curl -fs "$API/torrents/setLocation" -d "hashes=$HASH" --data-urlencode "location=$WORK/elsewhere" >/dev/null
settle
where "4a. setLocation to another folder"
curl -fs "$API/torrents/setLocation" -d "hashes=$HASH" --data-urlencode "location=$MOUNT" >/dev/null
settle
where "4b. and back to the destination"
