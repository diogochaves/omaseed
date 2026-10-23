#!/usr/bin/env bash
# omaseed #151: does a file priority set right after a stopped .torrent add stay set?
# Adds tests/fixtures/open_movies_v1.torrent stopped, as App::addTorrent does for a .torrent
# whose files were chosen in the add sheet (renames, then one torrents/filePrio per priority,
# then torrents/start), and prints the file priorities after each step and for a few seconds
# after. Each variant leaves out a part of that sequence, to see which part matters.
#
#   probe.sh [URL] [VARIANT...]   # URL default http://127.0.0.1:8081 (the playground)
#
# VARIANT: full (the app's sequence), norename, noflp (no firstLastPiecePrio), nostart,
# wait (the same as full, but filePrio only once torrents/info has left checkingResumeData).
# Run it under the playground lock (docs/TESTING.md). The fixture is deleted at the end.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
URL="${1:-http://127.0.0.1:8081}"
shift || true
case "$URL" in *:8080*) echo "probe.sh: never :8080 (a real library)" >&2; exit 1 ;; esac
Q="$URL/api/v2"
TOR="$repo/tests/fixtures/open_movies_v1.torrent"
H=429d28a2cdae51f937735430e0a88cec3ee76cbc
VARIANTS=("$@")
[ "${#VARIANTS[@]}" -gt 0 ] || VARIANTS=(full norename noflp nostart wait)

del() { curl -fs -X POST "$Q/torrents/delete" -d "hashes=$H" -d deleteFiles=true >/dev/null || true; }
trap del EXIT

# prios LABEL: the state and every file's priority, by index.
prios() {
    local files info
    files="$(curl -fs "$Q/torrents/files?hash=$H" || echo '[]')"
    info="$(curl -fs "$Q/torrents/info?hashes=$H" || echo '[]')"
    python3 -I -c '
import json, sys
files = json.loads(sys.argv[1]); info = json.loads(sys.argv[2])
state = info[0]["state"] if info else "-"
p = [f["priority"] for f in sorted(files, key=lambda f: f["index"])]
print(f"  {sys.argv[3]:<22} {state:<20} {p}")' "$files" "$info" "$1"
}

# post PATH ARGS...: one POST, printing its status code.
post() {
    local path="$1"
    shift
    local code
    code="$(curl -s -o /dev/null -w '%{http_code}' -X POST "$Q/$path" "$@")"
    printf '  %-22s %s\n' "$path" "$code"
}

run() {
    local variant="$1" root t
    echo "== $variant"
    del
    for _ in $(seq 1 20); do
        [ "$(curl -fs "$Q/torrents/info?hashes=$H")" = "[]" ] && break
        sleep 0.25
    done
    local flp=(-F firstLastPiecePrio=true)
    [ "$variant" = noflp ] && flp=()
    post torrents/add -F "torrents=@$TOR" -F stopped=true -F paused=true -F "rename=omaseed-151" \
        -F dlLimit=307200 "${flp[@]}"
    # The app's retry: 404/409 until the server knows the torrent.
    for _ in $(seq 1 20); do
        [ "$(curl -fs "$Q/torrents/files?hash=$H" 2>/dev/null || echo '[]')" != "[]" ] && break
        sleep 0.25
    done
    prios added
    if [ "$variant" = wait ]; then
        for _ in $(seq 1 40); do
            case "$(curl -fs "$Q/torrents/info?hashes=$H")" in
            *checkingResumeData*) sleep 0.25 ;;
            *) break ;;
            esac
        done
        prios loaded
    fi
    if [ "$variant" != norename ]; then
        root="$(curl -fs "$Q/torrents/files?hash=$H" |
            python3 -I -c 'import json,sys; print(json.load(sys.stdin)[0]["name"].split("/")[0])')"
        post torrents/renameFolder -d "hash=$H" -d "oldPath=$root/Tears of Steel" \
            -d "newPath=$root/Tears (2012)"
        post torrents/renameFile -d "hash=$H" -d "oldPath=$root/README.txt" \
            -d "newPath=$root/read me.txt"
    fi
    post torrents/filePrio -d "hash=$H" -d "id=3|4" -d priority=0
    post torrents/filePrio -d "hash=$H" -d "id=7" -d priority=7
    prios "after filePrio"
    [ "$variant" = nostart ] || post torrents/start -d "hashes=$H"
    prios "at once"
    for t in 0.5 1 2 4; do
        sleep "$t"
        prios "+${t}s"
    done
}

echo "qBittorrent $(curl -fs "$Q/app/version"), Web API $(curl -fs "$Q/app/webapiVersion")"
for v in "${VARIANTS[@]}"; do run "$v"; done
