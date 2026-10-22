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
#   scripts/dev/test-server.sh rotate --strict   # a new strict login; the API key stays
#
# Two profiles, chosen with --strict as the LAST argument:
#   default  :8081  localhost auth-bypassed. Plain requests need no credential. The
#                   playground: add, delete, reset at will. A wrong Bearer token is
#                   still rejected here (403), so a valid key can be tested too.
#   --strict :8082  localhost NOT bypassed. Cookie login and Bearer keys behave exactly
#                   like a real server. The login is QBT_STRICT_USER / QBT_STRICT_PASS,
#                   from the environment, else ~/.config/omaseed/dev.env (or
#                   $OMASEED_DEV_ENV). When neither has them, `start --strict` makes a
#                   random user and password, puts them in the profile and appends them
#                   to dev.env (created 0600): no credential lives in the repo. With no
#                   QBT_STRICT_API_KEY either, it also asks the server for an API key
#                   and saves that; an existing key is never replaced. Do not reset this
#                   profile casually: the key lives in it. `rotate --strict` changes the
#                   login and keeps the key.
set -euo pipefail

# shellcheck source=scripts/dev/home-guard.sh
. "$(dirname "${BASH_SOURCE[0]}")/home-guard.sh"
# shellcheck source=scripts/dev/detach.sh
. "$(dirname "${BASH_SOURCE[0]}")/detach.sh"

STRICT=0
if [ "${!#}" = "--strict" ]; then STRICT=1; set -- "${@:1:$(($#-1))}"; fi
if [ "$STRICT" = 1 ]; then
  PROFILE="${OMASEED_STRICT_PROFILE:-$HOME/.local/share/omaseed-test-strict}"
  PORT="${OMASEED_STRICT_PORT:-8082}"
  LOCALHOST_AUTH=true
  BT_PORT="${OMASEED_STRICT_BT_PORT:-26998}"
else
  PROFILE="${OMASEED_TEST_PROFILE:-$HOME/.local/share/omaseed-test}"
  PORT="${OMASEED_TEST_PORT:-8081}"
  LOCALHOST_AUTH=false
  BT_PORT="${OMASEED_TEST_BT_PORT:-26999}"
fi
BASE="http://127.0.0.1:$PORT"
PIDFILE="$PROFILE/qbt.pid"
LOGFILE="$PROFILE/qbt.log"
CONF="$PROFILE/qBittorrent/config/qBittorrent.conf"
refuse_home "$PORT" "$BT_PORT"
DEV_ENV="${OMASEED_DEV_ENV:-$HOME/.config/omaseed/dev.env}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1 (sudo pacman -S $2)" >&2; exit 1; }; }

# random_secret: a strong random string (32 characters of A-Z a-z 0-9 - _).
random_secret() { python3 -c 'import secrets; print(secrets.token_urlsafe(24))'; }

# dev_env_get KEY: KEY's last value in dev.env, read as text; the file is never executed here.
dev_env_get() {
  [ -f "$DEV_ENV" ] || return 0
  sed -n "s/^[[:space:]]*\(export[[:space:]]\{1,\}\)\{0,1\}$1=//p" "$DEV_ENV" | tail -n 1 |
    sed -e "s/^'\(.*\)'\$/\1/" -e 's/^"\(.*\)"$/\1/'
}

# The WebUI login. Strict: QBT_STRICT_USER / QBT_STRICT_PASS from the environment, else from
# dev.env; empty until `start --strict` makes them. Playground: localhost gets in without a
# login, so its password is random, made with each new profile and never kept.
if [ "$STRICT" = 1 ]; then
  WEBUI_USER="${QBT_STRICT_USER:-$(dev_env_get QBT_STRICT_USER)}"
  WEBUI_PASS="${QBT_STRICT_PASS:-$(dev_env_get QBT_STRICT_PASS)}"
else
  WEBUI_USER="omaseed-playground"
  WEBUI_PASS=""
fi

