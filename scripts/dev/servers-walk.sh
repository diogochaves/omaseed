#!/usr/bin/env bash
# omaseed — the two-servers walkthrough: two qBittorrents running side by side, "public" and
# "private", as people who keep public and private trackers apart run them. First run with
# both up, the second added from Settings › Servers, a switch each way (Settings, the rail and
# the qBittorrent group's `s`), an action after each switch checked on both servers, and the
# live server removed. Between the switches, Settings' docs/34 (#70): each Backup pane's
# scope line, the server select and its list, a copy of private's settings into public with no
# switch (checked on both servers), the same copy when private stops answering, and how the
# box's file tools name omaseed's archives.
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
BOX="${OMASEED_SERVERS_BOX:-servers-walk}"
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

# pref SERVER KEY: one of SERVER's preferences, as JSON.
pref() {
    api "$1" app/preferences | python3 -c 'import json, sys; print(json.dumps(json.load(sys.stdin).get(sys.argv[1])))' "$2"
}

# has_category SERVER NAME: SERVER has the category NAME.
has_category() {
    api "$1" torrents/categories | python3 -c 'import json, sys; sys.exit(0 if sys.argv[1] in json.load(sys.stdin) else 1)' "$2"
}

# log_has TEXT: omaseed's own output in the box (the run log) has TEXT.
log_has() { omabox log -b "$BOX" run 2>/dev/null | grep -F -- "$1" >/dev/null; }

# mime_of FILE: what GIO in the box says FILE (in the box's HOME) is.
mime_of() {
    # shellcheck disable=SC2016 # expanded by the box's sh
    omabox run -b "$BOX" -- sh -c 'gio info -a standard::content-type "$HOME/$1" | sed -n "s/.*standard::content-type: //p"' sh "$1"
}

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

# continue, "not now" on the links step, "not now" on the bar step (#17; the widget is not
# what this walk is about): the done screen.
keys Tab Tab Return -s 1500 Tab Return -s 1500 Tab Return -s 2000
step "Done, another running" "You're connected to public. Under the magnet line, dim: \"Another qBittorrent is running here; add it later in Settings › Servers.\" (Q26: two qbittorrent-nox processes, one candidate)."

# Open the library.
keys Return -s 3000
check "after setup the window tiles" window_is tiled
step "Library: public" "Tiled, the hero says PUBLIC · QBITTORRENT 5.2.3; one row, Public Domain Sampler; the rail has the public-domain category and no SERVERS group (one server)."
check "servers.json: public only, default, no password in it" servers_are "public" public
public_id="$(server_id public)"
check "the box's keyring holds public's password" keyring_has "$public_id"

# ---- add the second server ----------------------------------------------------------------
# `,` settings, `2` Servers, `a` a new server: name, URL, the chips, username, password, then
# path map remote, local, cancel, and the keyboard on test.
clip private
keys comma -s 600 2 -s 300 a -s 800 -t private Tab -t http://127.0.0.1:8091 Tab Tab space Tab Tab \
    -t admin Tab ctrl+v Tab Tab Tab Tab -s 800
unclip
step "Save waits for a test" "#68, as first run: the New server sheet filled in (private, cookie login as admin), the keyboard on test, which is the primary; save dimmed beside it and \"test first\" at the right of the footer, which says enter test."

# The dimmed save is out of the tab cycle: `tab` from test wraps to the Name field, and enter
# there runs the primary, the test, not a save.
keys Tab Return -s 2500
check "a dimmed save is not reached: nothing saved before the test" servers_are "public" public
step "Adding private" "The New server sheet, now titled private: cookie login as admin, the password as dots; under it reached http://127.0.0.1:8091 · qBittorrent 5.2.3 · API 2.15.1 · 4 torrents. save is the primary now, no \"test first\"."

# The test passed, so save is the primary: enter in the Name field saves.
keys Return -s 1200
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

# ---- docs/34 (#70): whose settings, the server select, copy from another server ------------
# Something private has and public doesn't: a queue limit and a category.
api private app/setPreferences --data-urlencode 'json={"max_active_downloads":7}' >/dev/null
api private torrents/createCategory -d category=from-private -d savePath= >/dev/null
check "public starts with its own queue limit" test "$(pref public max_active_downloads)" != 7

# omaseed's Backup, still on private: `h` to the nav, `k` onto the select (between Speed and
# About), `k` About, `k` Backup.
keys h -s 300 k -s 300 k -s 300 k -s 2500
step "omaseed's Backup" "D1 a: the head reads Backup · omaseed · on this machine; first a line with an accent rule and an i: omaseed's own: its settings, your 2 servers (no passwords) and their seed history, with a copy of each server's qBittorrent settings. Torrents aren't in it: each server keeps its own. t qBittorrent › Backup › (the link in accent). Back up omaseed's caption ends with private's qBittorrent settings. The footer has t torrents: qBittorrent › Backup."
keys t -s 1500
step "t: qBittorrent's Backup, on private" "D1 a, D2 a: qBittorrent › Backup for private, the cursor on Export private's torrents. The scope line: private's own settings and torrents, read from and sent to private only. public has its own: s switch server. The rows: Export private's settings, Import settings into private, Copy settings from another server (public ▾, copy…), Export private's torrents, Import torrents into private. The footer ends in s next server."

