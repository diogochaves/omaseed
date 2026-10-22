#!/usr/bin/env bash
# omaseed — disposable Transmission and Deluge test servers (DECISIONS C14 E11, #84–#87).
#
# Run by scripts/dev/test-server.sh when its last argument is --transmission or --deluge:
#
#   scripts/dev/test-server.sh start|stop|status|reset|seed|log --transmission
#   scripts/dev/test-server.sh start|stop|status|reset|seed|log --deluge
#
#   --transmission  transmission-daemon 4.1 on 127.0.0.1:8085 (RPC at /transmission/rpc), no
#                   login: localhost only, like the qBittorrent playground.
#   --deluge        deluged (its RPC on 127.0.0.1:58847) and deluge-web on 127.0.0.1:8086
#                   (/json), connected to that daemon at start. deluge-web always wants a
#                   password: a random one is made with the profile and saved to
#                   ~/.config/omaseed/dev.env (0600) as DELUGE_TEST_PASS; never in the repo.
#
# Each runs as a user process with its own config dir under ~/.local/share/omaseed-test/
# (transmission/, deluge/), its downloads beside it, DHT, PeX, LSD and port mapping off and
# peers on loopback only. The system units (transmission.service, deluged.service,
# deluge-web.service) are never enabled, started or touched.
#
# `seed` makes a small library of generic files (a text file, a folder of three, a 4 MiB
# blob) in the profile, makes a .torrent of each with transmission-create (no tracker; DHT
# and PeX are off) and adds them so they verify complete and seed. `reset` stops the server
# and wipes its profile (and nothing of the qBittorrent playground's).
set -euo pipefail

here="$(dirname "${BASH_SOURCE[0]}")"
# shellcheck source=scripts/dev/home-guard.sh
. "$here/home-guard.sh"
# shellcheck source=scripts/dev/detach.sh
. "$here/detach.sh"

ENGINE="${1:-}"
shift || true
ROOT="${OMASEED_ENGINES_ROOT:-$HOME/.local/share/omaseed-test}"
DEV_ENV="${OMASEED_DEV_ENV:-$HOME/.config/omaseed/dev.env}"

case "$ENGINE" in
transmission)
  PROFILE="$ROOT/transmission"
  PORT="${OMASEED_TRANSMISSION_PORT:-8085}"
  BT_PORT="${OMASEED_TRANSMISSION_BT_PORT:-26994}"
  BASE="http://127.0.0.1:$PORT"
  RPC="$BASE/transmission/rpc"
  PIDFILE="$PROFILE/daemon.pid"
  LOGFILE="$PROFILE/daemon.log"
  refuse_home "$PORT" "$BT_PORT"
  ;;
deluge)
  PROFILE="$ROOT/deluge"
  PORT="${OMASEED_DELUGE_WEB_PORT:-8086}"
  DAEMON_PORT="${OMASEED_DELUGE_DAEMON_PORT:-58847}"
  BT_PORT="${OMASEED_DELUGE_BT_PORT:-26993}"
  BASE="http://127.0.0.1:$PORT"
  RPC="$BASE/json"
  PIDFILE="$PROFILE/omaseed-deluged.pid" # deluged writes deluged.pid itself, "PID;PORT"
  WEB_PIDFILE="$PROFILE/omaseed-deluge-web.pid"
  LOGFILE="$PROFILE/deluged.log"
  WEB_LOGFILE="$PROFILE/deluge-web.log"
  refuse_home "$PORT" "$DAEMON_PORT" "$BT_PORT"
  ;;
*)
  echo "usage: test-engine.sh transmission|deluge start|stop|status|reset|seed|log" >&2
  exit 2
  ;;
esac
DOWNLOADS="$PROFILE/downloads"
LIBRARY="$PROFILE/library"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1 (sudo pacman -S $2)" >&2; exit 1; }; }

alive() { [ -f "$1" ] && kill -0 "$(cat "$1")" 2>/dev/null; }