# login USER PASSWORD JAR: a cookie session in JAR. The password reaches curl on stdin, never
# on a command line (where /proc/PID/cmdline would show it to every user). qBittorrent 5.2
# answers a good login with 204 and no body; older ones with 200 "Ok.", and a refusal is a
# 401, or a 200 "Fails." before 5.0.
login() {
  local reply
  reply="$(printf '%s' "$2" | curl -s -c "$3" -w '\n%{http_code}' --data-urlencode "username=$1" \
    --data-urlencode "password@-" "$BASE/api/v2/auth/login" 2>/dev/null)" || return 1
  case "${reply##*$'\n'}" in
  200 | 204) [ "${reply%$'\n'*}" != "Fails." ] ;;
  *) return 1 ;;
  esac
}

# api <path> [curl args…]: GET with a session cookie when strict, plain otherwise.
JAR="$PROFILE/.cookie"
api() {
  local path="$1"; shift
  if [ "$STRICT" = 1 ]; then
    login "$WEBUI_USER" "$WEBUI_PASS" "$JAR" || return 1
    curl -fs -b "$JAR" "$@" "$BASE$path"
  else
    curl -fs "$@" "$BASE$path"
  fi
}

# pbkdf2_hash: WEBUI_PASS as qBittorrent.conf stores it, base64 "salt:key". qBittorrent's
# WebUI checks PBKDF2-HMAC-SHA512 with 100000 iterations, a 16-byte salt and a 64-byte key.
# The password reaches python through its environment, not its command line.
pbkdf2_hash() {
  WEBUI_PASS="$WEBUI_PASS" python3 -c '
import base64, hashlib, os
salt = os.urandom(16)
key = hashlib.pbkdf2_hmac("sha512", os.environ["WEBUI_PASS"].encode(), salt, 100000, dklen=64)
print(base64.b64encode(salt).decode() + ":" + base64.b64encode(key).decode())'
}

