# omaseed — sourced by every dev script that talks to a qBittorrent server (not run on its own).
#
# :8080 is Diogo's real library. No dev script may start a server on it, send it a request or
# point the app at it, whatever OMASEED_TEST_PORT / QBT_TEST_URL say. refuse_home PORT|URL...
# exits 2 when any argument is on port 8080 (any host: localhost, ::1 and a LAN name can all
# reach it), so a mistyped variable stops the script before it does anything.
# shellcheck shell=bash

omaseed_home_port=8080

# is_home PORT|URL: true when the port, or the URL's port, is 8080.
is_home() {
    local target="$1" rest hostport port
    target="${target#"${target%%[![:space:]]*}"}"
    target="${target%"${target##*[![:space:]]}"}"
    if [[ "$target" =~ ^[0-9]+$ ]]; then
        port="$target"
    else
        rest="${target#*://}"
        hostport="${rest%%/*}"
        hostport="${hostport%%\?*}"
        case "$hostport" in
        *\]:*) port="${hostport##*]:}" ;;   # [::1]:8080
        \[*) port="" ;;                     # [::1]
        *:*) port="${hostport##*:}" ;;
        *) port="" ;;
        esac
    fi
    [[ "$port" =~ ^[0-9]+$ ]] || return 1
    [ "$((10#$port))" = "$omaseed_home_port" ]
}

refuse_home() {
    local target
    for target in "$@"; do
        if is_home "$target"; then
            echo "refusing $target: port $omaseed_home_port is the real library, never a test server" >&2
            exit 2
        fi
    done
}
