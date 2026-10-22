#!/usr/bin/env bash
# omaseed — a qBittorrent's torrents exported and imported through the app's own UI (docs/24
# §4–§5, §8 "checks"; #33). One box, two qBittorrents in it: the "source" with four swarm
# torrents downloaded for real (one a hybrid whose files share content, whose .torrent
# qBittorrent's own export breaks: #71) and a magnet that never got its metadata, set up the way a
# library is (a category, tags, a renamed file, a skipped file, limits, one stopped); and an
# empty "target". omaseed exports the source to a file through Settings › qBittorrent ›
# Backup and the portal's save dialog, the files are copied elsewhere in the box (the usual
# move to a new machine), omaseed switches to the target and imports the file through the
# import sheet with a "from → to" row. Then everything is compared, on both servers and on
# disk: every torrent's save path, category, tags, limits, file names and priorities,
# trackers and state (the complete ones started after the check, D6 a; the stopped one
# stopped), the category created first, the files untouched. The same file again skips them
# all (D8 a). Last, a stray file where a renamed file's old name points: the rename is not sent
# and that torrent stays stopped (#64, #67).
#
#   scripts/dev/backup-walk.sh [OUT_DIR]        # agents: a headless box, a shot per step,
#                                               # OUT_DIR/index.md says what each shot shows
#                                               # and lists every check
#   scripts/dev/backup-walk.sh --interactive    # Diogo: the box is a window on workspace 9;
#                                               # each step says what it will do and waits for
#                                               # enter, then does it and prints its checks
#
# The source is app-box.sh up --qbt's qBittorrent (127.0.0.1:8090 in the box, saving to
# ~/Torrents); the target a second qbittorrent-nox the walk starts in the box (127.0.0.1:8091,
# its own profile, saving to ~/Target). The box reaches only the swarm's tracker and seeder:
# nothing outside it is touched, and throwing it away throws both servers away.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
APP_BOX="$repo/scripts/dev/app-box.sh"
BOX="${OMASEED_BACKUP_BOX:-backup-walk}"
BOX_HOME=/home/sbx
SOURCE_DIR="$BOX_HOME/Torrents"   # app-box.sh --qbt's save path
MOVED_DIR="$BOX_HOME/Moved"       # where the files are copied, as on a new machine
TARGET_PORT=8091
TARGET_API="http://127.0.0.1:$TARGET_PORT/api/v2"
TARGET_PROFILE="$BOX_HOME/.local/share/qbittorrent-target"
EXPORT="$BOX_HOME/torrents-box-$(date +%F).omaseed-torrents"
MAGNET_HASH=dddddddddddddddddddddddddddddddddddddddd
INTERACTIVE=0
OUT=""
case "${1:-}" in
--interactive) INTERACTIVE=1 ;;
-h | --help) sed -n '2,/^set -e/p' "$0" | sed '$d'; exit 0 ;;
*) OUT="${1:-$(mktemp -d "${TMPDIR:-/tmp}/omaseed-backup-walk-$(date +%Y%m%d-%H%M%S)-XXXX")}" ;;
esac

[ -x build/omaseed ] || { echo "build first: cmake --build build" >&2; exit 1; }

# The target's BitTorrent port: the one app-box.sh keeps for a box's second qBittorrent, so
# two boxes are never one peer to the swarm's tracker (#22).
bt_slot=$(($(printf %s "$BOX" | cksum | cut -d' ' -f1) % 400))
TARGET_BT_PORT=$((27100 + 2 * bt_slot + 1))

# ---- helpers ------------------------------------------------------------------------------

# source_api PATH [CURL ARGS…] / target_api …: the box's two qBittorrents.
source_api() { "$APP_BOX" api -b "$BOX" "$@"; }
target_api() {
    local path="$1"
    shift
    omabox run -b "$BOX" -- curl -sS --fail-with-body "$@" "$TARGET_API/$path"
}

# in_box CMD...: run CMD in the box, quietly.
in_box() { omabox run -b "$BOX" -- "$@" >/dev/null 2>&1; }

keys() { omabox keys -b "$BOX" --window omaseed --wait "$@" >/dev/null 2>&1 || true; }
# dialog_keys ARGS…: keys to whatever has the focus (the portal's file chooser).
dialog_keys() { omabox keys -b "$BOX" --wait "$@" >/dev/null 2>&1 || true; }

