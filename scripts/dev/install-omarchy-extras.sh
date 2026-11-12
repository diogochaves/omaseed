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
# --menu        also write omaseed's block (data/omarchy-menu.jsonc, from "// >>> omaseed" to
#               "// <<< omaseed") into ~/.config/omarchy/extensions/omarchy-menu.jsonc, the
#               rule omaseed itself follows (docs/DECISIONS.md C36 M13): in place of the block
#               already there, else before the file's last "}", replacing the old one-line
#               "torrents" row; a Torrents row that isn't omaseed's leaves the file alone.
#               That file hot-reloads on save.
# --uninstall   remove everything this script installs (hook, hypr module, and omaseed's
#               block in the menu file, marker to marker).
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
# C36 M13: the markers OmarchyMenuFile (src/core) reads and writes; the block is copied from
# data/omarchy-menu.jsonc, marker to marker.
menu_open="// >>> omaseed"
menu_close="// <<< omaseed"
# What the pre-C36 --menu wrote above its one-line row.
old_menu_comment="omaseed T1.8 (scripts/dev/install-omarchy-extras.sh --menu)"
omarchy_menu="${OMARCHY_PATH:-/usr/share/omarchy}/default/omarchy/omarchy-menu.jsonc"

# data/omarchy-menu.jsonc from its opening marker to its closing one, both included.
menu_block() {
    awk -v o="${menu_open}" -v c="${menu_close}" '
        { t = $0; sub(/^[[:space:]]+/, "", t) }
        index(t, o) == 1 { on = 1 }
        on { print }
        on && index(t, c) == 1 { exit }
    ' "${menu_src}"
}

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

# True when the file has exactly one opening and one closing marker, in that order.
menu_block_paired() {
    awk -v o="${menu_open}" -v c="${menu_close}" '
        { t = $0; sub(/^[[:space:]]+/, "", t) }
        index(t, o) == 1 { opens++; if (!open_at) open_at = NR }
        index(t, c) == 1 { closes++; if (!close_at) close_at = NR }
        END { exit !(opens == 1 && closes == 1 && open_at < close_at) }
    ' "$1"
}

# True when the file has any marker at all.
menu_has_marker() {
    grep -qE "^[[:space:]]*(${menu_open}|${menu_close})" "$1"
}

# The pre-C36 first-page row: a "torrents" line whose action runs omaseed.
is_old_row() {
    [[ "$1" == *'"action"'* && "$1" == *omaseed* ]]
}

