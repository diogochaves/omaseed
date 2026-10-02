#!/usr/bin/env bash
# omaseed — the qBittorrent settings walkthrough (docs/16): every section of the pane, and the
# changes that ask first, driven through the app's own keys against a qBittorrent that lives
# in the box, so a moved Web UI or a new API key never touches :8081, :8082 or the real
# library.
#
#   scripts/dev/settings-walk.sh [OUT_DIR]        # agents: a headless box, a screenshot per
#                                                 # step; OUT_DIR/index.md says what to look
#                                                 # for in each and lists the checks
#   scripts/dev/settings-walk.sh --interactive    # Diogo: the box is a window on workspace 9;
#                                                 # the script still presses the keys, then says
#                                                 # what to check and waits for enter
#   OMASEED_BUILD=DIR                             # the build to run (default build/)
#
# The box comes from `scripts/dev/app-box.sh up --qbt`: qbittorrent-nox inside the box on its
# 127.0.0.1:8090, no login from there, omaseed pointed at it. The steps:
#   - the nine sections, one shot each;
#   - Seeding › Share ratio on: qBittorrent takes it only with a ratio, and gets 1;
#   - Connection › random port on, then off: off sends a port with it;
#   - Web UI › HTTPS with no certificate: refused in the pane, nothing sent;
#   - Web UI › port: esc on the confirm changes nothing; enter moves qBittorrent's Web UI and
#     omaseed's saved server with it, and the app keeps syncing;
#   - Web UI › API key › new key: esc changes nothing; enter makes a new key;
#   - Web UI › HTTPS with a self-signed certificate (made in the box): the confirm says what
#     follows; omaseed moves to https://, the certificate is refused and the server sheet
#     shows its fingerprint; tab enter trusts it (servers.json gets openssl's SHA-256) and the
#     app syncs again; a second certificate, then `test`, reads as changed (issue #2).
# Each check reads the box's qBittorrent (and omaseed's servers.json); any failed check makes
# the script exit 1. The box goes down at the end, also on ^C.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"
BOX=settings-walk
BUILD="${OMASEED_BUILD:-build}"
QBT_PORT=8090      # app-box.sh's qBittorrent in the box
NEW_WEBUI_PORT=8095
refuse_home "$QBT_PORT" "$NEW_WEBUI_PORT"
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-settings-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x "$BUILD/omaseed" ] || { echo "build first: cmake --build $BUILD" >&2; exit 1; }

SCHEME=http # https once the walk has turned HTTPS on (curl -k: the certificate is self-signed)

# pref KEY [PORT]: a value of the box's qBittorrent's preferences, as JSON; "" when the Web UI
# does not answer on PORT.
pref() {
    omabox run -b "$BOX" -- curl -fsk "$SCHEME://127.0.0.1:${2:-$QBT_PORT}/api/v2/app/preferences" 2>/dev/null |
        python3 -c 'import json, sys; print(json.dumps(json.load(sys.stdin).get(sys.argv[1])))' "$1" \
            2>/dev/null || true
}

# saved_url: the URL omaseed saved for its server.
saved_url() {
    python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["servers"][0]["url"])' \
        "$(omabox path -b "$BOX")/home/.config/omaseed/servers.json"
}

# saved_pin: the certificate omaseed trusts for its server ("" for none).
saved_pin() {
    python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["servers"][0].get("certificateSha256", ""))' \
        "$(omabox path -b "$BOX")/home/.config/omaseed/servers.json"
}

# set_prefs JSON: straight to the box's qBittorrent (what is not under test).
set_prefs() {
    omabox run -b "$BOX" -- curl -fsk -o /dev/null --data-urlencode "json=$1" \
        "$SCHEME://127.0.0.1:$QBT_PORT/api/v2/app/setPreferences"
}

# make_cert NAME: a self-signed certificate and key for 127.0.0.1 in the box's HOME; prints
# its SHA-256 the way omaseed stores it (lowercase hex, no colons).
make_cert() {
    omabox run -b "$BOX" -- openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 \
        -keyout "/home/sbx/$1.key" -out "/home/sbx/$1.crt" >/dev/null 2>&1
    omabox run -b "$BOX" -- openssl x509 -in "/home/sbx/$1.crt" -noout -fingerprint -sha256 |
        sed 's/.*=//; s/://g' | tr 'A-F' 'a-f'
}

# keys KEY...: into omaseed's window, once the screen has settled after them.
keys() {
    omabox keys -b "$BOX" --window omaseed "$@" >/dev/null
    omabox wait -b "$BOX" --timeout 5s still >/dev/null || true
}

# section N: qBittorrent section N (1-9), the keyboard on its first row. A digit keeps the
# keyboard where it was (nav or pane); h hands it to the nav, l back to the pane, row 0.
section() {
    keys "$1" h l
}

