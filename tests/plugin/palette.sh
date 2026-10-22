#!/usr/bin/env bash
# The bar widget reads the shell's palette as Commons.Color (#109): Qt 6.12's own Color type
# hides a bare `Color`, and every colour read through it comes out undefined.
set -euo pipefail

dir="${1:?usage: palette.sh PLUGIN_DIR}"

if grep -nE '(^|[^.[:alnum:]_])Color\.' "$dir"/*.qml; then
    echo "bare Color.* above: use Commons.Color (import qs.Commons as Commons)" >&2
    exit 1
fi