# The select: `h` to the nav, `1` Speed, `k` onto it.
keys h -s 300 1 -s 300 k -s 600
step "The server select" "D3 a: under QBITTORRENT, a bordered select ● private 5.2.3 · s ▾ with the accent edge (the nav's cursor); no section row lit with the cursor. The footer: j k move, tab pane, enter servers, s next server."
keys Return -s 2500
step "The server list" "D3 a, D5 a: a list under the select: switch omaseed to; public with a green dot and 1 torrent (probed just now), the cursor on it; private, green, on it ✓; under a hairline, dim: The library and the tray follow, and it becomes the default."
keys Return -s 2500
step "Switched from the list" "D5 a: the select reads public; the panes are public's; the footer's right end says omaseed is on public now · the library too. Behind the modal the hero says PUBLIC."
check "servers.json: the list moved the default to public" servers_are "public private" public

# qBittorrent › Backup on public, then the copy row: `0`, tab to the rows, `j j`.
keys 0 -s 1500 Tab -s 300 j -s 300 j -s 600
step "Copy row on public" "D2 a, D6 a: the scope line names public and says private has its own; the cursor on Copy settings from another server: Read from that server just now, then shown as an import into public. Nothing changes on the other server. Its select reads private ▾."
keys Return -s 4000
step "Copy settings from private" "D6 a: a sheet with the ⇣ glyph: Copy settings from private into public; Read from private just now (qBittorrent 5.2.3). N settings differ from what public has now; … private is not changed. Columns setting · public now · private; max active downloads 3 → 7 ticked, categories + from-private ticked; under this machine only, unticked: the listening port, the default save path, the Web UI port. Buttons cancel · export public first · apply 2 changes."
check "the copy read private: the app logged it" log_has "qBittorrent settings: read private to copy"
keys Return -s 3000
step "Copied into public" "Under the rows, in green: Imported into public: 1 setting, 1 category. The select still reads public."
check "public now has private's queue limit" test "$(pref public max_active_downloads)" = 7
check "public now has private's category" has_category public from-private
check "public kept its own Web UI port (machine-specific, unticked)" test "$(pref public web_ui_port)" = 8090
check "private was not changed: its queue limit" test "$(pref private max_active_downloads)" = 7
check "private was not changed: its Web UI port" test "$(pref private web_ui_port)" = 8091
check "servers.json: the copy never moved the app off public" servers_are "public private" public

# private stops answering (frozen, not killed): the copy falls back to what omaseed kept.
omabox run -b "$BOX" -- pkill -STOP -f qbittorrent-private >/dev/null
keys Return -s 12000
step "Copy from a private that doesn't answer" "D6 a's fallback: the same sheet, its line now private didn't answer (no answer in time). These are its settings as omaseed last kept them, on <today, the copy's time> (qBittorrent 5.2.3). … private is not changed. (Nothing much differs any more: the copy above took them.)"
check "the fallback was taken: the app logged why" log_has "qBittorrent settings: private not read"
keys Escape -s 800
omabox run -b "$BOX" -- pkill -CONT -f qbittorrent-private >/dev/null
check "private answers again" test "$(pref private web_ui_port)" = 8091

# The files: omaseed's two archives are named by their MIME types in the box (D7 a), and
# omaseed is not their opener (D9 a).
# shellcheck disable=SC2016 # expanded by the box's sh
omabox run -b "$BOX" -- sh -c 'cd "$HOME" && tar cf walk.omaseed qbittorrent-login.txt &&
    cp walk.omaseed walk.omaseed-torrents' >/dev/null
check "gio: a .omaseed is an omaseed backup" test "$(mime_of walk.omaseed)" = application/x-omaseed-backup
check "gio: a .omaseed-torrents is omaseed torrents" \
    test "$(mime_of walk.omaseed-torrents)" = application/x-omaseed-torrents
check "omaseed does not open backups on a double click" \
    test "$(omabox run -b "$BOX" -- xdg-mime query default application/x-omaseed-backup 2>/dev/null)" != "$APP_ID.desktop"

# Back on private, on Speed, as the next part expects: `1` Speed, `s`.
keys 1 -s 300 s -s 2500
check "servers.json: s moved the default to private again" servers_are "public private" private

# ---- remove the live server ----------------------------------------------------------------
# Settings opens where it was left, on Speed in the qBittorrent group, where the digits pick
# qBittorrent sections: `h` gives the nav the keyboard, seven `k` walk up to Servers (the
# server select, About, Backup, Appearance, Keybindings, Notifications, Servers), `tab` to its
# rows (the cursor on public), `j` to private, `x` asks.
keys Escape -s 600 comma -s 600 h -s 200 k k k k k k k -s 300 Tab -s 300 j -s 300 x -s 800
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
