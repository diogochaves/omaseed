#!/usr/bin/env bash
# omaseed #107: does qBittorrent's Web API tell a stopped errored torrent from a running one?
# A throwaway qBittorrent (its own profile and port) adds the swarm's unwritable fixture into a
# read-only folder, downloads from the swarm's seeder until it errors, then stops it. Prints,
# after each step, every torrents/info and torrents/properties field that changed, and which
# `filter=` values of torrents/info list it.
#
#   probe.sh [PORT]      # default 8095; needs scripts/dev/swarm.sh start (the seeder)
#
# The profile is a fresh temporary folder, removed at the end with the server.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PORT="${1:-8095}"
case "$PORT" in 8080 | 8081 | 8082 | 8083 | 8084) echo "probe.sh: $PORT is taken by another server" >&2; exit 1 ;; esac
Q="http://127.0.0.1:$PORT/api/v2"
SWARM="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}"
TOR="$SWARM/torrents/unwritable-fixture.bin.torrent"
[ -f "$TOR" ] || { echo "probe.sh: no $TOR (scripts/dev/swarm.sh start)" >&2; exit 1; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-107-XXXX")"
export OMASEED_TEST_PROFILE="$WORK/profile" OMASEED_TEST_PORT="$PORT" OMASEED_TEST_BT_PORT=26990
cleanup() {
    "$repo/scripts/dev/test-server.sh" stop >/dev/null 2>&1 || true
    chmod -R u+w "$WORK" 2>/dev/null || true
    rm -rf -- "$WORK"
}
trap cleanup EXIT
"$repo/scripts/dev/test-server.sh" start >/dev/null
RO="$WORK/readonly"
mkdir -p "$RO" && chmod 555 "$RO"
curl -fs -X POST "$Q/torrents/add" -F "torrents=@$TOR" -F "savepath=$RO" >/dev/null
H=""
for _ in $(seq 1 20); do
    H="$(curl -fs "$Q/torrents/info" | python3 -I -c 'import json,sys; t=json.load(sys.stdin); print(t[0]["hash"] if t else "")')"
    [ -n "$H" ] && break
    sleep 0.5
done
[ -n "$H" ] || { echo "probe.sh: the fixture didn't appear" >&2; exit 1; }
curl -fs -X POST "$Q/torrents/setForceStart" -d "hashes=$H" -d value=true
curl -fs -X POST "$Q/torrents/addPeers" -d "hashes=$H" -d "peers=127.0.0.1:26997"

FILTERS="all downloading seeding completed stopped paused active inactive resumed running stalled stalled_uploading stalled_downloading checking moving errored"

# snap FILE: torrents/info, torrents/properties and the filters that list it, as one object.
snap() {
    local info props filters="" f
    info="$(curl -fs "$Q/torrents/info?hashes=$H")"
    props="$(curl -fs "$Q/torrents/properties?hash=$H")"
    for f in $FILTERS; do
        case "$(curl -fs "$Q/torrents/info?filter=$f&hashes=$H")" in *"$H"*) filters+="$f " ;; esac
    done
    python3 -I -c '
import json, sys
info = json.loads(sys.argv[1]); info = info[0] if info else {}
props = json.loads(sys.argv[2])
out = {"info." + k: v for k, v in info.items()}
out.update({"props." + k: v for k, v in props.items()})
out["filters"] = sys.argv[3].split()
json.dump(out, open(sys.argv[4], "w"), sort_keys=True)' "$info" "$props" "$filters" "$1"
}
# diff_snaps A B: the fields that changed, clocks left out.
diff_snaps() {
    python3 -I -c '
import json, sys
a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
skip = {"info.last_activity", "props.time_elapsed", "info.time_active", "props.last_seen",
        "info.seen_complete", "info.reannounce", "props.reannounce"}
for k in sorted(set(a) | set(b)):
    if k not in skip and a.get(k) != b.get(k):
        print(f"   {k}: {a.get(k)!r} -> {b.get(k)!r}")' "$1" "$2"
}
filters_of() { python3 -I -c 'import json,sys; print(" ".join(json.load(open(sys.argv[1]))["filters"]))' "$1"; }
state() { curl -fs "$Q/torrents/info?hashes=$H" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)[0]["state"])'; }

for _ in $(seq 1 120); do [ "$(state)" = error ] && break; sleep 0.5; done
echo "qBittorrent $(curl -fs "$Q/app/version"), state $(state)"
sleep 3
snap "$WORK/errored.json"
echo "errored: filters $(filters_of "$WORK/errored.json")"
curl -fs -X POST "$Q/torrents/stop" -d "hashes=$H"
sleep 3
snap "$WORK/stopped.json"
echo "after torrents/stop: state $(state), filters $(filters_of "$WORK/stopped.json")"
echo "changed fields, errored -> errored and stopped:"
diff_snaps "$WORK/errored.json" "$WORK/stopped.json"
curl -fs -X POST "$Q/torrents/start" -d "hashes=$H"
sleep 3
snap "$WORK/started.json"
echo "after torrents/start: state $(state), filters $(filters_of "$WORK/started.json")"
echo "changed fields, stopped -> started:"
diff_snaps "$WORK/stopped.json" "$WORK/started.json"