# stop_pid PIDFILE: TERM, then KILL after 10 s.
stop_pid() {
  alive "$1" || { rm -f "$1"; return 0; }
  kill "$(cat "$1")"
  for _ in $(seq 1 40); do alive "$1" || break; sleep 0.25; done
  if alive "$1"; then kill -9 "$(cat "$1")" || true; fi
  rm -f "$1"
}

# answers URL: anything HTTP answers there.
answers() { [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 -X POST "$1")" != 000 ]; }

wait_answer() {
  for _ in $(seq 1 80); do answers "$1" && return 0; sleep 0.25; done
  echo "nothing answered on $1 within 20 s; see $2" >&2
  return 1
}

dev_env_get() {
  [ -f "$DEV_ENV" ] || return 0
  sed -n "s/^[[:space:]]*\(export[[:space:]]\{1,\}\)\{0,1\}$1=//p" "$DEV_ENV" | tail -n 1
}

# dev_env_set KEY: KEY's value (from the environment) into dev.env, replacing its line; the
# value reaches python through the environment, never a command line. dev.env stays 0600.
dev_env_set() {
  mkdir -p "$(dirname "$DEV_ENV")"
  export "${1?}"
  python3 - "$DEV_ENV" "$1" <<'PY'
import os, sys
path, key = sys.argv[1], sys.argv[2]
try:
    lines = open(path, encoding="utf-8").read().splitlines()
except FileNotFoundError:
    lines = ["# omaseed development credentials: never commit, never paste into chat."]
lines = [l for l in lines if not l.strip().removeprefix("export ").startswith(key + "=")]
lines.append(f"{key}={os.environ[key]}")
tmp = path + ".omaseed-tmp"
fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
os.replace(tmp, path)
PY
}

# ---- Transmission ------------------------------------------------------------------------------

# trpc METHOD [ARGUMENTS-JSON]: one JSON-RPC 2.0 call, with the 409 session dance; prints the
# answer's JSON.
trpc() {
  local body session
  body="$(python3 -c 'import json,sys; print(json.dumps({"jsonrpc":"2.0","method":sys.argv[1],"params":json.loads(sys.argv[2]),"id":1}))' "$1" "${2:-{\}}")"
  session="$(curl -s -o /dev/null -D - -X POST "$RPC" | sed -n 's/^X-Transmission-Session-Id: *//Ip' | tr -d '\r')"
  curl -fs -H "X-Transmission-Session-Id: $session" -H 'Content-Type: application/json' \
    --data "$body" "$RPC"
}

transmission_settings() {
  mkdir -p "$PROFILE" "$DOWNLOADS"
  python3 - "$PROFILE/settings.json" "$PORT" "$BT_PORT" "$DOWNLOADS" <<'PY'
import json, sys
path, port, bt, downloads = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
try:
    settings = json.load(open(path))
except (FileNotFoundError, ValueError):
    settings = {}
settings.update({
    "rpc-enabled": True, "rpc-port": port, "rpc-bind-address": "127.0.0.1",
    "rpc-authentication-required": False, "rpc-whitelist-enabled": True,
    "rpc-whitelist": "127.0.0.1,::1", "rpc-host-whitelist-enabled": False,
    "peer-port": bt, "peer-port-random-on-start": False, "bind-address-ipv4": "127.0.0.1",
    "bind-address-ipv6": "::1", "dht-enabled": False, "pex-enabled": False,
    "lpd-enabled": False, "port-forwarding-enabled": False, "utp-enabled": False,
    "download-dir": downloads, "incomplete-dir-enabled": False, "start-added-torrents": True,
    "blocklist-enabled": False, "message-level": 3,
})
json.dump(settings, open(path, "w"), indent=4, sort_keys=True)
PY
}

transmission_start() {
  need transmission-daemon transmission-cli
  if alive "$PIDFILE"; then echo "already running (pid $(cat "$PIDFILE")) on $BASE"; return 0; fi
  if answers "$RPC"; then
    echo "something already answers on $BASE, and $PIDFILE names no live daemon of this profile;" \
      "refusing to start (stop it first)" >&2
    exit 1
  fi
  transmission_settings
  detach "$LOGFILE" transmission-daemon --foreground --config-dir "$PROFILE"
  echo "$!" >"$PIDFILE"
  wait_answer "$RPC" "$LOGFILE"
  local session
  session="$(trpc session_get '{"fields":["version","rpc_version_semver","download_dir"]}')"
  python3 -c '
import json, os, sys
r = json.loads(sys.argv[1])["result"]
if os.path.normpath(r["download_dir"]) != os.path.normpath(sys.argv[2]):
    sys.exit("the daemon on this port does not save into " + sys.argv[2] + ": not ours")
print("Transmission", r["version"], "· RPC", r["rpc_version_semver"], "·", sys.argv[3], "· profile", sys.argv[4])
' "$session" "$DOWNLOADS" "$RPC" "$PROFILE"
}

transmission_status() {
  if ! alive "$PIDFILE"; then echo "not running"; return 1; fi
  echo "running (pid $(cat "$PIDFILE"))"
  trpc torrent_get '{"fields":["name","status","percent_done"]}' | python3 -c '
import json, sys
torrents = json.load(sys.stdin)["result"]["torrents"]
print(len(torrents), "torrents")
for t in torrents:
    print("  %-40s status %d  %5.1f%%" % (t["name"][:40], t["status"], 100 * t["percent_done"]))'
}

transmission_add() { # TORRENT-FILE DIR
  local meta
  meta="$(base64 -w0 "$1")"
  trpc torrent_add "{\"metainfo\":\"$meta\",\"download_dir\":\"$2\"}" >/dev/null
}

# ---- Deluge ------------------------------------------------------------------------------------

DELUGE_PASS="${DELUGE_TEST_PASS:-$(dev_env_get DELUGE_TEST_PASS)}"
JAR="$PROFILE/.cookie"

# drpc METHOD [PARAMS-JSON]: one deluge-web call with the session cookie; prints the answer.
drpc() {
  local body
  body="$(python3 -c 'import json,sys; print(json.dumps({"method":sys.argv[1],"params":json.loads(sys.argv[2]),"id":1}))' "$1" "${2:-[]}")"
  curl -fs -b "$JAR" -c "$JAR" -H 'Content-Type: application/json' --data "$body" "$RPC"
}

deluge_login() {
  DELUGE_PASS="$DELUGE_PASS" python3 -c '
import json, os; print(json.dumps({"method":"auth.login","params":[os.environ["DELUGE_PASS"]],"id":1}))' |
    curl -fs -c "$JAR" -H 'Content-Type: application/json' --data @- "$RPC" | grep -q '"result": *true'
}

deluge_config() {
  mkdir -p "$PROFILE" "$DOWNLOADS"
  if [ -z "$DELUGE_PASS" ]; then
    DELUGE_PASS="$(python3 -c 'import secrets; print(secrets.token_urlsafe(18))')"
    DELUGE_TEST_PASS="$DELUGE_PASS" dev_env_set DELUGE_TEST_PASS
    echo "new deluge-web password saved to $DEV_ENV as DELUGE_TEST_PASS"
  fi
  # Deluge's config files are two JSON objects, a header and the settings. Only the keys that
  # matter are written; Deluge fills the rest with its defaults.
  DELUGE_PASS="$DELUGE_PASS" python3 - "$PROFILE" "$PORT" "$DAEMON_PORT" "$BT_PORT" "$DOWNLOADS" <<'PY'
import hashlib, json, os, secrets, sys
profile, web, daemon, bt, downloads = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]

def write(name, header, settings):
    path = os.path.join(profile, name)
    try:
        text = open(path).read()
        old = json.loads(text[text.index("}{") + 1:])
    except (FileNotFoundError, ValueError):
        old = {}
    old.update(settings)
    with open(path, "w") as f:
        f.write(json.dumps(header, indent=4) + json.dumps(old, indent=4))

write("core.conf", {"file": 1, "format": 1}, {
    "download_location": downloads, "move_completed": False, "daemon_port": daemon,
    "allow_remote": False, "listen_ports": [bt, bt], "random_port": False,
    "listen_interface": "127.0.0.1", "outgoing_interface": "", "dht": False, "upnp": False,
    "natpmp": False, "lsd": False, "utpex": False, "new_release_check": False,
    "enabled_plugins": ["Label"],
})
salt = secrets.token_hex(20)
write("web.conf", {"file": 2, "format": 1}, {
    "port": web, "interface": "127.0.0.1", "https": False, "first_login": False,
    "pwd_salt": salt,
    "pwd_sha1": hashlib.sha1((salt + os.environ["DELUGE_PASS"]).encode()).hexdigest(),
    "default_daemon": "omaseed0test0daemon",
})
PY
}

