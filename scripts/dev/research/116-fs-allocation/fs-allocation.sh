#!/usr/bin/env bash
# omaseed #116: what a filesystem does with the files a torrent starts writing. Makes a loop
# image of one filesystem, mounts it through udisks (as the user, no root), and runs three cases
# on a swarm fixture in the playground qBittorrent, measuring allocated bytes (st_blocks):
#   A      only some files wanted from the start
#   Cstop  the torrent stopped: want every file for 3 s, skip them again (nothing can download)
#   C      the torrent finished: want every file for ~3 s, skip them again
#
#   fs-allocation.sh LABEL MKFS_KIND [MOUNT_TYPE] [MOUNT_OPTIONS]
#   fs-allocation.sh ntfs3 ntfs ntfs3            # Linux's ntfs3 driver
#   fs-allocation.sh ntfs3-noprealloc ntfs ntfs3 prealloc=0
#   fs-allocation.sh ntfs ntfs ntfs              # the newer in-kernel ntfs driver
#   fs-allocation.sh exfat exfat; fs-allocation.sh vfat vfat; fs-allocation.sh ext4 ext4
#   fs-allocation.sh btrfs btrfs
#
#   MKFS_KIND  ntfs | exfat | vfat | ext4 | btrfs (needs mkfs.ntfs, mkfs.exfat, mkfs.vfat…)
#   QBT_URL    the qBittorrent (default the playground, http://127.0.0.1:8081/api/v2)
#   FIXTURE    the swarm fixture (default "Open Movie Night (2026)": one big file, small ones)
#   WANT       regex of the files case A wants (default: the README and subtitles)
#   IMGSIZE    the image's size (default 3G); WORK: where it goes (default a temp folder)
#
# The fixture is removed from the qBittorrent at the end (its files stay in the swarm), and the
# image is unmounted, detached and deleted. The type omaseed would see is QStorageInfo's, which
# is the mount's type as findmnt prints it (fstype.cpp beside this prints QStorageInfo's own).
set -uo pipefail

