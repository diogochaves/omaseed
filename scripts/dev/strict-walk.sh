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
# Credentials come from ~/.config/omaseed/dev.env and are never printed. The password and the
# API key reach the box through its clipboard (stdin, then ctrl+v in the field), and the checks
# that they were not written to disk read them from the environment, so neither is ever on a
# command line (where /proc/PID/cmdline would show it to every user). The box's network
# reaches :8082 alone, and its keyring and HOME are thrown away with it. The server locks 127.0.0.1 out for an hour after 5 failed logins, so the walk makes one
# wrong cookie login and no more. Needs `scripts/dev/test-server.sh start --strict` (started
# here if it is not running); never `reset --strict`, the API key lives in that profile.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
STRICT_PORT="${OMASEED_STRICT_PORT:-8082}"
STRICT="http://127.0.0.1:$STRICT_PORT"
refuse_home "$STRICT"
BOX=strict-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-strict-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }
dev_env="${OMASEED_DEV_ENV:-$HOME/.config/omaseed/dev.env}"
if [ -f "$dev_env" ]; then
    set -a
    # Outside the repo on purpose (it holds the strict credentials); nothing to follow.
    # shellcheck source=/dev/null
    . "$dev_env"
    set +a
fi
for v in QBT_STRICT_USER QBT_STRICT_PASS QBT_STRICT_API_KEY; do
    [ -n "${!v:-}" ] || {
        echo "$v is not set in the environment or $dev_env;" \
            "scripts/dev/test-server.sh start --strict makes it" >&2
        exit 1
    }
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

# window_is floating|tiled [WxH]: the app's window, as the box's Hyprland has it.
window_is() {
    omabox hyprctl -b "$BOX" -j clients | python3 -c '
import json, sys
want, size = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
for c in json.load(sys.stdin):
    if c["class"] == "io.github.diogochaves.omaseed":
        ok = c["floating"] == (want == "floating")
        if size:
            ok = ok and "%dx%d" % tuple(c["size"]) == size
        sys.exit(0 if ok else 1)
sys.exit(1)' "$@"
}

# keys ARGS…: `omabox keys` into the app, quietly.
keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

# clip VAR: put the value of the environment variable VAR on the box's clipboard. It goes on
# stdin (printf is a builtin), so it is on no command line; ctrl+v then types it. Done before
# a launch, while the app is not up, as the API key always was.
clip() {
    printf '%s' "${!1}" | omabox run -b "$BOX" -- wl-copy
}

# servers_json_is FILE MODE SECRET_VAR: FILE's one server has auth mode MODE (and, for cookie,
# QBT_STRICT_USER as its user), and FILE does not contain the value of SECRET_VAR. Python reads
# the credentials from its environment (dev.env is exported), never from its arguments.
servers_json_is() {
    python3 -c '
import json, os, sys
path, mode, secret = sys.argv[1], sys.argv[2], os.environ[sys.argv[3]]
text = open(path, encoding="utf-8").read()
server = json.loads(text)["servers"][0]
ok = server.get("authMode") == mode and secret not in text
if mode == "cookie":
    ok = ok and server.get("username") == os.environ["QBT_STRICT_USER"]
sys.exit(0 if ok else 1)
' "$@"
}

STEP=0
CHECKS=""
FAILED=0
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
    if "$@"; then mark="ok"; else FAILED=$((FAILED + 1)); fi
    CHECKS+="- $mark: $what"$'\n'
    echo "  check $mark: $what"
}

