#!/usr/bin/env bash
# omaseed — a box with omaseed running in it, ready to look at, drive and screenshot: the dev
# build against the :8081 playground (agents), or the installed app with its own qBittorrent
# (the manual box, through scripts/dev/manual-box.sh).
#
#   scripts/dev/app-box.sh up [-b NAME] [--interactive] [--build DIR] [--handlers] [--qbt]
#                             [--plugin DIR] [--stock-bar]
#       a box on its own network that reaches only :8081; servers.json pointing omaseed at the
#       playground; every swarm .torrent in ~/Downloads (the add sheet's "In Downloads" list);
#       then DIR/omaseed (default build/) is started and the command returns once its window
#       is drawn and still. --handlers registers omaseed for magnet: and .torrent in the box.
#       --qbt: a qBittorrent of the box's own instead of the playground (127.0.0.1:8090 in the
#       box, no login, saving to ~/Torrents), reaching only the swarm's tracker and seeder, so
#       deletes, moves and settings changes touch nothing outside the box.
#       --plugin DIR: that shell plugin (the repo's plugin/, the bar widget) mounted and enabled
#       in the box's bar, beside the app.
#       --stock-bar: Omarchy's default bar (at the top, workspaces and clock) instead of a copy
#       of yours; also for `up --manual` (manual-box.sh --stock-bar).
#   scripts/dev/app-box.sh restart [-b NAME] [--build DIR]
#       stop omaseed, start it again (after a rebuild), return once its window is still; prints
#       the run log, which has the app's own output (QT_FORCE_STDERR_LOGGING, omabox#26).
#   scripts/dev/app-box.sh api [-b NAME] [--server public|private] [-X METHOD] PATH [CURL ARGS...]
#       ask the qBittorrent this box's omaseed uses and print its answer as it comes:
#         app-box.sh api torrents/info
#         app-box.sh api torrents/reannounce -d hashes=all
#       The playground straight from here; a --qbt or manual box's own qBittorrent from inside
#       the box, signed in with the login the box keeps (never printed). A manual box runs two
#       qBittorrents: "public" (:8090, the default) and "private" (:8091, --server private).
#   scripts/dev/app-box.sh shot [-b NAME] [SHOT ARGS...]     omaseed's window (omabox shot)
#   scripts/dev/app-box.sh down [-b NAME]
#
# NAME defaults to app-box-<this checkout's directory>: every call from one checkout finds the
# same box without a name, and each git worktree (an agent's) gets its own. Subagents sharing a
# checkout pass their own -b. `up --manual` is the manual box's setup; use manual-box.sh.
#
# Everything is scripted from the repo, so a box can be thrown away and made again at will.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"

PLAY_PORT=8081
PLAY="http://127.0.0.1:$PLAY_PORT"
BOX_HOME=/home/sbx
BOX_QBT_PORT="${OMASEED_BOX_QBT_PORT:-8090}"
# BOX_BT_PORT and BOX_BT2_PORT, each box's own, are set from its name once that is known.
# The manual box's second qBittorrent, "private" (the first is "public").
BOX_QBT2_PORT="${OMASEED_BOX_QBT2_PORT:-8091}"
BOX_QBT2_PROFILE=$BOX_HOME/.local/share/qbittorrent-private
# The swarm torrents "private" starts with (small ones: they download for real in the box).
PRIVATE_TORRENTS=("tiny-notes.txt" "Ünïcødé ✓ Spaces & Symbols" "Field Recordings Vol. 3 [FLAC]"
    "Nested Library Sampler")
# Each manual-box qBittorrent's login, as box_qbt writes it, for `api` (in the box's HOME).
LOGINS=.omaseed-box-logins
TRACKER_PORT=26996
SEEDER_PORT=8083
SEEDER_BT_PORT=26997
SEEDER="http://127.0.0.1:$SEEDER_PORT/api/v2"
TORRENTS="${OMASEED_SWARM_PROFILE:-$HOME/.local/share/omaseed-swarm}/torrents"
# What `up` left in the box for `restart` and `api`: KEY=VALUE lines, outside omaseed's config.
MARKER=.omaseed-box
refuse_home "$PLAY" "$BOX_QBT_PORT" "$BOX_QBT2_PORT" "$SEEDER"

