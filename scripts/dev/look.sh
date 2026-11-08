#!/usr/bin/env bash
# scripts/dev/look.sh DIR [PX]
#   A copy of each DIR/*.png scaled to PX (default 1280) on its longest side, in DIR/look/, for
#   an agent to read: a full window shot (1854x1056) costs about twice the tokens of its
#   1280 px copy (#251). The full-size shots stay as they are, for the evidence and for a
#   detail the copy blurs (or `omabox shot -g` on that part). Shots already that small are
#   copied as they are; a copy newer than its shot is kept. Prints one line saying where they
#   are. Every walk runs it on its OUT when it ends, passed or failed.
#
#   Needs ImageMagick (`magick`, which Omarchy ships); without it says so and does nothing.
set -euo pipefail

dir="${1:?usage: look.sh DIR [PX]}"
px="${2:-1280}"
[ -d "$dir" ] || exit 0

shopt -s nullglob
shots=("$dir"/*.png)
[ ${#shots[@]} -gt 0 ] || exit 0

if ! command -v magick >/dev/null 2>&1; then
    echo "look: no ImageMagick (magick), so no ${px} px copies; the shots in $dir are full size" >&2
    exit 0
fi

mkdir -p "$dir/look"
for shot in "${shots[@]}"; do
    copy="$dir/look/$(basename "$shot")"
    [ "$copy" -nt "$shot" ] && continue
    # `>`: only shrinks, never enlarges a small shot.
    magick "$shot" -resize "${px}x${px}>" "$copy"
done
echo "look: Read the ${px} px copies in $dir/look/ (full size beside them, only for a detail)"