# deluge_hostlist: deluge-web's one daemon, this profile's, signed in as deluged's own
# localclient (the auth file deluged writes on its first start; Deluge itself copies that
# password into hostlist.conf for a local daemon). Read and written by python, never printed.
deluge_hostlist() {
  for _ in $(seq 1 40); do [ -s "$PROFILE/auth" ] && break; sleep 0.25; done
  python3 - "$PROFILE" "$DAEMON_PORT" <<'PY'
import json, os, sys
profile, daemon = sys.argv[1], int(sys.argv[2])
password = ""
for line in open(os.path.join(profile, "auth")):
    fields = line.strip().split(":")
    if fields[0] == "localclient" and len(fields) > 1:
        password = fields[1]
path = os.path.join(profile, "hostlist.conf")
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w") as f:
    f.write(json.dumps({"file": 3, "format": 1}, indent=4) + json.dumps(
        {"hosts": [["omaseed0test0daemon", "127.0.0.1", daemon, "localclient", password]]}, indent=4))
PY
}

deluge_start() {
  need deluged deluge
  need deluge-web deluge
  if alive "$PIDFILE" && alive "$WEB_PIDFILE"; then
    echo "already running (pids $(cat "$PIDFILE"), $(cat "$WEB_PIDFILE")) on $BASE"
    return 0
  fi
  if ! alive "$WEB_PIDFILE" && answers "$RPC"; then
    echo "something already answers on $BASE; refusing to start (stop it first)" >&2
    exit 1
  fi
  deluge_config
  if ! alive "$PIDFILE"; then
    detach "$LOGFILE" deluged --do-not-daemonize --config "$PROFILE" --port "$DAEMON_PORT" \
      --ui-interface 127.0.0.1 --interface 127.0.0.1 --loglevel info
    echo "$!" >"$PIDFILE"
    for _ in $(seq 1 80); do
      (exec 3<>"/dev/tcp/127.0.0.1/$DAEMON_PORT") 2>/dev/null && break
      sleep 0.25
    done
  fi
  deluge_hostlist
  if ! alive "$WEB_PIDFILE"; then
    detach "$WEB_LOGFILE" deluge-web --do-not-daemonize --config "$PROFILE" --port "$PORT" \
      --interface 127.0.0.1 --loglevel info
    echo "$!" >"$WEB_PIDFILE"
  fi
  wait_answer "$RPC" "$WEB_LOGFILE"
  deluge_login || { echo "deluge-web refuses DELUGE_TEST_PASS; reset --deluge makes a new one" >&2; exit 1; }
  # default_daemon connects it at start; a deluge-web started before the daemon needs this.
  for _ in $(seq 1 40); do
    drpc web.connected | grep -q '"result": *true' && break
    drpc web.connect '["omaseed0test0daemon"]' >/dev/null || true
    sleep 0.25
  done
  local info
  info="$(drpc daemon.get_version | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"])')"
  echo "Deluge $info · deluge-web $RPC · daemon 127.0.0.1:$DAEMON_PORT · profile $PROFILE"
}

