#!/usr/bin/env bash
# data/hooks/theme-set.d/omaseed against a private session bus whose service directory can
# D-Bus-activate io.github.diogochaves.omaseed (as the installed package's can). With omaseed
# closed the hook must neither start it nor hold the theme switch up.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
command -v dbus-daemon >/dev/null || { echo "dbus-daemon is not installed"; exit 77; }
command -v busctl >/dev/null || { echo "busctl is not installed"; exit 77; }

# Short: a unix socket path is limited to 108 bytes. The policy below is the stock session
# bus one; a narrower one left busctl waiting for replies it was not allowed to receive.
dir="$(mktemp -d -p "${XDG_RUNTIME_DIR:-/tmp}" omaseed-hook.XXXXXX)"
pid=""
# shellcheck disable=SC2329 # run by the EXIT trap
cleanup() {
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT

mkdir "$dir/services"
cat >"$dir/services/io.github.diogochaves.omaseed.service" <<EOF
[D-BUS Service]
Name=io.github.diogochaves.omaseed
Exec=/usr/bin/touch $dir/activated
EOF
cat >"$dir/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=$dir/bus</listen>
  <servicedir>$dir/services</servicedir>
  <policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy>
</busconfig>
EOF
pid="$(dbus-daemon --config-file="$dir/bus.conf" --fork --print-pid)"
for _ in $(seq 1 20); do [ -S "$dir/bus" ] && break; sleep 0.1; done

start=$(date +%s%N)
rc=0
DBUS_SESSION_BUS_ADDRESS="unix:path=$dir/bus" timeout 10 \
    "$repo/data/hooks/theme-set.d/omaseed" test-theme || rc=$?
elapsed=$((($(date +%s%N) - start) / 1000000))
sleep 0.5 # an activation, had there been one, has run by now

fail=0
[ "$rc" = 0 ] || { echo "FAIL: the hook exited $rc (124: it blocked until timeout killed it)"; fail=1; }
[ "$elapsed" -lt 3000 ] || { echo "FAIL: the hook took ${elapsed} ms"; fail=1; }
[ ! -e "$dir/activated" ] || { echo "FAIL: the hook D-Bus-activated omaseed"; fail=1; }
[ "$fail" = 0 ] && echo "theme hook: returned in ${elapsed} ms, nothing activated"
exit "$fail"
