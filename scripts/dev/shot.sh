#!/usr/bin/env bash
# scripts/dev/shot.sh <theme> <base> <out.png> <omaseed args...>
#   env: OMASEED_BIN (default: this checkout's build/omaseed), SHOT_SIZE=WxH, SHOT_KEYS="j j r",
#        SHOT_DELAY=4 (seconds to let the app settle before the shot sequence starts)
# Example: OMASEED_DEMO_STATE=empty scripts/dev/shot.sh lupine 14 out.png --demo states
# Needs Hyprland (hyprctl, grim, wtype) and an empty workspace 9. Runs the given build's omaseed
# under a fake XDG state/config so the theme and base size are chosen without touching the
# desktop, floats it on workspace 9 and captures it with grim.
#
# This drives the REAL desktop: the view jumps to workspace 9 while it runs, and SHOT_KEYS go
# to whatever has focus. It puts you back on the workspace you were on and closes the app
# however it exits. For anything unattended prefer the omabox walks (scripts/dev/*-walk.sh).
set -euo pipefail
theme=$1; base=$2; out=$3; shift 3
bin=${OMASEED_BIN:-"$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/omaseed"}
[[ -x "$bin" ]] || { echo "no omaseed at $bin (build first, or set OMASEED_BIN)" >&2; exit 1; }
root=$(mktemp -d "${TMPDIR:-/tmp}/omaseed-shot-XXXX")
home_ws=$(hyprctl -j activeworkspace | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
pid=""
A=""
# shellcheck disable=SC2329 # run by the EXIT trap
cleanup() {
  if [[ -n "$A" ]]; then hyprctl dispatch "hl.dsp.window.close(\"address:$A\")" >/dev/null 2>&1 || true; fi
  if [[ -n "$pid" ]]; then sleep 0.5; kill "$pid" 2>/dev/null || true; fi
  hyprctl dispatch "hl.dsp.focus({ workspace = \"$home_ws\" })" >/dev/null 2>&1 || true
  rm -rf "$root"
}
trap cleanup EXIT

mkdir -p "$root/state/omarchy/current" "$root/config/omarchy" "$root/config/omaseed"
ln -s "/usr/share/omarchy/themes/$theme" "$root/state/omarchy/current/theme"
echo "$theme" > "$root/state/omarchy/current/theme.name"
printf '[font]\nbase-size = %s\n' "$base" > "$root/config/omarchy/shell.toml"
XDG_STATE_HOME="$root/state" XDG_CONFIG_HOME="$root/config" QT_FORCE_STDERR_LOGGING=1 \
  "$bin" "$@" > "$root/log" 2>&1 &
pid=$!

# window: this process's window as "address x y w h", empty until it is mapped. Matched by
# *this* process's pid, not just the window class: several worktrees can have their own
# `--demo` instance up at once, all sharing the "io.github.diogochaves.omaseed" class, so
# picking "the first match" is a race that grabs someone else's window.
window() {
  hyprctl -j clients | python3 -c '
import json, sys
for c in json.load(sys.stdin):
    if "omaseed" in c["class"] and c["pid"] == int(sys.argv[1]):
        print(c["address"], *c["at"], *c["size"]); break
' "$pid"
}
sleep "${SHOT_DELAY:-4}"
deadline=$((SECONDS + 10)) # a loaded machine may map it later still
until [[ -n "$(window)" ]]; do
  kill -0 "$pid" 2>/dev/null || { echo "omaseed exited:" >&2; tail -5 "$root/log" >&2; exit 1; }
  (( SECONDS < deadline )) || { echo "no omaseed window after $((${SHOT_DELAY:-4} + 10)) s" >&2; exit 1; }
  sleep 0.25
done
read -r A _ <<<"$(window)"

hyprctl dispatch "hl.dsp.window.float(\"address:$A\")" >/dev/null
hyprctl dispatch "hl.dsp.window.move({ workspace = \"9\", follow = true, window = \"address:$A\" })" >/dev/null
if [[ -n "${SHOT_SIZE:-}" ]]; then
  hyprctl dispatch "hl.dsp.window.resize({ x = ${SHOT_SIZE%x*}, y = ${SHOT_SIZE#*x}, window = \"address:$A\" })" >/dev/null
fi
hyprctl dispatch "hl.dsp.window.move({ x = 40, y = 60, window = \"address:$A\" })" >/dev/null
# `follow = true` above is not always enough to leave the compositor's keyboard focus on the
# window by the time the next command runs; an explicit focus plus a longer settle makes
# SHOT_KEYS land reliably.
hyprctl dispatch "hl.dsp.focus({ window = \"address:$A\" })" >/dev/null
sleep 2
# SHOT_KEYS="ctrl+k f e d D Return": a token with `+` is a chord (every part but the last is a
# modifier held with `-M`, released with `-m` after the key), a single uppercase ASCII letter
# is shift+letter (wtype has no bare keysym for capitals), anything else goes to `wtype -k`.
if [[ -n "${SHOT_KEYS:-}" ]]; then
  for k in $SHOT_KEYS; do
    if [[ "$k" == *+* ]]; then
      mods=${k%+*}; key=${k##*+}
      args=()
      IFS='+' read -ra modlist <<<"$mods"
      for m in "${modlist[@]}"; do args+=(-M "$m"); done
      args+=(-k "$key")
      for m in "${modlist[@]}"; do args+=(-m "$m"); done
      wtype "${args[@]}"
    elif [[ "$k" =~ ^[A-Z]$ ]]; then
      lower=$(printf '%s' "$k" | tr '[:upper:]' '[:lower:]')
      wtype -M shift -k "$lower" -m shift
    else
      wtype -k "$k"
    fi
    sleep 0.3
  done
  # An overlay needs a moment past the state change to be painted on a loaded machine.
  sleep 1.5
fi
read -r _ x y w h <<<"$(window)"
grim -g "$((x - 20)),$((y - 20)) $((w + 40))x$((h + 40))" "$out"
grep -E "theme:|demo" "$root/log" | head -3 || true
