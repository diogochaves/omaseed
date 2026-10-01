#!/usr/bin/env bash
# omaseed — the two-servers walkthrough: two qBittorrents running side by side, "public" and
# "private", as people who keep public and private trackers apart run them. First run with
# both up, the second added from Settings › Servers, a switch each way (Settings, the rail and
# the qBittorrent group's `s`), an action after each switch checked on both servers, and the
# live server removed.
#
#   scripts/dev/servers-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                                # OUT_DIR/index.md says what to look for in each
#                                                # and lists the checks
#   scripts/dev/servers-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                                # script still does the typing, then says what to
#                                                # check and waits for enter
#
# The box is a manual box (scripts/dev/app-box.sh up --manual): omaseed from build-box/ installed
# in it, "public" on the box's 127.0.0.1:8090 (the usual ~/.config/qBittorrent, given the Public
# Domain Sampler here) and "private" on :8091 (its own --profile, four of the swarm's private
# torrents). The box's network reaches only the swarm's tracker and seeder, so nothing outside
# it is touched. The logins stay in the box: each password goes from the box's login file to its
# clipboard inside the box (ctrl+v in the field), never through a command line or this terminal.
# What the server must show after each action is read from both qBittorrents (app-box.sh api).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
APP_ID=io.github.diogochaves.omaseed
BOX=servers-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-servers-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

SAMPLER="Public Domain Sampler"
TINY="tiny-notes.txt"

# keys ARGS…: `omabox keys` into the app, quietly.
keys() { omabox keys -b "$BOX" "$@" >/dev/null; }

# api SERVER PATH [CURL ARGS…]: the box's qBittorrent SERVER (public or private).
api() {
    local server="$1"; shift
    "$APP_BOX" api -b "$BOX" --server "$server" "$@"
}

# state_of SERVER NAME: the state of SERVER's torrent whose name starts with NAME, "" if none.
state_of() {
    api "$1" torrents/info | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"].startswith(sys.argv[1]):
        print(t["state"]); break' "$2"
}

# stopped SERVER NAME / running SERVER NAME: the torrent's state on that server.
stopped() { [[ "$(state_of "$1" "$2")" == stopped* ]]; }
running() { local s; s="$(state_of "$1" "$2")"; [ -n "$s" ] && [[ "$s" != stopped* ]]; }

# count SERVER: how many torrents SERVER has.
count() { api "$1" torrents/info | python3 -c 'import json, sys; print(len(json.load(sys.stdin)))'; }

# clip SERVER: SERVER's password onto the box's clipboard, from the login file the box keeps,
# all inside the box.
clip() {
    # shellcheck disable=SC2016 # expanded by the box's sh
    omabox run -b "$BOX" -- sh -c \
        'sed -n "s/^password  //p" -- "$HOME/.omaseed-box-logins/$1.txt" | tr -d "\n" | wl-copy' \
        sh "$1"
}
unclip() { omabox run -b "$BOX" -- wl-copy --clear >/dev/null 2>&1 || true; }

# servers_are NAMES DEFAULT: servers.json lists exactly NAMES (space separated, in order), each
# a cookie login as admin, DEFAULT is the default one, and neither box password is in the file.
servers_are() {
    python3 - "$home" "$1" "$2" <<'PY'
import json, os, sys
home, names, default = sys.argv[1], sys.argv[2].split(), sys.argv[3]
text = open(os.path.join(home, ".config/omaseed/servers.json"), encoding="utf-8").read()
data = json.loads(text)
servers = data["servers"]
ok = [s["name"] for s in servers] == names
ok = ok and all(s.get("authMode") == "cookie" and s.get("username") == "admin" for s in servers)
ids = {s["id"]: s["name"] for s in servers}
ok = ok and ids.get(data.get("defaultId")) == default
for server in ("public", "private"):
    for line in open(os.path.join(home, ".omaseed-box-logins", server + ".txt"), encoding="utf-8"):
        if line.startswith("password  "):
            ok = ok and line[len("password  "):].strip() not in text
sys.exit(0 if ok else 1)
PY
}

