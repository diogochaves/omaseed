#!/usr/bin/env bash
# omaseed — a skin under every theme, in a desktop nobody sees (DECISIONS C37 U7, #226).
# A scenario for omabox's visual check (omabox.app): run it through `omabox check run`, never
# by hand.
#
#   omabox check run --approve --base DIR -- /usr/share/omaseed/agents/skin-check.sh warm
#       the first run: three fresh boxes, the baseline kept in DIR. Read the baseline.png it
#       prints: one picture per skin and theme.
#   omabox check run --base DIR -- /usr/share/omaseed/agents/skin-check.sh warm
#       after an edit: `same ID`, or `changed ID` with where; read the report.png it prints.
#
# SKIN…: skins in ~/.config/omaseed/skins/ (each one's skin.json is copied into the box), or
# omarchy and console. None: every skin there. Each checkpoint is SKIN.THEME (omabox's IDs
# take letters, digits, . _ -; the theme is after the last dot).
# OMASEED_CHECK_THEMES: the themes, separated by spaces or commas (`tokyo-night
# catppuccin-latte`, one dark and one light, for a quick pass). Unset: every theme the box has.
# A theme of yours in ~/.config/omarchy/themes/ that the box lacks is copied in when named.
# `--keep fonts` on `check run` draws your own fonts (a skin naming a family you installed).
#
# omaseed runs as `omaseed --demo table`: invented torrents, no server and nothing of the
# person's library, in a box with no network. Nothing is written outside the box.
# OMASEED_BUILD: another omaseed to run (a build's binary, or the folder holding it), which
# the box must see: inside the folder `omabox check run` starts from.
set -euo pipefail

die() { echo "skin-check.sh: $*" >&2; exit 1; }
[ -n "${OMABOX:-}" ] || die "run it through omabox check run (see the top of this file)"

omaseed="${OMASEED_BUILD:-omaseed}"
[ -n "${OMASEED_BUILD:-}" ] && [ -d "$omaseed" ] && omaseed="$omaseed/omaseed"
case "$omaseed" in
*/*) [ -x "$omaseed" ] || die "no omaseed at $omaseed"; omaseed="$(realpath "$omaseed")" ;;
*) command -v omaseed >/dev/null || die "omaseed isn't installed (or set OMASEED_BUILD)" ;;
esac

APP="class:io.github.diogochaves.omaseed"
skins_dir="${XDG_CONFIG_HOME:-$HOME/.config}/omaseed/skins"
themes_dir="$HOME/.config/omarchy/themes"
home="$(omabox path --home)"

# ---- the skins ----------------------------------------------------------------------------------
skins=("$@")
if [ ${#skins[@]} -eq 0 ]; then
    for f in "$skins_dir"/*/skin.json; do
        [ -f "$f" ] && skins+=("$(basename "$(dirname "$f")")")
    done
    [ ${#skins[@]} -gt 0 ] || die "no skins in $skins_dir: name one (omaseed skin list lists them)"
fi
mkdir -p "$home/.config/omaseed/skins"
chmod 700 "$home/.config/omaseed"
for skin in "${skins[@]}"; do
    case "$skin" in
    omarchy | console) continue ;;
    */* | .*) die "$skin isn't a skin's name" ;;
    esac
    [ -f "$skins_dir/$skin/skin.json" ] || die "no skin called $skin in $skins_dir"
    # Only skin.json: files in faces/ never load (C37 U2), and nothing else in the folder is read.
    mkdir -p "$home/.config/omaseed/skins/$skin"
    cp -L "$skins_dir/$skin/skin.json" "$home/.config/omaseed/skins/$skin/skin.json"
done

# A skin that doesn't draw would be checked as Omarchy's: say why and stop (the scenario's log).
# check runs offscreen with no bus and writes only its picture, in the box's cache.
for skin in "${skins[@]}"; do
    if ! out="$(omabox run -- "$omaseed" check "$skin" 2>/dev/null)"; then
        printf '%s\n' "$out" >&2
        die "$skin doesn't draw as itself: the lines above say why"
    fi
done

# ---- the themes ---------------------------------------------------------------------------------
themes=()
if [ -n "${OMASEED_CHECK_THEMES:-}" ]; then
    read -r -a themes <<<"${OMASEED_CHECK_THEMES//,/ }"
    mkdir -p "$home/.config/omarchy/themes"
    for theme in "${themes[@]}"; do
        case "$theme" in */* | .*) die "$theme isn't a theme's name" ;; esac
        # shellcheck disable=SC2016 # expanded by the box's sh
        if ! omabox run -- sh -c '[ -d "$HOME/.config/omarchy/themes/$1" ] || [ -d "$OMARCHY_PATH/themes/$1" ]' sh "$theme"; then
            [ -d "$themes_dir/$theme" ] || die "no theme called $theme (omarchy-theme-list lists them)"
            cp -rL "$themes_dir/$theme" "$home/.config/omarchy/themes/$theme"
        fi
    done
else
    # shellcheck disable=SC2016 # expanded by the box's sh
    mapfile -t themes < <(omabox run -- sh -c 'find "$HOME/.config/omarchy/themes/" "$OMARCHY_PATH/themes/" \
        -mindepth 1 -maxdepth 1 \( -type d -o -type l \) -printf "%f\n" 2>/dev/null | sort -u')
    [ ${#themes[@]} -gt 0 ] || die "the box has no Omarchy themes"
fi

# ---- each theme, each skin ----------------------------------------------------------------------
# omaseed closed while the theme changes, and started again for each skin: each picture is a
# first start under that theme, as the person's next start would be.
for theme in "${themes[@]}"; do
    omabox run -- omarchy-theme-set "$theme" >/dev/null 2>&1 || die "omarchy-theme-set $theme failed in the box"
    for skin in "${skins[@]}"; do
        # run -d prints the job's pid first, then what --wait saw.
        job="$(omabox run -d --wait -- env OMASEED_SKIN="$skin" OMASEED_DISPLAY="${OMASEED_DISPLAY:-rows}" \
            "$omaseed" --demo table)"
        job="${job%%$'\n'*}"
        omabox wait --timeout 20s window "$APP" >/dev/null
        omabox check point "$skin.$theme" --window "$APP" \
            --expect "omaseed's library drawn in the $skin skin under $theme" >/dev/null
        omabox stop "$job" >/dev/null
        omabox wait --timeout 15s window "$APP" --gone >/dev/null
    done
done
