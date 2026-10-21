#!/usr/bin/env bash
# omaseed — the move test library, in a box's qBittorrent: what a person needs to try moving
# (boards 42 and 43, DECISIONS C15 and C16) by hand, the same every time.
#
#   scripts/dev/move-library.sh                       # the manual box's "public" qBittorrent
#   scripts/dev/move-library.sh -b BOX [--server public|private]
#
# Run it after scripts/dev/manual-box.sh (or app-box.sh up --manual / --qbt). It makes, in the
# box, under ~/library (the box's home is btrfs):
# - games: Arcade Set 01-35, and games/handheld: Handheld Pack 01-10; films: Short Film 01-60.
#   105 torrents, each a folder with one small file, added as complete.
# - games: Big Collection 1-4, six files each, only the first two wanted and on disk: partial
#   packs, 100 % of what they want.
# - files no torrent owns: games/manual.pdf, games/notes/readme.txt, films/poster.jpg.
# And the destinations:
# - /tmp/fast: the box's tmpfs, another filesystem, so a move there copies real bytes.
# - /tmp/fast/clash: already holds a different "Arcade Set 04".
# - /tmp/locked/dest: read-only, so qBittorrent can't write there. On the tmpfs, so taking the
#   box down leaves nothing behind (a read-only folder in the box's home would: omabox#264).
#
# Torrents and files are generated (private, no tracker): nothing downloads, nothing leaves the
# box. Running it again on the same box skips what is already there.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_MANUAL_BOX:-manual-box}"
SERVER=public
while [ $# -gt 0 ]; do
    case "$1" in
    -b) BOX="$2"; shift 2 ;;
    --server) SERVER="$2"; shift 2 ;;
    -h | --help) sed -n '2,24p' "$0"; exit 0 ;;
    *) echo "move-library.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

BOX_HOME=/home/sbx
LIB="$BOX_HOME/library"
home="$(omabox path -b "$BOX")/home"
[ -d "$home" ] || { echo "move-library.sh: no box $BOX (scripts/dev/manual-box.sh first)" >&2; exit 1; }

api() { "$APP_BOX" api -b "$BOX" --server "$SERVER" "$@"; }
in_box() { omabox run -b "$BOX" -- "$@" >/dev/null; }

echo "making the library in $BOX:$LIB"
python3 -I - "$home" <<'PY'
import hashlib, os, sys

home = sys.argv[1]
PIECE = 16384

def bencode(x):
    if isinstance(x, int):
        return b"i%de" % x
    if isinstance(x, bytes):
        return b"%d:%s" % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode())
    if isinstance(x, list):
        return b"l" + b"".join(map(bencode, x)) + b"e"
    return b"d" + b"".join(bencode(k) + bencode(x[k]) for k in sorted(x)) + b"e"

def blob(seed, size):
    return (hashlib.sha256(seed.encode()).digest() * (size // 32 + 1))[:size]

def write_torrent(group, name, files, on_disk):
    """files: [(relative path, bytes)]; on_disk: how many of them to write."""
    folder = os.path.join(home, "library", group, name)
    for path, data in files[:on_disk]:
        full = os.path.join(folder, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)
    stream = b"".join(data for _, data in files)
    pieces = b"".join(hashlib.sha1(stream[i:i + PIECE]).digest() for i in range(0, len(stream), PIECE))
    info = {"name": name, "piece length": PIECE, "pieces": pieces, "private": 1,
            "files": [{"length": len(d), "path": p.split("/")} for p, d in files]}
    out = os.path.join(home, "move-library", group.replace("/", "-"))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".torrent"), "wb") as f:
        f.write(bencode({"info": info, "created by": "omaseed move-library.sh"}))

def single(group, name, size, seed):
    write_torrent(group, name, [("data.bin", blob(seed, size))], 1)

for i in range(1, 36):
    single("games", "Arcade Set %02d" % i, 8192 + 997 * i, "g%d" % i)
for i in range(1, 11):
    single("games/handheld", "Handheld Pack %02d" % i, 8192 + 991 * i, "h%d" % i)
for i in range(1, 61):
    single("films", "Short Film %02d" % i, 8192 + 983 * i, "f%d" % i)
# Partial packs: every file a whole number of pieces, so the two wanted ones verify alone.
for i in range(1, 5):
    files = [("Disc %d/part %d.bin" % (i, n), blob("p%d-%d" % (i, n), PIECE * (2 + n))) for n in range(6)]
    write_torrent("packs", "Big Collection %d" % i, files, 2)