# server_id NAME: the id servers.json gives the server NAME.
server_id() {
    python3 -c '
import json, sys
for s in json.load(open(sys.argv[1], encoding="utf-8"))["servers"]:
    if s["name"] == sys.argv[2]:
        print(s["id"])' "$home/.config/omaseed/servers.json" "$1"
}

# keyring_has ID: the box's keyring holds the password of server ID. The search prints the
# secrets too; its output only goes to grep. Not `grep -q`: under pipefail, grep leaving at the
# first match can kill secret-tool with SIGPIPE and fail a search that found it.
keyring_has() {
    omabox run -b "$BOX" -- secret-tool search --all server "$APP_ID" 2>&1 |
        grep "^attribute.user = $1/password$" >/dev/null
}

keyring_lacks() { ! keyring_has "$1"; }

# window_is floating|tiled: the app's window, as the box's Hyprland has it.
window_is() {
    omabox hyprctl -b "$BOX" -j clients | python3 -c '
import json, sys
for c in json.load(sys.stdin):
    if c["class"] == sys.argv[2]:
        sys.exit(0 if c["floating"] == (sys.argv[1] == "floating") else 1)
sys.exit(1)' "$1" "$APP_ID"
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
        omabox wait -b "$BOX" --timeout 5s still >/dev/null 2>&1 || true
        omabox shot -b "$BOX" -o "$OUT/$file" >/dev/null 2>&1
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

# ---- the box: two qBittorrents, omaseed installed, nothing set up --------------------------
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --manual --interactive -b "$BOX" | grep -v 'admin / '
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Servers walk %s\n\nBuild `%s`. Two qBittorrents in the box: public (:8090) and private (:8091). Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up --manual --headless -b "$BOX" | grep -v 'admin / '
fi
home="$(omabox path -b "$BOX")/home"

# "public" gets the swarm's one public torrent, in a category of its own, so the table and the
# rail differ from "private"'s at a glance.
cp "$TORRENTS/$SAMPLER.torrent" "$home/sampler.torrent"
api public torrents/add -F "torrents=@/home/sbx/sampler.torrent" -F category=public-domain >/dev/null
rm -f "$home/sampler.torrent"
check "both qBittorrents answer: public has 1 torrent, private 4" \
    test "$(count public):$(count private)" = "1:4"

# ---- first run with two clients running ---------------------------------------------------
"$APP_BOX" restart -b "$BOX" >/dev/null
check "first run floats" window_is floating
keys Return -s 800
step "First run, two clients" "Step 1 with both qBittorrents running: one row, \"Use the server on this machine · 127.0.0.1:8090 · qbittorrent-nox running\" (the detector reads ~/.config/qBittorrent only; the --profile one on :8091 is not offered), then remote and install."

# Step 2: URL (filled), name, the three auth chips (cookie is the second), username, password,
# then test.
clip public
keys Return -s 600 Tab ctrl+a -t public Tab Tab space Tab Tab -t admin Tab ctrl+v Tab Return -s 2500
unclip
step "Signed in to public" "Step 2: URL http://127.0.0.1:8090, name public, username and password chosen, a green dot: signed in · qBittorrent 5.2.3 · API 2.15.1 · 1 torrent."

# continue, "not now" on the links step, then open the library.
keys Tab Tab Return -s 1500 Tab Return -s 2000 Return -s 3000
check "after setup the window tiles" window_is tiled
step "Library: public" "Tiled, the hero says PUBLIC · QBITTORRENT 5.2.3; one row, Public Domain Sampler; the rail has the public-domain category and no SERVERS group (one server)."
check "servers.json: public only, default, no password in it" servers_are "public" public
public_id="$(server_id public)"
check "the box's keyring holds public's password" keyring_has "$public_id"

# ---- add the second server ----------------------------------------------------------------
# `,` settings, `2` Servers, `a` a new server: name, URL, the chips, username, password, then
# path map remote, local, cancel, test.
clip private
keys comma -s 600 2 -s 300 a -s 800 -t private Tab -t http://127.0.0.1:8091 Tab Tab space Tab Tab \
    -t admin Tab ctrl+v Tab Tab Tab Tab Return -s 2500
unclip
step "Adding private" "The New server sheet, now titled private: cookie login as admin, the password as dots; under it reached http://127.0.0.1:8091 · qBittorrent 5.2.3 · API 2.15.1 · 4 torrents."

keys Tab Return -s 1200
step "Two servers" "Settings › Servers: public (default) and private, both green, private with 4 torrents; add a server below."
check "servers.json: public and private, public still the default, no password in it" \
    servers_are "public private" public
private_id="$(server_id private)"
check "the box's keyring holds private's password" keyring_has "$private_id"

# ---- switch: Settings › Servers `d` --------------------------------------------------------
keys d -s 800 Escape -s 2500
step "Switched to private" "The hero says PRIVATE; the table has private's four torrents (tiny-notes, Ünïcødé, Field Recordings, Nested Library Sampler), none of public's; the rail lost public-domain and gained a SERVERS group with private selected."
check "servers.json: private is the default" servers_are "public private" private

# An action right after the switch: stop tiny-notes, picked by name.
keys ctrl+k -s 400 -t tiny -s 300 Return -s 600 p -s 2000
check "p stopped tiny-notes on private" stopped private "$TINY"
check "public's sampler was not touched" running public "$SAMPLER"
step "Stopped on private" "tiny-notes.txt reads completed (a finished torrent, stopped; the rail counts it under stopped); the other three still run."

# A restart while on private: its password has to come back from the keyring.
"$APP_BOX" restart -b "$BOX" >/dev/null
step "Private after a restart" "Straight back to private's four torrents (tiny-notes still completed, stopped 1 in the rail), signed in from the keyring: no first run, no banner, the hero says PRIVATE."

# ---- switch back: the rail's SERVERS group -------------------------------------------------
# `h` to the rail, `G` to its last row (private), `k` to public, enter.
keys h -s 300 G -s 300 k -s 300 Return -s 2500
step "Back to public, from the rail" "The hero says PUBLIC; one row, the sampler; the rail's SERVERS group has public selected."
check "servers.json: public is the default again" servers_are "public private" public

# The rail hands the keyboard back to the table once it has switched.
keys ctrl+k -s 400 -t sampler -s 300 Return -s 600 p -s 2000
check "p stopped the sampler on public" stopped public "$SAMPLER"
check "private's tiny-notes is still stopped" stopped private "$TINY"
check "private's other torrents still run" running private "Field Recordings"
step "Stopped on public" "Public Domain Sampler reads completed; the rail counts 1 stopped."

# ---- switch: the qBittorrent group's `s` ---------------------------------------------------
keys ctrl+shift+comma -s 1500
step "qBittorrent settings: public" "Settings opens on the qBittorrent group, its heading public 5.2.3, Speed's rows read from public."
keys s -s 2500
step "qBittorrent settings: private" "After s: the heading reads private 5.2.3 and the rows are private's; the hero behind says PRIVATE."
check "servers.json: s moved the default to private" servers_are "public private" private

# ---- remove the live server ----------------------------------------------------------------
# Settings opens where it was left, on Speed in the qBittorrent group, where the digits pick
# qBittorrent sections: `h` gives the nav the keyboard, five `k` walk up to Servers, `tab` to
# its rows (the cursor on public), `j` to private, `x` asks.
keys Escape -s 600 comma -s 600 h -s 200 k k k k k -s 300 Tab -s 300 j -s 300 x -s 800
step "Remove private?" "The confirm card: Remove the server private? · http://127.0.0.1:8091 · the stored secret goes with it. Nothing on the server is touched. The cursor on cancel, remove in red."
keys l Return -s 2500
step "One server left" "Settings › Servers: public only, now the default."
keys Escape -s 1500
step "Library after the removal" "The app moved to public on its own: the hero says PUBLIC, the sampler (completed, stopped), no SERVERS group in the rail."
check "servers.json: public only, the default" servers_are "public" public
check "private's password left the keyring" keyring_lacks "$private_id"
check "public's password is still there" keyring_has "$public_id"
check "nothing on the private server was touched (4 torrents)" test "$(count private)" = 4

# ---- a restart ------------------------------------------------------------------------------
"$APP_BOX" restart -b "$BOX" >/dev/null
check "a later launch tiles straight away" window_is tiled
step "After a restart" "Straight to public's table, signed in from the keyring: no first run, no banner."

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
