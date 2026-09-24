#!/usr/bin/env bash
# omaseed — install (or remove) the dev build's desktop integration files, without touching
# system directories or needing sudo: everything lands under ~/.local/share.
#
#   scripts/dev/install-desktop.sh [binary-path] [--set-default]
#   scripts/dev/install-desktop.sh --uninstall
#
# binary-path defaults to $PWD/build/omaseed. --set-default additionally registers omaseed
# as the default handler for magnet: links and .torrent files via `xdg-mime`; without it the
# .desktop and D-Bus service are installed but Diogo's existing handler is left alone. When
# --set-default is used, this script prints the previous default so it can be restored, e.g.:
#
#   scripts/dev/install-desktop.sh --uninstall
#   xdg-mime default <previous>.desktop x-scheme-handler/magnet
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app_id="io.github.diogochaves.omaseed"

apps_dir="$HOME/.local/share/applications"
services_dir="$HOME/.local/share/dbus-1/services"
icons_dir="$HOME/.local/share/icons/hicolor/scalable/apps"

desktop_dest="$apps_dir/$app_id.desktop"
service_dest="$services_dir/$app_id.service"
icon_dest="$icons_dir/$app_id.svg"

set_default=0
uninstall=0
binary="$repo/build/omaseed"

for arg in "$@"; do
    case "$arg" in
        --set-default) set_default=1 ;;
        --uninstall) uninstall=1 ;;
        -*)
            echo "install-desktop.sh: unknown option $arg" >&2
            exit 1
            ;;
        *) binary="$arg" ;;
    esac
done

if [[ $uninstall -eq 1 ]]; then
    rm -f "$desktop_dest" "$service_dest" "$icon_dest"
    echo "removed $desktop_dest"
    echo "removed $service_dest"
    echo "removed $icon_dest"
    if command -v update-desktop-database >/dev/null; then
        update-desktop-database "$apps_dir" >/dev/null 2>&1 || true
    fi
    echo "uninstalled. If --set-default was used earlier, restore the previous magnet/torrent"
    echo "handler yourself, e.g.: xdg-mime default <previous>.desktop x-scheme-handler/magnet"
    exit 0
fi

if [[ ! -x "$binary" ]]; then
    echo "install-desktop.sh: $binary is not an executable file (build it first, or pass its path)" >&2
    exit 1
fi
binary="$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")"
# The path goes unquoted into a sed replacement and into Exec= lines, where a space, &, #, %,
# a quote or a backslash would each need their own escaping (and the Desktop Entry spec's
# quoting rules differ from D-Bus's). A checkout path needs none of those, so refuse them.
if [[ ! "$binary" =~ ^[A-Za-z0-9/._+@,=:-]+$ ]]; then
    echo "install-desktop.sh: $binary has characters Exec= would need escaped; use a checkout path" \
        "made of letters, digits and / . _ + @ , = : -" >&2
    exit 1
fi

mkdir -p "$apps_dir" "$services_dir" "$icons_dir"

sed "s#^Exec=omaseed #Exec=$binary #" "$repo/data/$app_id.desktop" > "$desktop_dest"
sed "s#^Exec=omaseed\$#Exec=$binary#" "$repo/data/$app_id.service" > "$service_dest"
cp "$repo/data/icons/hicolor/scalable/apps/$app_id.svg" "$icon_dest"

echo "installed $desktop_dest (Exec=$binary %U)"
echo "installed $service_dest (Exec=$binary)"
echo "installed $icon_dest"

if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$apps_dir"
fi

if [[ $set_default -eq 1 ]]; then
    if command -v xdg-mime >/dev/null; then
        previous_magnet="$(xdg-mime query default x-scheme-handler/magnet 2>/dev/null || true)"
        previous_torrent="$(xdg-mime query default application/x-bittorrent 2>/dev/null || true)"
        echo "previous default for x-scheme-handler/magnet: ${previous_magnet:-<none>}"
        echo "previous default for application/x-bittorrent: ${previous_torrent:-<none>}"
        xdg-mime default "$app_id.desktop" x-scheme-handler/magnet application/x-bittorrent
        echo "set $app_id.desktop as the default for magnet: and .torrent"
        echo "restore afterwards with, e.g.:"
        echo "  xdg-mime default ${previous_magnet:-<previous>} x-scheme-handler/magnet"
        echo "  xdg-mime default ${previous_torrent:-<previous>} application/x-bittorrent"
    else
        echo "install-desktop.sh: xdg-mime not found, cannot set default handlers" >&2
        exit 1
    fi
fi