die() { echo "app-box.sh: $*" >&2; exit 1; }

# quiet CMD...: run CMD and show what it printed only when it fails (omabox's own chatter).
quiet() {
    local out
    out="$("$@" 2>&1)" || { printf '%s\n' "$out" >&2; return 1; }
}

# The box's HOME on this side (its files are written from here).
box_home() { echo "$(omabox path -b "$BOX")/home"; }

box_is_up() { omabox path -b "$BOX" >/dev/null 2>&1 && [ -d "$(box_home)" ]; }

# marker KEY: a value `up` wrote, or "" when there is none (a box from an older script).
marker() {
    local file
    file="$(box_home)/$MARKER"
    if [ -f "$file" ]; then sed -n "s/^$1=//p" "$file" | head -n 1; fi
}

# The kind of box: "manual" (its own qBittorrent, a login in ~/qbittorrent-login.txt) or "app"
# (the playground). A manual box made before the marker existed still has its login file.
box_kind() {
    local kind
    kind="$(marker kind)"
    if [ -z "$kind" ]; then
        if [ -f "$(box_home)/qbittorrent-login.txt" ]; then kind=manual; else kind=app; fi
    fi
    echo "$kind"
}

# launch BINARY: start omaseed in the box (stopping a running one first) and wait until its
# window is drawn and still. BINARY is a path as the box sees it.
launch() {
    local binary="$1" out log
    if omabox run -b "$BOX" -- pgrep -x omaseed >/dev/null 2>&1; then
        # #128: the app's own quit, so what it writes on quit is written; pkill only when it
        # does not go (an older build ignores --quit and is still there after its 15 s).
        omabox run -b "$BOX" -- "$binary" --quit >/dev/null 2>&1 ||
            omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
        omabox wait -b "$BOX" --timeout 10s window omaseed --gone >/dev/null ||
            die "omaseed did not quit in the box"
    fi
    # Qt logs to journald when stderr is not a terminal, and a box has none: without this the
    # run log stays empty (omabox#26).
    # OMASEED_LEDGER_DEMO (the seed console's made-up year, #24), OMASEED_RECAP_DUE (the weekly
    # recap at once, whatever the day, #24 T8) and OMASEED_LOOKUP_CLOCK_SCALE (the add sheet's
    # magnet lookup on a faster clock, docs/29, #44) go through when set.
    local pass=(--pass QT_FORCE_STDERR_LOGGING)
    [ -n "${OMASEED_LEDGER_DEMO:-}" ] && pass+=(--pass OMASEED_LEDGER_DEMO)
    [ -n "${OMASEED_RECAP_DUE:-}" ] && pass+=(--pass OMASEED_RECAP_DUE)
    [ -n "${OMASEED_LOOKUP_CLOCK_SCALE:-}" ] && pass+=(--pass OMASEED_LOOKUP_CLOCK_SCALE)
    out="$(QT_FORCE_STDERR_LOGGING=1 omabox run -b "$BOX" "${pass[@]}" -d -- \
        "$binary" 2>&1)" || die "could not start $binary in the box: $out"
    log="$(sed -n 's/.*(log: \(.*\))$/\1/p' <<<"$out" | head -n 1)"
    omabox wait -b "$BOX" --timeout 20s window omaseed >/dev/null ||
        die "no omaseed window after 20 s${log:+; its log: $log}"
    omabox wait -b "$BOX" --timeout 10s still >/dev/null || true
    echo "omaseed is up in $BOX${log:+ (log: $log)}"
}

