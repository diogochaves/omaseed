#!/usr/bin/env bash
# omaseed — the strict-auth walkthrough: first run against the :8082 twin, which asks for a
# login like a real server (the :8081 playground lets localhost in without one). Cookie login
# and an API key, each typed into first run, tested, saved, and read back after a restart.
#
#   scripts/dev/strict-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                               # OUT_DIR/index.md says what to look for in each
#   scripts/dev/strict-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                               # script still does the typing, then says what to
#                                               # check and waits for enter
#
# Credentials come from ~/.config/omaseed/dev.env and are never printed. The API key reaches
# the box through its clipboard (stdin, then ctrl+v in the Key field), so it is on no command
# line. The box's network reaches :8082 alone, and its keyring and HOME are thrown away with
# it. The server locks 127.0.0.1 out for an hour after 5 failed logins, so the walk makes one
# wrong cookie login and no more. Needs `scripts/dev/test-server.sh start --strict` (started
# here if it is not running); never `reset --strict`, the API key lives in that profile.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
STRICT="http://127.0.0.1:8082"
BOX=strict-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-${TMPDIR:-/tmp}/omaseed-strict-walk-$(date +%Y%m%d-%H%M%S)}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }
set -a
# shellcheck disable=SC1090
. "$HOME/.config/omaseed/dev.env"
set +a
for v in QBT_STRICT_USER QBT_STRICT_PASS QBT_STRICT_API_KEY; do
    [ -n "${!v:-}" ] || { echo "$v is not set in ~/.config/omaseed/dev.env" >&2; exit 1; }
done

# bearer PATH [curl args…]: a request to the strict server with the API key. The key goes to
# curl on stdin (a config line), not on its command line.
bearer() {
    local path="$1"; shift
    printf 'header = "Authorization: Bearer %s"\n' "$QBT_STRICT_API_KEY" |
        curl -fs -K - "$@" "$STRICT/api/v2/$path"
}

# launch: (re)start the app in the box and wait for its window.
launch() {
    omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
    sleep 0.5
    omabox run -b "$BOX" -d -- ./build/omaseed >/dev/null
    for _ in $(seq 1 40); do
        omabox hyprctl -b "$BOX" -j clients | grep -q '"class": "io.github.diogochaves.omaseed"' && break
        sleep 0.25
    done
    sleep 3
}

# keys ARGS…: `omabox keys` into the app, quietly.
keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

STEP=0
CHECKS=""
# step "title" "what to look for": a screenshot (headless) or a pause (interactive).
step() {
    STEP=$((STEP + 1))
    local file
    file="$(printf '%02d' "$STEP")-$(echo "$1" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-').png"
    if [ "$INTERACTIVE" = 1 ]; then
        printf '\n\033[1m%02d %s\033[0m\n    %s\n    enter for the next step ' "$STEP" "$1" "$2"
        read -r _
    else
        omabox shot -b "$BOX" -o "$OUT/$file" >/dev/null
        printf -- '- **%02d %s**: %s\n  ![](%s)\n' "$STEP" "$1" "$2" "$file" >>"$OUT/index.md"
        echo "$(printf '%02d' "$STEP") $1 -> $OUT/$file"
    fi
}

# check "what" CONDITION…: a pass/fail line for what a screenshot cannot show.
check() {
    local what="$1"; shift
    local mark="FAIL"
    if "$@"; then mark="ok"; fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}

