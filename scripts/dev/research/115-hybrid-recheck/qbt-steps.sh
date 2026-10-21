#!/usr/bin/env bash
# omaseed #115 through a real qBittorrent: add a swarm fixture, skip all but some files, delete
# the skipped ones on disk, then stop+start and/or recheck, printing the state after each step.
# On a hybrid fixture (the Nested Library Sampler is one) a recheck after deleting files whose
# pieces were downloaded ends in `error`.
#
#   qbt-steps.sh SAVE_DIR KEEP_REGEX [recheck|stopstart|stopstart-recheck]
#
#   QBT_URL   the qBittorrent's Web API (default the playground, http://127.0.0.1:8081/api/v2;
#             a throwaway profile on another port is better: `test-server.sh` can make one)
#   FIXTURE   the swarm fixture's name (default "Nested Library Sampler (2026)")
#   NOCOPY=1  don't copy the fixture's content in first: let it download from the swarm's
#             seeder (scripts/dev/swarm.sh start), which is what triggers the bug
#   PEER      host:port to add as a peer (the seeder, 127.0.0.1:26997) when NOCOPY is set
#
# SAVE_DIR is wiped first. Never point QBT_URL at a real library: it deletes torrents.
set -euo pipefail

[ $# -ge 2 ] || { sed -n '2,17p' "$0"; exit 2; }
SAVE="$1" KEEP="$2" MODE="${3:-recheck}"
Q="${QBT_URL:-http://127.0.0.1:8081/api/v2}"
case "$Q" in *:8080/* | *:8080) echo "qbt-steps.sh: :8080 is the real library's port; refusing" >&2; exit 1 ;; esac
FIXTURE="${FIXTURE:-Nested Library Sampler (2026)}"
SWARM="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}"
TOR="$SWARM/torrents/$FIXTURE.torrent"
[ -f "$TOR" ] || { echo "qbt-steps.sh: no $TOR (scripts/dev/swarm.sh start)" >&2; exit 1; }

hash_of() {
    curl -fs "$Q/torrents/info" | python3 -I -c '
import json, sys
print(next((t["hash"] for t in json.load(sys.stdin) if t["name"] == sys.argv[1]), ""))' "$FIXTURE"
}
info() {
    curl -fs "$Q/torrents/info?hashes=$H" | python3 -I -c '
import json, sys
t = json.load(sys.stdin)
t = t[0] if t else {}
print("  ", t.get("state"), round(t.get("progress", 0), 3), "size", t.get("size"),
      "done", t.get("completed"))'
}

H="$(hash_of)"
[ -z "$H" ] || { curl -fs -X POST "$Q/torrents/delete" -d "hashes=$H&deleteFiles=false"; sleep 2; }
rm -rf "$SAVE" && mkdir -p "$SAVE"
[ -n "${NOCOPY:-}" ] || cp -a "$SWARM/content/$FIXTURE" "$SAVE/"
curl -fs -X POST "$Q/torrents/add" -F "torrents=@$TOR" -F "savepath=$SAVE" >/dev/null
for _ in $(seq 1 20); do
    H="$(hash_of)"
    [ -n "$H" ] && break
    sleep 0.5
done
[ -n "$H" ] || { echo "qbt-steps.sh: the fixture didn't appear" >&2; exit 1; }
[ -z "${PEER:-}" ] || curl -fs -X POST "$Q/torrents/addPeers" -d "hashes=$H&peers=$PEER"
for _ in $(seq 1 90); do
    case "$(curl -fs "$Q/torrents/info?hashes=$H")" in *stalledUP* | *uploading*) break ;; esac
    sleep 1
done
echo "added:"
info

not="$(curl -fs "$Q/torrents/files?hash=$H" | python3 -I -c '
import json, re, sys
print("|".join(str(f["index"]) for f in json.load(sys.stdin) if not re.search(sys.argv[1], f["name"])))' "$KEEP")"
curl -fs -X POST "$Q/torrents/filePrio" -d "hash=$H&id=$not&priority=0"
sleep 2
echo "skipped:"
info

python3 -I - "$SAVE" "$KEEP" <<'PY'
import os, re, sys
root, keep = sys.argv[1], sys.argv[2]
n = 0
for d, _, files in os.walk(root):
    for f in files:
        p = os.path.join(d, f)
        rel = os.path.relpath(p, root)
        if rel.startswith(".") or re.search(keep, rel):
            continue
        os.remove(p)
        n += 1
print("   deleted", n, "skipped files")
PY
sleep 2
echo "after delete:"
info

case "$MODE" in stopstart*)
    curl -fs -X POST "$Q/torrents/stop" -d "hashes=$H"
    sleep 2
    echo "stopped:"
    info
    curl -fs -X POST "$Q/torrents/start" -d "hashes=$H"
    sleep 4
    echo "started:"
    info
    ;;
esac
case "$MODE" in *recheck)
    curl -fs -X POST "$Q/torrents/recheck" -d "hashes=$H"
    for i in 1 2 3 4; do
        sleep 3
        echo "recheck +$((i * 3))s:"
        info
    done
    ;;
esac