deluge_stop() {
  stop_pid "$WEB_PIDFILE"
  stop_pid "$PIDFILE"
}

deluge_status() {
  if ! alive "$PIDFILE" || ! alive "$WEB_PIDFILE"; then
    echo "not running (deluged: $(alive "$PIDFILE" && echo up || echo down), deluge-web: $(alive "$WEB_PIDFILE" && echo up || echo down))"
    return 1
  fi
  echo "running (deluged pid $(cat "$PIDFILE"), deluge-web pid $(cat "$WEB_PIDFILE"))"
  deluge_login || { echo "deluge-web refuses DELUGE_TEST_PASS" >&2; return 1; }
  drpc core.get_torrents_status '[{}, ["name","state","progress"]]' | python3 -c '
import json, sys
torrents = json.load(sys.stdin)["result"]
print(len(torrents), "torrents")
for t in torrents.values():
    print("  %-40s %-12s %5.1f%%" % (t["name"][:40], t["state"], t["progress"]))'
}

deluge_add() { # TORRENT-FILE DIR
  deluge_login
  local meta
  meta="$(base64 -w0 "$1")"
  drpc core.add_torrent_file "[\"$(basename "$1")\", \"$meta\", {\"download_location\": \"$2\"}]" >/dev/null
}

# ---- both --------------------------------------------------------------------------------------