cleanup() {
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

scripts/dev/test-server.sh status --strict >/dev/null 2>&1 || scripts/dev/test-server.sh start --strict >/dev/null
# The playground's three stopped fixtures, so the table has rows. Adding one twice is a no-op.
for m in "magnet:?xt=urn:btih:4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d&dn=fixture-one" \
    "magnet:?xt=urn:btih:b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0&dn=fixture-two" \
    "magnet:?xt=urn:btih:c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00&dn=fixture-three"; do
    bearer torrents/add -X POST -F "urls=$m" -F "stopped=true" -F "category=fixtures" >/dev/null
done

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow 8082 >/dev/null
else
    mkdir -p "$OUT"
    printf '# Strict walk %s\n\nBuild `%s`, against %s. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" "$STRICT" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow 8082 >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
config="$home/.config/omaseed"

# In first run's choose step the rows are [this machine?], remote, install: j to the bottom
# and one k up lands on "remote" whether or not a local server was found.
REMOTE="j j j k Return"

# ---- cookie ---------------------------------------------------------------------------------
launch
step "First run" "The choose step: connect to a remote server, install qbittorrent-nox, and a row for this machine only if it found one. The box cannot reach :8080."

# URL, name, the cookie chip (API key, cookie, none are three tab stops), user, password, test.
# shellcheck disable=SC2086
keys $REMOTE -s 500 -t "$STRICT" Tab -t strict Tab Tab space Tab Tab -t "$QBT_STRICT_USER" \
    Tab -t "not-the-password" Tab Tab Return -s 3000
step "Wrong password" "Cookie chosen: Username and Password fields (no Key field). Red: wrong username or password, the raw 401 dim underneath; continue stays off."

keys shift+Tab shift+Tab ctrl+a -t "$QBT_STRICT_PASS" Tab Tab Return -s 3000
step "Cookie test passes" "qBittorrent 5.2.3 · API 2.15.1 · 3 torrents under the buttons; continue is on."

keys Tab Tab Return -s 1500
step "Magnet handler" "Step 3 of 3: make omaseed the magnet and .torrent handler; the script answers not now."

keys Tab Return -s 3000
step "Signed in with a cookie" "The table: fixture-one, -two, -three (stopped); the hero says STRICT · QBITTORRENT 5.2.3; no banner."

check "servers.json keeps the user but not the password" \
    sh -c 'grep -q "\"authMode\": *\"cookie\"" "$1" && grep -q "\"username\": *\"$2\"" "$1" && ! grep -qF "$3" "$1"' \
    _ "$config/servers.json" "$QBT_STRICT_USER" "$QBT_STRICT_PASS"
check "no plain-file secret (the keyring took it)" test ! -e "$config/secrets"
check "the box's keyring holds <server id>/password" \
    sh -c 'omabox run -b "$1" -- secret-tool search --all server io.github.diogochaves.omaseed 2>&1 | grep -q "^attribute.user = .*/password$"' _ "$BOX"

launch
step "Cookie after a restart" "Straight to the table, no first run and no banner: the password came back from the keyring."

# ---- API key --------------------------------------------------------------------------------
# A fresh first run: the server list goes, the keyring entry can stay (it is keyed by server id).
omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
rm -f "$config/servers.json"
printf '%s' "$QBT_STRICT_API_KEY" | omabox run -b "$BOX" -- wl-copy
launch
# URL, name, the API key chip, then three tabs (cookie, none, Key) and a wrong key first.
# shellcheck disable=SC2086
keys $REMOTE -s 500 -t "$STRICT" Tab -t strict-key Tab space Tab Tab Tab -t "not-a-real-key" \
    Tab Tab Return -s 3000
step "Wrong API key" "API key chosen: one Key field (no Username / Password). Red: the server refused this API key, the raw 403 dim underneath; continue stays off."

keys shift+Tab shift+Tab ctrl+a ctrl+v Tab Tab Return -s 3000
step "API key test passes" "The pasted key (dots) tests fine: qBittorrent 5.2.3 · API 2.15.1 · 3 torrents; continue is on."

keys Tab Tab Return -s 1500 Tab Return -s 3000
omabox run -b "$BOX" -- wl-copy --clear >/dev/null 2>&1 || true
step "Signed in with an API key" "The same three fixtures; the hero says STRICT-KEY · QBITTORRENT 5.2.3."

check "servers.json has the API key mode and not the key" \
    sh -c 'grep -q "\"authMode\": *\"apiKey\"" "$1" && ! grep -qF "$2" "$1"' \
    _ "$config/servers.json" "$QBT_STRICT_API_KEY"
check "the box's keyring holds <server id>/apiKey" \
    sh -c 'omabox run -b "$1" -- secret-tool search --all server io.github.diogochaves.omaseed 2>&1 | grep -q "^attribute.user = .*/apiKey$"' _ "$BOX"

launch
step "API key after a restart" "Straight to the table again, signed in with the key from the keyring."

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
