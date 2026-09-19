#!/usr/bin/env bash
set -euo pipefail

# Exercises scripts/release.sh end to end in a throwaway clone, so a ctest run never
# touches this checkout's git state (no tags, no commits, no network) and does not
# require CHANGELOG.md/CMakeLists.txt to be in any particular uncommitted state.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/omaseed-release-test.XXXXXX")"
CLONE="$WORK/omaseed"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

echo "==> Cloning $REPO_ROOT into $CLONE"
git clone --quiet --no-hardlinks "$REPO_ROOT" "$CLONE"

# This checkout may be on a task branch, not main; scripts/release.sh requires main, so
# rename the clone's current branch locally. This never touches the source repository.
CLONE_BRANCH="$(cd "$CLONE" && git rev-parse --abbrev-ref HEAD)"
if [[ "$CLONE_BRANCH" != "main" ]]; then
    (cd "$CLONE" && git branch -m main)
fi

grep -q '^## v0.1.0' "$CLONE/CHANGELOG.md" || fail "CHANGELOG.md has no '## v0.1.0' section"

echo "==> Running scripts/release.sh 0.1.0 --dry-run"
OUTPUT="$(cd "$CLONE" && bash scripts/release.sh 0.1.0 --dry-run)"
echo "$OUTPUT"

TARBALL="$CLONE/dist/omaseed-0.1.0.tar.gz"
SUMS="$CLONE/dist/SHA256SUMS"
NOTES="$CLONE/dist/RELEASE_NOTES.md"

[[ -f "$TARBALL" ]] || fail "tarball not created: $TARBALL"
[[ -f "$SUMS" ]] || fail "SHA256SUMS not created: $SUMS"
[[ -f "$NOTES" ]] || fail "RELEASE_NOTES.md not created: $NOTES"

TAR_LISTING="$(tar tf "$TARBALL")"

echo "==> Checking the tarball name and prefix"
FIRST_ENTRY="$(printf '%s\n' "$TAR_LISTING" | head -n1)"
case "$FIRST_ENTRY" in
    omaseed-0.1.0/*) ;;
    *) fail "tarball's first entry is not prefixed omaseed-0.1.0/: $FIRST_ENTRY" ;;
esac

echo "==> Checking docs/evidence and build*/ are excluded"
if printf '%s\n' "$TAR_LISTING" | grep -qE '^omaseed-0\.1\.0/(docs/evidence/|build)'; then
    fail "docs/evidence or a build*/ directory leaked into the tarball"
fi

echo "==> Checking the SHA256SUMS line format"
grep -qE '^[0-9a-f]{64}  omaseed-0\.1\.0\.tar\.gz$' "$SUMS" || fail "SHA256SUMS has the wrong line format"

echo "==> Verifying the checksum actually matches the tarball"
(cd "$CLONE/dist" && sha256sum --check --status SHA256SUMS) || fail "SHA256SUMS does not verify against the tarball"

echo "==> Checking the release notes title"
head -n1 "$NOTES" | grep -qE '^# omaseed v0\.1\.0$' || fail "RELEASE_NOTES.md title is wrong: $(head -n1 "$NOTES")"

echo "==> Checking the printed gh release create command"
echo "$OUTPUT" | grep -qF \
    'gh release create v0.1.0 dist/omaseed-0.1.0.tar.gz dist/SHA256SUMS --notes-file dist/RELEASE_NOTES.md' \
    || fail "did not print the expected gh release create command"

echo "==> Checking the tag exists only in the clone"
(cd "$CLONE" && git rev-parse -q --verify refs/tags/v0.1.0 >/dev/null) \
    || fail "tag v0.1.0 was not created in the clone"
if (cd "$REPO_ROOT" && git rev-parse -q --verify refs/tags/v0.1.0 >/dev/null 2>&1); then
    fail "tag v0.1.0 leaked into $REPO_ROOT"
fi

echo "==> Checking the dry run did not push or touch the source repo"
SOURCE_STATUS="$(cd "$REPO_ROOT" && git status --porcelain)"
[[ -z "$SOURCE_STATUS" ]] || fail "source repo working tree is no longer clean: $SOURCE_STATUS"

echo "ALL CHECKS PASSED"