def loose(path, size):
    full = os.path.join(home, "library", path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f:
        f.write(os.urandom(size))
    t = os.path.getmtime(full) - 2 * 86400  # older than the carry's 15 minutes
    os.utime(full, (t, t))

loose("games/manual.pdf", 300 * 1024)
loose("games/notes/readme.txt", 2048)
loose("films/poster.jpg", 120 * 1024)
PY

# Names already on the server: a second run skips them (qBittorrent answers a duplicate with 409).
existing="$(api torrents/info | python3 -I -c '
import json, sys
print("\n".join(t["name"] for t in json.load(sys.stdin)))')"
add_group() { # add_group DIR SAVE_PATH [CURL ARGS]: every .torrent in DIR not there yet, 40 a request
    local dir="$1" save="$2" args=() file n=0
    shift 2
    for file in "$home/move-library/$dir"/*.torrent; do
        grep -qxF "$(basename "$file" .torrent)" <<<"$existing" && continue
        args+=(-F "torrents=@$BOX_HOME/move-library/$dir/$(basename "$file")")
        n=$((n + 1))
        if [ "$n" = 40 ]; then
            api torrents/add "${args[@]}" -F "savepath=$save" "$@" >/dev/null
            args=()
            n=0
        fi
    done
    [ ${#args[@]} = 0 ] || api torrents/add "${args[@]}" -F "savepath=$save" "$@" >/dev/null
}
echo "adding 105 complete torrents"
add_group games "$LIB/games" -F skip_checking=true
add_group games-handheld "$LIB/games/handheld" -F skip_checking=true
add_group films "$LIB/films" -F skip_checking=true

# The packs exist on disk under library/packs; save them in games, where the plan looks.
mkdir -p "$home/library/games"
for i in 1 2 3 4; do
    [ -d "$home/library/games/Big Collection $i" ] || mv "$home/library/packs/Big Collection $i" "$home/library/games/"
done
rmdir "$home/library/packs" 2>/dev/null || true
echo "adding 4 partial packs (two of six files wanted, checked)"
add_group packs "$LIB/games" -F stopped=true
# Only packs added by this run: they start at 0 %, so the wait below can tell when their check ends.
hashes="$(api torrents/info | python3 -I -c '
import json, sys
old = set(sys.argv[1].splitlines())
print("|".join(t["hash"] for t in json.load(sys.stdin)
               if t["name"].startswith("Big Collection ") and t["name"] not in old))' "$existing")"
if [ -n "$hashes" ]; then
    for hash in ${hashes//|/ }; do
        api -X POST torrents/filePrio -d "hash=$hash" -d "id=2|3|4|5" -d priority=0 >/dev/null
    done
    # Checked while stopped: started, the packs would wait behind the queue's active slots to be
    # checked at all. A pack added stopped reads 0 % until the check, so 100 % means it is over;
    # only then start it, since a start sent during a check is lost.
    api -X POST torrents/recheck -d "hashes=$hashes" >/dev/null
    for _ in $(seq 1 60); do
        left="$(api "torrents/info?hashes=$hashes" | python3 -I -c '
import json, sys
print(sum(1 for t in json.load(sys.stdin) if t["state"].startswith("checking") or t["progress"] < 1))')"
        [ "$left" = 0 ] && break
        sleep 0.5
    done
    [ "$left" = 0 ] || { echo "move-library.sh: the packs didn't reach 100 % of their two files" >&2; exit 1; }
    api -X POST torrents/start -d "hashes=$hashes" >/dev/null
fi

echo "making the destinations"
in_box mkdir -p /tmp/fast "/tmp/fast/clash/Arcade Set 04" /tmp/locked/dest
in_box sh -c 'echo "not the same data" >"/tmp/fast/clash/Arcade Set 04/data.bin"'
in_box chmod 0555 /tmp/locked/dest

complete="$(api torrents/info | python3 -I -c '
import json, sys
t = json.load(sys.stdin)
print(sum(1 for x in t if x["save_path"].rstrip("/").startswith("'"$LIB"'")))')"
echo "done: $complete torrents under $LIB on \"$SERVER\"; destinations /tmp/fast, /tmp/fast/clash, /tmp/locked/dest"