# box_qbt PORT BT_PORT LOGIN_FILE [PROFILE] [SAVE_PATH]: start a qbittorrent-nox in the box,
# its Web UI on the box's 127.0.0.1:PORT, listening for peers on BT_PORT, and wait until it
# answers. LOGIN_FILE (a path on this side) gets a random admin login, kept there for `api`
# and the person at the box; "" makes it a playground instead: no login from 127.0.0.1.
# PROFILE is a directory in the box for --profile (its config under PROFILE/qBittorrent/
# config); "" is the user's own ~/.config/qBittorrent, the one first run reads. SAVE_PATH
# defaults to ~/Downloads.
box_qbt() {
    local port="$1" bt_port="$2" login_file="$3" profile="${4:-}" save="${5:-$BOX_HOME/Downloads}"
    local conf password hash auth code
    if [ -n "$profile" ]; then
        conf="$(box_home)${profile#"$BOX_HOME"}/qBittorrent/config"
    else
        conf="$(box_home)/.config/qBittorrent"
    fi
    mkdir -p "$conf" "$(box_home)${save#"$BOX_HOME"}"
    if [ -n "$login_file" ]; then
        password="$(python3 -c 'import secrets; print(secrets.token_urlsafe(12))')"
        hash="$(PASS="$password" python3 -c '
import base64, hashlib, os
salt = os.urandom(16)
key = hashlib.pbkdf2_hmac("sha512", os.environ["PASS"].encode(), salt, 100000, dklen=64)
print(base64.b64encode(salt).decode() + ":" + base64.b64encode(key).decode())')"
        auth="WebUI\\Username=admin
WebUI\\Password_PBKDF2=\"@ByteArray($hash)\""
    else
        auth="WebUI\\LocalHostAuth=false"
    fi
    # MultiConnectionsPerIp: every peer of this swarm is 127.0.0.1 (the seeder, the playground,
    # this box). With it off, libtorrent keeps one peer per IP, the last port the tracker named,
    # so the box could be left trying only the playground's port, closed in here (M15).
    cat >"$conf/qBittorrent.conf" <<EOF
[LegalNotice]
Accepted=true

[BitTorrent]
Session\\Port=$bt_port
Session\\DefaultSavePath=$save
Session\\MultiConnectionsPerIp=true

[Preferences]
WebUI\\Enabled=true
WebUI\\Address=127.0.0.1
WebUI\\Port=$port
$auth
EOF
    if [ -n "$login_file" ]; then
        printf 'qBittorrent Web UI  http://127.0.0.1:%s\nusername  admin\npassword  %s\n' \
            "$port" "$password" >"$login_file"
        chmod 600 "$login_file"
    fi
    if [ -n "$profile" ]; then
        omabox run -b "$BOX" -d -- qbittorrent-nox "--profile=$profile" >/dev/null 2>&1
    else
        omabox run -b "$BOX" -d -- qbittorrent-nox >/dev/null 2>&1
    fi
    code=""
    for _ in $(seq 1 40); do
        code="$(omabox run -b "$BOX" -- curl -s -o /dev/null -w '%{http_code}' \
            "http://127.0.0.1:$port/api/v2/app/version" 2>/dev/null || true)"
        [ "$code" = 403 ] || [ "$code" = 200 ] && break
        sleep 0.5
    done
    [ "$code" = 403 ] || [ "$code" = 200 ] ||
        die "the box's qBittorrent did not answer on :$port (last: ${code:-none})"
}

# ---- up: the app box ---------------------------------------------------------------------

