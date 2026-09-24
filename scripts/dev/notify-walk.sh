#!/usr/bin/env bash
# omaseed — the notification walkthrough: real downloads finish while the app runs, and each
# one raises exactly one "Download complete" notice, drawn by the box's own Omarchy shell.
# Clicking a notice brings the app up on that torrent, even from the tray.
#
#   scripts/dev/notify-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per step,
#                                               # OUT_DIR/index.md says what to look for in each
#                                               # and lists the checks
#   scripts/dev/notify-walk.sh --interactive    # Diogo: the box is a window on workspace 9; the
#                                               # script still closes and clicks, then says what
#                                               # to check and waits for enter
#
# What the app sent is read from the box's session bus (dbus-monitor), what the shell drew from
# its state files (~/.local/state/omarchy/notifications/, one JSON per toast on screen,
# moved to history/ when it leaves), so no check depends on catching a 5-second toast in a
# shot. It resets the swarm (scripts/dev/swarm.sh reset + seed) and holds the playground's
# downloads at 2 KiB/s: tiny-notes.txt (4 KB) finishes in seconds, the unicode torrent (5 MB)
# only once the limit is lifted, after the window is in the tray. So a slow read of a step
# never lets a toast come and go unseen. The limit is restored at the end (also on ^C). The
# box's network reaches :8081 alone.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
SWARM="$repo/scripts/dev/swarm.sh"
PLAY="http://127.0.0.1:8081/api/v2"
DOWNLOADS="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}/downloads"
APP_ID=io.github.diogochaves.omaseed
BOX=notify-walk
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-${TMPDIR:-/tmp}/omaseed-notify-walk-$(date +%Y%m%d-%H%M%S)}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

TINY="tiny-notes.txt"
UNICODE="Spaces & Symbols"

# wait_state NAME REGEX [SECONDS]: until the playground's torrent NAME is in a matching state.
wait_state() {
    local name="$1" pattern="$2" limit="${3:-120}" state=""
    for _ in $(seq 1 $((limit * 2))); do
        state="$(curl -fs "$PLAY/torrents/info?category=swarm" | python3 -c '
import json, sys
for t in json.load(sys.stdin):
    if sys.argv[1].lower() in t["name"].lower():
        print(t["state"], round(t["progress"], 3)); break
' "$name" 2>/dev/null || true)"
        if [[ "$state" =~ $pattern ]]; then return 0; fi
        sleep 0.5
    done
    echo "  (waited ${limit}s: \"$name\" is \"$state\", not /$pattern/)" >&2
    return 1
}

# launch: (re)start the app in the box and wait for its window and first sync.
launch() {
    omabox run -b "$BOX" -- pkill -x omaseed >/dev/null 2>&1 || true
    sleep 0.5
    omabox run -b "$BOX" -d -- ./build/omaseed >/dev/null
    for _ in $(seq 1 40); do
        window_shown && break
        sleep 0.25
    done
    sleep 2.5
}

window_shown() { omabox hyprctl -b "$BOX" -j clients | grep -q "\"class\": \"$APP_ID\""; }

# notices: every Notify call on the box's bus so far, as JSON (app, icon, summary, body,
# actions, hints), and every ActionInvoked key under "invoked".
notices() {
    python3 - "$home/bus.log" <<'PY'
import json, re, sys
calls, invoked, block = [], [], []
def flush():
    if not block:
        return
    head = block[0]
    top = [l for l in block[1:] if l.startswith("   ") and not l.startswith("    ")]
    strings = [re.match(r'   string "(.*)"$', l).group(1) for l in top if l.startswith("   string ")]
    if "member=Notify" in head and head.startswith("method call"):
        actions, hints, key, where = [], {}, None, None
        for l in block[1:]:
            s = l.strip()
            if l == "   array [":
                where = "actions" if where is None else "hints"
            elif l == "   ]":
                pass
            elif where == "actions" and s.startswith("string "):
                actions.append(re.match(r'string "(.*)"$', s).group(1))
            elif where == "hints" and s.startswith("string ") and key is None:
                key = re.match(r'string "(.*)"$', s).group(1)
            elif where == "hints" and s.startswith("variant"):
                hints[key] = s.split(None, 2)[2].strip('"'); key = None
        calls.append({"app": strings[0], "icon": strings[1], "summary": strings[2],
                      "body": strings[3], "actions": actions, "hints": hints})
    elif "member=ActionInvoked" in head and len(strings) == 1:
        invoked.append(strings[0])
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if line and not line.startswith(" "):
        flush(); block = []
    block.append(line)
flush()
print(json.dumps({"calls": calls, "invoked": invoked}, ensure_ascii=False))
PY
}

