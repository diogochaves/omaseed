#!/usr/bin/env bash
# ShellCheck every tracked shell script: *.sh, plus files whose shebang runs bash or sh.
#   scripts/dev/shellcheck.sh
# Settings are in .shellcheckrc. Run from anywhere; `source=` directives are repo-relative.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"

command -v shellcheck >/dev/null || { echo "shellcheck: not installed" >&2; exit 77; }

files=()
while IFS= read -r -d '' f; do
    [[ -f "$f" ]] || continue
    if [[ "$f" == *.sh ]] || head -n1 "$f" | grep -qE '^#!.*[/ ](ba)?sh([[:space:]]|$)'; then
        files+=("$f")
    fi
done < <(git ls-files -z)

echo "shellcheck: ${#files[@]} file(s)"
shellcheck "${files[@]}"
echo "shellcheck: clean"