up_app() {
    local build="$1" interactive="$2" handlers="$3" qbt="$4" plugin="${5:-}" home url name up_args=()
    [ -x "$build/omaseed" ] || die "no $build/omaseed: build it first (cmake --build $build)"
    if [ "$qbt" = 0 ] && ! "$repo/scripts/dev/test-server.sh" status >/dev/null 2>&1; then
        echo "starting the playground (:$PLAY_PORT)"
        "$repo/scripts/dev/test-server.sh" start >/dev/null
    fi
    if [ "$qbt" = 1 ]; then
        # The box's qBittorrent downloads from the swarm: its tracker and seeder must be up.
        "$SWARM" start >/dev/null
        compgen -G "$TORRENTS/*.torrent" >/dev/null || "$SWARM" seed >/dev/null
    elif ! compgen -G "$TORRENTS/*.torrent" >/dev/null; then
        echo "making the swarm's .torrent files"
        "$SWARM" start >/dev/null
        "$SWARM" seed >/dev/null
    fi

    echo "starting the box $BOX"
    omabox down -b "$BOX" >/dev/null 2>&1 || true
    if [ "$qbt" = 1 ]; then
        # Only the swarm's tracker and seeder: nothing of this machine's qBittorrents.
        up_args=(--net isolated --allow "$TRACKER_PORT,$SEEDER_BT_PORT")
    else
        up_args=(--net isolated --allow "$PLAY_PORT")
    fi
    if [ -n "$plugin" ]; then up_args+=(--plugin "$plugin"); fi
    up_args+=("${bar_args[@]}")
    if [ "$interactive" = 1 ]; then
        quiet omabox up "$BOX" --interactive "${up_args[@]}"
    else
        quiet omabox up "$BOX" "${up_args[@]}"
    fi
    home="$(box_home)"

    mkdir -p "$home/.config/omaseed" "$home/Downloads"
    chmod 700 "$home/.config/omaseed"
    if [ "$qbt" = 1 ]; then
        echo "starting the box's qBittorrent (127.0.0.1:$BOX_QBT_PORT, saves to ~/Torrents)"
        box_qbt "$BOX_QBT_PORT" "$BOX_BT_PORT" "" "" "$BOX_HOME/Torrents"
        url="http://127.0.0.1:$BOX_QBT_PORT"
        name=box
    else
        url="$PLAY"
        name=playground
    fi
    printf '{"version":1,"defaultId":"%s","servers":[{"id":"%s","name":"%s","url":"%s","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' \
        "$name" "$name" "$name" "$url" >"$home/.config/omaseed/servers.json"
    chmod 600 "$home/.config/omaseed/servers.json"
    cp "$TORRENTS"/*.torrent "$home/Downloads/"
    printf 'kind=%s\nbinary=%s\napi=%s/api/v2\n' "$([ "$qbt" = 1 ] && echo qbt || echo app)" \
        "$repo/$build/omaseed" "$url" >"$home/$MARKER"
    if [ "$handlers" = 1 ]; then
        omabox run -b "$BOX" -- scripts/dev/install-desktop.sh "$repo/$build/omaseed" --set-default \
            >/dev/null 2>&1 || die "install-desktop.sh failed in the box"
    fi
    launch "$repo/$build/omaseed"
    echo "  $(find "$home/Downloads" -name '*.torrent' | wc -l) .torrent files in ~/Downloads; server: the $name $url"
    if [ "$interactive" = 1 ]; then
        echo "  It is a window on workspace 9. SUPER+ALT+ESC sends SUPER keys to it."
    fi
    echo "  restart after a rebuild: scripts/dev/app-box.sh restart -b $BOX"
    echo "  throw it away:           scripts/dev/app-box.sh down -b $BOX"
}

# ---- up --manual: the manual box (scripts/dev/manual-box.sh) -----------------------------

# magnet_of NAME: the seeder's magnet for NAME (its tracker included).
magnet_of() {
    curl -fs "$SEEDER/torrents/info" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if t["name"] == sys.argv[1]:
        print(t["magnet_uri"]); break' "$1"
}

# v1_torrent: "private" makes and seeds a v1-only torrent of four 8 MiB files (its own torrent
# creator), so the manual box has one whose skipped files "free the space" deletes for real:
# every swarm torrent there is hybrid or v2-only, and a hybrid's files with downloaded pieces
# stay (DECISIONS C17 D3, #115). One file is a quarter of it, past the band's 5 % (D2).
V1_TORRENT="Sketchbook Scans (v1)"
v1_torrent() {
    local dir="$home/Downloads/private/$V1_TORRENT" task n
    mkdir -p "$dir"
    for n in 1 2 3 4; do
        head -c $((8 * 1024 * 1024)) /dev/urandom >"$dir/scan 0$n.raw"
    done
    task="$(SERVER=private api "" torrentcreator/addTask \
        --data-urlencode "sourcePath=$BOX_HOME/Downloads/private/$V1_TORRENT" -d format=v1 \
        -d startSeeding=true | python3 -c 'import json, sys; print(json.load(sys.stdin)["taskID"])')" ||
        die "the private qBittorrent did not take the v1 torrent's task"
    for _ in $(seq 1 60); do
        SERVER=private api "" torrents/info | python3 -c '
import json, sys
sys.exit(0 if any(t["name"] == sys.argv[1] and t["progress"] == 1 for t in json.load(sys.stdin)) else 1)' \
            "$V1_TORRENT" && break
        sleep 0.5
    done
    SERVER=private api POST torrentcreator/deleteTask -d "taskID=$task" >/dev/null 2>&1 || true
}

up_manual() {
    local build="$1" interactive="$2" home stage password password2 sampler name file
    echo "building omaseed for the box ($build/, installed under $BOX_HOME/.local)"
    if [ ! -f "$build/build.ninja" ]; then
        cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
            -DCMAKE_INSTALL_PREFIX="$BOX_HOME/.local" >/dev/null
    fi
    cmake --build "$build" >/dev/null

    echo "starting the swarm (tracker :$TRACKER_PORT, seeder :$SEEDER_PORT)"
    "$SWARM" start >/dev/null
    "$SWARM" seed >/dev/null
    sampler="$(magnet_of "Public Domain Sampler")"
    [ -n "$sampler" ] || die "the seeder has no Public Domain Sampler: run scripts/dev/swarm.sh seed"

    echo "starting the box $BOX ($([ "$interactive" = 1 ] && echo interactive || echo headless))"
    omabox down -b "$BOX" >/dev/null 2>&1 || true
    # Only the swarm's tracker and seeder: the box's own qBittorrent downloads for real, offline,
    # and nothing of this machine's (the playground, the real library) can be reached.
    if [ "$interactive" = 1 ]; then
        quiet omabox up "$BOX" --interactive --net isolated --allow "$TRACKER_PORT,$SEEDER_BT_PORT" \
            "${bar_args[@]}"
    else
        quiet omabox up "$BOX" --idle 0 --net isolated --allow "$TRACKER_PORT,$SEEDER_BT_PORT" \
            "${bar_args[@]}"
    fi
    home="$(box_home)"
    # shellcheck disable=SC2016 # the box's $HOME, expanded inside the box
    [ "$(omabox run -b "$BOX" -- sh -c 'printf %s "$HOME"')" = "$BOX_HOME" ] ||
        die "the box's HOME is not $BOX_HOME; the install's paths would be wrong"

    # The binary, the .desktop entry, the D-Bus service and the icons, as a user would have
    # them. The box's session bus reads ~/.local/share/dbus-1/services (omabox#4, fixed in
    # 0.1.2), so the launcher starts the app through D-Bus activation like on a desktop.
    echo "installing omaseed in the box"
    stage="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-manual-box-XXXX")"
    DESTDIR="$stage" cmake --install "$build" >/dev/null
    cp -a "$stage$BOX_HOME/.local/." "$home/.local/"
    rm -rf -- "$stage"
    omabox run -b "$BOX" -- update-desktop-database "$BOX_HOME/.local/share/applications" >/dev/null 2>&1 || true
    # omaseed's two archives as MIME types (docs/34 D7 a): what shared-mime-info's pacman hook
    # does for a package (the install above went through DESTDIR, which skips it).
    omabox run -b "$BOX" -- update-mime-database "$BOX_HOME/.local/share/mime" >/dev/null 2>&1 || true

    # Two qBittorrents, as people who split public and private trackers run them: "public" in
    # the usual profile (~/.config/qBittorrent, the one first run reads), "private" in its own
    # --profile with its own ports, login and save path.
    echo "setting up the box's qBittorrents: public (127.0.0.1:$BOX_QBT_PORT), private (127.0.0.1:$BOX_QBT2_PORT)"
    mkdir -p "$home/Downloads" "$home/$LOGINS"
    chmod 700 "$home/$LOGINS"
    box_qbt "$BOX_QBT_PORT" "$BOX_BT_PORT" "$home/$LOGINS/public.txt" ""
    box_qbt "$BOX_QBT2_PORT" "$BOX_BT2_PORT" "$home/$LOGINS/private.txt" "$BOX_QBT2_PROFILE" \
        "$BOX_HOME/Downloads/private"
    printf 'kind=manual\nbinary=%s/.local/bin/omaseed\napi=http://127.0.0.1:%s/api/v2\napi_private=http://127.0.0.1:%s/api/v2\n' \
        "$BOX_HOME" "$BOX_QBT_PORT" "$BOX_QBT2_PORT" >"$home/$MARKER"
    password="$(sed -n 's/^password  //p' "$home/$LOGINS/public.txt")"
    password2="$(sed -n 's/^password  //p' "$home/$LOGINS/private.txt")"
    {
        echo "public   qBittorrent's usual profile (~/.config/qBittorrent): the one first run finds"
        printf '  Web UI    http://127.0.0.1:%s\n  username  admin\n  password  %s\n  saves to  ~/Downloads\n\n' \
            "$BOX_QBT_PORT" "$password"
        echo "private  a second qBittorrent (--profile ~${BOX_QBT2_PROFILE#"$BOX_HOME"}) with the swarm's private torrents"
        printf '  Web UI    http://127.0.0.1:%s\n  username  admin\n  password  %s\n  saves to  ~/Downloads/private\n' \
            "$BOX_QBT2_PORT" "$password2"
    } >"$home/qbittorrent-login.txt"
    chmod 600 "$home/qbittorrent-login.txt"

    echo "filling ~/Downloads"
    for name in "tiny-notes.txt" "Field Recordings Vol. 3 [FLAC]" "distro-2026.09-x86_64.iso" \
        "Ünïcødé ✓ Spaces & Symbols" "Nested Library Sampler"; do
        # A plain prefix match: [FLAC] would be a character class to find -name.
        for file in "$TORRENTS"/*.torrent; do
            [[ "$(basename "$file")" == "$name"* ]] && cp "$file" "$home/Downloads/"
        done
    done
    echo "adding the swarm's private torrents and a v1-only one to \"private\""
    for name in "${PRIVATE_TORRENTS[@]}"; do
        for file in "$TORRENTS"/*.torrent; do
            [[ "$(basename "$file")" == "$name"* ]] || continue
            cp "$file" "$home/$LOGINS/"
            SERVER=private api "" torrents/add -F "torrents=@$BOX_HOME/$LOGINS/$(basename "$file")" \
                >/dev/null || die "the private qBittorrent did not take $(basename "$file")"
            rm -f "$home/$LOGINS/$(basename "$file")"
        done
    done
    v1_torrent
    MAGNET="$sampler" python3 - "$home/Downloads/links.html" <<'PY'
import html, os, sys
magnet = html.escape(os.environ["MAGNET"])
open(sys.argv[1], "w").write(f"""<!doctype html>
<meta charset="utf-8"><title>omaseed test links</title>
<style>body{{font:16px system-ui;margin:3em;max-width:40em}} a{{display:block;margin:1em 0}}</style>
<h1>omaseed test links</h1>
<a href="{magnet}">Public Domain Sampler (magnet, 24 MB)</a>
<p>Clicking it asks to open xdg-open; omaseed should take it from there once first run
registered it. The .torrent files are in this folder: open them from Files.</p>
""")
PY

    echo
    echo "The box is up: $BOX."
    if [ "$interactive" = 1 ]; then
        echo "  It is a window on workspace 9. SUPER+ALT+ESC sends SUPER keys to it."
    fi
    echo "  Start omaseed from the launcher (SUPER+SPACE). First run should find the box's qBittorrent"
    echo "  \"public\"; add \"private\" in Settings › Servers (a) to have two."
    echo "  public:  http://127.0.0.1:$BOX_QBT_PORT  admin / $password  (empty: fill it from ~/Downloads)"
    echo "  private: http://127.0.0.1:$BOX_QBT2_PORT  admin / $password2  (${#PRIVATE_TORRENTS[@]} private swarm torrents, and $V1_TORRENT)"
    echo "  Both logins are in the box's ~/qbittorrent-login.txt."
    echo "  ~/Downloads: .torrent files for Files, and links.html (open it in Chromium) with a magnet."
    echo "  Throw it away: scripts/dev/manual-box.sh down (or close the window)."
}

# ---- api -------------------------------------------------------------------------------

# api METHOD PATH [CURL ARGS...]: the answer of the qBittorrent this box's omaseed talks to.
api() {
    local method="$1" path="$2" kind url
    shift 2
    path="${path#/}"
    path="${path#api/v2/}"
    kind="$(box_kind)"
    if [ "$kind" = manual ]; then
        # SERVER: public (the default) or private. A manual box made before there were two
        # has only ~/qbittorrent-login.txt, in the one-server format, and no api_private.
        case "$SERVER" in
        public) url="$(marker api)" ;;
        private) url="$(marker api_private)" ;;
        *) die "no server \"$SERVER\" in a manual box: public or private" ;;
        esac
        if [ -z "$url" ] && [ "$SERVER" = public ]; then
            url="$(sed -n 's/^qBittorrent Web UI  //p' "$(box_home)/qbittorrent-login.txt")/api/v2"
        fi
        [ -n "$url" ] && [ "$url" != /api/v2 ] || die "this box has no \"$SERVER\" qBittorrent"
        refuse_home "$url"
        # Inside the box (its 127.0.0.1 is not this machine's). The password goes from the
        # login file to curl through a 0600 file, never through a command line.
        # shellcheck disable=SC2016 # expanded by the box's sh
        omabox run -b "$BOX" -- sh -c '
set -e
url="$1"; method="$2"; path="$3"; login="$4"; shift 4
[ -f "$login" ] || login="$HOME/qbittorrent-login.txt"
dir="$(mktemp -d)"; trap "rm -rf \"$dir\"" EXIT
umask 077
sed -n "s/^password  //p" "$login" | tr -d "\n" >"$dir/pass"
code="$(curl -s -o /dev/null -w "%{http_code}" -c "$dir/jar" --data-urlencode username=admin \
    --data-urlencode "password@$dir/pass" "$url/auth/login")"
case "$code" in 200 | 204) ;; *) echo "app-box.sh: the box qBittorrent refused the login ($code)" >&2; exit 1 ;; esac
if [ -n "$method" ]; then set -- -X "$method" "$@"; fi
curl -sS --fail-with-body -b "$dir/jar" "$@" "$url/$path"
' sh "$url" "$method" "$path" "$BOX_HOME/$LOGINS/$SERVER.txt" "$@"
    elif [ "$SERVER" != public ]; then
        die "only a manual box has a \"$SERVER\" qBittorrent"
    elif [ "$kind" = qbt ]; then
        url="$(marker api)"
        refuse_home "$url"
        if [ -n "$method" ]; then set -- -X "$method" "$@"; fi
        omabox run -b "$BOX" -- curl -sS --fail-with-body "$@" "$url/$path"
    else
        url="$(marker api)"
        url="${url:-$PLAY/api/v2}"
        refuse_home "$url"
        if [ -n "$method" ]; then set -- -X "$method" "$@"; fi
        curl -sS --fail-with-body "$@" "$url/$path"
    fi
}

# ---- the command line ------------------------------------------------------------------

cmd="${1:-}"
[ $# -gt 0 ] && shift
case "$cmd" in
-h | --help | "") sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
esac

BOX="app-box-$(basename "$repo")"
build=""
interactive=0
handlers=0
qbt=0
plugin=""
bar_args=()
manual=0
SERVER="${SERVER:-public}"
method=""
args=()
while [ $# -gt 0 ]; do
    case "$1" in
    -b) [ $# -ge 2 ] || die "-b needs a box name"; BOX="$2"; shift ;;
    --build) [ $# -ge 2 ] || die "--build needs a directory"; build="${2%/}"; shift ;;
    --interactive) interactive=1 ;;
    --headless) interactive=0 ;;
    --handlers) handlers=1 ;;
    --qbt) qbt=1 ;;
    --plugin) [ $# -ge 2 ] || die "--plugin needs a directory"; plugin="$(cd "$2" && pwd)"; shift ;;
    --stock-bar) bar_args=(--stock-bar) ;;
    --manual) manual=1 ;;
    -X) [ $# -ge 2 ] || die "-X needs a method"; method="$2"; shift ;;
    --server) [ $# -ge 2 ] || die "--server needs a name"; SERVER="$2"; shift ;;
    *) args+=("$1") ;;
    esac
    shift
done

# --build is used as $repo/$build, and the box sees only this checkout: an absolute path inside it
# becomes relative, one outside it can't run there.
case "$build" in
"$repo"/*) build="${build#"$repo"/}" ;;
/*) die "--build $build is outside this checkout ($repo), which is all the box sees: build inside it" ;;
esac

# The BitTorrent ports of the box's qBittorrents ("public", and the manual box's "private"),
# from the box's name. From the host every box is 127.0.0.1, so two boxes on the same port are
# one peer to the swarm's tracker, and the second one up never gets a peer (#22). 27100-27899:
# clear of the swarm's 26996-26999.
bt_slot=$(($(printf %s "$BOX" | cksum | cut -d' ' -f1) % 400))
BOX_BT_PORT=$((27100 + 2 * bt_slot))
BOX_BT2_PORT=$((BOX_BT_PORT + 1))

case "$cmd" in
up)
    [ ${#args[@]} -eq 0 ] || die "up: unexpected ${args[0]} (see --help)"
    if [ "$manual" = 1 ]; then
        up_manual "${build:-build-box}" "$interactive"
    else
        up_app "${build:-build}" "$interactive" "$handlers" "$qbt" "$plugin"
    fi
    ;;
restart)
    [ ${#args[@]} -eq 0 ] || die "restart: unexpected ${args[0]} (see --help)"
    box_is_up || die "no box $BOX: scripts/dev/app-box.sh up -b $BOX"
    binary="$(marker binary)"
    [ -n "$build" ] && binary="$repo/$build/omaseed"
    [ -n "$binary" ] || binary="$([ "$(box_kind)" = manual ] && echo "$BOX_HOME/.local/bin/omaseed" || echo "$repo/build/omaseed")"
    launch "$binary"
    ;;
api)
    [ ${#args[@]} -ge 1 ] || die "api: which path? e.g. app-box.sh api torrents/info"
    box_is_up || die "no box $BOX: scripts/dev/app-box.sh up -b $BOX"
    api "$method" "${args[@]}"
    ;;
shot)
    box_is_up || die "no box $BOX"
    omabox shot -b "$BOX" --window omaseed "${args[@]}"
    ;;
down)
    omabox down -b "$BOX"
    ;;
*)
    die "unknown command $cmd (see --help)"
    ;;
esac
