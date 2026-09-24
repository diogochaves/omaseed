#!/usr/bin/env bash
# scripts/dev/test-server.sh --strict makes its own login: with no QBT_STRICT_USER /
# QBT_STRICT_PASS anywhere, `start --strict` generates them, puts them in the profile and in
# dev.env (0600); a restart reuses them; `rotate --strict` replaces them through the WebUI;
# dropping them from dev.env makes new ones for the existing profile; with no API key, start
# makes one, which neither a restart nor a new login replaces. And none of it is
# printed. Everything is in a scratch directory with its own qbittorrent-nox on a free port:
# the :8081/:8082 servers and ~/.config/omaseed/dev.env are never touched.
set -euo pipefail

for tool in qbittorrent-nox curl python3; do
    command -v "$tool" >/dev/null || { echo "$tool is not installed"; exit 77; }
done

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-strict-creds.XXXXXX")"
free_port() {
    python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
port="$(free_port)"
bt_port="$(free_port)"
unset QBT_STRICT_USER QBT_STRICT_PASS QBT_STRICT_API_KEY QBT_STRICT_URL
export OMASEED_STRICT_PROFILE="$scratch/profile"
export OMASEED_STRICT_PORT="$port"
export OMASEED_STRICT_BT_PORT="$bt_port"
export OMASEED_DEV_ENV="$scratch/config/dev.env"
base="http://127.0.0.1:$port"
server() { "$repo/scripts/dev/test-server.sh" "$@" --strict; }
# shellcheck disable=SC2329 # run by the EXIT trap
cleanup() {
    server stop >/dev/null 2>&1 || true
    rm -rf "$scratch"
}
trap cleanup EXIT

fail=0
check() { # check DESCRIPTION COMMAND...
    if "${@:2}"; then echo "ok: $1"; else echo "FAIL: $1"; fail=1; fi
}
get() { sed -n "s/^$1=//p" "$OMASEED_DEV_ENV" | tail -n 1; }
# accepts USER PASSWORD: the server signs the login in (204 on qBittorrent 5.2, 200 "Ok."
# before). The password goes on stdin.
# shellcheck disable=SC2329 # run through check
accepts() {
    local reply
    reply="$(printf '%s' "$2" | curl -s -w '\n%{http_code}' --data-urlencode "username=$1" \
        --data-urlencode "password@-" "$base/api/v2/auth/login")"
    case "${reply##*$'\n'}" in
    200 | 204) [ "${reply%$'\n'*}" != "Fails." ] ;;
    *) return 1 ;;
    esac
}
# shellcheck disable=SC2329 # run through check
refuses() { ! accepts "$@"; }
# bearer_works KEY: the key signs a request in. It reaches curl on stdin, as a config line.
# shellcheck disable=SC2329 # run through check
bearer_works() {
    printf 'header = "Authorization: Bearer %s"\n' "$1" |
        curl -fs -o /dev/null -K - "$base/api/v2/app/version"
}
# shellcheck disable=SC2329 # run through check
mode_is_600() { [ "$(stat -c %a "$OMASEED_DEV_ENV")" = 600 ]; }
# shellcheck disable=SC2329 # run through check
never_printed() { # never_printed LOG SECRET
    ! grep -qF -- "$2" "$1"
}

echo "==> start --strict with no login anywhere"
server start >"$scratch/start1.log" 2>&1 || { cat "$scratch/start1.log"; exit 1; }
user1="$(get QBT_STRICT_USER)"
pass1="$(get QBT_STRICT_PASS)"
key1="$(get QBT_STRICT_API_KEY)"
check "dev.env was created" test -f "$OMASEED_DEV_ENV"
check "dev.env is 0600" mode_is_600
check "a user was generated" test -n "$user1"
check "a password of 32 characters was generated" test "${#pass1}" -ge 32
check "QBT_STRICT_URL names the server" test "$(get QBT_STRICT_URL)" = "$base"
check "the server accepts the generated login" accepts "$user1" "$pass1"
check "the server refuses a wrong password" refuses "$user1" "not-$pass1"
check "start printed no password" never_printed "$scratch/start1.log" "$pass1"
check "an API key was made" test -n "$key1"
check "the API key signs requests in" bearer_works "$key1"
check "start printed no API key" never_printed "$scratch/start1.log" "$key1"

echo "==> a restart reuses the login"
server stop >/dev/null
server start >"$scratch/start2.log" 2>&1 || { cat "$scratch/start2.log"; exit 1; }
check "dev.env keeps the user" test "$(get QBT_STRICT_USER)" = "$user1"
check "dev.env keeps the password" test "$(get QBT_STRICT_PASS)" = "$pass1"
check "the server still accepts it" accepts "$user1" "$pass1"
check "the API key is unchanged" test "$(get QBT_STRICT_API_KEY)" = "$key1"

echo "==> rotate --strict"
printf '# a line of the contributor\nQBT_TEST_URL=http://127.0.0.1:8081\n' >>"$OMASEED_DEV_ENV"
server rotate >"$scratch/rotate.log" 2>&1 || { cat "$scratch/rotate.log"; exit 1; }
user2="$(get QBT_STRICT_USER)"
pass2="$(get QBT_STRICT_PASS)"
check "the password changed" test "$pass2" != "$pass1"
check "the server accepts the new login" accepts "$user2" "$pass2"
check "the server refuses the old password" refuses "$user1" "$pass1"
check "other dev.env lines are kept" grep -qx 'QBT_TEST_URL=http://127.0.0.1:8081' "$OMASEED_DEV_ENV"
check "the API key is unchanged" test "$(get QBT_STRICT_API_KEY)" = "$key1"
check "the API key still signs requests in" bearer_works "$key1"
check "each key is written once" test "$(grep -c '^QBT_STRICT_PASS=' "$OMASEED_DEV_ENV")" = 1
check "dev.env is still 0600" mode_is_600
check "rotate printed no password" never_printed "$scratch/rotate.log" "$pass2"

echo "==> an existing profile gets a new login when dev.env has none"
server stop >/dev/null
sed -i '/^QBT_STRICT_USER=/d; /^QBT_STRICT_PASS=/d' "$OMASEED_DEV_ENV"
server start >"$scratch/start3.log" 2>&1 || { cat "$scratch/start3.log"; exit 1; }
user3="$(get QBT_STRICT_USER)"
pass3="$(get QBT_STRICT_PASS)"
check "a new password was generated" test -n "$pass3" -a "$pass3" != "$pass2"
check "the existing profile accepts it" accepts "$user3" "$pass3"
check "the existing profile refuses the previous one" refuses "$user2" "$pass2"
check "the API key survives the new login" bearer_works "$key1"
check "dev.env is still 0600" mode_is_600

exit "$fail"
