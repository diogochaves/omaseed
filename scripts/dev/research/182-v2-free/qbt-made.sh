#!/usr/bin/env bash
# omaseed #182 through a real qBittorrent: a v2-only torrent the box's qBittorrent makes and
# seeds (as the actions walk's b44-v2), one file skipped, then what "free the space" did before
# C29 (stop, delete it, start, recheck) on one copy, and "add it again" on another. Prints the
# state and progress after each.
#
#   qbt-made.sh BOX       # a box from `scripts/dev/app-box.sh up --qbt -b BOX`;
#                         # its r182-* torrents and folders are made again each run
set -euo pipefail

[ $# = 1 ] || { sed -n '2,8p' "$0"; exit 2; }
BOX="$1"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
api() { "$repo/scripts/dev/app-box.sh" api -b "$BOX" "$@"; }
home="$(omabox path -b "$BOX")/home"
TORRENTS=/home/sbx/Torrents

hash_of() {
    api torrents/info | python3 -I -c '
import json, sys
print(next((t["hash"] for t in json.load(sys.stdin) if t["name"] == sys.argv[1]), ""))' "$1"
}
# info NAME HASH: one line, the torrent's state and progress.
info() {
    api "torrents/info?hashes=$2" | python3 -I -c '
import json, sys
t = json.load(sys.stdin)[0]
print("  %-16s state=%-10s progress=%.4f v1=%s v2=%s" % (sys.argv[1], t["state"], t["progress"],
      bool(t["infohash_v1"]), bool(t["infohash_v2"])))' "$1"
}
settle() {
    for _ in $(seq 1 60); do
        case "$(api "torrents/info?hashes=$1")" in *checking* | *\"moving\"*) sleep 0.5 ;; *) break ;; esac
    done
    sleep 2
}

# make NAME: a.bin to d.bin, no size a piece multiple, made into a v2-only torrent and seeded.
make() {
    local i sizes=(8388608 3158073 2097929 500003) files=(a b c d)
    mkdir -p "$home/Torrents/$1"
    for i in 0 1 2 3; do head -c "${sizes[$i]}" /dev/urandom >"$home/Torrents/$1/${files[$i]}.bin"; done
    api torrentcreator/addTask -d "sourcePath=$TORRENTS/$1" -d format=v2 -d startSeeding=true >/dev/null
    for _ in $(seq 1 120); do
        [ -n "$(hash_of "$1")" ] && [ "$(api "torrents/info?hashes=$(hash_of "$1")" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)[0]["progress"])')" = 1 ] && break
        sleep 0.5
    done
}

for name in r182-recheck r182-readd; do
    h="$(hash_of "$name")"
    [ -z "$h" ] || api torrents/delete -d "hashes=$h" -d deleteFiles=true >/dev/null
    for _ in $(seq 1 40); do [ -z "$(hash_of "$name")" ] && break; sleep 0.5; done
done
rm -rf "$home/Torrents/r182-recheck" "$home/Torrents/r182-readd"
for name in r182-recheck r182-readd; do make "$name"; done
R="$(hash_of r182-recheck)"
A="$(hash_of r182-readd)"
for h in "$R" "$A"; do api torrents/filePrio -d "hash=$h" -d id=2 -d priority=0 >/dev/null; done
sleep 1
echo "made, c.bin skipped:"
info r182-recheck "$R"
info r182-readd "$A"

echo "stop, delete c.bin, start, recheck (free the space before C29):"
api torrents/stop -d "hashes=$R" >/dev/null
sleep 2
rm "$home/Torrents/r182-recheck/c.bin"
api torrents/start -d "hashes=$R" >/dev/null
api torrents/recheck -d "hashes=$R" >/dev/null
settle "$R"
info r182-recheck "$R"

echo "add it again: export, remove keeping files, delete c.bin, add stopped, skip c.bin, start:"
api "torrents/export?hash=$A" -o /home/sbx/r182-readd.torrent
api torrents/delete -d "hashes=$A" -d deleteFiles=false >/dev/null
sleep 2
rm "$home/Torrents/r182-readd/c.bin"
api torrents/add -F "torrents=@/home/sbx/r182-readd.torrent" -F "savepath=$TORRENTS" -F stopped=true >/dev/null
for _ in $(seq 1 40); do [ -n "$(hash_of r182-readd)" ] && break; sleep 0.5; done
A="$(hash_of r182-readd)"
api torrents/filePrio -d "hash=$A" -d id=2 -d priority=0 >/dev/null
api torrents/start -d "hashes=$A" >/dev/null
settle "$A"
info r182-readd "$A"
