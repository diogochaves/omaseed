# shellcheck shell=bash
# omaseed dev scripts: start a long-lived server without the caller's descriptors (#141).
#
#   . scripts/dev/detach.sh
#   detach LOGFILE CMD [ARGS…]     # CMD in the background under nohup, its output in LOGFILE;
#                                  # $! is CMD's pid
#   detach -a LOGFILE CMD [ARGS…]  # the same, appending to LOGFILE, in a session of its own,
#                                  # stdin from /dev/null
#
# Agents take turns on the playground with `flock -o LOCK cmd`. A plain `flock LOCK cmd`
# leaves the lock's descriptor open in cmd, and a server cmd starts in the background
# inherited it: qbittorrent-nox held the lock long after the walk that restarted it had
# ended, and every other agent queued behind it. So every descriptor beyond 0–2 is closed
# before the server starts, whatever the caller holds.

detach() {
    local append=0 log
    if [ "$1" = -a ]; then
        append=1
        shift
    fi
    log="$1"
    shift
    (
        local fd name
        for fd in /proc/self/fd/*; do
            name="${fd##*/}"
            [ "$name" -gt 2 ] 2>/dev/null || continue
            eval "exec $name>&-" 2>/dev/null || true
        done
        if [ "$append" = 1 ]; then
            exec setsid nohup "$@" >>"$log" 2>&1 </dev/null
        fi
        exec nohup "$@" >"$log" 2>&1
    ) &
}