[ $# -ge 2 ] || { sed -n '2,25p' "$0"; exit 2; }
LABEL="$1" KIND="$2" MOUNT_TYPE="${3:-}" MOUNT_OPTIONS="${4:-}"
P="${QBT_URL:-http://127.0.0.1:8081/api/v2}"
case "$P" in *:8080/* | *:8080) echo "fs-allocation.sh: :8080 is the real library's port; refusing" >&2; exit 1 ;; esac
FIXTURE="${FIXTURE:-Open Movie Night (2026)}"
WANT="${WANT:-README|\.srt$|[Ss]ubs?/}"
TOR="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents/$FIXTURE.torrent"
[ -f "$TOR" ] || { echo "fs-allocation.sh: no $TOR (scripts/dev/swarm.sh start)" >&2; exit 1; }
WORK="${WORK:-$(mktemp -d "${TMPDIR:-/tmp}/fs-allocation-XXXX")}"
IMG="$WORK/$LABEL.img"

truncate -s "${IMGSIZE:-3G}" "$IMG"
case "$KIND" in
ntfs) mkfs.ntfs -Q -F -L t116 "$IMG" >/dev/null ;;
exfat) mkfs.exfat -L t116 "$IMG" >/dev/null ;;
vfat) mkfs.vfat -F 32 -n T116 "$IMG" >/dev/null ;;
ext4) mkfs.ext4 -q -F -L t116 -E root_owner="$(id -u):$(id -g)" "$IMG" ;;
btrfs) mkdir -p "$WORK/root" && mkfs.btrfs -q -f -L t116 --rootdir "$WORK/root" "$IMG" >/dev/null ;;
*) echo "fs-allocation.sh: unknown kind $KIND" >&2; exit 2 ;;
esac
DEV="$(udisksctl loop-setup -f "$IMG" --no-user-interaction | grep -o '/dev/loop[0-9]*')"
sleep 2
# udisks may automount it with its own type; mount it again the way asked.
findmnt -n "$DEV" >/dev/null && udisksctl unmount -b "$DEV" --no-user-interaction >/dev/null
args=(-b "$DEV" --no-user-interaction)
[ -n "$MOUNT_TYPE" ] && args+=(-t "$MOUNT_TYPE")
[ -n "$MOUNT_OPTIONS" ] && args+=(-o "$MOUNT_OPTIONS")
cleanup() {
    udisksctl unmount -b "$DEV" --no-user-interaction >/dev/null 2>&1
    udisksctl loop-delete -b "$DEV" --no-user-interaction >/dev/null 2>&1
    rm -f "$IMG"
}
trap cleanup EXIT
udisksctl mount "${args[@]}" >/dev/null || { echo "$LABEL: mount failed"; exit 1; }
M="$(findmnt -no TARGET "$DEV")"
echo "== $LABEL: $(findmnt -no FSTYPE,OPTIONS "$DEV")"

measure() { # measure DIR TAG: the files' apparent and allocated sizes; .parts counted apart
    find "$1" -type f -printf '%s %b %f\n' | awk -v tag="$2" '
      { if (substr($3, 1, 1) == ".") { ps += $1; pb += $2 * 512; next } n++; a += $1; b += $2 * 512 }
      END { printf "  %-8s files %3d  apparent %7.1f MB  allocated %7.1f MB  (.parts %.1f/%.1f MB)", tag, n, a / 1e6, b / 1e6, ps / 1e6, pb / 1e6 }'
    echo "  df used $(df -B1 --output=used "$1" | tail -1 | awk '{ printf "%.1f", $1 / 1e6 }') MB"
}
hash_of() {
    curl -s "$P/torrents/info" | python3 -I -c '
import json, sys
print(next((t["hash"] for t in json.load(sys.stdin) if t["name"] == sys.argv[1]), ""))' "$FIXTURE"
}
info() {
    curl -s "$P/torrents/info?hashes=$H" | python3 -I -c '
import json, sys
t = json.load(sys.stdin)
t = t[0] if t else {}
print(t.get("state"), round(t.get("progress", 0), 3), t.get("size"))'
}
remove() { local h; h="$(hash_of)"; [ -z "$h" ] || curl -s -X POST "$P/torrents/delete" -d "hashes=$h&deleteFiles=false"; sleep 2; }
fresh() { # fresh FOLDER: the fixture added stopped in M/FOLDER, all but WANT skipped
    remove
    mkdir -p "$M/$1"
    curl -s -X POST "$P/torrents/add" -F "torrents=@$TOR" -F stopped=true -F "savepath=$M/$1" >/dev/null
    sleep 2
    H="$(hash_of)"
    read -r NOT ALL < <(curl -s "$P/torrents/files?hash=$H" | python3 -I -c '
import json, re, sys
files = json.load(sys.stdin)
skip = [str(f["index"]) for f in files if not re.search(sys.argv[1], f["name"])]
print("|".join(skip), "|".join(skip))' "$WANT")
    curl -s -X POST "$P/torrents/filePrio" -d "hash=$H&id=$NOT&priority=0"
}
waitup() {
    local s
    for _ in $(seq 1 30); do
        sleep 1
        s="$(info)"
        case "$s" in *UP* | error*) break ;; esac
    done
    echo "    state: $s"
}

fresh A
curl -s -X POST "$P/torrents/start" -d "hashes=$H"
waitup
measure "$M/A" A

fresh Cstop
curl -s -X POST "$P/torrents/filePrio" -d "hash=$H&id=$ALL&priority=1"
sleep 3
curl -s -X POST "$P/torrents/filePrio" -d "hash=$H&id=$ALL&priority=0"
sleep 3
measure "$M/Cstop" Cstop

fresh C
curl -s -X POST "$P/torrents/start" -d "hashes=$H"
waitup
t0="$(date +%s.%N)"
curl -s -X POST "$P/torrents/filePrio" -d "hash=$H&id=$ALL&priority=1"
for _ in 1 2 3 4 5 6; do
    sleep 0.5
    measure "$M/C" "+$(awk -v a="$(date +%s.%N)" -v b="$t0" 'BEGIN { printf "%.1f", a - b }')s"
done
curl -s -X POST "$P/torrents/filePrio" -d "hash=$H&id=$ALL&priority=0"
sleep 6
echo "    state: $(info)"
measure "$M/C" C
find "$M/C" -type f -not -name '.*' -printf '    %s %b %P\n' | sort -n | tail -3
remove
