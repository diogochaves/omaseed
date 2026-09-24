#!/usr/bin/env bash
# scripts/dev/home-guard.sh: every dev script refuses port 8080 (the real library) before it
# does anything. Runs no server and sends no request: the refusals below happen before the
# scripts get that far, and the one "status" call uses a port and profile nobody listens on.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/dev/home-guard.sh
. "$repo/scripts/dev/home-guard.sh"

fail=0
expect() { # expect home|ok TARGET
    local got=ok
    if is_home "$2"; then got=home; fi
    if [ "$got" != "$1" ]; then
        echo "FAIL: is_home '$2' said $got, expected $1"
        fail=1
    fi
}

expect home 8080
expect home 08080
expect home " 8080 "
expect home http://127.0.0.1:8080
expect home http://127.0.0.1:8080/
expect home http://localhost:8080/api/v2
expect home "http://[::1]:8080"
expect home https://nas.lan:8080
expect home 127.0.0.1:8080
expect home "http://127.0.0.1:8080?x=1"
expect ok 8081
expect ok 8082
expect ok 18080
expect ok 80801
expect ok http://127.0.0.1:8081/api/v2
expect ok http://127.0.0.1:80801
expect ok "http://[::1]"
expect ok http://127.0.0.1
expect ok ""

# refuses: SCRIPT ENV... -- ARGS: the script exits 2 and names the refusal.
refuses() {
    local name="$1" out rc=0; shift
    local envs=()
    while [ "$1" != -- ]; do envs+=("$1"); shift; done
    shift
    out="$(env "${envs[@]}" "$repo/scripts/dev/$name" "$@" 2>&1)" || rc=$?
    if [ "$rc" != 2 ] || ! grep -q "refusing .*8080" <<<"$out"; then
        echo "FAIL: $name ${envs[*]} $* was not refused (rc $rc): $out"
        fail=1
    fi
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
refuses test-server.sh OMASEED_TEST_PORT=8080 OMASEED_TEST_PROFILE="$scratch/p" -- status
refuses test-server.sh OMASEED_TEST_PORT=8080 OMASEED_TEST_PROFILE="$scratch/p" -- start
refuses test-server.sh OMASEED_STRICT_PORT=8080 OMASEED_STRICT_PROFILE="$scratch/s" -- start --strict
refuses test-server.sh OMASEED_TEST_BT_PORT=8080 OMASEED_TEST_PROFILE="$scratch/p" -- status
refuses swarm.sh OMASEED_TEST_PORT=8080 OMASEED_SWARM_PROFILE="$scratch/w" -- status
for walk in swarm-walk.sh notify-walk.sh magnet-walk.sh; do
    refuses "$walk" OMASEED_TEST_PORT=8080 -- --help
done
refuses strict-walk.sh OMASEED_STRICT_PORT=8080 -- --help
[ -d "$scratch/p" ] && { echo "FAIL: a refused test-server.sh created its profile"; fail=1; }

# Not running is not success: strict-walk relies on `status || start`.
rc=0
OMASEED_TEST_PORT=18089 OMASEED_TEST_PROFILE="$scratch/none" \
    "$repo/scripts/dev/test-server.sh" status >/dev/null 2>&1 || rc=$?
[ "$rc" = 1 ] || { echo "FAIL: status of a stopped server exited $rc, expected 1"; fail=1; }

[ "$fail" = 0 ] && echo "home guard: all checks passed"
exit "$fail"
