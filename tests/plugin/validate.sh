#!/usr/bin/env bash
# Runs Omarchy's own plugin validator over the bar widget. Exit 77 (skip) where Omarchy is
# not installed.
set -euo pipefail

dir="${1:?usage: validate.sh PLUGIN_DIR}"

if command -v omarchy-plugin-validate >/dev/null 2>&1; then
    exec omarchy-plugin-validate "$dir"
fi
if command -v omarchy >/dev/null 2>&1; then
    exec omarchy plugin validate "$dir"
fi
echo "omarchy is not installed: skipping the plugin validator"
exit 77