STEP=0
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
        omabox shot -b "$BOX" --window omaseed -o "$OUT/$file" >/dev/null 2>&1
        printf -- '- **%02d %s**: %s\n  ![](%s)\n' "$STEP" "$1" "$2" "$file" >>"$OUT/index.md"
        echo "$(printf '%02d' "$STEP") $1 -> $OUT/$file"
    fi
}

# check "what" ACTUAL EXPECTED: one line in the log (and index.md), and a failure remembered.
check() {
    local mark=ok
    if [ "$2" != "$3" ]; then
        mark=FAILED
        FAILED=$((FAILED + 1))
    fi
    printf '  check %-6s %s (%s%s)\n' "$mark" "$1" "$2" "$([ "$mark" = ok ] || echo ", wanted $3")"
    if [ -n "$OUT" ]; then
        # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
        printf -- '- [%s] %s: `%s`%s\n' "$([ "$mark" = ok ] && echo x || echo ' ')" "$1" "$2" \
            "$([ "$mark" = ok ] || echo " (wanted \`$3\`)")" >>"$OUT/checks.md"
    fi
}

# wait_pref KEY EXPECTED [PORT]: until the box's qBittorrent has KEY = EXPECTED (10 s).
wait_pref() {
    for _ in $(seq 1 40); do
        [ "$(pref "$1" "${3:-$QBT_PORT}" 2>/dev/null || true)" = "$2" ] && return 0
        sleep 0.25
    done
    return 0
}

cleanup() {
    "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [ "$INTERACTIVE" = 0 ]; then
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Settings walk %s\n\nBuild `%s`. Each shot: what should be on screen. Checks: checks.md.\n\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    : >"$OUT/checks.md"
fi
echo "starting the box (omaseed on a qBittorrent of the box's own)"
if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --qbt --interactive -b "$BOX" --build "$BUILD" >/dev/null
else
    "$APP_BOX" up --qbt -b "$BOX" --build "$BUILD" >/dev/null
fi
omabox pointer -b "$BOX" -- move 5 5 >/dev/null 2>&1 || true

# ---- the nine sections ----------------------------------------------------------------------
keys ctrl+shift+comma
names=("Speed" "Discovery and privacy" "Connection" "Proxy" "Seeding" "Queue" "Downloads" "Web UI" "Advanced")
for i in $(seq 1 9); do
    section "$i"
    step "Section $i ${names[$((i - 1))]}" "qBittorrent › ${names[$((i - 1))]}: every row filled from the box's qBittorrent (5.2.3 defaults), none empty or reading undefined; the nav's qBittorrent group has the section lit."
done

# ---- Seeding › Share ratio ------------------------------------------------------------------
section 5
keys space
wait_pref max_ratio_enabled true
step "Share ratio on" "Seeding: Share ratio's switch is on and its field reads 1.00 (qBittorrent ignores the switch sent alone while the ratio is ∞). The head says saved."
check "share ratio switch on" "$(pref max_ratio_enabled)" true
check "share ratio set to 1" "$(pref max_ratio)" 1

# ---- Connection › random port, on and off ---------------------------------------------------
section 3
keys j space
wait_pref random_port true
check "random port on" "$(pref random_port)" true
keys space
wait_pref random_port false
step "Random port off" "Connection: 'New random port on every start' is off again, and the listening port above it is a new random port (not 0): qBittorrent leaves random_port on unless a port comes with it."
check "random port off" "$(pref random_port)" false
port="$(pref listen_port)"
check "a listening port came with it" "$([ "$port" -ge 1024 ] && [ "$port" -le 65535 ] && echo yes || echo "$port")" yes

# ---- Web UI › HTTPS without a certificate ---------------------------------------------------
section 8
keys j j space
step "HTTPS needs a certificate" "Web UI: HTTPS stays off and the pane's head says to set the certificate and key first; no confirm opened. The Certificate, key row is not dimmed."
check "HTTPS not turned on" "$(pref use_https)" false

# ---- Web UI › port: cancel, then confirm ----------------------------------------------------
# The Address, port row: enter edits the address, enter again the port.
keys k k Return Return BackSpace BackSpace BackSpace BackSpace -t "$NEW_WEBUI_PORT" Return
step "Web UI port asks first" "A confirm: 'Change the Web UI's address, port?', saying omaseed follows a new port. Nothing sent yet."
check "port unchanged while asking" "$(pref web_ui_port)" "$QBT_PORT"
keys Escape
step "Web UI port cancelled" "The confirm is gone and the port field reads $QBT_PORT again."
check "port unchanged after esc" "$(pref web_ui_port)" "$QBT_PORT"
check "saved server unchanged after esc" "$(saved_url)" "http://127.0.0.1:$QBT_PORT"

keys Return Return BackSpace BackSpace BackSpace BackSpace -t "$NEW_WEBUI_PORT" Return
keys Return
for _ in $(seq 1 40); do
    [ "$(saved_url)" = "http://127.0.0.1:$NEW_WEBUI_PORT" ] && break
    sleep 0.25
done
sleep 3 # a sync or two from the new place
step "Web UI port moved" "The port field reads $NEW_WEBUI_PORT; the hero still shows the box server connected (no lost-server banner); omaseed followed by itself."
check "qBittorrent's Web UI on the new port" "$(pref web_ui_port "$NEW_WEBUI_PORT")" "$NEW_WEBUI_PORT"
check "saved server moved" "$(saved_url)" "http://127.0.0.1:$NEW_WEBUI_PORT"
QBT_PORT=$NEW_WEBUI_PORT

# ---- Web UI › API key: cancel, then confirm -------------------------------------------------
before="$(pref web_ui_api_key)"
keys j j j j j j Return
step "New API key asks first" "A confirm: 'Make a new API key?', saying the old key stops working at once."
keys Escape
check "key unchanged after esc" "$(pref web_ui_api_key)" "$before"
keys Return Return
for _ in $(seq 1 20); do
    [ "$(pref web_ui_api_key)" != "$before" ] && break
    sleep 0.25
done
step "New API key made" "The head says new API key made; the confirm is gone."
after="$(pref web_ui_api_key)"
check "a new key" "$([ "$after" != "$before" ] && [ "$after" != '""' ] && echo yes || echo no)" yes

# ---- Web UI › HTTPS with a self-signed certificate (issue #2) -------------------------------
first_sha="$(make_cert walk-https)"
set_prefs '{"web_ui_https_cert_path":"/home/sbx/walk-https.crt","web_ui_https_key_path":"/home/sbx/walk-https.key"}'
# The pane reads the server's settings when it opens: close it and come back to see the paths.
keys Escape Escape
keys ctrl+shift+comma
section 8
keys j j space
step "HTTPS asks first" "A confirm: 'Turn on HTTPS for the Web UI?', saying a self-signed certificate's fingerprint is shown to trust and that omaseed cannot reach the server until then."
check "HTTPS not on while asking" "$(pref use_https)" false
keys Return
SCHEME=https
for _ in $(seq 1 60); do
    [ "$(saved_url)" = "https://127.0.0.1:$QBT_PORT" ] && omabox run -b "$BOX" -- pgrep -x omaseed >/dev/null &&
        omabox shot -b "$BOX" --window omaseed -o /dev/null >/dev/null 2>&1 && break
    sleep 0.25
done
sleep 3 # the refused sync, then the sheet's own test
step "Untrusted certificate" "Settings › Servers with the server sheet on top: 'untrusted certificate' in red under Reachable, then the warning line, the sentence on trusting, SHA-256 in two lines of 16 pairs, and 'trust this certificate' (accent outline)."
check "HTTPS on" "$(pref use_https)" true
check "saved server on https" "$(saved_url)" "https://127.0.0.1:$QBT_PORT"
check "nothing trusted yet" "$(saved_pin)" ""

# The sheet's keyboard starts on its primary button (edit); tab wraps to the first focusable,
# the trust button.
keys Tab
step "Trust button focused" "The focus ring is on 'trust this certificate'."
keys Return
for _ in $(seq 1 40); do
    [ -n "$(saved_pin)" ] && break
    sleep 0.25
done
sleep 3
step "Certificate trusted" "The sheet: Reachable 'yes · N ms' in green, a Certificate row 'trusted · AB:CD:EF:01 …' matching the fingerprint, no warning any more."
check "trusted the certificate qBittorrent serves" "$(saved_pin)" "$first_sha"
keys Escape Escape
step "Synced over HTTPS" "The library, no lost-server banner: omaseed syncs over https:// with the trusted certificate."

# A different certificate on the same server: `test` on the sheet opens a new connection.
second_sha="$(make_cert walk-https-2)"
set_prefs '{"web_ui_https_cert_path":"/home/sbx/walk-https-2.crt","web_ui_https_key_path":"/home/sbx/walk-https-2.key"}'
sleep 2 # qBittorrent restarts its Web UI with the new certificate
keys comma 2 l Return
keys Tab Return
sleep 3
step "Certificate changed" "The server sheet: 'the certificate changed' in red (error line, ✕), the warning to trust it only if you changed it yourself, the new fingerprint, and 'trust the new certificate' without the accent outline."
check "the old certificate is still the trusted one" "$(saved_pin)" "$first_sha"
check "a different certificate is served" "$([ "$first_sha" != "$second_sha" ] && echo yes || echo no)" yes
keys Escape Escape

echo
if [ "$FAILED" -gt 0 ]; then
    echo "settings walk: $FAILED check(s) FAILED${OUT:+; see $OUT/checks.md}" >&2
    exit 1
fi
echo "settings walk: every check passed${OUT:+; shots and notes in $OUT/index.md}"