# hash_of PREFIX: the source's torrent whose name starts with PREFIX.
hash_of() {
    source_api torrents/info | python3 -c '
import json, sys
print(next(t["hash"] for t in json.load(sys.stdin) if t["name"].startswith(sys.argv[1])))' "$1"
}

# layers_sound FILE: a v2 or hybrid .torrent whose every piece layer is as long as its files
# are (ceil(length / piece length) hashes), as libtorrent wants before it loads one (#71).
layers_sound() {
    python3 -I - "$1" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
def dec(i):
    c = data[i:i + 1]
    if c == b"i":
        j = data.index(b"e", i)
        return int(data[i + 1:j]), j + 1
    if c in (b"l", b"d"):
        i, out = i + 1, []
        while data[i:i + 1] != b"e":
            v, i = dec(i)
            out.append(v)
        return (dict(zip(out[::2], out[1::2])) if c == b"d" else out), i + 1
    j = data.index(b":", i)
    n = int(data[i:j])
    return data[j + 1:j + 1 + n], j + 1 + n
t = dec(0)[0]
size = t[b"info"][b"piece length"]
layers = t.get(b"piece layers", {})
bad = []
def walk(node, path):
    for name, child in node.items():
        if name == b"":
            want = -(-child[b"length"] // size) * 32 if child[b"length"] > size else 0
            got = len(layers.get(child.get(b"pieces root", b""), b""))
            if want and got != want:
                bad.append("%s: %d bytes, not %d" % (path, got, want))
        else:
            walk(child, path + "/" + name.decode(errors="replace"))
walk(t[b"info"].get(b"file tree", {}), "")
if bad:
    print("%d files' layers wrong, e.g. %s" % (len(bad), bad[0]), file=sys.stderr)
    sys.exit(1)
PY
}

CHECKS=0
STEP=0
STEP_SLUG=""

fail() {
    printf '  \033[31m✗ %s\033[0m\n' "$1"
    if [ "$INTERACTIVE" = 0 ]; then
        printf '  - ✗ **%s**\n' "$1" >>"$OUT/index.md"
        omabox shot -b "$BOX" -o "$OUT/failed.png" >/dev/null 2>&1 || true
        printf '\n**Stopped here.** ![](failed.png)\n' >>"$OUT/index.md"
        echo "failed: $OUT/index.md"
    fi
    exit 1
}

pass() {
    CHECKS=$((CHECKS + 1))
    printf '  ✓ %s\n' "$1"
    [ "$INTERACTIVE" = 1 ] || printf '  - ✓ %s\n' "$1" >>"$OUT/index.md"
}

# wait_for "WHAT" SECONDS CMD...: until CMD succeeds.
wait_for() {
    local what="$1" limit="$2"
    shift 2
    for _ in $(seq 1 $((limit * 2))); do
        "$@" >/dev/null 2>&1 && { pass "$what"; return 0; }
        sleep 0.5
    done
    fail "$what (not after ${limit}s)"
}

# step "TITLE" "WHAT IT DOES": interactive, say it and wait for enter; headless, a heading.
step() {
    STEP=$((STEP + 1))
    STEP_SLUG="$(printf '%02d' "$STEP")-$(echo "$1" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')"
    printf '\n\033[1m%02d %s\033[0m\n    %s\n' "$STEP" "$1" "$2"
    if [ "$INTERACTIVE" = 1 ]; then
        printf '    enter to do it '
        read -r _
    else
        printf '\n### %02d %s\n\n%s\n\n' "$STEP" "$1" "$2" >>"$OUT/index.md"
    fi
}

# shot "WHAT TO SEE" [SUFFIX] [screen]: headless, the app's window (or the whole screen, for
# the portal's dialog) into OUT and index.md.
shot() {
    [ "$INTERACTIVE" = 0 ] || return 0
    local file="$STEP_SLUG${2:+-$2}.png"
    omabox wait -b "$BOX" --timeout 5s still >/dev/null 2>&1 || true
    if [ "${3:-}" = screen ]; then
        omabox shot -b "$BOX" -o "$OUT/$file" >/dev/null 2>&1
    else
        omabox shot -b "$BOX" --window omaseed -o "$OUT/$file" >/dev/null 2>&1
    fi
    printf '  ![%s](%s)\n  *%s*\n' "$file" "$file" "$1" >>"$OUT/index.md"
}

# snapshot API OUT: every torrent of that server as the comparison reads it, one JSON file.
snapshot() {
    local api="$1" out="$2" hashes
    "$api" torrents/info >"$out.info"
    hashes="$(python3 -c 'import json, sys; print(" ".join(t["hash"] for t in json.load(open(sys.argv[1]))))' "$out.info")"
    printf '{' >"$out.parts"
    local first=1 h
    for h in $hashes; do
        [ "$first" = 1 ] || printf ',' >>"$out.parts"
        first=0
        printf '"%s":{"files":%s,"trackers":%s}' "$h" \
            "$("$api" "torrents/files?hash=$h" 2>/dev/null || echo '[]')" \
            "$("$api" "torrents/trackers?hash=$h")" >>"$out.parts"
    done
    printf '}' >>"$out.parts"
    "$api" torrents/categories >"$out.categories"
}

# import_done: omaseed's import journal for the target is gone, which it is once the import
# ran every step (the check and the starts included).
import_done() { [ ! -e "$home/.local/share/omaseed/imports/target.json" ]; }

# sums: the sha256 of every file under ~/Moved, sorted, from inside the box.
sums() {
    # shellcheck disable=SC2016 # expanded by the box's sh
    omabox run -b "$BOX" -- sh -c 'cd "$1" && find . -type f -print0 | sort -z | xargs -0 sha256sum' sh "$MOVED_DIR"
}

cleanup() {
    local status=$?
    if [ "$INTERACTIVE" = 1 ]; then
        echo
        echo "The box stays up for a look around; throw it away with: scripts/dev/app-box.sh down -b $BOX"
    else
        "$APP_BOX" down -b "$BOX" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$work"
    exit "$status"
}
work="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-backup-walk-work-XXXX")"
trap cleanup EXIT

