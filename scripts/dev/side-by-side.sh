#!/usr/bin/env bash
# omaseed — main and the seed-console branch side by side (#24, docs/22): each build in its
# own interactive omabox, both on the :8081 playground, with the swarm's leecher keeping the
# playground uploading, so the two faces show the same library at the same moment.
#
#   scripts/dev/side-by-side.sh        # build both, start the leech if needed, open both boxes
#   scripts/dev/side-by-side.sh down   # close both boxes (the leech keeps running:
#                                      # scripts/dev/swarm.sh leech stop)
#
# The boxes are windows on workspace 9: "side-main" (main as it is) and "side-seed" (this
# branch, with OMASEED_LEDGER_DEMO=1, a made-up year before today so the console looks as it
# will a year in). In side-seed, ` or the hero's "seed" tab opens the console.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
main_dir="$(git -C "$here" worktree list --porcelain |
    awk '/^worktree /{w=$2} /^branch refs\/heads\/main$/{print w}')"
[ -n "$main_dir" ] || { echo "no checkout of main found (git worktree list)" >&2; exit 1; }

if [ "${1:-up}" = "down" ]; then
    (cd "$main_dir" && scripts/dev/app-box.sh down -b side-main) || true
    "$here/scripts/dev/app-box.sh" down -b side-seed || true
    exit 0
fi

export PATH="/usr/lib/qt6/bin:$PATH"
for dir in "$main_dir" "$here"; do
    echo "building $dir"
    [ -d "$dir/build" ] || cmake -S "$dir" -B "$dir/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$dir/build" >/dev/null
done

"$here/scripts/dev/swarm.sh" leech status | grep -q '^leech running' ||
    "$here/scripts/dev/swarm.sh" leech start

(cd "$main_dir" && scripts/dev/app-box.sh up -b side-main --interactive)
OMASEED_LEDGER_DEMO=1 "$here/scripts/dev/app-box.sh" up -b side-seed --interactive
echo "both are on workspace 9: side-main (main) and side-seed (this branch; \` opens the console)"
