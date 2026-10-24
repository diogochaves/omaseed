#!/usr/bin/env bash
# The skin's two lints (#171, the console skin's design note "Tests"):
#   1. No QML branches on a skin's name. A component may branch on a token's value, never on
#      which skin is drawn (`Theme.skin.name === "console"`), so a skin is only its tokens.
#      Settings' chooser compares the choice it saves (`App.skinChoice`); that is allowed
#      there and nowhere else.
#   2. No literal `border.width` in the Kit: an edge is a line token (`Theme.hairWidth`,
#      `Theme.edgeWidth`…), so a skin can draw it. 0 (no edge) is not a width. Theme.qml,
#      where the tokens are, is not checked.
# Prints each offending line and fails; reads only the source tree.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
qml="$repo/src/ui/qml"
fail=0

# 1. `skin ===`, `skin.name !=`, `=== Theme.skin.name`…, and the choice outside Settings.
if grep -rnE --include='*.qml' \
    '\bskin(\.name|\.requested)?[[:space:]]*[!=]==?|[!=]==?[[:space:]]*[A-Za-z_.]*\bskin(\.name|\.requested)?\b' \
    "$qml"; then
    echo "FAIL: QML branches on a skin's name (read a token instead)"
    fail=1
fi
if grep -rnE --include='*.qml' --exclude='SettingsModal.qml' 'skinChoice[[:space:]]*[!=]==?' "$qml"; then
    echo "FAIL: QML outside Settings branches on the skin choice"
    fail=1
fi

# 2. A `border.width:` whose value, once names are taken out, still has a number other than 0.
# shellcheck disable=SC2016 # the single quotes hold a Perl program, not shell expansions
literal=$(find "$qml/Kit" -name '*.qml' ! -name 'Theme.qml' -print0 | sort -z |
    xargs -0 perl -ne '
        if (/^\s*border\.width:\s*(.*)$/) {
            (my $value = $1) =~ s/[A-Za-z_][A-Za-z0-9_.]*//g;
            print "$ARGV:$.: $_" if $value =~ /[1-9]|0\.\d/;
        }
        close ARGV if eof;')
if [ -n "$literal" ]; then
    printf '%s\n' "$literal"
    echo "FAIL: a literal border.width in the Kit (use a Theme line token)"
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "skin lint: clean"
fi
exit "$fail"
