#!/usr/bin/env bash
# scripts/dev/install-omarchy-extras.sh --menu / --uninstall and scripts/dev/install-desktop.sh,
# run with HOME pointed at a temporary directory: the user's real ~/.config and
# ~/.local/share are never read or written. Review INT-16.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
extras="$repo/scripts/dev/install-omarchy-extras.sh"
desktop="$repo/scripts/dev/install-desktop.sh"
command -v python3 >/dev/null || { echo "python3 is not installed"; exit 77; }

dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
failures=0

fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# A fresh fake HOME holding only the menu file, given on stdin.
new_home() {
    local home="$dir/$1"
    mkdir -p "$home/.config/omarchy/extensions"
    cat >"$home/.config/omarchy/extensions/omarchy-menu.jsonc"
    chmod 0644 "$home/.config/omarchy/extensions/omarchy-menu.jsonc"
    echo "$home"
}

menu_of() { echo "$1/.config/omarchy/extensions/omarchy-menu.jsonc"; }

# JSONC -> JSON (comments and trailing commas dropped, strings kept) and parse it; prints the
# top-level keys.
keys_of() {
    python3 - "$1" <<'EOF'
import json, re, sys
text = open(sys.argv[1], encoding="utf-8").read()
out, i, n = [], 0, len(text)
while i < n:
    c = text[i]
    if c == '"':
        j = i + 1
        while text[j] != '"':
            j += 2 if text[j] == "\\" else 1
        out.append(text[i:j + 1]); i = j + 1
    elif text.startswith("//", i):
        i = text.find("\n", i) if "\n" in text[i:] else n
    elif text.startswith("/*", i):
        i = text.index("*/", i) + 2
    else:
        out.append(c); i += 1
clean = re.sub(r",(\s*[}\]])", r"\1", "".join(out))
print(" ".join(json.loads(clean).keys()))
EOF
}

install_menu() { HOME="$1" bash "$extras" --menu >/dev/null 2>"$1.err"; }
uninstall_all() { HOME="$1" bash "$extras" --uninstall >/dev/null 2>&1; }

check_installed() {
    local name="$1" home="$2" expected="$3" keys
    if ! keys="$(keys_of "$(menu_of "$home")" 2>&1)"; then
        fail "$name: not valid JSONC after --menu: $keys"
        return
    fi
    [[ "$keys" == "$expected" ]] || fail "$name: keys are '$keys', wanted '$expected'"
}

# 1. The plain case: last entry has its comma, "}" is the last line.
h="$(new_home plain <<'EOF'
{
  "a": {"label":"A"},
}
EOF
)"
install_menu "$h"
check_installed plain "$h" "a torrents"

# 2. Blank lines after the closing brace: the row still goes inside the object.
h="$(new_home blank-tail <<'EOF'
{
  "a": {"label":"A"},
}


EOF
)"
install_menu "$h"
check_installed blank-tail "$h" "a torrents"

# 3. The last entry has no comma: one is added.
h="$(new_home no-comma <<'EOF'
{
  "a": {"label":"A"},
  "b": {"label":"B"}
  // a closing remark
}
EOF
)"
install_menu "$h"
check_installed no-comma "$h" "a b torrents"

# 4. A "//" inside a string is not a comment; a comma is still added.
h="$(new_home url <<'EOF'
{
  "web": {"action":"xdg-open https://example.org"}
}
EOF
)"
install_menu "$h"
check_installed url "$h" "web torrents"

# 5. The last entry ends in a comment: no safe place for a comma, the file is left as it was.
h="$(new_home trailing-comment <<'EOF'
{
  "a": {"label":"A"} // mine
}
EOF
)"
before="$(cat "$(menu_of "$h")")"
install_menu "$h"
[[ "$(cat "$(menu_of "$h")")" == "$before" ]] || fail "trailing-comment: the file was changed"
grep -q "untouched" "$h.err" || fail "trailing-comment: no message said why"

# 6. The file keeps its mode (mktemp's 0600 must not replace it).
h="$(new_home mode <<'EOF'
{
  "a": {"label":"A"},
}
EOF
)"
install_menu "$h"
mode="$(stat -c %a "$(menu_of "$h")")"
[[ "$mode" == "644" ]] || fail "mode: the menu file is $mode after --menu, wanted 644"

# 7. Uninstall removes only the row it added; a user's own "torrents": line stays.
printf '  "mine": {"torrents": {"label":"kept"}},\n' >"$dir/user-line"
sed -i '1r '"$dir/user-line" "$(menu_of "$h")"
uninstall_all "$h"
grep -q '"label":"kept"' "$(menu_of "$h")" || fail "uninstall: the user's own \"torrents\" line was removed"
grep -q 'install-omarchy-extras.sh --menu' "$(menu_of "$h")" && fail "uninstall: the marker comment is still there"
grep -q '"Torrents"' "$(menu_of "$h")" && fail "uninstall: the installed row is still there"
keys="$(keys_of "$(menu_of "$h")")" || fail "uninstall: not valid JSONC afterwards"
[[ "$keys" == "mine a" ]] || fail "uninstall: keys are '$keys', wanted 'mine a'"

# 8. install-desktop.sh refuses a binary path that sed or Exec= would misread.
for bad in 'with#hash' 'with&amp' 'with\backslash' 'with space'; do
    mkdir -p "$dir/bin/$bad"
    printf '#!/bin/sh\n' >"$dir/bin/$bad/omaseed"
    chmod +x "$dir/bin/$bad/omaseed"
    home="$dir/desktop-home"
    mkdir -p "$home"
    if HOME="$home" bash "$desktop" "$dir/bin/$bad/omaseed" >/dev/null 2>&1; then
        fail "install-desktop: accepted a path containing '$bad'"
    fi
    [[ -e "$home/.local/share/applications/io.github.diogochaves.omaseed.desktop" ]] &&
        fail "install-desktop: wrote a .desktop file for '$bad'"
done
# And a plain path works, with the Exec= line pointing at it.
mkdir -p "$dir/bin/plain"
printf '#!/bin/sh\n' >"$dir/bin/plain/omaseed"
chmod +x "$dir/bin/plain/omaseed"
home="$dir/desktop-home"
if HOME="$home" bash "$desktop" "$dir/bin/plain/omaseed" >/dev/null 2>&1; then
    grep -qF "Exec=$dir/bin/plain/omaseed " \
        "$home/.local/share/applications/io.github.diogochaves.omaseed.desktop" ||
        fail "install-desktop: Exec= does not name the binary"
else
    fail "install-desktop: refused a plain path"
fi

if ((failures > 0)); then
    echo "$failures failure(s)"
    exit 1
fi
echo "ok"