cleanup() {
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# `status` exits 1 when the server is down; `start` is a no-op when it is up.
scripts/dev/test-server.sh status --strict >/dev/null 2>&1 || scripts/dev/test-server.sh start --strict >/dev/null
# The playground's three stopped fixtures, so the table has rows. qBittorrent answers 409
# for one it already has, which is fine.
for m in "magnet:?xt=urn:btih:4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d&dn=fixture-one" \
    "magnet:?xt=urn:btih:b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0&dn=fixture-two" \
    "magnet:?xt=urn:btih:c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00&dn=fixture-three"; do
    bearer torrents/add -X POST -F "urls=$m" -F "stopped=true" -F "category=fixtures" >/dev/null ||
        true
done
[ "$(bearer torrents/info?category=fixtures | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')" = 3 ] ||
    { echo "the strict server does not have the three fixtures (is the API key right?)" >&2; exit 1; }

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow "$STRICT_PORT" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Strict walk %s\n\nBuild `%s`, against %s. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" "$STRICT" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow "$STRICT_PORT" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
config="$home/.config/omaseed"

# First run opens on the welcome (enter sets up). In step 1 the rows are [this machine?],
# remote, install: j to the bottom and one k up lands on "remote" whether or not a local
# server was found. In step 2 the tab order is URL, name, the three auth chips, the chosen
# mode's field(s), then test, open web UI, save anyway (after a failed test), continue.
REMOTE="Return -s 500 j j j k Return"

# ---- cookie ---------------------------------------------------------------------------------
launch
check "first run floats, centred, at Omarchy's 875x600" window_is floating 875x600
step "Welcome" "docs/14 screen 0, a floating 875x600 window, no top bar: the mark with omaseed beside it, the pitch, the server search's answer (the box cannot reach :8080, so most likely nothing found), set up / quit, the version in the footer."

keys Return -s 800
step "Step 1: server" "Centred column: the mark, 1 server · 2 sign in · 3 links, the question, the rows (remote, install; this machine only if found). Footer: step 1 of 3, j k choose, enter continue, esc quit."
keys Escape -s 1500
# shellcheck disable=SC2016 # expands in the inner sh, which gets the values as $1, $2
check "esc on step 1 quits, as the footer says" \
    sh -c '! omabox hyprctl -b "$1" -j clients | grep -q "io.github.diogochaves.omaseed"' _ "$BOX"
clip QBT_STRICT_PASS
launch

# URL, name, the cookie chip (API key, cookie, none are three tab stops), user, password, test.
# shellcheck disable=SC2086 # $REMOTE is a list of keys: split on purpose
keys $REMOTE -s 500 -t "$STRICT" Tab -t strict Tab Tab space Tab Tab -t "$QBT_STRICT_USER" \
    Tab -t "not-the-password" Tab Return -s 3000
step "Wrong password" "✓ server · 2 sign in. URL and name on one row; cookie shows Username and Password. A red dot and wrong username or password, the raw 401 dim underneath, then test / open web UI / save anyway / continue (off)."

keys shift+Tab ctrl+a ctrl+v Tab Return -s 3000
omabox run -b "$BOX" -- wl-copy --clear >/dev/null 2>&1 || true
step "Cookie test passes" "A green dot: signed in · qBittorrent 5.2.3 · API 2.15.1 · 3 torrents, above the buttons; save anyway gone, continue on."

keys Tab Tab Return -s 1500
step "Magnet handler" "✓ server ✓ sign in 3 links: Open magnet links and .torrent files with omaseed?, the dim line, open them with omaseed / not now; the script answers not now."

keys Tab Return -s 2000
step "Done" "✓ ✓ ✓. You're connected to strict. qBittorrent 5.2.3 · 3 torrents (· free space once synced); magnet links still open elsewhere; a / , / ? hints; open the library. Footer: setup done."

keys Return -s 2500
check "after setup the window tiles like any other launch" window_is tiled
step "Signed in with a cookie" "The library, tiled full size, with its top bar back: fixture-one, -two, -three (stopped); the hero says STRICT · QBITTORRENT 5.2.3; no banner."

check "servers.json keeps the user but not the password" \
    servers_json_is "$config/servers.json" cookie QBT_STRICT_PASS
check "no plain-file secret (the keyring took it)" test ! -e "$config/secrets"
# shellcheck disable=SC2016 # expands in the inner sh, which gets the values as $1, $2
check "the box's keyring holds <server id>/password" \
    sh -c 'omabox run -b "$1" -- secret-tool search --all server io.github.diogochaves.omaseed 2>&1 | grep -q "^attribute.user = .*/password$"' _ "$BOX"

launch
check "a later launch tiles straight away" window_is tiled
step "Cookie after a restart" "Straight to the table, no first run and no banner: the password came back from the keyring."

# ---- API key --------------------------------------------------------------------------------
# A fresh first run: the server list goes, the keyring entry can stay (it is keyed by server id).
omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
rm -f "$config/servers.json"
clip QBT_STRICT_API_KEY
launch
# URL, name, the API key chip, then three tabs (cookie, none, Key) and a wrong key first.
# shellcheck disable=SC2086 # $REMOTE is a list of keys: split on purpose
keys $REMOTE -s 500 -t "$STRICT" Tab -t strict-key Tab space Tab Tab Tab -t "not-a-real-key" \
    Tab Return -s 3000
step "Wrong API key" "API key chosen: one Key field (no Username / Password). Red: the server refused this API key, the raw 403 dim underneath; continue stays off."

keys shift+Tab ctrl+a ctrl+v Tab Return -s 3000
step "API key test passes" "The pasted key (dots) tests fine: qBittorrent 5.2.3 · API 2.15.1 · 3 torrents; continue is on."

keys Tab Tab Return -s 1500 Tab Return -s 1500 Return -s 3000
omabox run -b "$BOX" -- wl-copy --clear >/dev/null 2>&1 || true
step "Signed in with an API key" "The same three fixtures; the hero says STRICT-KEY · QBITTORRENT 5.2.3."

check "servers.json has the API key mode and not the key" \
    servers_json_is "$config/servers.json" apiKey QBT_STRICT_API_KEY
# shellcheck disable=SC2016 # expands in the inner sh, which gets the values as $1, $2
check "the box's keyring holds <server id>/apiKey" \
    sh -c 'omabox run -b "$1" -- secret-tool search --all server io.github.diogochaves.omaseed 2>&1 | grep -q "^attribute.user = .*/apiKey$"' _ "$BOX"

launch
step "API key after a restart" "Straight to the table again, signed in with the key from the keyring."

# ---- a bigger font --------------------------------------------------------------------------
# Setup's window grows with the shell's base-size (875 x 600 at 14), so at 18 the tallest step
# still fits above the footer. Auth "none" against the strict server fails its test without a
# login attempt, so the lockout counter is not touched.
omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
rm -f "$config/servers.json"
sed -i 's/^base-size = .*/base-size = 18/' "$home/.config/omarchy/shell.toml"
launch
check "at base-size 18 setup floats at 1125x771" window_is floating 1125x771
# shellcheck disable=SC2086 # $REMOTE is a list of keys: split on purpose
keys $REMOTE -s 500 -t "$STRICT" Tab -t strict Tab Tab Tab Tab Return -s 3000
step "Base size 18" "The same step 2 at font size 18 in a 1125x771 window: none chosen, the red 403 line, the buttons, and the footer clear of them (nothing overlaps)."

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
# A failed check fails the walk, so a caller need not read index.md to know.
if [ "$FAILED" -gt 0 ]; then
    echo "$FAILED check(s) failed" >&2
    exit 1
fi