# set_conf_credentials: WEBUI_USER and WEBUI_PASS into an existing profile's conf; every
# other line, the API key included, is kept. Only while the server is stopped: it rewrites
# the conf when it exits.
#
# Where they go depends on the qBittorrent that last ran the profile. Up to 5.2 the login is
# `[Preferences]` `WebUI\Username` / `WebUI\Password_PBKDF2`. 5.3 moves every WebUI key to a
# `[WebUI]` section of its own (`Username`, `Password_PBKDF2`), once, on its first start
# (migration 11, Meta/MigrationVersion), and never reads the old keys again. So a profile with
# a `[WebUI]` section takes the new keys there and loses any stale old ones; any other profile
# takes the old keys, which a 5.3 then migrates. A fresh profile (write_conf) is always in the
# old layout, which every version reads.
set_conf_credentials() {
  local hash
  hash="$(pbkdf2_hash)"
  WEBUI_USER="$WEBUI_USER" HASH="$hash" python3 - "$CONF" <<'PY'
import os, sys
path = sys.argv[1]
lines = open(path, encoding="utf-8").read().splitlines()
user, password = os.environ["WEBUI_USER"], '"@ByteArray(%s)"' % os.environ["HASH"]
old = {"WebUI\\Username": user, "WebUI\\Password_PBKDF2": password}
if "[WebUI]" in (line.strip() for line in lines):
    target, wanted, stale = "WebUI", {"Username": user, "Password_PBKDF2": password}, set(old)
else:
    target, wanted, stale = "Preferences", old, set()
out, section, done = [], None, set()

def add_missing():
    # Before the blank lines that close the section, so the file keeps its shape.
    trailing = []
    while out and not out[-1].strip():
        trailing.append(out.pop())
    for key, value in wanted.items():
        if key not in done:
            out.append(f"{key}={value}")
            done.add(key)
    out.extend(reversed(trailing))

for line in lines:
    stripped = line.strip()
    key = line.split("=", 1)[0]
    if stripped.startswith("[") and stripped.endswith("]"):
        if section == target:
            add_missing()
        section = stripped[1:-1]
    elif section == target and key in wanted:
        line = f"{key}={wanted[key]}"
        done.add(key)
    elif section == "Preferences" and key in stale:
        continue
    out.append(line)
if section == target:
    add_missing()
elif len(done) < len(wanted):
    out += ["", f"[{target}]"]
    add_missing()
tmp = path + ".omaseed-tmp"
with open(tmp, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
os.chmod(tmp, os.stat(path).st_mode & 0o777)
os.replace(tmp, path)
PY
}

# save_dev_env KEY...: each KEY (a QBT_STRICT_* name, set by the caller) into dev.env:
# replaced where its line is, appended where it is not (with QBT_STRICT_URL if that is
# missing). Every other line stays. Creates dev.env if needed and always leaves it 0600.
# Values reach python through the environment, never a command line.
save_dev_env() {
  mkdir -p "$(dirname "$DEV_ENV")"
  local name
  for name in "$@"; do export "${name?}"; done
  python3 - "$DEV_ENV" "$BASE" "$@" <<'PY'
import datetime, os, sys
path, base, names = sys.argv[1], sys.argv[2], sys.argv[3:]
values = {name: os.environ[name] for name in names}
try:
    lines = open(path, encoding="utf-8").read().splitlines()
except FileNotFoundError:
    lines = [
        "# omaseed development credentials: never commit, never paste into chat.",
        "# Load with `set -a; . ~/.config/omaseed/dev.env; set +a`.",
    ]
out, seen = [], set()
for line in lines:
    body = line.strip()
    if body.startswith("export "):
        body = body[len("export "):].lstrip()
    key = body.split("=", 1)[0] if "=" in body and not body.startswith("#") else None
    if key in values:
        if key not in seen:
            out.append(f"{key}={values[key]}")
            seen.add(key)
        continue
    if key == "QBT_STRICT_URL":
        seen.add(key)
    out.append(line)
missing = [k for k in ["QBT_STRICT_URL"] + names if k not in seen]
if missing:
    today = datetime.date.today().isoformat()
    out += ["", f"# Strict twin, made by scripts/dev/test-server.sh start --strict on {today}."]
    out += [f"{k}={base if k == 'QBT_STRICT_URL' else values[k]}" for k in missing]
tmp = path + ".omaseed-tmp"
fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
os.chmod(tmp, 0o600)
os.replace(tmp, path)
PY
}

# save_strict_credentials: WEBUI_USER / WEBUI_PASS into dev.env as QBT_STRICT_USER / _PASS.
save_strict_credentials() {
  QBT_STRICT_USER="$WEBUI_USER" QBT_STRICT_PASS="$WEBUI_PASS" save_dev_env QBT_STRICT_USER QBT_STRICT_PASS
}

# ensure_api_key: when neither the environment nor dev.env has a QBT_STRICT_API_KEY, ask the
# running strict server for one (app/rotateAPIKey, qBittorrent 5.2+) and save it to dev.env.
# A key that is already there is never replaced: this profile holds it.
ensure_api_key() {
  [ -z "${QBT_STRICT_API_KEY:-$(dev_env_get QBT_STRICT_API_KEY)}" ] || return 0
  local key
  key="$(curl -fs -b "$JAR" -X POST "$BASE/api/v2/app/rotateAPIKey" |
    python3 -c 'import json, sys; print(json.load(sys.stdin).get("apiKey", ""))' 2>/dev/null)" || key=""
  if [ -z "$key" ]; then
    echo "no API key made (qBittorrent before 5.2?): create one in the web UI and add it to $DEV_ENV as QBT_STRICT_API_KEY" >&2
    return 0
  fi
  QBT_STRICT_API_KEY="$key" save_dev_env QBT_STRICT_API_KEY
  echo "new strict API key saved to $DEV_ENV as QBT_STRICT_API_KEY"
}

# new_strict_credentials: a random user and password in WEBUI_USER / WEBUI_PASS.
new_strict_credentials() {
  WEBUI_USER="omaseed-$(python3 -c 'import secrets; print(secrets.token_hex(4))')"
  WEBUI_PASS="$(random_secret)"
}