# ---- setup --------------------------------------------------------------------------------

if [ "$INTERACTIVE" = 1 ]; then
    "$APP_BOX" up --qbt --interactive -b "$BOX" >/dev/null
else
    mkdir -p "$OUT"
    # shellcheck disable=SC2016 # backticks are Markdown, not command substitution
    printf '# Backup walk %s\n\nBuild `%s`, one box (`app-box.sh up --qbt`) with two qBittorrents: the source (:8090) and an empty target (:8091). Each step: what was done, every check on the servers and on disk, and shots.\n' \
        "$(date '+%Y-%m-%d %H:%M')" "$(git rev-parse --short HEAD)" >"$OUT/index.md"
    "$APP_BOX" up --qbt -b "$BOX" >/dev/null
fi
home="$(omabox path -b "$BOX")/home"
echo "box $BOX is up; filling the source and starting the target"

# The target: a second qbittorrent-nox in the box, no login from the box's own 127.0.0.1.
conf="$home${TARGET_PROFILE#"$BOX_HOME"}/qBittorrent/config"
mkdir -p "$conf" "$home/Target"
cat >"$conf/qBittorrent.conf" <<EOF
[LegalNotice]
Accepted=true

[BitTorrent]
Session\\Port=$TARGET_BT_PORT
Session\\DefaultSavePath=$BOX_HOME/Target
Session\\MultiConnectionsPerIp=true

[Preferences]
WebUI\\Enabled=true
WebUI\\Address=127.0.0.1
WebUI\\Port=$TARGET_PORT
WebUI\\LocalHostAuth=false
EOF
omabox run -b "$BOX" -d -- qbittorrent-nox "--profile=$TARGET_PROFILE" >/dev/null 2>&1
wait_for "the target answers on :$TARGET_PORT" 20 target_api app/version

