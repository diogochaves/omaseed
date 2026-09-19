#!/usr/bin/env bash
# Lint every QML and C++ file in the repo.
#   scripts/dev/lint.sh [build-dir]
# qmllint needs the generated qmldir files, so it is pointed at the build tree's QML output
# directory; configure once (cmake -S . -B build -G Ninja) before running.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$repo/build}"
export PATH="/usr/lib/qt6/bin:$PATH"

status=0

mapfile -t qml_files < <(find "$repo/src" "$repo/tests" -name '*.qml' | sort)
if [[ ${#qml_files[@]} -gt 0 ]]; then
    if [[ -d "$build/qml" ]]; then
        echo "qmllint: ${#qml_files[@]} file(s)"
        qmllint -I "$build/qml" "${qml_files[@]}" || status=1
    else
        echo "qmllint: skipped, no $build/qml (configure the build first)" >&2
        status=1
    fi
fi

mapfile -t cpp_files < <(find "$repo/src" "$repo/tests" \( -name '*.cpp' -o -name '*.hpp' \) | sort)
if [[ ${#cpp_files[@]} -gt 0 ]]; then
    echo "clang-format: ${#cpp_files[@]} file(s)"
    clang-format --dry-run --Werror "${cpp_files[@]}" || status=1
fi

if [[ $status -eq 0 ]]; then
    echo "lint: clean"
fi
exit $status
