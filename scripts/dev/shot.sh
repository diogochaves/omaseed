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
# window by the time the next command runs — an explicit focus, plus a longer settle, made
# SHOT_KEYS land reliably where a bare `sleep 1` sometimes did not.
hyprctl dispatch 'hl.dsp.focus({ window = "address:'$A'" })' >/dev/null
sleep 2
# A single uppercase ASCII letter (e.g. "D", "L") is the shell's own shorthand for the
# spec's capital variant: wtype has no bare keysym for it (xkbcommon's is the lowercase
# letter), so it goes as an explicit shift+key chord instead. Everything else — "Return",
# "question", a literal lowercase key — goes through unchanged, so existing SHOT_KEYS callers
# keep working.
if [[ -n "${SHOT_KEYS:-}" ]]; then
  for k in $SHOT_KEYS; do
    if [[ "$k" =~ ^[A-Z]$ ]]; then
      lower=$(printf '%s' "$k" | tr '[:upper:]' '[:lower:]')
      wtype -M shift -k "$lower" -m shift
    else
      wtype -k "$k"
    fi
    sleep 0.3
  done
  # A key that opens an overlay (a Sheet, a Menu, a ConfirmDialog) needs a moment past the
  # logical state change for the scene graph to actually paint it — under a loaded machine
  # (several worktrees building at once) 0.7s was sometimes not enough and grim caught the
  # window still showing the plain table.
  sleep 1.5
fi
G=$(hyprctl -j clients | python3 -c "import json,sys; c=[c for c in json.load(sys.stdin) if 'omaseed' in c['class'] and c['pid'] == $pid][0]; print('%d,%d %dx%d'%(c['at'][0]-20,c['at'][1]-20,c['size'][0]+40,c['size'][1]+40))")
grim -g "$G" "$out"
hyprctl dispatch 'hl.dsp.window.close("address:'$A'")' >/dev/null
sleep 0.5; kill $pid 2>/dev/null || true
hyprctl dispatch 'hl.dsp.focus({ workspace = "2" })' >/dev/null
grep -E "theme:|demo" "$root/log" | head -3
rm -rf "$root"