# make_library: generic files and a .torrent of each, made once.
make_library() {
  need transmission-create transmission-cli
  mkdir -p "$LIBRARY/files" "$LIBRARY/torrents"
  local files="$LIBRARY/files"
  if [ ! -f "$files/sample-notes.txt" ]; then
    printf 'A plain text file for omaseed tests.\n%.0s' $(seq 1 64) >"$files/sample-notes.txt"
    mkdir -p "$files/Sample Album"
    for track in 01 02 03; do
      head -c $((256 * 1024)) /dev/urandom >"$files/Sample Album/track-$track.bin"
    done
    head -c $((4 * 1024 * 1024)) /dev/urandom >"$files/sample-blob.bin"
  fi
  local item
  for item in "sample-notes.txt" "Sample Album" "sample-blob.bin"; do
    [ -f "$LIBRARY/torrents/$item.torrent" ] && continue
    transmission-create --comment "omaseed test library" \
      -o "$LIBRARY/torrents/$item.torrent" "$files/$item" >/dev/null
  done
}

cmd_seed() {
  "cmd_start"
  make_library
  local file
  for file in "$LIBRARY"/torrents/*.torrent; do
    "${ENGINE}_add" "$file" "$LIBRARY/files"
    echo "added $(basename "$file" .torrent)"
  done
  sleep 2
  "${ENGINE}_status"
}

cmd_start() { "${ENGINE}_start"; }
cmd_status() { "${ENGINE}_status"; }
cmd_stop() {
  if [ "$ENGINE" = deluge ]; then deluge_stop; else stop_pid "$PIDFILE"; fi
  echo "stopped"
}
cmd_reset() {
  cmd_stop
  rm -rf "$PROFILE"
  if [ "$ENGINE" = deluge ]; then DELUGE_PASS="${DELUGE_TEST_PASS:-$(dev_env_get DELUGE_TEST_PASS)}"; fi
  cmd_start
}
cmd_log() {
  if [ "$ENGINE" = deluge ]; then tail -n 50 -f "$LOGFILE" "$WEB_LOGFILE"; else tail -n 50 -f "$LOGFILE"; fi
}

case "${1:-}" in
start) cmd_start ;;
stop) cmd_stop ;;
status) cmd_status ;;
reset) cmd_reset ;;
seed) cmd_seed ;;
log) cmd_log ;;
*) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 2 ;;
esac
