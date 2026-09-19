#!/usr/bin/env bash
# omaseed — disposable qBittorrent test server.
#
# Runs a native `qbittorrent-nox` as the current user on 127.0.0.1:8081 with its
# own profile directory, so agents can add, delete, stop and recheck freely
# without touching the real library server on :8080.
#
#   scripts/dev/test-server.sh start    # start (creates the profile on first run)
#   scripts/dev/test-server.sh stop
#   scripts/dev/test-server.sh status
#   scripts/dev/test-server.sh reset    # stop, wipe the profile, start fresh
#   scripts/dev/test-server.sh seed     # add a few stopped fixture torrents
#   scripts/dev/test-server.sh log      # tail the server log
#
# Auth: localhost is auth-bypassed (WebUI\LocalHostAuth=false), so plain requests
# work with no key. Cookie login also works with omaseed / omaseed-test for the
# cookie-fallback code path. Bearer keys are created in the WebUI only.
set -euo pipefail

PROFILE="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}"
PORT="${OMASEED_TEST_PORT:-8081}"
WEBUI_USER="omaseed"
WEBUI_PASS="omaseed-test"
BASE="http://127.0.0.1:$PORT"
PIDFILE="$PROFILE/qbt.pid"
LOGFILE="$PROFILE/qbt.log"
CONF="$PROFILE/qBittorrent/config/qBittorrent.conf"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1 (sudo pacman -S $2)" >&2; exit 1; }; }

write_conf() {
  mkdir -p "$(dirname "$CONF")" "$PROFILE/downloads"
  local hash
  hash="$(python3 - "$WEBUI_PASS" <<'PY'
import base64, hashlib, os, sys
# qBittorrent Utils::Password: PBKDF2-HMAC-SHA512, 100000 iterations, 16-byte salt, 64-byte key.
salt = os.urandom(16)
key = hashlib.pbkdf2_hmac("sha512", sys.argv[1].encode(), salt, 100000, dklen=64)
print(base64.b64encode(salt).decode() + ":" + base64.b64encode(key).decode())
PY
)"
  cat > "$CONF" <<EOF
[LegalNotice]
Accepted=true

[Preferences]
WebUI\\Address=127.0.0.1
WebUI\\Port=$PORT
WebUI\\Username=$WEBUI_USER
WebUI\\Password_PBKDF2="@ByteArray($hash)"
WebUI\\LocalHostAuth=false
WebUI\\CSRFProtection=false
WebUI\\HostHeaderValidation=false
General\\Locale=en

[BitTorrent]
Session\\Port=26999
Session\\DHTEnabled=false
Session\\PeXEnabled=false
Session\\LSDEnabled=false
Session\\DefaultSavePath=$PROFILE/downloads
Session\\TempPathEnabled=false
Session\\QueueingSystemEnabled=true
Session\\MaxActiveDownloads=2
EOF
}

is_running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

wait_up() {
  for _ in $(seq 1 60); do
    if curl -fs "$BASE/api/v2/app/version" >/dev/null 2>&1; then return 0; fi
    sleep 0.25
  done
  echo "server did not answer on $BASE within 15 s; see $LOGFILE" >&2
  return 1
}

cmd_start() {
  need qbittorrent-nox qbittorrent-nox
  if is_running; then echo "already running (pid $(cat "$PIDFILE")) on $BASE"; return 0; fi
  [ -f "$CONF" ] || write_conf
  nohup qbittorrent-nox --profile="$PROFILE" --webui-port="$PORT" --confirm-legal-notice \
    >"$LOGFILE" 2>&1 &
  echo $! > "$PIDFILE"
  wait_up
  echo "qBittorrent $(curl -fs "$BASE/api/v2/app/version") · API $(curl -fs "$BASE/api/v2/app/webapiVersion") · $BASE · profile $PROFILE"
}

cmd_stop() {
  if is_running; then
    kill "$(cat "$PIDFILE")"
    for _ in $(seq 1 40); do is_running || break; sleep 0.25; done
    is_running && kill -9 "$(cat "$PIDFILE")" || true
    echo "stopped"
  else
    echo "not running"
  fi
  rm -f "$PIDFILE"
}

cmd_status() {
  if is_running; then
    echo "running (pid $(cat "$PIDFILE"))"
    curl -fs "$BASE/api/v2/app/version" && echo " on $BASE" || echo "process alive but WebUI not answering"
    curl -fs "$BASE/api/v2/torrents/info" | python3 -c 'import json,sys; t=json.load(sys.stdin); print(len(t), "torrents")'
  else
    echo "not running"
  fi
}

cmd_seed() {
  is_running || cmd_start
  # Public, legal, well-seeded fixtures. Added stopped so the test box never downloads.
  local magnets=(
    "magnet:?xt=urn:btih:4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d&dn=fixture-one"
    "magnet:?xt=urn:btih:b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0&dn=fixture-two"
    "magnet:?xt=urn:btih:c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00&dn=fixture-three"
  )
  for m in "${magnets[@]}"; do
    curl -fs -X POST "$BASE/api/v2/torrents/add" \
      -F "urls=$m" -F "stopped=true" -F "category=fixtures" >/dev/null && echo "added ${m##*dn=}"
  done
  cmd_status
}

cmd_reset() { cmd_stop; rm -rf "$PROFILE"; cmd_start; }

case "${1:-}" in
  start) cmd_start ;;
  stop) cmd_stop ;;
  status) cmd_status ;;
  reset) cmd_reset ;;
  seed) cmd_seed ;;
  log) tail -n 50 -f "$LOGFILE" ;;
  *) sed -n '2,17p' "$0"; exit 2 ;;
esac
