#!/bin/bash
# Install omaseed's Omarchy integration files for local development (T1.8).
#
# Installs, always:
#   - the theme-set hook  -> ~/.config/omarchy/hooks/theme-set.d/omaseed
#   - the Hyprland module -> ~/.config/hypr/omaseed.lua
#
# Neither is auto-loaded by Omarchy on its own:
#   - the hook runs automatically once it exists (Omarchy calls every executable file in
#     .../theme-set.d/), so nothing else is needed for it;
#   - the Hyprland module is NOT auto-required. This script prints the one line you add to
#     ~/.config/hypr/hyprland.lua yourself; it never edits that file for you (see CLAUDE.md
#     "no sudo" / don't touch the user's own configs silently).
#
# --menu        also append the "torrents" row from data/omarchy-menu.jsonc into
#               ~/.config/omarchy/extensions/omarchy-menu.jsonc, but only if the "torrents"
#               id is not already present there. That file hot-reloads on save.
# --uninstall   remove everything this script installs (hook, hypr module, and -- only if
#               present -- the menu row this script itself appended).
#
# The packaged (PKGBUILD) copies live under ${CMAKE_INSTALL_DATADIR}/omaseed/ once T5.1
# lands; this script is for developers running out of a source checkout and always reads
# from data/ next to this script, not from an install prefix.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
repo_root="$(cd -- "${script_dir}/../.." &>/dev/null && pwd)"
data_dir="${repo_root}/data"

hook_src="${data_dir}/hooks/theme-set.d/omaseed"
hook_dest_dir="${HOME}/.config/omarchy/hooks/theme-set.d"
hook_dest="${hook_dest_dir}/omaseed"

hypr_src="${data_dir}/hyprland/omaseed.lua"
hypr_dest="${HOME}/.config/hypr/omaseed.lua"

menu_src="${data_dir}/omarchy-menu.jsonc"
menu_dest="${HOME}/.config/omarchy/extensions/omarchy-menu.jsonc"
menu_marker="\"torrents\":"
menu_comment="omaseed T1.8 (scripts/dev/install-omarchy-extras.sh --menu)"

mode="install"
do_menu=0
for arg in "$@"; do
    case "${arg}" in
        --uninstall) mode="uninstall" ;;
        --menu) do_menu=1 ;;
        -h | --help)
            sed -n '2,/^set -e/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "unknown argument: ${arg}" >&2
            exit 1
            ;;
    esac
done

install_hook() {
    mkdir -p "${hook_dest_dir}"
    install -m 0755 "${hook_src}" "${hook_dest}"
    echo "installed hook:   ${hook_dest}"
}

uninstall_hook() {
    if [[ -e "${hook_dest}" ]]; then
        rm -f "${hook_dest}"
        echo "removed hook:     ${hook_dest}"
    fi
}

install_hypr() {
    mkdir -p "$(dirname -- "${hypr_dest}")"
    ln -sf "${hypr_src}" "${hypr_dest}"
    echo "installed rule:   ${hypr_dest} -> ${hypr_src}"
    echo
    echo "Not auto-loaded. Add this line to ~/.config/hypr/hyprland.lua, next to the other"
    echo "require(\"hypr...\") lines (it is not added for you):"
    echo
    echo "    require(\"hypr.omaseed\")"
}

uninstall_hypr() {
    if [[ -L "${hypr_dest}" || -e "${hypr_dest}" ]]; then
        rm -f "${hypr_dest}"
        echo "removed rule:     ${hypr_dest}"
    fi
    echo "(if you added \"require(\\\"hypr.omaseed\\\")\" to hyprland.lua, remove that line too)"
}

# Appends the row from data/omarchy-menu.jsonc just before the file's final closing brace,
# but only when the "torrents" id is not already present. Deliberately not a JSON parse
# (the file is JSONC, with comments and trailing commas) -- a plain text append matches how
# Diogo's own file documents extending it (see the comment block at its top).
install_menu() {
    if [[ ! -f "${menu_dest}" ]]; then
        echo "menu file not found, skipping --menu: ${menu_dest}" >&2
        return
    fi
    if grep -qF "${menu_marker}" "${menu_dest}"; then
        echo "menu row already present, leaving ${menu_dest} untouched"
        return
    fi
    local row
    row=$(grep -F "${menu_marker}" "${menu_src}")
    local tmp
    tmp="$(mktemp)"
    # Insert the row (with a marker comment) before the file's last line, which is the
    # closing "}". Every row in this file already ends its own line with a comma, so a row
    # inserted immediately before the closing brace is always valid trailing-comma JSONC.
    head -n -1 "${menu_dest}" > "${tmp}"
    {
        echo "  // ---- ${menu_comment} ----"
        echo "  ${row}"
        tail -n 1 "${menu_dest}"
    } >> "${tmp}"
    # Written through the existing file (not mv): its inode, mode and any symlink stay the
    # user's; mktemp's 0600 would otherwise replace their permissions.
    cat "${tmp}" > "${menu_dest}"
    rm -f "${tmp}"
    echo "appended menu row to: ${menu_dest}"
}

# Removes the marker comment and the one row right after it (the row install_menu wrote),
# nothing else: a "torrents" row the user added themselves elsewhere in the file stays.
uninstall_menu() {
    if [[ -f "${menu_dest}" ]] && grep -qF "${menu_comment}" "${menu_dest}"; then
        local tmp
        tmp="$(mktemp)"
        awk -v comment="${menu_comment}" -v marker="${menu_marker}" '
            skip && index($0, marker) { skip = 0; next }
            { skip = 0 }
            index($0, comment) { skip = 1; next }
            { print }
        ' "${menu_dest}" > "${tmp}"
        cat "${tmp}" > "${menu_dest}"
        rm -f "${tmp}"
        echo "removed menu row from: ${menu_dest}"
    fi
}

if [[ "${mode}" == "uninstall" ]]; then
    uninstall_hook
    uninstall_hypr
    uninstall_menu
else
    install_hook
    install_hypr
    if [[ "${do_menu}" -eq 1 ]]; then
        install_menu
    else
        echo
        echo "Menu row not installed (pass --menu to append it to ${menu_dest})."
    fi
fi