write_conf() {
  mkdir -p "$(dirname "$CONF")" "$PROFILE/downloads"
  [ -n "$WEBUI_PASS" ] || WEBUI_PASS="$(random_secret)"
  local hash
  hash="$(pbkdf2_hash)"
  cat > "$CONF" <<EOF
[LegalNotice]
Accepted=true

[Preferences]
WebUI\\Address=127.0.0.1
WebUI\\Port=$PORT
WebUI\\Username=$WEBUI_USER
WebUI\\Password_PBKDF2="@ByteArray($hash)"
WebUI\\LocalHostAuth=$LOCALHOST_AUTH
WebUI\\CSRFProtection=false
WebUI\\HostHeaderValidation=false
General\\Locale=en

[BitTorrent]
Session\\Port=$BT_PORT
Session\\Interface=lo
Session\\InterfaceName=lo
Session\\InterfaceAddress=127.0.0.1
Session\\DHTEnabled=false
Session\\PeXEnabled=false
Session\\LSDEnabled=false
Session\\DefaultSavePath=$PROFILE/downloads
Session\\TempPathEnabled=false
Session\\QueueingSystemEnabled=true
Session\\MaxActiveDownloads=2

[Network]
PortForwardingEnabled=false
EOF
}

# ours: the server answering on $BASE saves into this profile, so it is the one we started and
# not a stranger that already held the port (the new qbittorrent-nox then fails to bind, and
# wait_up alone would happily configure whatever answered).
ours() {
  api /api/v2/app/preferences 2>/dev/null | python3 -c '
import json, os, sys
try:
    path = json.load(sys.stdin).get("save_path", "")
except ValueError:
    sys.exit(1)
sys.exit(0 if path and os.path.normpath(path) == os.path.normpath(sys.argv[1]) else 1)
' "$PROFILE/downloads"
}

is_running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

# wait_up: the WebUI answers at all. No login here: qBittorrent bans 127.0.0.1 after a few
# refused logins, and a poll loop with a wrong password would get there in two seconds.
wait_up() {
  for _ in $(seq 1 60); do
    if [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$BASE/api/v2/app/version")" != 000 ]; then
      return 0
    fi
    sleep 0.25
  done
  echo "server did not answer on $BASE within 15 s; see $LOGFILE" >&2
  return 1
}

cmd_start() {
  need qbittorrent-nox qbittorrent-nox
  if is_running; then echo "already running (pid $(cat "$PIDFILE")) on $BASE"; return 0; fi
  if curl -s -o /dev/null --max-time 2 "$BASE/"; then
    echo "something already answers on $BASE, and $PIDFILE names no live server of this profile;" \
      "refusing to start or configure it (stop it first)" >&2
    exit 1
  fi
  if [ "$STRICT" = 1 ] && { [ -z "$WEBUI_USER" ] || [ -z "$WEBUI_PASS" ]; }; then
    new_strict_credentials
    if [ -f "$CONF" ]; then set_conf_credentials; fi
    save_strict_credentials
    echo "new strict login saved to $DEV_ENV as QBT_STRICT_USER / QBT_STRICT_PASS"
  fi
  [ -f "$CONF" ] || write_conf
  # #141: none of the caller's descriptors (a flock's lock) go with it.
  detach "$LOGFILE" qbittorrent-nox --profile="$PROFILE" --webui-port="$PORT" --confirm-legal-notice
  local pid=$!
  echo "$pid" > "$PIDFILE"
  wait_up
  kill -0 "$pid" 2>/dev/null || { echo "qbittorrent-nox exited at once; see $LOGFILE" >&2; rm -f "$PIDFILE"; exit 1; }
  if [ "$STRICT" = 1 ] && ! login "$WEBUI_USER" "$WEBUI_PASS" "$JAR"; then
    echo "the strict server refuses QBT_STRICT_USER / QBT_STRICT_PASS (environment or $DEV_ENV)." \
      "Stop it, drop those two lines from $DEV_ENV and start --strict again for a new login." >&2
    exit 1
  fi
  if [ "$STRICT" = 1 ]; then ensure_api_key; fi
  if ! ours; then
    echo "the server on $BASE does not save into $PROFILE/downloads: not ours, refusing to configure it" >&2
    kill "$pid" 2>/dev/null || true
    rm -f "$PIDFILE"
    exit 1
  fi
  # Peers on loopback only, no UPnP: a test server is never reachable from the LAN. A fresh
  # profile's conf says so from the first start (write_conf); this is for older profiles.
  api /api/v2/app/setPreferences -X POST --data-urlencode \
    'json={"current_network_interface":"lo","current_interface_address":"127.0.0.1","upnp":false}' >/dev/null
  echo "qBittorrent $(api /api/v2/app/version) · API $(api /api/v2/app/webapiVersion) · $BASE · profile $PROFILE$([ "$STRICT" = 1 ] && echo ' · strict auth')"
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
  if [ "$STRICT" = 1 ] && { [ -z "$WEBUI_USER" ] || [ -z "$WEBUI_PASS" ]; }; then
    echo "no QBT_STRICT_USER / QBT_STRICT_PASS in the environment or $DEV_ENV;" \
      "start --strict makes them" >&2
  fi
  if is_running; then
    echo "running (pid $(cat "$PIDFILE"))"
    api /api/v2/app/version && echo " on $BASE" || echo "process alive but WebUI not answering"
    api /api/v2/torrents/info | python3 -c 'import json,sys; t=json.load(sys.stdin); print(len(t), "torrents")'
  else
    echo "not running"
    return 1
  fi
}

