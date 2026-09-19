#!/usr/bin/env bash
# scripts/dev/shot.sh <theme> <base> <out.png> <omaseed args...>
#   env: OMASEED_BIN (default build/omaseed), SHOT_SIZE=WxH, SHOT_KEYS="j j r", SHOT_DELAY=4
# Example: OMASEED_DEMO_STATE=empty scripts/dev/shot.sh lupine 14 out.png --demo states
# Needs Hyprland (hyprctl, grim, wtype) and an empty workspace 9. Runs the given build's omaseed under a fake XDG state/config so the theme and base size are
# chosen without touching the desktop, floats it on workspace 9 and captures it with grim.
set -euo pipefail
theme=$1; base=$2; out=$3; shift 3
bin=${OMASEED_BIN:-/home/chaves/code/omarchy/omaseed/build/omaseed}
root=$(mktemp -d "${TMPDIR:-/tmp}/omaseed-shot-XXXX")
mkdir -p "$root/state/omarchy/current" "$root/config/omarchy" "$root/config/omaseed"
ln -s "/usr/share/omarchy/themes/$theme" "$root/state/omarchy/current/theme"
echo "$theme" > "$root/state/omarchy/current/theme.name"
printf '[font]\nbase-size = %s\n' "$base" > "$root/config/omarchy/shell.toml"
if pgrep -x omaseed >/dev/null; then echo "omaseed already running" >&2; exit 2; fi
XDG_STATE_HOME="$root/state" XDG_CONFIG_HOME="$root/config" QT_FORCE_STDERR_LOGGING=1 \
  "$bin" "$@" > "$root/log" 2>&1 &
pid=$!
sleep "${SHOT_DELAY:-4}"
A=$(hyprctl -j clients | python3 -c "import json,sys; print([c['address'] for c in json.load(sys.stdin) if 'omaseed' in c['class']][0])")
hyprctl dispatch 'hl.dsp.window.float("address:'$A'")' >/dev/null
hyprctl dispatch 'hl.dsp.window.move({ workspace = "9", follow = true, window = "address:'$A'" })' >/dev/null
if [[ -n "${SHOT_SIZE:-}" ]]; then hyprctl dispatch 'hl.dsp.window.resize({ x = '${SHOT_SIZE%x*}', y = '${SHOT_SIZE#*x}', window = "address:'$A'" })' >/dev/null; fi
hyprctl dispatch 'hl.dsp.window.move({ x = 40, y = 60, window = "address:'$A'" })' >/dev/null
sleep 1
if [[ -n "${SHOT_KEYS:-}" ]]; then for k in $SHOT_KEYS; do wtype -k "$k"; sleep 0.3; done; sleep 0.7; fi
G=$(hyprctl -j clients | python3 -c "import json,sys; c=[c for c in json.load(sys.stdin) if 'omaseed' in c['class']][0]; print('%d,%d %dx%d'%(c['at'][0]-20,c['at'][1]-20,c['size'][0]+40,c['size'][1]+40))")
grim -g "$G" "$out"
hyprctl dispatch 'hl.dsp.window.close("address:'$A'")' >/dev/null
sleep 0.5; kill $pid 2>/dev/null || true
hyprctl dispatch 'hl.dsp.focus({ workspace = "2" })' >/dev/null
grep -E "theme:|demo" "$root/log" | head -3
rm -rf "$root"