# Writes omaseed's block (C36 M13, the same markers and block OmarchyMenuFile writes) into
# the menu file: in place of the block already there, or before the file's final closing
# brace, or as a new file holding only it when Omarchy's menu is installed. Deliberately not
# a JSON parse (the file is JSONC, with comments and trailing commas): every byte outside the
# block stays, but for the comma a last entry may need.
install_menu() {
    local block
    block="$(menu_block)"
    if [[ -z "${block}" ]]; then
        echo "no block between the markers in ${menu_src}" >&2
        return
    fi
    if [[ ! -f "${omarchy_menu}" ]]; then
        echo "Omarchy's menu is not installed (${omarchy_menu}), skipping --menu" >&2
        return
    fi
    if [[ ! -e "${menu_dest}" ]]; then
        mkdir -p "$(dirname -- "${menu_dest}")"
        printf '{\n%s\n}\n' "${block}" >"${menu_dest}"
        echo "created menu file with omaseed's block: ${menu_dest}"
        return
    fi

    local tmp
    tmp="$(mktemp)"
    if menu_has_marker "${menu_dest}"; then
        if ! menu_block_paired "${menu_dest}"; then
            rm -f "${tmp}"
            echo "omaseed's markers don't pair in ${menu_dest}, leaving it untouched" >&2
            return
        fi
        menu_block="${block}" awk -v o="${menu_open}" -v c="${menu_close}" '
            { t = $0; sub(/^[[:space:]]+/, "", t) }
            index(t, o) == 1 { print ENVIRON["menu_block"]; skip = 1; next }
            skip && index(t, c) == 1 { skip = 0; next }
            !skip { print }
        ' "${menu_dest}" >"${tmp}"
        cat "${tmp}" >"${menu_dest}"
        rm -f "${tmp}"
        echo "rewrote omaseed's block in: ${menu_dest}"
        return
    fi

    # A Torrents row that isn't omaseed's is the user's (C36 M14): touch nothing. Today's
    # one-line first-page row (runs omaseed) is ours and goes when the block comes.
    local line
    while IFS= read -r line; do
        if [[ "${line}" =~ ^[[:space:]]*\"trigger\.torrents\"[[:space:]]*: ]] ||
            { [[ "${line}" =~ ^[[:space:]]*\"torrents\"[[:space:]]*: ]] &&
                ! is_old_row "${line}"; }; then
            rm -f "${tmp}"
            echo "another row in ${menu_dest} uses the Torrents id, leaving it untouched:" >&2
            echo "    ${line}" >&2
            return
        fi
    done <"${menu_dest}"

    # The old row (and the marker comment the old --menu wrote above it) out; then the block in
    # before the top-level object's closing brace: the last line that is only "}" (trailing
    # blank lines or comments after it are kept where they are). The last entry before it gets
    # a comma if it has none; one that ends in a comment is left alone and the file is not
    # touched, since a comma cannot be added safely there.
    if ! menu_block="${block}" awk -v old_comment="${old_menu_comment}" '
        # True when the line has a // or /* outside a string ("https://..." is not one).
        function has_comment(s,    i, c, in_str) {
            in_str = 0
            for (i = 1; i <= length(s); i++) {
                c = substr(s, i, 1)
                if (in_str) {
                    if (c == "\\") i++
                    else if (c == "\"") in_str = 0
                } else if (c == "\"") {
                    in_str = 1
                } else if (c == "/" && (substr(s, i + 1, 1) == "/" || substr(s, i + 1, 1) == "*")) {
                    return 1
                }
            }
            return 0
        }
        # The pre-C36 row: a one-line "torrents" entry whose action runs omaseed.
        function is_old(s) {
            return s ~ /^[[:space:]]*"torrents"[[:space:]]*:/ && index(s, "\"action\"") && index(s, "omaseed")
        }
        {
            if (is_old($0)) {
                if (n > 0 && index(line[n], old_comment)) n--
                next
            }
            line[++n] = $0
        }
        END {
            for (i = n; i >= 1; i--) if (line[i] ~ /^[[:space:]]*}[[:space:]]*$/) { close_at = i; break }
            if (!close_at) { print "no closing brace on a line of its own" > "/dev/stderr"; exit 2 }
            prev = 0
            for (i = close_at - 1; i >= 1; i--) {
                t = line[i]
                sub(/^[[:space:]]+/, "", t)
                sub(/[[:space:]]+$/, "", t)
                if (t == "" || t ~ /^\/\//) continue
                prev = i
                break
            }
            if (prev) {
                t = line[prev]
                sub(/[[:space:]]+$/, "", t)
                if (has_comment(t) || t ~ /\*\/$/) {
                    print "the entry before the closing brace ends in a comment" > "/dev/stderr"
                    exit 2
                }
                if (t !~ /[,{]$/) line[prev] = t ","
            }
            for (i = 1; i <= n; i++) {
                if (i == close_at) print ENVIRON["menu_block"]
                print line[i]
            }
        }
    ' "${menu_dest}" >"${tmp}"; then
        rm -f "${tmp}"
        echo "cannot find where to add the block, leaving ${menu_dest} untouched;" \
            "add it yourself from ${menu_src}" >&2
        return
    fi
    # Written through the existing file (not mv): its inode, mode and any symlink stay the
    # user's; mktemp's 0600 would otherwise replace their permissions.
    cat "${tmp}" >"${menu_dest}"
    rm -f "${tmp}"
    echo "added omaseed's block to: ${menu_dest}"
}

# Removes omaseed's block, marker to marker, nothing else: a "torrents" row the user added
# themselves elsewhere in the file stays.
uninstall_menu() {
    if [[ ! -f "${menu_dest}" ]] || ! menu_has_marker "${menu_dest}"; then
        return
    fi
    if ! menu_block_paired "${menu_dest}"; then
        echo "omaseed's markers don't pair in ${menu_dest}, leaving it untouched" >&2
        return
    fi
    local tmp
    tmp="$(mktemp)"
    awk -v o="${menu_open}" -v c="${menu_close}" '
        { t = $0; sub(/^[[:space:]]+/, "", t) }
        index(t, o) == 1 { skip = 1; next }
        skip && index(t, c) == 1 { skip = 0; next }
        !skip { print }
    ' "${menu_dest}" >"${tmp}"
    cat "${tmp}" >"${menu_dest}"
    rm -f "${tmp}"
    echo "removed omaseed's block from: ${menu_dest}"
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
        echo "Menu block not installed (pass --menu to add it to ${menu_dest})."
    fi
fi