# completed_for NAME: how many "Download complete" notices name NAME in their body.
completed_for() {
    notices | python3 -c '
import json, sys
calls = json.load(sys.stdin)["calls"]
print(sum(1 for c in calls if c["summary"] == "Download complete" and sys.argv[1] in c["body"]))' "$1"
}

# tiny_notice PYTHON-EXPR: whether EXPR holds for tiny-notes' notice `c` (sys.argv[1] = APP_ID).
tiny_notice() {
    notices | python3 -c '
import json, sys
c = next((c for c in json.load(sys.stdin)["calls"] if "tiny-notes" in c["body"]), None)
sys.exit(0 if c is not None and eval(sys.argv[2]) else 1)' "$APP_ID" "$1"
}

window_hidden() { ! window_shown; }
invoked_default() {
    notices | python3 -c 'import json, sys; sys.exit(0 if "default" in json.load(sys.stdin)["invoked"] else 1)'
}
one_each() { [ "$(completed_for "$TINY")" = 1 ] && [ "$(completed_for "$UNICODE")" = 1 ]; }
shell_drew() {
    grep -rlqF -- "$TINY" "$home/.local/state/omarchy/notifications" &&
        grep -rlqF -- "$UNICODE" "$home/.local/state/omarchy/notifications"
}

# toast_up NAME [SECONDS]: until the shell has a toast on screen naming NAME.
toast_up() {
    local limit="${2:-60}"
    for _ in $(seq 1 $((limit * 4))); do
        grep -lqF -- "$1" "$home"/.local/state/omarchy/notifications/*.json 2>/dev/null && return 0
        sleep 0.25
    done
    echo "  (no toast naming \"$1\" in ${limit}s)" >&2
    return 1
}

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

PLAY_DL_LIMIT="$(curl -fs "$PLAY/transfer/downloadLimit" || echo 0)"
cleanup() {
    curl -fs -X POST "$PLAY/transfer/setDownloadLimit" -d "limit=$PLAY_DL_LIMIT" >/dev/null 2>&1 || true
    [ "$INTERACTIVE" = 1 ] || omabox down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "resetting the swarm (the playground's swarm downloads start over)"
"$SWARM" start >/dev/null
"$SWARM" reset >/dev/null
curl -fs -X POST "$PLAY/transfer/setDownloadLimit" -d limit=2048 >/dev/null
mkdir -p "$DOWNLOADS"

echo "starting the box"
omabox down -b "$BOX" >/dev/null 2>&1 || true
if [ "$INTERACTIVE" = 1 ]; then
    omabox up "$BOX" --interactive --net isolated --allow 8081 --ro-bind "$DOWNLOADS" >/dev/null
else
    mkdir -p "$OUT"
    printf '# Notification walk %s\n\nBuild `%s`. Each shot: what should be on screen.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    omabox up "$BOX" --net isolated --allow 8081 --ro-bind "$DOWNLOADS" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
mkdir -p "$home/.config/omaseed" && chmod 700 "$home/.config/omaseed"
printf '%s' '{"version":1,"defaultId":"playground","servers":[{"id":"playground","name":"playground","url":"http://127.0.0.1:8081","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-09-23T00:00:00Z"}]}' \
    >"$home/.config/omaseed/servers.json"
chmod 600 "$home/.config/omaseed/servers.json"
# The desktop entry and icon, so the shell can resolve the notice's icon and desktop-entry.
omabox run -b "$BOX" -- scripts/dev/install-desktop.sh >/dev/null 2>&1
omabox run -b "$BOX" -d -- sh -c \
    'exec dbus-monitor --session "interface=org.freedesktop.Notifications" >"$HOME/bus.log" 2>&1' >/dev/null
launch
# Toasts stack from the top-right corner, newest on top; this is the middle of the top one.
width="$(omabox hyprctl -b "$BOX" -j monitors | python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["width"])')"
TOAST_X=$((width - 230))
TOAST_Y=75

# ---- the walk -----------------------------------------------------------------------------
# Seeded only now, so every swarm torrent finishes while the app watches.
"$SWARM" seed >/dev/null
wait_state "$TINY" "UP|^uploading" 60 || true
toast_up "$TINY" 15 || true
sleep 0.6 # past the fade-in
step "Toast" "Top right, over the app: \"Download complete\", \"tiny-notes.txt · 4 KB · swarm\" and omaseed's icon, in the shell's own toast style. Omarchy's toasts draw no buttons."

# Hide the window to the tray (closing it hides it), then click the next notice: the app has
# to come back on its own, with the cursor on that torrent.
omabox hyprctl -b "$BOX" dispatch "hl.dsp.window.close(\"class:$APP_ID\")" >/dev/null
sleep 1
check "closing the window hides it to the tray" window_hidden
curl -fs -X POST "$PLAY/transfer/setDownloadLimit" -d "limit=$PLAY_DL_LIMIT" >/dev/null
wait_state "$UNICODE" "UP|^uploading" 90 || true
toast_up "$UNICODE" 15 || true
# The newest toast is the top one. A toast under the pointer does not time out, so park the
# pointer there before the shot; the click then finds it whatever the timing.
omabox pointer -b "$BOX" -- move "$TOAST_X" "$TOAST_Y" >/dev/null
step "Toast while hidden" "Only the wallpaper and the bar: the window is in the tray, and the unicode torrent's notice is the top one (its long name wraps whole words; the pointer rests on it, which keeps it up). Interactive: keep your mouse off the box; the script clicks the toast after enter."
omabox click -b "$BOX" "$TOAST_X" "$TOAST_Y"
sleep 2
check "a click on the toast sends the \"default\" action" invoked_default
check "the click brings the hidden window back" window_shown
step "Click shows the torrent" "The app is back, tiled, with the cursor on the unicode torrent (seeding, done 100 %) and its details open."

check "exactly one notice each for $TINY and the unicode torrent" one_each
check "the body reads name · size · category" tiny_notice 'c["body"] == "tiny-notes.txt · 4 KB · swarm"'
check "app omaseed, icon and desktop-entry $APP_ID, urgency low" tiny_notice \
    'c["app"] == "omaseed" and c["icon"] == sys.argv[1] and c["hints"].get("desktop-entry") == sys.argv[1] and c["hints"].get("urgency") == "0"'
check "actions: open folder (the content is local), show, and show again as default" tiny_notice \
    'c["actions"] == ["open-folder", "open folder", "show", "show", "default", "show"]'
check "the shell drew both (its state files name them)" shell_drew

# A fresh start sees both torrents already finished: that is not a completion.
launch
sleep 6
check "a restart raises no notice for torrents that were already done" one_each

if [ "$INTERACTIVE" = 0 ]; then
    printf '\n## Checks\n\n%s' "$CHECKS" >>"$OUT/index.md"
fi
echo
if [ "$INTERACTIVE" = 1 ]; then
    echo "done. The box stays up for a look around; close it with: omabox down $BOX"
else
    echo "done: $OUT/index.md"
fi
