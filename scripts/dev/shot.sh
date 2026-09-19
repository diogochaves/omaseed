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
XDG_STATE_HOME="$root/state" XDG_CONFIG_HOME="$root/config" QT_FORCE_STDERR_LOGGING=1 \
  "$bin" "$@" > "$root/log" 2>&1 &
pid=$!
sleep "${SHOT_DELAY:-4}"
# Match by *this* process's pid, not just the window class: several worktrees can have their
# own `--demo` instance up at once (their own agent, their own shot.sh), all sharing the same
# "io.github.diogochaves.omaseed" class, so picking "the first match" is a race that grabs
# someone else's window.
A=$(hyprctl -j clients | python3 -c "import json,sys; print([c['address'] for c in json.load(sys.stdin) if 'omaseed' in c['class'] and c['pid'] == $pid][0])")
hyprctl dispatch 'hl.dsp.window.float("address:'$A'")' >/dev/null
hyprctl dispatch 'hl.dsp.window.move({ workspace = "9", follow = true, window = "address:'$A'" })' >/dev/null
if [[ -n "${SHOT_SIZE:-}" ]]; then hyprctl dispatch 'hl.dsp.window.resize({ x = '${SHOT_SIZE%x*}', y = '${SHOT_SIZE#*x}', window = "address:'$A'" })' >/dev/null; fi
hyprctl dispatch 'hl.dsp.window.move({ x = 40, y = 60, window = "address:'$A'" })' >/dev/null
# `follow = true` above is not always enough to leave the compositor's keyboard focus on the
# window by the time the next command runs; an explicit focus plus a longer settle makes
# SHOT_KEYS land reliably.
hyprctl dispatch 'hl.dsp.focus({ window = "address:'$A'" })' >/dev/null
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
G=$(hyprctl -j clients | python3 -c "import json,sys; c=[c for c in json.load(sys.stdin) if 'omaseed' in c['class'] and c['pid'] == $pid][0]; print('%d,%d %dx%d'%(c['at'][0]-20,c['at'][1]-20,c['size'][0]+40,c['size'][1]+40))")
grim -g "$G" "$out"
hyprctl dispatch 'hl.dsp.window.close("address:'$A'")' >/dev/null
sleep 0.5; kill $pid 2>/dev/null || true
hyprctl dispatch 'hl.dsp.focus({ workspace = "2" })' >/dev/null
grep -E "theme:|demo" "$root/log" | head -3
rm -rf "$root"