# The source: four swarm torrents (from the box's ~/Downloads, app-box.sh put them there),
# downloaded for real, and a magnet nobody seeds. The Nested Library Sampler is a hybrid whose
# files share content (one "pieces root" for several files): qBittorrent's torrents/export
# repeats its piece layers, and qBittorrent then refuses that .torrent (415, #71).
add_args=()
for name in "tiny-notes.txt" "Field Recordings" "Ünïcødé" "Nested Library Sampler"; do
    for file in "$home"/Downloads/*.torrent; do
        [[ "$(basename "$file")" == "$name"* ]] && add_args+=(-F "torrents=@$BOX_HOME/Downloads/$(basename "$file")")
    done
done
[ ${#add_args[@]} = 8 ] || { echo "the box's ~/Downloads lacks some swarm .torrent files" >&2; exit 1; }
source_api torrents/add "${add_args[@]}" >/dev/null
source_api torrents/add -F "urls=magnet:?xt=urn:btih:$MAGNET_HASH&dn=link%20only" -F stopped=true >/dev/null
all_complete() {
    source_api torrents/info | python3 -c '
import json, sys
done = [t for t in json.load(sys.stdin) if t["has_metadata"] and t["progress"] >= 1]
sys.exit(0 if len(done) == 4 else 1)'
}
wait_for "the source downloaded its four torrents" 240 all_complete
TINY="$(hash_of tiny-notes.txt)"
ALBUM="$(hash_of "Field Recordings")"
UNICODE="$(hash_of "Ünïcødé")"
NESTED="$(hash_of "Nested Library Sampler")"
TRACK="Field Recordings Vol. 3 [FLAC]/01 - Harbour at Dawn.flac"
RENAMED="Field Recordings Vol. 3 [FLAC]/01 - Harbour.flac"
source_api torrents/createCategory --data-urlencode category=notes --data-urlencode "savePath=$SOURCE_DIR/notes" >/dev/null
source_api torrents/setCategory -d "hashes=$TINY" -d category=notes >/dev/null
source_api torrents/addTags -d "hashes=$TINY" -d tags=keep >/dev/null
source_api torrents/addTags -d "hashes=$ALBUM" -d "tags=flac,field" >/dev/null
source_api torrents/setUploadLimit -d "hashes=$TINY" -d limit=10240 >/dev/null
source_api torrents/setShareLimits -d "hashes=$ALBUM" -d ratioLimit=2 -d seedingTimeLimit=-2 \
    -d inactiveSeedingTimeLimit=-2 -d shareLimitAction=Default >/dev/null
source_api torrents/renameFile --data-urlencode "hash=$ALBUM" --data-urlencode "oldPath=$TRACK" \
    --data-urlencode "newPath=$RENAMED" >/dev/null
cover="$(source_api "torrents/files?hash=$ALBUM" | python3 -c '
import json, sys
print(next(f["index"] for f in json.load(sys.stdin) if f["name"].endswith("cover.jpg")))')"
source_api torrents/filePrio -d "hash=$ALBUM" -d "id=$cover" -d priority=0 >/dev/null
source_api torrents/stop -d "hashes=$UNICODE" >/dev/null

# omaseed knows both servers, on the source.
cat >"$home/.config/omaseed/servers.json" <<EOF
{"version":1,"defaultId":"box","servers":[
 {"id":"box","name":"box","url":"http://127.0.0.1:8090","authMode":"none","username":"","pathMap":[],"isDefault":true,"addedAt":"2026-10-15T00:00:00Z"},
 {"id":"target","name":"target","url":"http://127.0.0.1:$TARGET_PORT","authMode":"none","username":"","pathMap":[],"isDefault":false,"addedAt":"2026-10-15T00:00:00Z"}]}
EOF
"$APP_BOX" restart -b "$BOX" >/dev/null
sleep 2
snapshot source_api "$work/source"

# ---- the walk -----------------------------------------------------------------------------

# qBittorrent › Backup's rows, top to bottom: Export settings, Import settings, Copy settings
# from another server (docs/34 D6 a, always there, dim with one server), Export torrents,
# Import torrents. Tab puts the cursor on the first; each j moves it one row (#94).
step "Export from the source" "Settings › qBittorrent (ctrl+shift+,), 0 for Backup, tab, j j j past the settings rows and Copy settings to Export torrents, enter: the portal's save dialog proposes ~/torrents-box-<date>.omaseed-torrents; enter saves it."
keys ctrl+shift+comma
keys 0
keys Tab
keys j j j
shot "Backup, the torrents group: Export torrents (cursor) says 5 torrents with their save paths…, the files stay where they are, 1 magnet goes in as a link."
keys Return
omabox wait -b "$BOX" --timeout 10s window "Export qBittorrent's torrents" >/dev/null 2>&1 || fail "no save dialog"
shot "The portal's save dialog, ~/torrents-box-<date>.omaseed-torrents proposed." dialog screen
dialog_keys Return
wait_for "the file is written" 30 in_box test -s "$EXPORT"
perms="$(omabox run -b "$BOX" -- stat -c %a "$EXPORT")"
if [ "$perms" = 600 ]; then pass "the file is private (0600)"; else fail "the file is $perms, not 600"; fi
entries="$(omabox run -b "$BOX" -- tar -tf "$EXPORT" | sort | tr '\n' ' ')"
expected="manifest.json torrents/$ALBUM.json torrents/$ALBUM.torrent torrents/$MAGNET_HASH.json torrents/$NESTED.json torrents/$NESTED.torrent torrents/$TINY.json torrents/$TINY.torrent torrents/$UNICODE.json torrents/$UNICODE.torrent "
expected="$(tr ' ' '\n' <<<"$expected" | sed '/^$/d' | sort | tr '\n' ' ')"
if [ "$entries" = "$expected" ]; then
    pass "a plain tar: the manifest, 5 records, 4 .torrent files (D2 a)"
else
    fail "the archive lists: $entries"
fi
# The hybrid's .torrent as the file holds it, and as qBittorrent's own export gives it: every
# piece layer as long as its files are (#71). qBittorrent's is only said, not checked: it is
# qBittorrent's to fix.
omabox run -b "$BOX" -- tar -xOf "$EXPORT" "torrents/$NESTED.torrent" >"$work/nested.torrent"
source_api "torrents/export?hash=$NESTED" >"$work/nested-qbt.torrent"
if layers_sound "$work/nested.torrent"; then
    pass "the Nested Library Sampler's .torrent: each piece layer as long as its files (#71)"
else
    fail "the Nested Library Sampler's .torrent: $(layers_sound "$work/nested.torrent" 2>&1)"
fi
layers_sound "$work/nested-qbt.torrent" >/dev/null 2>&1 ||
    echo "  (qBittorrent's own torrents/export of it still repeats its piece layers)"
shot "The note under the rows: Exported 5 torrents to ~/torrents-box-<date>.omaseed-torrents. 1 magnet went in as a link." note

step "Copy the files elsewhere" "In the box, ~/Torrents is copied to ~/Moved: the files as a new machine would have them, under another folder."
in_box cp -a "$SOURCE_DIR" "$MOVED_DIR"
sums >"$work/sums-before"
if [ -s "$work/sums-before" ]; then pass "$(wc -l <"$work/sums-before") files in ~/Moved"; else fail "nothing was copied"; fi

step "Switch to the target" "omaseed's default server becomes the empty target and it starts again on it."
sed -i 's/"defaultId":"box"/"defaultId":"target"/; s/"isDefault":true/"isDefault":X/; s/"isDefault":false/"isDefault":true/; s/"isDefault":X/"isDefault":false/' \
    "$home/.config/omaseed/servers.json"
"$APP_BOX" restart -b "$BOX" >/dev/null
count="$(target_api torrents/info | python3 -c 'import json, sys; print(len(json.load(sys.stdin)))')"
if [ "$count" = 0 ]; then pass "the target is empty"; else fail "the target has $count torrents"; fi
shot "The library on target: empty."

step "Import into the target" "Settings › qBittorrent, 0, tab, j j j j to Import torrents, enter; the path typed into the portal's open dialog; the sheet: 4 new, 1 link, the save path row from ~/Torrents; tab to its \"to\", ~/Moved typed."
keys ctrl+shift+comma
keys 0
keys Tab
keys j j j j
keys Return
omabox wait -b "$BOX" --timeout 10s window "Import qBittorrent's torrents" >/dev/null 2>&1 || fail "no open dialog"
dialog_keys -t "$EXPORT"
shot "The portal's open dialog, the file's path typed." dialog screen
dialog_keys Return
omabox wait -b "$BOX" --timeout 10s window --gone "Import qBittorrent's torrents" >/dev/null 2>&1 || true
sleep 1
shot "Import 5 torrents into target: new to target 4 (added), already 0 (skipped), magnets 1 (added as links); save paths /home/sbx/Torrents → (empty), the ! line: save paths are kept as they are; then: check files, start the complete ones; + 1 category and 3 tags they use are created first; import 5." sheet
keys Tab
keys -t "$MOVED_DIR"
shot "The row reads /home/sbx/Torrents → /home/sbx/Moved; the ! line is gone (every path is under it)." row
keys Return
settled() {
    target_api torrents/info | python3 -c '
import json, sys
every = json.load(sys.stdin)
busy = [t for t in every if t["state"].startswith("checking") or t["state"] in ("moving", "queuedForChecking")]
running = [t for t in every if t["has_metadata"] and not t["state"].startswith("stopped")]
sys.exit(0 if len(every) == 5 and not busy and len(running) == 3 else 1)'
}
wait_for "the target has all 5, checked, the three complete started ones running" 180 settled
wait_for "the import is through: its journal is gone" 30 import_done
shot "The note under the rows: Imported into target: 4 torrents, 1 link. After the check: 3 started, 1 left stopped." note

step "Compare" "Every torrent on both servers, and the files on disk."
snapshot target_api "$work/target"
sums >"$work/sums-after"
if cmp -s "$work/sums-before" "$work/sums-after"; then
    pass "the files in ~/Moved are as they were (sha256)"
else
    fail "the files in ~/Moved changed"
fi
python3 - "$work/source" "$work/target" "$SOURCE_DIR" "$MOVED_DIR" "$MAGNET_HASH" <<'PY' >"$work/compare" || true
import json, sys
src, dst, old, new, magnet = sys.argv[1:]
def load(prefix):
    info = {t["hash"]: t for t in json.load(open(prefix + ".info"))}
    parts = json.load(open(prefix + ".parts"))
    return info, parts, json.load(open(prefix + ".categories"))
si, sp, sc = load(src)
ti, tp, tc = load(dst)
def say(ok, what):
    print(("ok " if ok else "NO ") + what)
say(set(si) == set(ti), "the same %d torrents, by hash" % len(si))
def mapped(path):
    return new + path[len(old):] if path == old or path.startswith(old + "/") else path
def trackers(parts):
    return sorted(x["url"] for x in parts["trackers"] if not x["url"].startswith("** ["))
for h, s in si.items():
    t = ti.get(h)
    if t is None:
        continue
    n = s["name"]
    say(t["save_path"] == mapped(s["save_path"]), "%s: save path %s" % (n, t["save_path"]))
    say(t["category"] == s["category"], "%s: category %r" % (n, t["category"]))
    say(sorted(t["tags"].split(", ")) == sorted(s["tags"].split(", ")), "%s: tags %r" % (n, t["tags"]))
    say(t["up_limit"] == s["up_limit"] and t["dl_limit"] == s["dl_limit"], "%s: speed limits %s/%s" % (n, t["up_limit"], t["dl_limit"]))
    say(t["ratio_limit"] == s["ratio_limit"] and t["seeding_time_limit"] == s["seeding_time_limit"], "%s: share limits %s/%s" % (n, t["ratio_limit"], t["seeding_time_limit"]))
    sf = [(f["name"], f["priority"]) for f in sp[h]["files"]]
    tf = [(f["name"], f["priority"]) for f in tp[h]["files"]]
    say(sf == tf, "%s: %d files, names and priorities" % (n, len(tf)))
    say(trackers(sp[h]) == trackers(tp[h]), "%s: trackers %s" % (n, ", ".join(trackers(tp[h]))))
    stopped = t["state"].startswith("stopped")
    if h == magnet:
        say(stopped, "%s: a link, stopped as exported" % n)
    elif s["state"].startswith("stopped"):
        say(stopped and t["progress"] >= 1, "%s: checked complete, stopped as exported (%s)" % (n, t["state"]))
    else:
        say(not stopped and t["progress"] >= 1, "%s: checked complete and started (%s)" % (n, t["state"]))
say("notes" in tc and tc["notes"]["savePath"] == new + "/notes", "the category notes, its folder %s" % tc.get("notes", {}).get("savePath"))
PY
while read -r verdict what; do
    if [ "$verdict" = ok ]; then pass "$what"; else fail "$what"; fi
done <"$work/compare"
[ -s "$work/compare" ] || fail "the comparison printed nothing"

step "The same file again" "Import torrents once more (the cursor is still on it): everything is already there (D8 a), the button says nothing to import; esc."
keys Return
omabox wait -b "$BOX" --timeout 10s window "Import qBittorrent's torrents" >/dev/null 2>&1 || fail "no open dialog"
dialog_keys -t "$EXPORT"
dialog_keys Return
omabox wait -b "$BOX" --timeout 10s window --gone "Import qBittorrent's torrents" >/dev/null 2>&1 || true
sleep 1
shot "Import 5 torrents into target: new 0, already in target 5 (skipped); the button reads nothing to import." sheet
keys Escape
count="$(target_api torrents/info | python3 -c 'import json, sys; print(len(json.load(sys.stdin)))')"
if [ "$count" = 5 ]; then pass "still 5 torrents on the target"; else fail "the target has $count torrents"; fi

step "A stray file under a renamed file's old name" "The album is taken off the target (its files stay), and a stray file is put where its first track was called before the rename. Imported again (tab to \"to\", ~/Moved), the rename would move the stray file over the real track (#64, #67): it is not sent, and the album stays stopped."
ALBUM_DIR="$MOVED_DIR/Field Recordings Vol. 3 [FLAC]"
target_api torrents/delete -d "hashes=$ALBUM" -d deleteFiles=false >/dev/null
gone() { ! target_api torrents/info | grep -q "$ALBUM"; }
wait_for "the album is off the target" 20 gone
# shellcheck disable=SC2016 # expanded by the box's sh
in_box sh -c 'printf "a stray file, not the album'"'"'s" >"$1"' sh "$ALBUM_DIR/01 - Harbour at Dawn.flac"
real_before="$(omabox run -b "$BOX" -- sha256sum "$ALBUM_DIR/01 - Harbour.flac" | cut -d' ' -f1)"
keys Return
omabox wait -b "$BOX" --timeout 10s window "Import qBittorrent's torrents" >/dev/null 2>&1 || fail "no open dialog"
dialog_keys -t "$EXPORT"
dialog_keys Return
omabox wait -b "$BOX" --timeout 10s window --gone "Import qBittorrent's torrents" >/dev/null 2>&1 || true
sleep 1
keys Tab
keys -t "$MOVED_DIR"
shot "Import 5 torrents into target: new 1, already 4; the row /home/sbx/Torrents → /home/sbx/Moved; import 1." sheet
keys Return
album_settled() {
    target_api torrents/info | python3 -c '
import json, sys
t = next((x for x in json.load(sys.stdin) if x["hash"] == sys.argv[1]), None)
sys.exit(0 if t and not t["state"].startswith("checking") and t["state"].startswith("stopped") else 1)' "$ALBUM"
}
wait_for "the album is back, checked and stopped" 60 album_settled
wait_for "the import is through: its journal is gone" 30 import_done
if album_settled; then pass "it stays stopped"; else fail "the album was started"; fi
first="$(target_api "torrents/files?hash=$ALBUM" | python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["name"])')"
if [ "$first" = "$TRACK" ]; then pass "its first file keeps the .torrent's name: the rename was not sent"; else fail "its first file is $first"; fi
stray="$(omabox run -b "$BOX" -- cat "$ALBUM_DIR/01 - Harbour at Dawn.flac")"
if [ "$stray" = "a stray file, not the album's" ]; then pass "the stray file is untouched"; else fail "the stray file now holds: ${stray:0:40}"; fi
real_after="$(omabox run -b "$BOX" -- sha256sum "$ALBUM_DIR/01 - Harbour.flac" | cut -d' ' -f1)"
if [ "$real_after" = "$real_before" ]; then pass "the real track is untouched"; else fail "the real track changed"; fi
shot "The note: Imported into target: 1 torrent. After the check: 1 left stopped. Kept stopped, its files not renamed: the rename would replace a file already on disk (Field Recordings Vol. 3 [FLAC]), in yellow." note

echo
echo "all $CHECKS checks passed"
if [ "$INTERACTIVE" = 0 ]; then
    printf '\n**All %s checks passed.**\n' "$CHECKS" >>"$OUT/index.md"
    echo "shots and checks: $OUT/index.md"
fi