cmd_seed() {
  [ "$STRICT" = 1 ] && { echo "seed only targets the :8081 playground" >&2; exit 2; }
  is_running || cmd_start
  # Made-up hashes, added stopped: they never fetch metadata and never download anything.
  local magnets=(
    "magnet:?xt=urn:btih:4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d&dn=fixture-one"
    "magnet:?xt=urn:btih:b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0&dn=fixture-two"
    "magnet:?xt=urn:btih:c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00&dn=fixture-three"
  )
  for m in "${magnets[@]}"; do
    api /api/v2/torrents/add -X POST -F "urls=$m" -F "stopped=true" -F "category=fixtures" >/dev/null && echo "added ${m##*dn=}"
  done
  cmd_status
}

cmd_reset() {
  if [ "$STRICT" = 1 ]; then
    echo "reset --strict wipes the API key in this profile. Type YES to continue:"; read -r a; [ "$a" = YES ] || exit 2
  fi
  cmd_stop; rm -rf "$PROFILE"; cmd_start
}

# rotate --strict: a new random login on the running strict server, set through the WebUI
# with the current one, then saved to dev.env. The profile, and the API key in it, stay.
cmd_rotate() {
  [ "$STRICT" = 1 ] || { echo "rotate only applies to the strict server: rotate --strict" >&2; exit 2; }
  is_running || { echo "the strict server is not running: start --strict first" >&2; exit 1; }
  if [ -z "$WEBUI_USER" ] || [ -z "$WEBUI_PASS" ]; then
    echo "no current QBT_STRICT_USER / QBT_STRICT_PASS to sign in with" >&2; exit 1
  fi
  login "$WEBUI_USER" "$WEBUI_PASS" "$JAR" ||
    { echo "the strict server refuses the current login; nothing changed" >&2; exit 1; }
  new_strict_credentials
  WEBUI_USER="$WEBUI_USER" WEBUI_PASS="$WEBUI_PASS" python3 -c '
import json, os
print(json.dumps({"web_ui_username": os.environ["WEBUI_USER"], "web_ui_password": os.environ["WEBUI_PASS"]}))' |
    curl -fs -b "$JAR" --data-urlencode "json@-" "$BASE/api/v2/app/setPreferences" >/dev/null ||
    { echo "setPreferences failed: the old login still holds and $DEV_ENV is unchanged" >&2; exit 1; }
  save_strict_credentials
  login "$WEBUI_USER" "$WEBUI_PASS" "$JAR" ||
    { echo "the new login is saved in $DEV_ENV, but the server refuses it; see $LOGFILE" >&2; exit 1; }
  echo "strict login rotated and saved to $DEV_ENV; the API key is unchanged"
}

case "${1:-}" in
  start) cmd_start ;;
  stop) cmd_stop ;;
  status) cmd_status ;;
  reset) cmd_reset ;;
  seed) cmd_seed ;;
  rotate) cmd_rotate ;;
  log) tail -n 50 -f "$LOGFILE" ;;
  *) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 2 ;;
esac
