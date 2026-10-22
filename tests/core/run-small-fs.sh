#!/bin/sh
# Runs a test binary with OMASEED_TEST_SMALL_FS on a 1 MiB tmpfs, mounted in a user namespace,
# so a full disk can be tested without root. Exits 77 (ctest: skipped) where user namespaces
# aren't allowed, as in some CI containers.
set -eu
unshare -rm true 2>/dev/null || exit 77
dir=$(mktemp -d)
trap 'rmdir "$dir"' EXIT
# shellcheck disable=SC2016 # expanded by the inner sh, from its own arguments
unshare -rm sh -c 'mount -t tmpfs -o size=1m omaseed-small "$1" || exit 77
OMASEED_TEST_SMALL_FS="$1" exec "$2" "$3"' sh "$dir" "$@"
